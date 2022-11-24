/**
 * Copyright (c) 2021-2022, UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* fabtsuite_types.h
 *
 * fabtsuite types. Currently a collection of most of the types used in
 * the code. These will be disentangled later, when the API calls have been
 * moved to different files.
 */

#ifndef fabtsuite_types_H
#define fabtsuite_types_H

#include <inttypes.h>
#include <stdatomic.h>

#include <rdma/fabric.h>
#include <rdma/fi_rma.h>

/* If you include any fabtsuite headers here, you are probably doing something
 * wrong and will create circular dependencies.
 */

/* 
 * Typedefs and forward declarations for types in other files
 */

/* fabtsuite_buffer.h */
struct bufhdr;
typedef struct bufhdr bufhdr_t;

struct buflist;
typedef struct buflist buflist_t;

struct bytebuf;
typedef struct bytebuf bytebuf_t;

struct fragment;
typedef struct fragment fragment_t;

struct progbuf;
typedef struct progbuf progbuf_t;

struct vecbuf;
typedef struct vecbuf vecbuf_t;

/* fabtsuite_fifo.h */
struct fifo;
typedef struct fifo fifo_t;

/* fabtsuite_seqsource.h */
typedef struct seqsource {
    uint64_t next_key;
} seqsource_t;

/* fabtsuite_rxctl.h */
typedef struct rxctl {
    fifo_t *posted; // buffers posted for vector messages
    fifo_t *rcvd;   // buffers holding received vector messages
    seqsource_t tags;
    uint64_t ignore;
} rxctl_t;

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
    uint32_t pad;
    struct {
        uint64_t addr, len, key;
    } iov[12];
} vector_msg_t;

typedef struct progress_msg {
    uint64_t nfilled;
    uint64_t nleftover;
} progress_msg_t;

/* Communication buffers */

typedef enum {
    xft_ack,
    xft_fragment,
    xft_initial,
    xft_progress,
    xft_rdma_write,
    xft_vector
} xfc_type_t;

typedef enum { xfp_first = 0x1, xfp_last = 0x2 } xfc_place_t;

typedef enum { xfo_program = 0, xfo_nic = 1 } xfc_owner_t;

typedef struct {
    struct fi_context ctx; // this has to be the first member
    uint32_t type : 4;
    uint32_t owner : 1;
    uint32_t place : 2;
    uint32_t nchildren : 8;
    uint32_t cancelled : 1;
    uint32_t unused : 16;
} xfer_context_t;

typedef struct completion {
    uint64_t flags;
    size_t len;
    xfer_context_t *xfc;
} completion_t;

/* Communication terminals: sources and sinks */

typedef enum {
    loop_continue,
    loop_end,
    loop_error,
    loop_canceled
} loop_control_t;

struct terminal;
typedef struct terminal terminal_t;

struct terminal {
    /* trade(t, ready, completed) */
    loop_control_t (*trade)(terminal_t *, fifo_t *, fifo_t *);
};

typedef struct {
    terminal_t terminal;
    size_t idx;
    size_t txbuflen;
    size_t entirelen;
} sink_t;

typedef struct {
    terminal_t terminal;
    size_t idx;
    size_t txbuflen;
    size_t entirelen;
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

typedef struct {
    bool local, remote;
} eof_state_t;

struct cxn {
    uint32_t magic;
    loop_control_t (*loop)(worker_t *, session_t *);
    void (*shutdown)(cxn_t *);
    void (*cancel)(cxn_t *);
    bool (*cancellation_complete)(cxn_t *);
    struct fid_ep *ep;
    fi_addr_t peer_addr;
    struct fid_cq *cq;
    int cq_wait_fd; /* if we're using FI_WAIT_FD, the descriptor
                     * to use with epoll(2) to sleep until I/O is
                     * ready
                     */
    struct fid_av *av;
    session_t *parent; // pointer to the connection's current session_t
    bool sent_first;   /* receiving: set to `true` once this receiver sends an
                        * acknowledgement for the transmitter's original
                        * message
                        *
                        * transmitting: set to `true` once this transmitter
                        * sends an initial message to its peer
                        */
    bool cancelled;
    bool ended;
    loop_control_t end_reason;
    bool started;
    /* Receiver needs to send an empty vector.msg.niovs == 0 to close,
     * sender needs to send progress.msg.nleftover == 0, record having
     * received the remote close in `eof.remote` and having completed
     * sending it in `eof.local`.
     */
    eof_state_t eof;
    seqsource_t keys;
};

typedef struct {
    fifo_t *ready;   // message buffers ready to transmit
    fifo_t *posted;  // buffers posted with messages
    buflist_t *pool; // unused buffers
    seqsource_t tags;
    uint64_t ignore;
    unsigned rotate_ready_countdown; // counts down to 0, then resets
                                     // to rotate_ready_interval
} txctl_t;

typedef struct {
    cxn_t cxn;
    uint64_t nfull;
    fifo_t *tgtposted; // posted RDMA target buffers in order of issuance
    struct {
        xfer_context_t xfc;
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
    txctl_t vec;
    rxctl_t progress;
    unsigned split_vector_countdown; // counts down to 0, then resets
                                     // to split_vector_interval
} rcvr_t;

typedef struct {
    cxn_t cxn;
    fifo_t *wrposted; // posted RDMA writes in order of issuance
    size_t bytes_progress;
    rxctl_t vec;
    txctl_t progress;
    struct {
        xfer_context_t xfc;
        void *desc;
        struct fid_mr *mr;
        initial_msg_t msg;
    } initial;
    struct {
        xfer_context_t xfc;
        void *desc;
        struct fid_mr *mr;
        ack_msg_t msg;
    } ack;
    struct {
        struct iovec iov[12];
        void *desc[12];
        struct iovec iov2[12];
        void *desc2[12];
        struct fid_mr *mr;
    } payload;
    struct {
        buflist_t *pool; // unused fragment headers
        size_t offset;   // offset into buffer at head of ready_for_cxn
    } fragment;
    struct fi_rma_iov riov[12], riov2[12];
    size_t nriovs;
    size_t next_riov;
    bool phase;
    bool rcvd_ack; /* set to `true` once this transmitter receives
                    * an acknowledgement from its peer for the initial
                    * message
                    */
    unsigned split_progress_countdown; // counts down to 0, then resets
                                       // to split_progress_interval
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
    int max_loop_contexts;
    int min_loop_contexts;
} load_t;

#define WORKER_SESSIONS_MAX 8
#define WORKERS_MAX         128
#define SESSIONS_MAX        (WORKER_SESSIONS_MAX * WORKERS_MAX)

struct session {
    terminal_t *terminal;
    cxn_t *cxn;
    fifo_t *ready_for_cxn;
    fifo_t *ready_for_terminal;
    bool waitable;
};

typedef struct worker_stats worker_stats_t;

struct worker_stats {
    struct {
        uint64_t waitable;
        uint64_t total;
    } epoll_loops;
    struct {
        uint64_t no_io_ready;
        uint64_t no_session_ready;
        uint64_t total;
    } half_loops;
};

struct worker {
    pthread_t thd;
    sigset_t epoll_sigset;
    load_t load;
    terminal_t *term[WORKER_SESSIONS_MAX];
    session_t session[WORKER_SESSIONS_MAX];
    volatile _Atomic size_t nsessions[2]; // number of sessions in each half
                                          // of session[]
    struct fid_poll *pollset[2];
    pthread_mutex_t mtx[2]; /* mtx[0] protects pollset[0] and the first half
                             * of session[]; mtx[1], pollset[1] and the second
                             * half
                             */
    pthread_cond_t sleep;   /* Used in conjunction with workers_mtx. */
    volatile atomic_bool shutting_down;
    volatile atomic_bool canceled;
    bool failed;
    bool pollable;
    struct {
        buflist_t *tx;
        buflist_t *rx;
    } paybufs; /* Reservoirs for free payload buffers. */
    seqsource_t keys;
    worker_stats_t stats;
    int epoll_fd; /* returned by epoll_create(2) */
};

typedef struct {
    struct fi_context ctx; // this has to be the first member
    sink_t sink;
    rcvr_t rcvr;
    session_t sess;
} get_session_t;

typedef struct {
    struct fid_ep *listen_ep;
    struct fid_cq *listen_cq;
    struct fid_av *av;
    get_session_t *session;
} get_state_t;

typedef struct {
    source_t source;
    xmtr_t xmtr;
    session_t sess;
} put_session_t;

typedef struct {
    struct fid_av *av;
    put_session_t *session;
    fi_addr_t peer_addr;
} put_state_t;

typedef int (*personality_t)(void);

typedef struct {
    struct fid_domain *domain;
    struct fid_fabric *fabric;
    struct fi_info *info;
    size_t mr_maxsegs;
    size_t rx_maxsegs;
    size_t tx_maxsegs;
    size_t rma_maxsegs;
    seqsource_t keys;
    bool contiguous;
    bool expect_cancellation;
    bool reregister;
    bool waitfd;
    bool mr_endpoint;
    size_t local_sessions;
    size_t total_sessions;
    personality_t personality;
    int nextcpu;
    struct {
        unsigned first, last;
    } processors;
    volatile bool cancelled;
    pthread_t cancel_thd;
    pthread_t main_thd;
    char peer_addr_buf[256];
    char *peer_addr;
    char *address_filename;
} state_t;

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Global variables
 */

extern state_t global_state;

extern const uint64_t desired_rx_flags;
extern const uint64_t desired_tagged_rx_flags;
extern const uint64_t desired_tagged_tx_flags;

extern const unsigned split_progress_interval;
extern const unsigned split_vector_interval;
extern const unsigned rotate_ready_interval;


#ifdef __cplusplus
}
#endif

#endif
