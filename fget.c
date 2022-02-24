#include <assert.h>
#include <err.h>
#include <libgen.h> /* basename(3) */
#include <limits.h> /* INT_MAX */
#include <inttypes.h>   /* PRIu32 */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strcmp(3), strdup(3) */
#include <unistd.h> /* sysconf(3) */

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>     /* fi_listen, fi_getname */
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>    /* struct fi_msg_rma */

#define arraycount(a)   (sizeof(a) / sizeof(a[0]))

/*
 * Message definitions
 */
typedef struct {
    uint64_t bits[2];
} nonce_t;

typedef struct initial_msg {
    nonce_t nonce;
    uint32_t nsources;
    uint32_t id;
    uint32_t addrlen;
    char addr[512];
} initial_msg_t;

typedef struct ack_msg {
    uint32_t addrlen;
    char addr[512];
} ack_msg_t;

typedef struct vector_msg {
    uint32_t niovs;
    struct {
        uint64_t addr, len, key;
    } iov[12];
} vector_msg_t;

typedef struct progress_msg {
    uint64_t nfilled;
    uint64_t nleftover;
} progress_msg_t;

/* Communication buffers */

struct buf;
typedef struct buf buf_t;

struct buf {
    size_t nfull;
    size_t nallocated;
    struct fid_mr *mr;
    void *desc;
    char content[];
};

typedef struct fifo {
    uint64_t insertions;
    uint64_t removals;
    size_t index_mask;  // for some integer n > 0, 2^n - 1 == index_mask
    buf_t *buf[];
} fifo_t;

typedef struct buflist {
    uint64_t access;
    size_t nfull;
    size_t nallocated;
    buf_t *buf[];
} buflist_t;

/* Communication terminals: sources and sinks */

typedef enum {
    tr_ready
,   tr_end
,   tr_error
} trade_result_t;

struct terminal;
typedef struct terminal terminal_t;

struct terminal {
    /* trade(t, ready, completed) */
    trade_result_t (*trade)(terminal_t *, fifo_t *, fifo_t *);
};

typedef struct {
    terminal_t terminal;
    size_t idx;
    size_t txbuflen;
} sink_t;

typedef struct {
    terminal_t terminal;
} source_t;

/*
 * Communications state definitions
 */

struct cxn;
typedef struct cxn cxn_t;

struct session;
typedef struct session session_t;

struct worker;
typedef struct worker worker_t;

struct cxn {
    session_t *(*loop)(worker_t *, session_t *);
    fi_addr_t peer_addr;
    struct fid_cq *cq;
    struct fid_av *av;
};

typedef struct {
    cxn_t cxn;
    bool started;
    struct fid_ep *ep;
    struct fid_eq *eq;
    uint64_t nfull;
    fifo_t *rxfifo;
    struct {
        struct iovec iov[12];
        void *desc[12];
        struct fid_mr *mr[12];
        uint64_t raddr[12];
        ssize_t niovs;
        ack_msg_t msg;
    } ack;
    struct {
        struct iovec iov[12];
        void *desc[12];
        struct fid_mr *mr[12];
        uint64_t raddr[12];
        ssize_t niovs;
        initial_msg_t msg;
    } initial;
    struct {
        struct iovec iov[12];
        void *desc[12];
        struct fid_mr *mr[12];
        uint64_t raddr[12];
        ssize_t niovs;
        vector_msg_t msg;
    } vector;
    struct {
        struct iovec iov[12];
        void *desc[12];
        struct fid_mr *mr[12];
        uint64_t raddr[12];
        ssize_t niovs;
        progress_msg_t msg;
        struct fi_context context;
    } progress;
    struct {
        struct iovec iov[12];
        void *desc[12];
        struct fid_mr *mr[12];
        uint64_t raddr[12];
        ssize_t niovs;
        char rxbuf[128];
    } payload;
} rcvr_t;

typedef struct {
    cxn_t cxn;
    bool started;
    struct fid_ep *ep;
    struct fid_eq *eq;
    struct {
        void *desc;
        struct fid_mr *mr;
        initial_msg_t msg;
    } initial;
    struct {
        void *desc;
        struct fid_mr *mr;
        ack_msg_t msg;
    } ack;
    struct {
        void *desc;
        struct fid_mr *mr;
        vector_msg_t msg;
    } vector;
    struct {
        void *desc;
        struct fid_mr *mr;
        progress_msg_t msg;
        struct fi_context context;
    } progress;
    struct {
        struct iovec iov[12];
        void *desc[12];
        struct iovec iov2[12];
        void *desc2[12];
        struct fid_mr *mr[12];
        uint64_t raddr[12];
        ssize_t niovs;
        struct fi_context context;
    } payload;
} xmtr_t;

/* On each loop, a worker checks its poll set for any completions.
 * If `loops_since_mark < UINT16_MAX`, a worker increases it by
 * one, and increases `ctxs_serviced_since_mark` by the number
 * of queues that actually held one or more completions.  If
 * `loops_since_mark == UINT16_MAX`, then a worker updates
 * `average`, `average = (average + 256 * ctxs_serviced_since_mark /
 * (UINT16_MAX + 1)) / 2`, and resets `loops_since_mark` and
 * `ctxs_serviced_since_mark` to 0.
 */
typedef struct {
    /* A fixed-point number with 8 bits right of the decimal point: */
    volatile atomic_uint_fast16_t average;
    uint_fast16_t loops_since_mark;
    uint32_t ctxs_serviced_since_mark;
} loadavg_t;

#define WORKER_SESSIONS_MAX 64
#define WORKERS_MAX 128
#define SESSIONS_MAX (WORKER_SESSIONS_MAX * WORKERS_MAX)

struct session {
    terminal_t *terminal;
    cxn_t *cxn;
    fifo_t *ready_for_cxn;
    fifo_t *ready_for_terminal;
};

typedef struct keysource {
    uint64_t next_key;
} keysource_t;

struct worker {
    pthread_t thd;
    struct fid_domain *dom;
    loadavg_t avg;
    terminal_t *term[WORKER_SESSIONS_MAX];
    session_t session[WORKER_SESSIONS_MAX];
    volatile _Atomic size_t nsessions[2];   // number of sessions in each half
                                            // of session[]
    struct fid_poll *pollset[2];
    pthread_mutex_t mtx[2]; /* mtx[0] protects pollset[0] and the first half
                             * of session[]; mtx[1], pollset[1] and the second
                             * half
                             */
    pthread_cond_t sleep;   /* Used in conjunction with workers_mtx. */
    volatile atomic_bool cancelled;
    struct {
        buflist_t *tx;
        buflist_t *rx;
    } freebufs;    /* Reservoirs for free buffers. */
    keysource_t keys;
    size_t mr_maxsegs;
    size_t rma_maxsegs;
    size_t rx_maxsegs;
};

typedef struct {
    struct fid_eq *listen_eq;
    struct fid_ep *listen_ep;
    struct fid_cq *listen_cq;
    sink_t sink;
    rcvr_t rcvr;
} get_state_t;

typedef struct {
    xmtr_t xmtr;
    source_t source;
} put_state_t;

typedef struct {
    struct fid_domain *domain;
    struct fid_fabric *fabric;
    struct fi_info *info;
    union {
        get_state_t get;
        put_state_t put;
    } u;
    size_t mr_maxsegs;
    size_t rx_maxsegs;
    size_t tx_maxsegs;
    size_t rma_maxsegs;
    keysource_t keys;
} state_t;

typedef int (*personality_t)(state_t *);

static const char fget_fput_service_name[] = "4242";

static pthread_mutex_t workers_mtx = PTHREAD_MUTEX_INITIALIZER;
static worker_t workers[WORKERS_MAX];
static size_t nworkers_running;
static size_t nworkers_allocated;
static pthread_cond_t nworkers_cond = PTHREAD_COND_INITIALIZER;

static bool workers_assignment_suspended = false;

static const uint64_t desired_rx_flags = FI_RECV | FI_MSG;
static const uint64_t desired_tx_flags = FI_SEND | FI_MSG;
static const uint64_t desired_wr_flags =
    FI_RMA | FI_WRITE | FI_COMPLETION | FI_DELIVERY_COMPLETE;

static uint64_t _Atomic next_key_pool = 512;

static char txbuf[] =
    "If this message was received in error then please "
    "print it out and shred it.";

#define bailout_for_ofi_ret(ret, ...)                          \
        bailout_for_ofi_ret_impl(ret, __func__, __LINE__, __VA_ARGS__)

#define warn_about_ofi_ret(ret, ...)                          \
        warn_about_ofi_ret_impl(ret, __func__, __LINE__, __VA_ARGS__)

static void
warnv_about_ofi_ret_impl(int ret, const char *fn, int lineno,
    const char *fmt, va_list ap)
{
    fprintf(stderr, "%s.%d: ", fn, lineno);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ": %s\n", fi_strerror(-ret));
}

static void
warn_about_ofi_ret_impl(int ret, const char *fn, int lineno,
    const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    warnv_about_ofi_ret_impl(ret, fn, lineno, fmt, ap);
    va_end(ap);
}

static void
bailout_for_ofi_ret_impl(int ret, const char *fn, int lineno,
    const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    warnv_about_ofi_ret_impl(ret, fn, lineno, fmt, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}

#if 0
static int
maxsize(size_t l, size_t r)
{
    return (l > r) ? l : r;
}
#endif

static int
minsize(size_t l, size_t r)
{
    return (l < r) ? l : r;
}

static inline bool
size_is_power_of_2(size_t size)
{
    return ((size - 1) & size) == 0;
}

static fifo_t *
fifo_create(size_t size)
{
    if (!size_is_power_of_2(size))
        return NULL;

    fifo_t *f = malloc(offsetof(fifo_t, buf[0]) + sizeof(f->buf[0]) * size);

    if (f == NULL)
        return NULL;

    f->insertions = f->removals = 0;
    f->index_mask = size - 1;

    return f;
}

static void
fifo_destroy(fifo_t *f)
{
    free(f);
}

static inline buf_t *
fifo_get(fifo_t *f)
{
    assert(f->insertions >= f->removals);

    if (f->insertions == f->removals)
        return NULL;

    buf_t *b = f->buf[f->removals & (uint64_t)f->index_mask];
    f->removals++;

    return b;
}

static inline buf_t *
fifo_peek(fifo_t *f)
{
    assert(f->insertions >= f->removals);

    if (f->insertions == f->removals)
        return NULL;

    return f->buf[f->removals & (uint64_t)f->index_mask];
}

static inline bool
fifo_empty(fifo_t *f)
{
    return f->insertions == f->removals;
}

static inline bool
fifo_full(fifo_t *f)
{
    return f->insertions - f->removals == f->index_mask + 1;
}

static inline bool
fifo_put(fifo_t *f, buf_t *b)
{
    assert(f->insertions - f->removals <= f->index_mask + 1);

    if (f->insertions - f->removals > f->index_mask)
        return false;

    f->buf[f->insertions & (uint64_t)f->index_mask] = b;
    f->insertions++;

    return true;
}

static buf_t *
buflist_get(buflist_t *bl)
{
    if (bl->nfull == 0)
        return NULL;

    return bl->buf[--bl->nfull];
}

#if 0
static bool
buflist_put(buflist_t *bl, buf_t *b)
{
    if (bl->nfull == bl->nallocated)
        return false;

    bl->buf[bl->nfull++] = b;
    return true;
}
#endif

static bool
session_init(session_t *s, cxn_t *c, terminal_t *t)
{
    memset(s, 0, sizeof(*s));

    s->cxn = c;
    s->terminal = t;

    if ((s->ready_for_cxn = fifo_create(64)) == NULL)
        return NULL;

    if ((s->ready_for_terminal = fifo_create(64)) == NULL) {
        fifo_destroy(s->ready_for_cxn);
        return NULL;
    }

    return s;
}

static void
keysource_init(keysource_t *s)
{
    memset(s, 0, sizeof(*s));
}

static uint64_t
keysource_next(keysource_t *s)
{
    if (s->next_key % 256 == 0) {
            s->next_key = atomic_fetch_add_explicit(&next_key_pool, 256,
                memory_order_relaxed);
    }

    return s->next_key++;
}

static bool
worker_buflist_replenish(worker_t *w, buflist_t *bl)
{
    size_t i, buflen;
    int rc;

    if (bl->nfull >= bl->nallocated / 2)
        return true;

    size_t ntofill = bl->nallocated / 2 - bl->nfull;

    for (buflen = 0, i = bl->nfull; i < ntofill; i++) {
        buf_t *buf;

        // buflen cycle: 7 -> 11 -> 13 -> 17 -> 19 -> 23 -> 29 -> 31 -> 7
        switch (buflen) {
        case 0:
        default:
            buflen = 7;
            break;
        case 7:
            buflen = 11;
            break;
        case 11:
            buflen = 13;
            break;
        case 13:
            buflen = 17;
            break;
        case 17:
            buflen = 19;
            break;
        case 23:
            buflen = 29;
            break;
        case 29:
            buflen = 31;
            break;
        case 31:
            buflen = 7;
            break;
        }
        buf = malloc(offsetof(buf_t, content[0]) + buflen);
        if (buf == NULL)
            err(EXIT_FAILURE, "%s.%d: malloc", __func__, __LINE__);
        warnx("%s: pushing %zu-byte buffer", __func__, buflen);
        buf->nallocated = buflen;
        buf->nfull = 0;

        rc = fi_mr_reg(w->dom, buf->content, buflen,
            bl->access, 0, keysource_next(&w->keys), 0, &buf->mr, NULL);

        if (rc != 0) {
            warn_about_ofi_ret(rc, "fi_mr_reg");
            free(buf);
            break;
        }

        buf->desc = fi_mr_desc(buf->mr);
        bl->buf[i] = buf;
    }
    bl->nfull = i;

    return bl->nfull > 0;
}

static buf_t *
worker_rxbuf_get(worker_t *w)
{
    buf_t *b;

    while ((b = buflist_get(w->freebufs.rx)) == NULL &&
           worker_buflist_replenish(w, w->freebufs.rx))
        ;   // do nothing

    if (b != NULL)
        warnx("%s: buf length %zu", __func__, b->nallocated);
    return b;
}

static size_t
fibonacci_iov_setup(void *_buf, size_t len, struct iovec *iov, size_t niovs)
{
    char *buf = _buf;
    ssize_t i;
    struct fibo {
        size_t prev, curr;
    } state = {.prev = 0, .curr = 1}; // Fibonacci sequence state

    if (niovs < 1 && len > 0)
        return -1;

    if (niovs > SSIZE_MAX)
        niovs = SSIZE_MAX;

    for (i = 0; len > 0 && i < niovs - 1; i++) {
        iov[i].iov_len = (state.curr < len) ? state.curr : len;
        iov[i].iov_base = buf;
        len -= iov[i].iov_len;
        buf += iov[i].iov_len;
        state = (struct fibo){.prev = state.curr,
                              .curr = state.prev + state.curr};
    }
    if (len > 0) {
        iov[i].iov_len = len;
        iov[i].iov_base = buf;
        i++;
    }
    return i;
}

/* Register the `niovs`-segment I/O vector `iov` using up to `niovs`
 * of the registrations, descriptors, and remote addresses in the
 * vectors `mrp`, `descp`, and `raddrp`, respectively.  Register no
 * more than `maxsegs` segments in a single `fi_mr_regv` call.
 */
static int
mr_regv_all(struct fid_domain *domain, const struct iovec *iov,
    size_t niovs, size_t maxsegs, uint64_t access, uint64_t offset,
    keysource_t *keys, uint64_t flags, struct fid_mr **mrp,
    void **descp, uint64_t *raddrp, void *context)
{
    int rc;
    size_t i, j, nregs = (niovs + maxsegs - 1) / maxsegs;
    size_t nleftover;

    for (nleftover = niovs, i = 0;
         i < nregs;
         iov += maxsegs, nleftover -= maxsegs, i++) {
        struct fid_mr *mr;
        uint64_t raddr = 0;

        size_t nsegs = minsize(nleftover, maxsegs);

        warnx("%zu remaining I/O vectors", nleftover);

        rc = fi_mr_regv(domain, iov, nsegs,
            access, offset, keysource_next(keys), flags, &mr, context);

        if (rc != 0)
            goto err;

        for (j = 0; j < nsegs; j++) {
            warnx("filling descriptor %zu", i * maxsegs + j);
            mrp[i * maxsegs + j] = mr;
            descp[i * maxsegs + j] = fi_mr_desc(mr);
            raddrp[i * maxsegs + j] = raddr;
            raddr += iov[j].iov_len;
        }
    }

    return 0;

err:
    for (j = 0; j < i; j++)
        (void)fi_close(&mrp[j]->fid);

    return rc;
}

static session_t *
rcvr_start(worker_t *w, session_t *s)
{
    rcvr_t *r = (rcvr_t *)s->cxn;
    size_t i, nleftover, nloaded;
    int rc;
    buf_t *paybuf[16];
    size_t npaybufs = 0;

    r->started = true;

    for (nleftover = sizeof(r->payload.rxbuf), nloaded = 0; nleftover > 0; ) {
        buf_t *b = worker_rxbuf_get(w);

        if (b == NULL)
            errx(EXIT_FAILURE, "%s: could not get a buffer", __func__);

        b->nfull = minsize(nleftover, b->nallocated);
        nleftover -= b->nfull;
        nloaded += b->nfull;
        if (npaybufs == arraycount(paybuf))
            errx(EXIT_FAILURE, "%s: could not queue rx buffer", __func__);
        paybuf[npaybufs++] = b;
    }

    r->vector.msg.niovs = npaybufs;
    for (i = 0; i < npaybufs; i++) {
        buf_t *b = paybuf[i];
        fprintf(stderr, "paybuf[%zu]->nfull = %zu\n", i, b->nfull);
        if (!fifo_put(r->rxfifo, b))
            errx(EXIT_FAILURE, "%s: could not re-queue rx buffer", __func__);
        r->vector.msg.iov[i].addr = 0;
        r->vector.msg.iov[i].len = b->nfull;
        r->vector.msg.iov[i].key = fi_mr_key(b->mr);
    }

    r->vector.niovs = fibonacci_iov_setup(&r->vector.msg,
        (char *)&r->vector.msg.iov[r->vector.msg.niovs] -
        (char *)&r->vector.msg,
        r->vector.iov, w->rx_maxsegs);

    if (r->vector.niovs < 1) {
        errx(EXIT_FAILURE, "%s: unexpected I/O vector length %zd",
            __func__, r->vector.niovs);
    }

    rc = mr_regv_all(w->dom, r->vector.iov, r->vector.niovs,
        minsize(2, w->mr_maxsegs), FI_SEND, 0, &w->keys, 0,
        r->vector.mr, r->vector.desc, r->vector.raddr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "mr_regv_all");

    return s;
}

#if 0
/* Return 0 if the source is producing more bytes, 
 * 1 if the source will produce no more bytes.
 */
static int
source_trade(terminal_t *t, fifo_t *ready, fifo_t *completed)
{
    sink_t *s = (sink_t *)t;
    buf_t *b;

    while ((b = fifo_peek(ready)) != NULL && !fifo_full(completed)) {
        if (s->idx == s->txbuflen)
            return 1;

        b->nfull = minsize(s->txbuflen - s->idx, b->nallocated);

        memcpy(b->content, &txbuf[s->idx], b->nfull);

        (void)fifo_get(ready);
        (void)fifo_put(completed, b);

        s->idx += b->nfull;
    }
    return (s->idx == s->txbuflen) ? 1 : 0;
}
#endif

/* Return 0 if the sink is accepting more bytes, -1 if unexpected
 * bytes are on `ready`, 1 if the sink expects no more bytes.
 */
static trade_result_t
sink_trade(terminal_t *t, fifo_t *ready, fifo_t *completed)
{
    sink_t *s = (sink_t *)t;
    buf_t *b;

    while ((b = fifo_peek(ready)) != NULL && !fifo_full(completed)) {
        if (b->nfull > s->txbuflen - s->idx)
            return -1;

        if (memcmp(&txbuf[s->idx], b->content, b->nfull) != 0)
            return -1;

        (void)fifo_get(ready);
        (void)fifo_put(completed, b);

        s->idx += b->nfull;
    }
    return (s->idx == s->txbuflen) ? 1 : 0;
}

#if 0
typedef struct truncate_iov_params {
    struct iovec *iov_out;
    size_t *niovs_out;
    const struct iovec *iov_in;
    size_t niovs_in;
    size_t bytelimit;
} truncate_iov_params_t;

static void
truncate_iov(const truncate_iov_params_t p)
{
    size_t i, nremaining;

    for (i = 0, nremaining = p.bytelimit;
         i < p.niovs_in && nremaining > 0;
         i++) {
        size_t len = minsize(nremaining, p.iov_in[i].iov_len);

        p.iov_out[i].iov_base = p.iov_in[i].iov_base;
        p.iov_out[i].iov_len = len;
        nremaining -= len;
    }

    *p.niovs_out = i;
}
#endif

static rcvr_t *
rcvr_progress_rx(rcvr_t *r)
{
    struct fi_cq_msg_entry completion;
    ssize_t ncompleted;

    /* Check for progress message arrival. */
    warnx("%s: checking for progress message, context %p, "
        "last len %zu, count %zu", __func__, (void *)&r->progress,
        r->progress.iov[r->progress.niovs - 1].iov_len, r->progress.niovs);
    if ((ncompleted = fi_cq_read(r->cxn.cq, &completion, 1)) == -FI_EAGAIN)
        return r;

    if (ncompleted == -FI_EAVAIL) {
        struct fi_cq_err_entry e;
        char errbuf[256];
        ssize_t nfailed = fi_cq_readerr(r->cxn.cq, &e, 0);

        warnx("%s: read %zd errors, %s", __func__, nfailed,
            fi_strerror(e.err));
        warnx("%s: context %p", __func__, (void *)e.op_context);
        warnx("%s: completion flags %" PRIx64 " expected %" PRIx64,
            __func__, e.flags, desired_rx_flags);
        warnx("%s: provider error %s", __func__,
            fi_cq_strerror(r->cxn.cq, e.prov_errno, e.err_data, errbuf,
                sizeof(errbuf)));
        return NULL;
    } else if (ncompleted < 0)
        bailout_for_ofi_ret(ncompleted, "fi_cq_sread");

    if (ncompleted != 1) {
        errx(EXIT_FAILURE,
            "%s: expected 1 completion, read %zd", __func__, ncompleted);
    }

    if ((completion.flags & desired_rx_flags) != desired_rx_flags) {
        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_rx_flags, completion.flags & desired_rx_flags);
    }

    if (completion.len != sizeof(r->progress.msg)) {
        errx(EXIT_FAILURE,
            "received %zu bytes, expected %zu-byte progress\n", completion.len,
            sizeof(r->progress.msg));
    }

    r->nfull += r->progress.msg.nfilled;
    if (r->nfull != strlen(txbuf)) {
        errx(EXIT_FAILURE,
            "progress: %" PRIu64 " bytes filled, expected %" PRIu64 "\n",
            r->progress.msg.nfilled,
            strlen(txbuf));
    }

    if (r->progress.msg.nleftover != 0) {
        errx(EXIT_FAILURE,
            "progress: %" PRIu64 " bytes leftover, expected 0\n",
            r->progress.msg.nleftover);
    }

    return r;
}

static session_t *
rcvr_loop(worker_t *w, session_t *s)
{
    rcvr_t *r = (rcvr_t *)s->cxn;
    terminal_t *t = s->terminal;
    int rc;

    if (!r->started)
        return rcvr_start(w, s);

    /* Transmit vector. */

    if (r->vector.msg.niovs == 0)
        ; // nothing to do
    else if ((rc = fi_sendmsg(r->ep, &(struct fi_msg){
          .msg_iov = r->vector.iov
        , .desc = r->vector.desc
        , .iov_count = r->vector.niovs
        , .addr = r->cxn.peer_addr
        , .context = NULL
        , .data = 0
        }, 0)) == -FI_EAGAIN)
        return s;
    else if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_sendmsg");
    else
        r->vector.msg.niovs = 0;

    if (rcvr_progress_rx(r) == NULL)
        goto out;

    size_t nremaining;

    for (nremaining = r->nfull;
         nremaining > 0 && !fifo_full(s->ready_for_terminal); ) {
        buf_t *b = fifo_peek(r->rxfifo);

        (void)fifo_get(r->rxfifo);

        if (nremaining < b->nfull) {
            b->nfull = nremaining;
            nremaining = 0;
        } else {
            nremaining -= b->nfull;
        }

        (void)fifo_put(s->ready_for_terminal, b);
    }

out:
    switch (sink_trade(t, s->ready_for_terminal, s->ready_for_cxn)) {
    case tr_ready:
        return s;
    case tr_error:
    default:
        warnx("unexpected received message");
        /* FALLTHROUGH */
    case tr_end:
        if ((rc = fi_close(&r->ep->fid)) < 0)
            bailout_for_ofi_ret(rc, "fi_close");
        warnx("%s: closed.", __func__);
        return NULL;
    }
}

static session_t *
xmtr_start(session_t *s)
{
    struct fi_cq_msg_entry completion;
    xmtr_t *x = (xmtr_t *)s->cxn;
    ssize_t ncompleted;
    int rc;

    x->started = true;

    /* Post receive for first vector message. */
    x->vector.desc = fi_mr_desc(x->vector.mr);

    rc = fi_recvmsg(x->ep, &(struct fi_msg){
          .msg_iov = &(struct iovec){.iov_base = &x->ack.msg,
                                     .iov_len = sizeof(x->ack.msg)}
        , .desc = &x->ack.desc
        , .iov_count = 1
        , .addr = x->cxn.peer_addr
        , .context = NULL
        , .data = 0
        }, FI_COMPLETION);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_recvmsg");

    /* Transmit initial message. */
    while ((rc = fi_sendmsg(x->ep, &(struct fi_msg){
          .msg_iov = &(struct iovec){.iov_base = &x->initial.msg,
                                     .iov_len = sizeof(x->initial.msg)}
        , .desc = &x->initial.desc
        , .iov_count = 1
        , .addr = x->cxn.peer_addr
        , .context = NULL
        , .data = 0
        }, 0)) == -FI_EAGAIN) {

        /* Await reply to initial message: first vector message. */
        ncompleted = fi_cq_read(x->cxn.cq, &completion, 1);

        if (ncompleted == -FI_EAGAIN)
            continue;

        if (ncompleted < 0)
            bailout_for_ofi_ret(ncompleted, "fi_cq_sread");

        if (ncompleted != 1) {
            errx(EXIT_FAILURE,
                "%s: expected 1 completion, read %zd", __func__, ncompleted);
        }

        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_rx_flags, completion.flags & desired_rx_flags);
    }

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_sendmsg");

    /* Await reply to initial message: first ack message. */
    do {
        warnx("%s: awaiting ack message reception", __func__);
        ncompleted = fi_cq_sread(x->cxn.cq, &completion, 1, NULL, -1);
    } while (ncompleted == -FI_EAGAIN);

    if (ncompleted < 0)
        bailout_for_ofi_ret(ncompleted, "fi_cq_sread");

    if (ncompleted != 1) {
        errx(EXIT_FAILURE,
            "%s: expected 1 completion, read %zd", __func__, ncompleted);
    }

    if ((completion.flags & desired_rx_flags) != desired_rx_flags) {
        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_rx_flags, completion.flags & desired_rx_flags);
    }

    if (completion.len != sizeof(x->ack.msg))
        errx(EXIT_FAILURE, "%s: ack is incorrect size", __func__);

    fi_addr_t oaddr = x->cxn.peer_addr;
    rc = fi_av_insert(x->cxn.av, x->ack.msg.addr, 1, &x->cxn.peer_addr,
        0, NULL);

    if (rc < 0) {
        bailout_for_ofi_ret(rc, "fi_av_insert dest_addr %p", x->ack.msg.addr);
    }

    rc = fi_av_remove(x->cxn.av, &oaddr, 1, 0);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_av_remove old dest_addr");

    rc = fi_recvmsg(x->ep, &(struct fi_msg){
          .msg_iov = &(struct iovec){.iov_base = &x->vector.msg,
                                     .iov_len = sizeof(x->vector.msg)}
        , .desc = &x->vector.desc
        , .iov_count = 1
        , .addr = x->cxn.peer_addr
        , .context = NULL
        , .data = 0
        }, FI_COMPLETION);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_recvmsg");

    return s;
}

typedef struct write_fully_params {
    struct fid_ep *ep;
    const struct iovec *iov_in;
    void **desc_in;
    struct iovec *iov_out;
    void **desc_out;
    size_t niovs;
    size_t *niovs_out;
    const struct fi_rma_iov *riov_in;
    struct fi_rma_iov *riov_out;
    size_t nriovs;
    size_t *nriovs_out;
    size_t len;
    size_t maxsegs;
    uint64_t flags;
    fi_addr_t addr;
    struct fi_context *context;
} write_fully_params_t;

static ssize_t
write_fully(const write_fully_params_t p)
{
    ssize_t rc;
    size_t i, j, nremaining;
    struct {
        size_t local;
        size_t remote;
    } sumlen = {.local = 0, .remote = 0}, nsegs = {.local = 0, .remote = 0};
    size_t maxsegs = minsize(p.maxsegs, minsize(p.nriovs, p.niovs));

    for (i = 0; i < maxsegs; i++) {
        sumlen.local += p.iov_in[i].iov_len;
        sumlen.remote += p.riov_in[i].len;
    }

    const size_t len = minsize(minsize(sumlen.local, sumlen.remote),
                               minsize(p.len, SSIZE_MAX));

    for (i = 0, nremaining = len; 0 < nremaining && i < maxsegs; i++) {
        p.iov_out[i] = p.iov_in[i];
        p.desc_out[i] = p.desc_in[i];
        if (p.iov_in[i].iov_len > nremaining) {
            p.iov_out[i].iov_len = nremaining;
            nremaining = 0;
        } else {
            nremaining -= p.iov_in[i].iov_len;
        }
    }

    nsegs.local = i;

    for (i = 0, nremaining = len; 0 < nremaining && i < maxsegs; i++) {
        p.riov_out[i] = p.riov_in[i];
        if (p.riov_in[i].len > nremaining) {
            p.riov_out[i].len = nremaining;
            nremaining = 0;
        } else {
            nremaining -= p.riov_in[i].len;
        }
    }

    nsegs.remote = i;

    struct fi_msg_rma mrma = {
      .msg_iov = p.iov_out
    , .desc = p.desc_out
    , .iov_count = nsegs.local
    , .addr = p.addr
    , .rma_iov = p.riov_out
    , .rma_iov_count = nsegs.remote
    , .context = p.context
    , .data = 0
    };

    rc = fi_writemsg(p.ep, &mrma, p.flags);

    if (rc != 0)
        return rc;

    for (i = j = 0, nremaining = len; i < p.niovs; i++) {
        if (nremaining >= p.iov_in[i].iov_len) {
            nremaining -= p.iov_in[i].iov_len;
            continue;
        }
        p.desc_out[j] = p.desc_in[i];
        p.iov_out[j] = p.iov_in[i];
        if (nremaining > 0) {
            p.iov_out[j].iov_len -= nremaining;
            p.iov_out[j].iov_base = (char *)p.iov_out[j].iov_base + nremaining;
            nremaining = 0;
        }
        j++;
    }
    *p.niovs_out = j;

    for (i = j = 0, nremaining = len; i < p.nriovs; i++) {
        if (nremaining >= p.riov_in[i].len) {
            nremaining -= p.riov_in[i].len;
            continue;
        }
        p.riov_out[j] = p.riov_in[i];
        if (nremaining > 0) {
            p.riov_out[j].len -= nremaining;
            p.riov_out[j].addr = p.riov_out[j].addr + nremaining;
            nremaining = 0;
        }
        j++;
    }

    *p.nriovs_out = j;
    return len;
}

static xmtr_t *
xmtr_vector_rx(xmtr_t *x)
{
    struct fi_cq_msg_entry completion;
    ssize_t ncompleted;

    if ((ncompleted = fi_cq_read(x->cxn.cq, &completion, 1)) == -FI_EAGAIN)
        return x;

    if (ncompleted < 0)
        bailout_for_ofi_ret(ncompleted, "fi_cq_read");

    if (ncompleted != 1) {
        errx(EXIT_FAILURE,
            "%s: expected 1 completion, read %zd", __func__, ncompleted);
    }

    if ((completion.flags & desired_rx_flags) != desired_rx_flags) {
        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_rx_flags, completion.flags & desired_rx_flags);
    }

    const ptrdiff_t least_vector_msglen =
        (char *)&x->vector.msg.iov[0] - (char *)&x->vector.msg;

    if (completion.len < least_vector_msglen) {
        errx(EXIT_FAILURE, "%s: expected >= %zu bytes, received %zu",
            __func__, least_vector_msglen, completion.len);
    }

    if (completion.len == least_vector_msglen) {
        errx(EXIT_FAILURE, "%s: peer sent 0 vectors, disconnecting...",
            __func__);
    }

    if ((completion.len - least_vector_msglen) %
        sizeof(x->vector.msg.iov[0]) != 0) {
        errx(EXIT_FAILURE,
            "%s: %zu-byte vector message did not end on vector boundary, "
            "disconnecting...", __func__, completion.len);
    }

    const size_t niovs_space = (completion.len - least_vector_msglen) /
        sizeof(x->vector.msg.iov[0]);

    if (niovs_space < x->vector.msg.niovs) {
        errx(EXIT_FAILURE, "%s: peer sent truncated vectors, disconnecting...",
            __func__);
    }

    if (x->vector.msg.niovs > arraycount(x->vector.msg.iov)) {
        errx(EXIT_FAILURE, "%s: peer sent too many vectors, disconnecting...",
            __func__);
    }

    return x;
}

static session_t *
xmtr_loop(worker_t *w, session_t *s)
{
    struct fi_cq_msg_entry completion;
    struct fi_rma_iov riov[12], riov2[12];
    xmtr_t *x = (xmtr_t *)s->cxn;
    const size_t txbuflen = strlen(txbuf);
    size_t i, orig_nriovs;
    ssize_t rc;
    ssize_t ncompleted;

    if (!x->started)
        return xmtr_start(s);

    if (xmtr_vector_rx(x) == NULL)
        goto out;

    x->payload.iov[0] = (struct iovec){.iov_base = txbuf, .iov_len = txbuflen};
    x->payload.desc[0] = fi_mr_desc(x->payload.mr[0]);

    for (i = 0; i < x->vector.msg.niovs; i++) {
        warnx("%s: received vector %zd "
            "addr %" PRIu64 " len %" PRIu64 " key %" PRIx64,
            __func__, i, x->vector.msg.iov[i].addr, x->vector.msg.iov[i].len,
            x->vector.msg.iov[i].key);
        riov[i].len = x->vector.msg.iov[i].len;
        riov[i].addr = x->vector.msg.iov[i].addr;
        riov[i].key = x->vector.msg.iov[i].key;
    }

    orig_nriovs = i;

    bool which = true;
    ssize_t nwritten, total;
    size_t nriovs = orig_nriovs;
    size_t niovs = 1, niovs_out = 0, nriovs_out = 0;

    for (total = 0; total < txbuflen; total += nwritten, which = !which) {

        write_fully_params_t p = {.ep = x->ep,
            .iov_in = which ? x->payload.iov : x->payload.iov2,
            .desc_in = which ? x->payload.desc : x->payload.desc2,
            .iov_out = which ? x->payload.iov2 : x->payload.iov,
            .desc_out = which ? x->payload.desc2 : x->payload.desc,
            .niovs = niovs,
            .niovs_out = &niovs_out,
            .riov_in = which ? riov : riov2,
            .riov_out = which ? riov2 : riov,
            .nriovs = nriovs,
            .nriovs_out = &nriovs_out,
            .len = txbuflen - total,
            .maxsegs = minsize(w->rma_maxsegs, x->vector.msg.niovs),
            .flags = FI_COMPLETION | FI_DELIVERY_COMPLETE,
            .context = &x->payload.context,
            .addr = x->cxn.peer_addr};

        nwritten = write_fully(p);

        if (nwritten < 0)
            bailout_for_ofi_ret(nwritten, "write_fully");

        niovs = niovs_out;
        nriovs = nriovs_out;

        /* Await RDMA completion. Pass flags FI_COMPLETION |
         * FI_DELIVERY_COMPLETE to fi_writemsg, above.
         */
        do {
            warnx("%s: awaiting RMA completion, context %p, remote count %zu",
                __func__, (void *)&x->payload, orig_nriovs);
            ncompleted = fi_cq_sread(x->cxn.cq, &completion, 1, NULL, -1);
        } while (ncompleted == -FI_EAGAIN);

        if (ncompleted == -FI_EAVAIL) {
            struct fi_cq_err_entry e;
            char errbuf[256];
            ssize_t nfailed = fi_cq_readerr(x->cxn.cq, &e, 0);

            warnx("%s: read %zd errors, %s", __func__, nfailed,
                fi_strerror(e.err));
            warnx("%s: context %p", __func__, (void *)e.op_context);
            warnx("%s: completion flags %" PRIx64 " expected %" PRIx64,
                __func__, e.flags, desired_wr_flags);
            warnx("%s: provider error %s", __func__,
                fi_cq_strerror(x->cxn.cq, e.prov_errno, e.err_data, errbuf,
                    sizeof(errbuf)));
            goto out;
        } else if (ncompleted < 0) {
            bailout_for_ofi_ret(ncompleted, "fi_cq_sread");
        }

        if (ncompleted != 1) {
            errx(EXIT_FAILURE,
                "%s: expected 1 completion, read %zd", __func__, ncompleted);
        }
    }

    x->progress.msg.nfilled = txbuflen;
    x->progress.msg.nleftover = 0;

    x->progress.desc = fi_mr_desc(x->progress.mr);

    struct iovec iov = {
      .iov_base = &x->progress.msg
    , .iov_len = sizeof(x->progress.msg)
    };

    rc = fi_sendmsg(x->ep, &(struct fi_msg){
          .msg_iov = &iov
        , .desc = &x->progress.desc
        , .iov_count = 1
        , .addr = x->cxn.peer_addr
        , .context = &x->progress.context
        , .data = 0
        }, FI_COMPLETION);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_sendmsg");

    /* Await transmission of progress message. */
    do {
        warnx("%s: awaiting progress message transmission, context %p, len %zu", __func__,
            (void *)&x->progress, iov.iov_len);
        ncompleted = fi_cq_sread(x->cxn.cq, &completion, 1, NULL, -1);
    } while (ncompleted == -FI_EAGAIN);

    if (ncompleted == -FI_EAVAIL) {
        struct fi_cq_err_entry e;
        char errbuf[256];
        ssize_t nfailed = fi_cq_readerr(x->cxn.cq, &e, 0);

        warnx("%s: read %zd errors, %s", __func__, nfailed,
            fi_strerror(e.err));
        warnx("%s: context %p", __func__, (void *)e.op_context);
        warnx("%s: completion flags %" PRIx64 " expected %" PRIx64,
            __func__, e.flags, desired_rx_flags);
        warnx("%s: provider error %s", __func__,
            fi_cq_strerror(x->cxn.cq, e.prov_errno, e.err_data, errbuf,
                sizeof(errbuf)));
        abort();
    } else if (ncompleted < 0)
        bailout_for_ofi_ret(ncompleted, "fi_cq_sread");

    if (ncompleted != 1) {
        errx(EXIT_FAILURE,
            "%s: expected 1 completion, read %zd", __func__, ncompleted);
    }

    if ((completion.flags & desired_tx_flags) != desired_tx_flags) {
        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_tx_flags, completion.flags & desired_tx_flags);
    }

    warnx("sent %zu-byte progress message", sizeof(x->progress.msg));

out:

    return NULL;
}

static session_t *
cxn_loop(worker_t *w, session_t *s)
{
    warnx("%s: going around", __func__);
    return s->cxn->loop(w, s);
}

static void
worker_run_loop(worker_t *self)
{
    size_t half, i;

    for (half = 0; half < 2; half++) {
        void *context;
        pthread_mutex_t *mtx = &self->mtx[half];
        int rc;

        if (pthread_mutex_trylock(mtx) == EBUSY)
            continue;

        if ((rc = fi_poll(self->pollset[half], &context, 1)) < 0) {
            (void)pthread_mutex_unlock(mtx);
            bailout_for_ofi_ret(rc, "fi_poll");
        }

        /* Find a non-empty session slot and go once through
         * connection & terminal loops.
         */
        for (i = 0; i < arraycount(self->session) / 2; i++) {
            session_t *s;
            cxn_t *c, **cp;

            s = &self->session[half * arraycount(self->session) / 2 + i];
            cp = &s->cxn;

            // skip empty slots
            if ((c = *cp) == NULL)
                continue;

            // continue at next cxn_t if `c` did not exit
            if (cxn_loop(self, s) != NULL)
                continue;

            *cp = NULL;

            if ((rc = fi_poll_del(self->pollset[half], &c->cq->fid, 0)) != 0) {
                warn_about_ofi_ret(rc, "fi_poll_del");
                continue;
            }
            atomic_fetch_add_explicit(&self->nsessions[half], -1,
                memory_order_relaxed);
        }

        (void)pthread_mutex_unlock(mtx);
    }
}

static bool
worker_is_idle(worker_t *self)
{
    const ptrdiff_t self_idx = self - &workers[0];
    size_t half, nlocked;

    if (self->nsessions[0] != 0 || self->nsessions[1] != 0)
        return false;

    if (self_idx + 1 !=
            atomic_load_explicit(&nworkers_running, memory_order_relaxed))
        return false;

    if (pthread_mutex_trylock(&workers_mtx) == EBUSY)
        return false;

    for (nlocked = 0; nlocked < 2; nlocked++) {
        if (pthread_mutex_trylock(&self->mtx[nlocked]) == EBUSY)
            break;
    }

    bool idle = (nlocked == 2 &&
                 self->nsessions[0] == 0 && self->nsessions[1] == 0 &&
                 self_idx + 1 == nworkers_running);

    if (idle) {
        nworkers_running--;
        pthread_cond_signal(&nworkers_cond);
    }

    for (half = 0; half < nlocked; half++)
        (void)pthread_mutex_unlock(&self->mtx[half]);

    (void)pthread_mutex_unlock(&workers_mtx);

    return idle;
}

static void
worker_idle_loop(worker_t *self)
{
    const ptrdiff_t self_idx = self - &workers[0];

    (void)pthread_mutex_lock(&workers_mtx);
    while (nworkers_running <= self_idx && !self->cancelled)
        pthread_cond_wait(&self->sleep, &workers_mtx);
    (void)pthread_mutex_unlock(&workers_mtx);
}

static void *
worker_outer_loop(void *arg)
{
    worker_t *self = arg;

    while (!self->cancelled) {
        worker_idle_loop(self);
        do {
            worker_run_loop(self);
        } while (!worker_is_idle(self) && !self->cancelled);
    }
    return NULL;
}

static void
worker_buflist_destroy(struct fid_domain *dom, buflist_t *bl)
{
    size_t i;
    int rc;

    for (i = 0; i < bl->nfull; i++) {
        buf_t *b = bl->buf[i];

        if ((rc = fi_close(&b->mr->fid)) != 0)
            warn_about_ofi_ret(rc, "fi_mr_reg");

        free(b);
    }
    bl->nfull = bl->nallocated = 0;
    free(bl);
}

static buflist_t *
worker_buflist_create(worker_t *w, uint64_t access)
{
    buflist_t *bl = malloc(offsetof(buflist_t, buf[256]));

    if (bl == NULL)
        err(EXIT_FAILURE, "%s.%d: malloc", __func__, __LINE__);

    bl->access = access;
    bl->nallocated = 16;
    bl->nfull = 0;

    if (!worker_buflist_replenish(w, bl)) {
        worker_buflist_destroy(w->dom, bl);
        return NULL;
    }

    return bl;
}

static void
worker_init(worker_t *w, const state_t *st)
{
    struct fi_poll_attr attr = {.flags = 0};
    int rc;
    size_t i;

    w->cancelled = false;
    w->dom = st->domain;
    w->mr_maxsegs = st->mr_maxsegs;
    w->rma_maxsegs = st->rma_maxsegs;
    w->rx_maxsegs = st->rx_maxsegs;
    keysource_init(&w->keys);

    if ((rc = pthread_cond_init(&w->sleep, NULL)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_cond_init: %s", __func__, __LINE__,
            strerror(rc));
    }

    for (i = 0; i < arraycount(w->mtx); i++) {
        if ((rc = pthread_mutex_init(&w->mtx[i], NULL)) != 0) {
            errx(EXIT_FAILURE, "%s.%d: pthread_mutex_init: %s",
                __func__, __LINE__, strerror(rc));
        }
        if ((rc = fi_poll_open(w->dom, &attr, &w->pollset[i])) != 0)
            bailout_for_ofi_ret(rc, "fi_poll_open");
    }
    for (i = 0; i < arraycount(w->session); i++)
        w->session[i] = (session_t){.cxn = NULL, .terminal = NULL};

    w->freebufs.rx = worker_buflist_create(w, FI_RECV | FI_REMOTE_WRITE);
    w->freebufs.tx = worker_buflist_create(w, FI_SEND);
}

static void
worker_launch(worker_t *w)
{
    int rc;

    if ((rc = pthread_create(&w->thd, NULL, worker_outer_loop, w)) != 0) {
            errx(EXIT_FAILURE, "%s.%d: pthread_create: %s",
                __func__, __LINE__, strerror(rc));
    }
}

#if 0
static void
worker_teardown(worker_t *w)
{
    int rc;
    size_t i;

    for (i = 0; i < arraycount(w->mtx); i++) {
        if ((rc = pthread_mutex_destroy(&w->mtx[i])) != 0) {
            errx(EXIT_FAILURE, "%s.%d: pthread_mutex_destroy: %s",
                __func__, __LINE__, strerror(rc));
        }
        if ((rc = fi_close(&w->pollset[i]->fid)) != 0)
            bailout_for_ofi_ret(rc, "fi_close");
    }
    for (i = 0; i < arraycount(w->session); i++)
        assert(w->session[i].cxn == NULL && w->session[i].terminal == NULL);

    if ((rc = pthread_cond_destroy(&w->sleep)) != 0) {
        errx(EXIT_FAILURE, "%s.%d: pthread_cond_destroy: %s",
            __func__, __LINE__, strerror(rc));
    }
    // TBD release buffers and free-buffer reservior.
}
#endif

static worker_t *
worker_create(state_t *st)
{
    worker_t *w;

    (void)pthread_mutex_lock(&workers_mtx);
    w = (nworkers_allocated < arraycount(workers))
        ? &workers[nworkers_allocated++]
        : NULL;
    if (w != NULL)
        worker_init(w, st);
    (void)pthread_mutex_unlock(&workers_mtx);

    if (w != NULL)
        worker_launch(w);

    return w;
}

static void
workers_initialize(void)
{
}

static bool
worker_assign_session(worker_t *w, session_t *s)
{
    size_t half, i;
    int rc;

    for (half = 0; half < 2; half++) {
        pthread_mutex_t *mtx = &w->mtx[half];

        if (pthread_mutex_trylock(mtx) == EBUSY)
            continue;

        // find an empty receiver slot
        for (i = 0; i < arraycount(w->session) / 2; i++) {
            session_t *slot =
                &w->session[half * arraycount(w->session) / 2 + i];

            if (slot->cxn != NULL)
                continue;

            rc = fi_poll_add(w->pollset[half], &s->cxn->cq->fid, 0);
            if (rc != 0) {
                warn_about_ofi_ret(rc, "fi_poll_add");
                continue;
            }
            atomic_fetch_add_explicit(&w->nsessions[half], 1,
                memory_order_relaxed);
            *slot = *s;
            (void)pthread_mutex_unlock(mtx);
            return true;
        }
        (void)pthread_mutex_unlock(mtx);
    }
    return false;
}

/* Try to allocate `c` to an active worker, least active, first.
 * Caller must hold `workers_mtx`.
 */
static worker_t *
workers_assign_session_to_running(session_t *s)
{
    size_t iplus1;

    for (iplus1 = nworkers_running; 0 < iplus1; iplus1--) {
        size_t i = iplus1 - 1;
        worker_t *w = &workers[i];
        if (worker_assign_session(w, s))
            return w;
    }
    return NULL;
}

/* Try to assign `c` to the next idle worker servicing `dom`.
 * Caller must hold `workers_mtx`.
 */
static worker_t *
workers_assign_session_to_idle(session_t *s)
{
    size_t i;

    if ((i = nworkers_running) < nworkers_allocated) {
        worker_t *w = &workers[i];
        if (worker_assign_session(w, s))
            return w;
    }
    return NULL;
}

/* Try to wake the first idle worker.
 *
 * Caller must hold `workers_mtx`.
 */
static void
workers_wake(worker_t *w)
{
    assert(&workers[nworkers_running] == w);
    nworkers_running++;
    pthread_cond_signal(&w->sleep);
}

static worker_t *
workers_assign_session(state_t *st, session_t *s)
{
    worker_t *w;

    do {
        (void)pthread_mutex_lock(&workers_mtx);

        if (workers_assignment_suspended) {
            (void)pthread_mutex_unlock(&workers_mtx);
            return NULL;
        }

        if ((w = workers_assign_session_to_running(s)) != NULL)
            ;
        else if ((w = workers_assign_session_to_idle(s)) != NULL)
            workers_wake(w);
        (void)pthread_mutex_unlock(&workers_mtx);
    } while (w == NULL && (w = worker_create(st)) != NULL);

    return w;
}

static void
workers_join_all(void)
{
    size_t i;

    (void)pthread_mutex_lock(&workers_mtx);

    workers_assignment_suspended = true;

    while (nworkers_running > 0) {
        pthread_cond_wait(&nworkers_cond, &workers_mtx);
    }

    for (i = 0; i < nworkers_allocated; i++) {
        worker_t *w = &workers[i];
        w->cancelled = true;
        pthread_cond_signal(&w->sleep);
    }

    (void)pthread_mutex_unlock(&workers_mtx);

    for (i = 0; i < nworkers_allocated; i++) {
        worker_t *w = &workers[i];
        int rc;

        if ((rc = pthread_join(w->thd, NULL)) != 0) {
                errx(EXIT_FAILURE, "%s.%d: pthread_join: %s",
                    __func__, __LINE__, strerror(rc));
        }
    }
}

static void
cxn_init(cxn_t *c, struct fid_av *av,
    session_t *(*loop)(worker_t *, session_t *))
{
    memset(c, 0, sizeof(*c));
    c->loop = loop;
    c->av = av;
}

static void
xmtr_memory_init(state_t *st, xmtr_t *x)
{
    const size_t txbuflen = strlen(txbuf);
    int rc;

    rc = fi_mr_reg(st->domain, &x->initial.msg, sizeof(x->initial.msg),
        FI_SEND, 0, keysource_next(&st->keys), 0, &x->initial.mr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");

    rc = fi_mr_reg(st->domain, &x->ack.msg, sizeof(x->ack.msg),
        FI_RECV, 0, keysource_next(&st->keys), 0, &x->ack.mr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");

    rc = fi_mr_reg(st->domain, &x->vector.msg, sizeof(x->vector.msg),
        FI_RECV, 0, keysource_next(&st->keys), 0, &x->vector.mr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");

    rc = fi_mr_reg(st->domain, &x->progress.msg, sizeof(x->progress.msg),
        FI_SEND, 0, keysource_next(&st->keys), 0, &x->progress.mr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");

    rc = fi_mr_reg(st->domain, txbuf, txbuflen,
        FI_WRITE, 0, keysource_next(&st->keys), 0, x->payload.mr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_mr_reg");
}

static void
xmtr_init(state_t *st, xmtr_t *x, struct fid_av *av)
{
    memset(x, 0, sizeof(*x));

    cxn_init(&x->cxn, av, xmtr_loop);
    xmtr_memory_init(st, x);
    x->started = false;
}

static void
sink_init(sink_t *s)
{
    memset(s, 0, sizeof(*s));
    s->terminal.trade = sink_trade;
    s->txbuflen = strlen(txbuf);
    s->idx = 0;
}

static void
rcvr_memory_init(state_t *st, rcvr_t *r)
{
    int rc;

    r->initial.niovs = fibonacci_iov_setup(&r->initial.msg,
        sizeof(r->initial.msg), r->initial.iov, st->rx_maxsegs);

    if (r->initial.niovs < 1) {
        errx(EXIT_FAILURE, "%s: unexpected I/O vector length %zd",
            __func__, r->initial.niovs);
    }

    rc = mr_regv_all(st->domain, r->initial.iov, r->initial.niovs,
        minsize(2, st->mr_maxsegs), FI_RECV, 0, &st->keys, 0,
        r->initial.mr, r->initial.desc, r->initial.raddr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "mr_regv_all");

    r->ack.niovs = fibonacci_iov_setup(&r->ack.msg,
        sizeof(r->ack.msg), r->ack.iov, st->rx_maxsegs);

    if (r->ack.niovs < 1) {
        errx(EXIT_FAILURE, "%s: unexpected I/O vector length %zd",
            __func__, r->ack.niovs);
    }

    rc = mr_regv_all(st->domain, r->ack.iov, r->ack.niovs,
        minsize(2, st->mr_maxsegs), FI_RECV, 0, &st->keys, 0,
        r->ack.mr, r->ack.desc, r->ack.raddr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "mr_regv_all");

    r->progress.niovs = fibonacci_iov_setup(&r->progress.msg,
        sizeof(r->progress.msg), r->progress.iov, st->rx_maxsegs);

    if (r->progress.niovs < 1) {
        errx(EXIT_FAILURE, "%s: unexpected I/O vector length %zd",
            __func__, r->progress.niovs);
    }

    rc = mr_regv_all(st->domain, r->progress.iov, r->progress.niovs,
        minsize(2, st->mr_maxsegs), FI_RECV, 0, &st->keys, 0,
        r->progress.mr, r->progress.desc, r->progress.raddr, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "mr_regv_all");
}

static void
rcvr_init(state_t *st, rcvr_t *r, struct fid_av *av)
{
    memset(r, 0, sizeof(*r));

    cxn_init(&r->cxn, av, rcvr_loop);
    rcvr_memory_init(st, r);
    r->rxfifo = fifo_create(64);
    r->started = false;
}

static int
get(state_t *st)
{
    struct fi_av_attr av_attr = {
      .type = FI_AV_UNSPEC
    , .rx_ctx_bits = 0
    , .count = 0
    , .ep_per_node = 0
    , .name = NULL
    , .map_addr = NULL
    , .flags = 0
    };
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
    struct fid_av *av;
    get_state_t *gst = &st->u.get;
    rcvr_t *r = &gst->rcvr;
    sink_t *s = &gst->sink;
    session_t sess;
    struct fi_cq_msg_entry completion;
    ssize_t ncompleted;
    worker_t *w;
    int rc;

    rc = fi_av_open(st->domain, &av_attr, &av, NULL); 

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_av_open");

    rcvr_init(st, r, av);
    sink_init(s);

    if (!session_init(&sess, &r->cxn, &s->terminal))
        errx(EXIT_FAILURE, "%s: failed to initialize session", __func__);

    rc = fi_endpoint(st->domain, st->info, &gst->listen_ep, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_endpoint");

    rc = fi_eq_open(st->fabric, &eq_attr, &gst->listen_eq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_eq_open (listen)");

    rc = fi_cq_open(st->domain, &cq_attr, &gst->listen_cq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_cq_open");

    if ((rc = fi_ep_bind(gst->listen_ep, &gst->listen_cq->fid,
        FI_SELECTIVE_COMPLETION | FI_RECV | FI_TRANSMIT)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (completion queue)");

    if ((rc = fi_eq_open(st->fabric, &eq_attr, &r->eq, NULL)) != 0)
        bailout_for_ofi_ret(rc, "fi_eq_open (active)");

    if ((rc = fi_ep_bind(gst->listen_ep, &gst->listen_eq->fid, 0)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (event queue)");

    if ((rc = fi_ep_bind(gst->listen_ep, &av->fid, 0)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (address vector)");

    if ((rc = fi_enable(gst->listen_ep)) != 0)
        bailout_for_ofi_ret(rc, "fi_enable");

    rc = fi_recvmsg(gst->listen_ep, &(struct fi_msg){
          .msg_iov = r->initial.iov
        , .desc = r->initial.desc
        , .iov_count = r->initial.niovs
        , .addr = r->cxn.peer_addr
        , .context = NULL
        , .data = 0
        }, FI_COMPLETION);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_recvmsg");

    /* Await initial message. */
    do {
        ncompleted = fi_cq_sread(gst->listen_cq, &completion, 1, NULL, -1);
    } while (ncompleted == -FI_EAGAIN);

    if (ncompleted < 0)
        bailout_for_ofi_ret(ncompleted, "fi_cq_sread");

    if (ncompleted != 1) {
        errx(EXIT_FAILURE,
            "%s: expected 1 completion, read %zd", __func__, ncompleted);
    }

    if ((completion.flags & desired_rx_flags) != desired_rx_flags) {
        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_rx_flags, completion.flags & desired_rx_flags);
    }

    if (completion.len != sizeof(r->initial.msg)) {
        errx(EXIT_FAILURE,
            "initially received %zu bytes, expected %zu\n", completion.len,
            sizeof(r->initial.msg));
    }

    if (r->initial.msg.nsources != 1 || r->initial.msg.id != 0) {
        errx(EXIT_FAILURE,
            "received nsources %" PRIu32 ", id %" PRIu32 ", expected 1, 0\n",
            r->initial.msg.nsources, r->initial.msg.id);
    }

    rc = fi_av_insert(r->cxn.av, r->initial.msg.addr, 1, &r->cxn.peer_addr,
        0, NULL);

    if (rc < 0) {
        bailout_for_ofi_ret(rc, "fi_av_insert initial.msg.addr %p",
            r->initial.msg.addr);
    }

    struct fi_info *ep_info, *hints = fi_dupinfo(st->info);

    hints->dest_addr = r->initial.msg.addr;
    hints->dest_addrlen = r->initial.msg.addrlen;
    hints->src_addr = NULL;
    hints->src_addrlen = 0;

    rc = fi_getinfo(FI_VERSION(1, 13), NULL, NULL, 0, hints, &ep_info);

    if ((rc = fi_endpoint(st->domain, ep_info, &r->ep, NULL)) < 0)
        bailout_for_ofi_ret(rc, "fi_endpoint");

    hints->dest_addr = NULL;    // fi_freeinfo wants to free(3) dest_addr
    hints->dest_addrlen = 0;
    fi_freeinfo(hints);

    fi_freeinfo(ep_info);

    if ((rc = fi_ep_bind(r->ep, &r->eq->fid, 0)) < 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    if ((rc = fi_cq_open(st->domain, &cq_attr, &r->cxn.cq, NULL)) != 0)
        bailout_for_ofi_ret(rc, "fi_cq_open");

    if ((rc = fi_ep_bind(r->ep, &r->cxn.cq->fid,
        FI_SELECTIVE_COMPLETION | FI_RECV | FI_TRANSMIT)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    if ((rc = fi_ep_bind(r->ep, &av->fid, 0)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (address vector)");

    if ((rc = fi_enable(r->ep)) != 0)
        bailout_for_ofi_ret(rc, "fi_enable");

    rc = fi_recvmsg(r->ep, &(struct fi_msg){
          .msg_iov = r->progress.iov
        , .desc = r->progress.desc
        , .iov_count = r->progress.niovs
        , .addr = r->cxn.peer_addr
        , .context = &r->progress.context
        , .data = 0
        }, FI_COMPLETION);

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_recvmsg");

    size_t addrlen = sizeof(r->ack.msg.addr);

    rc = fi_getname(&r->ep->fid, r->ack.msg.addr, &addrlen);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_getname");

    r->ack.msg.addrlen = (uint32_t)addrlen;

    while ((rc = fi_sendmsg(r->ep, &(struct fi_msg){
          .msg_iov = r->ack.iov
        , .desc = r->ack.desc
        , .iov_count = r->ack.niovs
        , .addr = r->cxn.peer_addr
        , .context = NULL
        , .data = 0
        }, 0)) == -FI_EAGAIN) {
        /* Await reply to initial message: first vector message. */
        ncompleted = fi_cq_read(r->cxn.cq, &completion, 1);

        if (ncompleted == -FI_EAGAIN)
            continue;

        if (ncompleted < 0)
            bailout_for_ofi_ret(ncompleted, "fi_cq_sread");

        if (ncompleted != 1) {
            errx(EXIT_FAILURE,
                "%s: expected 1 completion, read %zd", __func__, ncompleted);
        }

        errx(EXIT_FAILURE,
            "%s: expected flags %" PRIu64 ", received flags %" PRIu64,
            __func__, desired_rx_flags, completion.flags & desired_rx_flags);
    }

    if (rc < 0)
        bailout_for_ofi_ret(rc, "fi_sendmsg");

    if ((w = workers_assign_session(st, &sess)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not assign a new receiver to a worker",
            __func__);
    }

    workers_join_all();

    return EXIT_SUCCESS;
}

static int
put(state_t *st)
{
    struct fi_av_attr av_attr = {
      .type = FI_AV_UNSPEC
    , .rx_ctx_bits = 0
    , .count = 0
    , .ep_per_node = 0
    , .name = NULL
    , .map_addr = NULL
    , .flags = 0
    };
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
    session_t sess;
    put_state_t *pst = &st->u.put;
    xmtr_t *x = &pst->xmtr;
    source_t *s = &pst->source;
    struct fid_av *av;
    worker_t *w;
    int rc;

    rc = fi_av_open(st->domain, &av_attr, &av, NULL); 

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_av_open");

    xmtr_init(st, x, av);

    if (!session_init(&sess, &x->cxn, &s->terminal))
        errx(EXIT_FAILURE, "%s: failed to initialize session", __func__);

    rc = fi_endpoint(st->domain, st->info, &x->ep, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_endpoint");

    rc = fi_cq_open(st->domain, &cq_attr, &x->cxn.cq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_cq_open");

    rc = fi_eq_open(st->fabric, &eq_attr, &x->eq, NULL);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_eq_open");

    rc = fi_ep_bind(x->ep, &x->eq->fid, 0);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    rc = fi_ep_bind(x->ep, &x->cxn.cq->fid,
        FI_SELECTIVE_COMPLETION | FI_RECV | FI_TRANSMIT);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind");

    if ((rc = fi_ep_bind(x->ep, &av->fid, 0)) != 0)
        bailout_for_ofi_ret(rc, "fi_ep_bind (address vector)");

    rc = fi_enable(x->ep);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_enable");

    rc = fi_av_insert(av, st->info->dest_addr, 1, &x->cxn.peer_addr, 0, NULL);

    if (rc < 0) {
        bailout_for_ofi_ret(rc, "fi_av_insert dest_addr %p",
            st->info->dest_addr);
    }

    /* Setup initial message. */
    memset(&x->initial.msg, 0, sizeof(x->initial.msg));
    x->initial.msg.nsources = 1;
    x->initial.msg.id = 0;

    x->initial.desc = fi_mr_desc(x->initial.mr);

    size_t addrlen = sizeof(x->initial.msg.addr);

    rc = fi_getname(&x->ep->fid, x->initial.msg.addr, &addrlen);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_getname");

    x->initial.msg.addrlen = (uint32_t)addrlen;

    if ((w = workers_assign_session(st, &sess)) == NULL) {
        errx(EXIT_FAILURE, "%s: could not assign a new transmitter to a worker",
            __func__);
    }

    workers_join_all();

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

    workers_initialize();

    keysource_init(&st.keys);

    warnx("%ld POSIX I/O vector items maximum", sysconf(_SC_IOV_MAX));

    if ((hints = fi_allocinfo()) == NULL)
        errx(EXIT_FAILURE, "%s: fi_allocinfo", __func__);

    hints->ep_attr->type = FI_EP_RDM;
    hints->caps = FI_MSG | FI_RMA | FI_REMOTE_WRITE | FI_WRITE;
    hints->mode = FI_CONTEXT;
    hints->domain_attr->mr_mode = FI_MR_PROV_KEY;

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

    warnx("%d infos found", count_info(st.info));

    if ((st.info->mode & FI_CONTEXT) != 0) {
        errx(EXIT_FAILURE,
           "contexts should embed fi_context, but I don't do that, yet.");
    }

    rc = fi_fabric(st.info->fabric_attr, &st.fabric, NULL /* app context */);

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_fabric");

    rc = fi_domain(st.fabric, st.info, &st.domain, NULL);

    warnx("provider %s, memory-registration I/O vector limit %zu",
        st.info->fabric_attr->prov_name,
        st.info->domain_attr->mr_iov_limit);

    warnx("provider %s %s application-requested memory-registration keys",
        st.info->fabric_attr->prov_name,
        ((st.info->domain_attr->mr_mode & FI_MR_PROV_KEY) != 0)
            ? "does not support"
            : "supports");

    if ((st.info->domain_attr->mr_mode & FI_MR_VIRT_ADDR) != 0) {
        warnx("provider %s RDMA uses virtual addresses instead of offsets, "
            "quitting.", st.info->fabric_attr->prov_name);
        exit(EXIT_FAILURE);
    }

    warnx("Rx/Tx I/O vector limits %zu/%zu",
        st.info->rx_attr->iov_limit, st.info->tx_attr->iov_limit);

    warnx("RMA I/O vector limit %zu", st.info->tx_attr->rma_iov_limit);

    st.mr_maxsegs = 1; // st.info->domain_attr->mr_iov_limit;
    st.rx_maxsegs = 1;
    st.tx_maxsegs = 1;
    st.rma_maxsegs = st.info->tx_attr->rma_iov_limit;

#if 0
    warnx("maximum endpoint message size (RMA limit) %zu",
        st.info->ep_attr->max_msg_size);
#endif

    if (rc != 0)
        bailout_for_ofi_ret(rc, "fi_domain");

    warnx("starting personality '%s'", personality_to_name(personality));

    return (*personality)(&st);
}
