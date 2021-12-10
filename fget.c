#include <err.h>
#include <libgen.h> /* basename(3) */
#include <limits.h> /* INT_MAX */
#include <inttypes.h>   /* PRIu32 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strcmp(3), strdup(3) */

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>     /* fi_listen, fi_getname */
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>

typedef struct {
    struct fid_ep *aep;
    struct fid_eq *listen_eq;
    struct fid_eq *active_eq;
    struct fid_pep *pep;
    struct fid_cq *cq;
    struct fid_mr *mr;
} get_state_t;

typedef struct {
    struct fid_ep *ep;
    struct fid_eq *connect_eq;
    struct fid_cq *cq;
    struct fid_mr *mr;
} put_state_t;

typedef struct {
    struct fid_domain *domain;
    struct fid_fabric *fabric;
    struct fi_info *info;
    union {
        get_state_t get;
        put_state_t put;
    } u;
} state_t;

typedef int (*personality_t)(state_t *);

static const char fget_fput_service_name[] = "4242";

#define bailout_for_ofi_ret(ret, ...)                          \
        bailout_for_ofi_ret_impl(ret, __func__, __VA_ARGS__)

static void
bailout_for_ofi_ret_impl(int ret, const char *fn, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", fn);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s\n", fi_strerror(-ret));
    exit(EXIT_FAILURE);
}

static int
get(state_t *st)
{
    /* completion fields:
     *
     * void     *op_context;
     * uint64_t flags;
     * size_t   len;
     */
    struct fi_cq_msg_entry completion;
    struct fi_cq_attr cq_attr = {
      .size = 128
    , .flags = 0
    , .format = FI_CQ_FORMAT_MSG
    , .wait_obj = FI_WAIT_UNSPEC
    , .signaling_vector = 0
    , .wait_cond = FI_CQ_COND_NONE
    , .wait_set = NULL
    };
    struct fi_eq_attr eq_attr = {
      .size = 128
    , .flags = 0
    , .wait_obj = FI_WAIT_UNSPEC
    , .signaling_vector = 0     /* don't care */
    , .wait_set = NULL          /* don't care */
    };
    struct fi_eq_cm_entry cm_entry;
    char rxbuf[128];
    get_state_t *gst = &st->u.get;
    ssize_t ncompleted;
    uint32_t event;
    int rc;

    rc = fi_mr_reg(st->domain, rxbuf, sizeof(rxbuf), FI_RECV, 0, 0, 0, &gst->mr,
        NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");

    rc = fi_passive_ep(st->fabric, st->info, &gst->pep, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_passive_ep");

    rc = fi_eq_open(st->fabric, &eq_attr, &gst->listen_eq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_eq_open (listen)");

    rc = fi_eq_open(st->fabric, &eq_attr, &gst->active_eq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_eq_open (active)");

    rc = fi_pep_bind(gst->pep, &gst->listen_eq->fid, 0);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_pep_bind");

    rc = fi_listen(gst->pep);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_listen");

    do {
        rc = fi_eq_sread(gst->listen_eq, &event, &cm_entry, sizeof(cm_entry),
            -1 /* wait forever */, 0 /* flags */ );
    } while (rc == -FI_EAGAIN);

#if 0
    if (rc == -FI_EINTR)
        errx(EXIT_FAILURE, "%s: fi_eq_sread: interrupted", __func__);
#endif

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_eq_sread");

    if (event != FI_CONNREQ) {
        errx(EXIT_FAILURE,
            "%s: expected connreq event (%" PRIu32 "), received %" PRIu32,
            __func__, FI_CONNREQ, event);
    }

    rc = fi_endpoint(st->domain, cm_entry.info, &gst->aep, NULL);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_endpoint");

    rc = fi_ep_bind(gst->aep, &gst->active_eq->fid, 0);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    rc = fi_cq_open(st->domain, &cq_attr, &gst->cq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_cq_open");

    rc = fi_ep_bind(gst->aep, &gst->cq->fid,
        /* FI_SELECTIVE_COMPLETION | */ FI_RECV | FI_TRANSMIT);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    rc = fi_enable(gst->aep);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_enable");

    rc = fi_recv(gst->aep, rxbuf, sizeof(rxbuf), fi_mr_desc(gst->mr),
        0, NULL);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_recv");

    rc = fi_accept(gst->aep, NULL, 0);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_accept");

    fi_freeinfo(cm_entry.info);

    do {
        rc = fi_eq_sread(gst->active_eq, &event, &cm_entry, sizeof(cm_entry),
            -1 /* wait forever */, 0 /* flags */ );
    } while (rc == -FI_EAGAIN);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_eq_sread");

    if (event != FI_CONNECTED) {
        errx(EXIT_FAILURE,
            "%s: expected connected event (%" PRIu32 "), received %" PRIu32,
            __func__, FI_CONNECTED, event);
    }

    do {
        ncompleted = fi_cq_sread(gst->cq, &completion, 1, NULL, -1);
    } while (rc == -FI_EAGAIN);

    if (ncompleted < 0)
        bailout_for_ofi_ret(rc, "fi_cq_sread");

    if (ncompleted != 1) {
        errx(EXIT_FAILURE,
            "%s: expected 1 completion, read %zd", __func__, ncompleted);
    }

    const uint64_t desired_flags = FI_RECV | FI_MSG;
    if ((completion.flags & desired_flags) != desired_flags) {
        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_flags, completion.flags & desired_flags);
    }

    int truncated_len = (completion.len > INT_MAX) ? INT_MAX : completion.len;
    printf("received %zu bytes, '%.*s'\n",
        completion.len, truncated_len, rxbuf);

    return EXIT_SUCCESS;
}

static int
put(state_t *st)
{
    /* completion fields:
     *
     * void     *op_context;
     * uint64_t flags;
     * size_t   len;
     */
    struct fi_cq_msg_entry completion;
    struct fi_cq_attr cq_attr = {
      .size = 128
    , .flags = 0
    , .format = FI_CQ_FORMAT_MSG
    , .wait_obj = FI_WAIT_UNSPEC
    , .signaling_vector = 0
    , .wait_cond = FI_CQ_COND_NONE
    , .wait_set = NULL
    };
    struct fi_eq_attr eq_attr = {
      .size = 128
    , .flags = 0
    , .wait_obj = FI_WAIT_UNSPEC
    , .signaling_vector = 0     /* don't care */
    , .wait_set = NULL          /* don't care */
    };
    struct fi_eq_cm_entry cm_entry;
    const char msg[] =
        "If this message was received in error then please "
        "print it out and shred it.";
    put_state_t *pst = &st->u.put;
    ssize_t ncompleted;
    uint32_t event;
    int rc;

    rc = fi_mr_reg(st->domain, msg, strlen(msg), FI_SEND, 0, 0, 0, &pst->mr,
        NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");

    rc = fi_endpoint(st->domain, st->info, &pst->ep, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_endpoint");

    rc = fi_cq_open(st->domain, &cq_attr, &pst->cq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_cq_open");

    rc = fi_eq_open(st->fabric, &eq_attr, &pst->connect_eq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_eq_open");

    rc = fi_ep_bind(pst->ep, &pst->connect_eq->fid, 0);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    rc = fi_ep_bind(pst->ep, &pst->cq->fid,
        /* FI_SELECTIVE_COMPLETION | */ FI_RECV | FI_TRANSMIT);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    rc = fi_enable(pst->ep);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_enable");

    rc = fi_connect(pst->ep, st->info->dest_addr, NULL, 0);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_connect dest_addr %p", st->info->dest_addr);

    do {
        rc = fi_eq_sread(pst->connect_eq, &event, &cm_entry, sizeof(cm_entry),
            -1 /* wait forever */, 0 /* flags */ );
    } while (rc == -FI_EAGAIN);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_eq_sread");

    if (event != FI_CONNECTED) {
        errx(EXIT_FAILURE,
            "%s: expected connected event (%" PRIu32 "), received %" PRIu32,
            __func__, FI_CONNECTED, event);
    }

    rc = fi_send(pst->ep, msg, strlen(msg), fi_mr_desc(pst->mr),
        0, NULL);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_send");

    do {
        ncompleted = fi_cq_sread(pst->cq, &completion, 1, NULL, -1);
    } while (rc == -FI_EAGAIN);

    if (ncompleted < 0)
        bailout_for_ofi_ret(rc, "fi_cq_sread");

    if (ncompleted != 1) {
        errx(EXIT_FAILURE,
            "%s: expected 1 completion, read %zd", __func__, ncompleted);
    }

    const uint64_t desired_flags = FI_SEND | FI_MSG;
    if ((completion.flags & desired_flags) != desired_flags) {
        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_flags, completion.flags & desired_flags);
    }
    return EXIT_SUCCESS;
}

static int
count_info(const struct fi_info *first)
{
    int count;
    const struct fi_info *info;

    for (info = first, count = 1; (info = info->next) != NULL; count++)
        ;

    return count;
}

static const char *
personality_to_name(personality_t p)
{
    if (p == get)
        return "fget";
    else if (p == put)
        return "fput";
    else
        return "unknown";
}

int
main(int argc, char **argv)
{
    struct fi_info *hints;
    personality_t personality;
    char *prog, *tmp;
    state_t st;
    int rc;

    if ((tmp = strdup(argv[0])) == NULL)
        err(EXIT_FAILURE, "%s: strdup", __func__);

    prog = basename(tmp);

    if (strcmp(prog, "fget") == 0)
        personality = get;
    else if (strcmp(prog, "fput") == 0)
        personality = put;
    else
        errx(EXIT_FAILURE, "program personality '%s' is not implemented", prog);

    if ((hints = fi_allocinfo()) == NULL)
        errx(EXIT_FAILURE, "%s: fi_allocinfo", __func__);

    hints->ep_attr->type = FI_EP_MSG;
    hints->caps = FI_FENCE | FI_MSG | FI_RMA | FI_REMOTE_WRITE | FI_WRITE;
    hints->mode = FI_CONTEXT;

    rc = fi_getinfo(FI_VERSION(1, 13), "10.10.10.120" /* -b */,
        fget_fput_service_name, (personality == get) ? FI_SOURCE : 0, hints,
        &st.info);

    fi_freeinfo(hints);

    switch (-rc) {
    case FI_ENODATA:
        warnx("capabilities not available?");
        break;
    case FI_ENOSYS:
        warnx("available libfabric version < 1.13?");
        break;
    default:
        break;
    }

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_getinfo");

    printf("%d infos found\n", count_info(st.info));

    rc = fi_fabric(st.info->fabric_attr, &st.fabric, NULL /* app context */);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_fabric");

    rc = fi_domain(st.fabric, st.info, &st.domain, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_domain");

    printf("starting personality '%s'\n", personality_to_name(personality)); 

    return (*personality)(&st);
}
