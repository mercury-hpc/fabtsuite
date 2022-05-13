/*
 * Copyright (c) 2004, 2005, 2006, 2007 David Young.  All rights reserved.
 *
 * See COPYING at the top of the hlog distribution for license terms.
 */
/*
 * Copyright (c) 2004 Urbana-Champaign Independent Media Center.
 * All rights reserved.
 *
 * See COPYING at the top of the hlog distribution for license terms.
 */
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h> /* for uintmax_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if __STDC_VERSION__ < 201112L
#	error "A C11 compiler is needed to compile this source file."
#elif defined(__STDC_NO_ATOMICS__)
#	error "C11 atomics are required for this source file."
#endif

#include <stdatomic.h>

#include "hlog.h"
#include "hlog_time.h"

struct _hlog_msg {
	struct timespec elapsed;
	int errnum;
	bool prefix;
	bool suffix;
};

typedef struct _hlog_msg hlog_msg_t;

struct _hlog_ring;
typedef struct _hlog_ring hlog_ring_t;

static hlog_ring_t * _Atomic volatile hlog_ring_head;

#define RING_LENGTH 16384
struct _hlog_ring {
	uint64_t consumer, producer;
	char content[RING_LENGTH];
	uint64_t head_exchanges, heads_erased, tails_zeroed, times_empty,
	         truncations;
	pthread_mutex_t mtx;
	hlog_ring_t *next;
};

TAILQ_HEAD(, hlog_outlet) hlog_outlets = TAILQ_HEAD_INITIALIZER(hlog_outlets);

HLOG_OUTLET_TOP_DEFN(all);

hlog_output_t hlog_out = HLOG_OUTPUT_STDERR;
static struct timespec timestamp_zero;
_Thread_local hlog_ring_t *hlog_ring = NULL;

void hlog_init(void) hlog_constructor;
static void hlog_timestamps_init(void);

/* Write the formatted message given by `fmt` and `ap` to the
 * `buflen`-character buffer `buf` with message prefix & suffix given by
 * `msg`.  If the message is truncated, return > `buflen`.  On success,
 * return the number of characters written, which will be strictly fewer
 * than `buflen`.  On failure, return < 0.
 */
static int
hlog_msg_vsnprintf(char *buf, size_t buflen, hlog_msg_t msg, const char *fmt,
    va_list ap)
{
	size_t remaining = buflen;
	char *p = buf;
	int nwritten;

	if (msg.prefix) {
		nwritten = snprintf(p, remaining, "%ju.%.9ld ",
		    (uintmax_t)msg.elapsed.tv_sec, msg.elapsed.tv_nsec);

		if (nwritten < 0 || remaining <= (size_t)nwritten)
			return nwritten;

		assert(strlen(p) == (size_t)nwritten);
		p += nwritten;
		remaining -= (size_t)nwritten;
	}

	nwritten = vsnprintf(p, remaining, fmt, ap);

	if (nwritten < 0)
		return nwritten;
	if (remaining <= (size_t)nwritten)
		return (int)(p - buf) + nwritten;

	assert(strlen(p) == (size_t)nwritten);
	p += nwritten;
	remaining -= (size_t)nwritten;

	nwritten = (msg.errnum == 0)
	    ? snprintf(p, remaining, "%s", msg.suffix ? "\n" : "")
	    : snprintf(p, remaining, ": %s%s", strerror(msg.errnum),
	               msg.suffix ? "\n" : "");

	if (nwritten < 0)
		return nwritten;
	if (remaining <= (size_t)nwritten)
		return (int)(p - buf) + nwritten;

	assert(strlen(p) == (size_t)nwritten);
	return (int)(p - buf) + nwritten;
}

/* Return a pointer to a thread-local message ring, allocating and
 * initializing a new one if necessary.
 */
static hlog_ring_t *
hlog_ring_get(void)
{
	hlog_ring_t *r;
	int errnum;

	if ((r = hlog_ring) != NULL)
		return r;

	if ((r = calloc(1, sizeof(*r))) == NULL)
		err(EXIT_FAILURE, "could not allocate hlog ring buffer");

	if ((errnum = pthread_mutex_init(&r->mtx, NULL)) != 0) {
		errx(EXIT_FAILURE, "could not initialize hlog ring mutex: %s",
		     strerror(errnum));
	}

	r->next = hlog_ring_head;
	while (!atomic_compare_exchange_weak(&hlog_ring_head, &r->next, r))
		r->head_exchanges++;

	hlog_ring = r;
	return r;
}

/* Try to write the formatted message given by `fmt` and `ap` to the
 * ring buffer `r` with message prefix & suffix given by `msg`.  The
 * caller must hold the lock on `r`.
 *
 * On success, set `*completep` to true and return the number of
 * characters written.
 *
 * If the ring is temporarily full, free some space, set `*completep` to
 * false, and return > 0.
 *
 * On failure, return < 0.
 */
static int
hlog_msg_ring_try_vprintf(hlog_ring_t *r, hlog_msg_t msg,
    const char *fmt, va_list ap, bool *completep)
{
	uint64_t nempty = RING_LENGTH - (r->producer - r->consumer);
	const uint64_t head = r->consumer % RING_LENGTH,
	               tail = r->producer % RING_LENGTH;

	assert(r->consumer <= r->producer);

	*completep = true;

	if (nempty == RING_LENGTH) {
		/* The ring is completely empty.  Take this
		 * opportunity to reset the consumer pointer.
		 */
		const int nwritten = hlog_msg_vsnprintf(
		    &r->content[0], RING_LENGTH, msg, fmt, ap);
		if (nwritten < 0)
			return nwritten;
		if ((unsigned)nwritten >= RING_LENGTH) {
			r->truncations++;
			return nwritten;
		}
		r->consumer = 0;
		r->producer = (uint64_t)nwritten + 1;
		assert(r->producer >= r->consumer);
		return nwritten;
	} else if (head < tail) {
		/* Try to write to the free bytes between the producer pointer
		 * and the end of the buffer.  If the message does not fit in
		 * that region, then overwrite the region with empty messages
		 * and advance the producer pointer over it so that on the
		 * next attempt the producer pointer is at the start of the
		 * buffer.
		 */
		const int nwritten = hlog_msg_vsnprintf(
		        &r->content[tail], RING_LENGTH - tail, msg, fmt, ap);
		if (nwritten < 0)
			return nwritten;
		if ((unsigned)nwritten < RING_LENGTH - tail) {
			r->producer += (uint64_t)nwritten + 1;
			assert(strlen(&r->content[tail]) == (size_t)nwritten);
			assert(r->producer >= r->consumer);
			return nwritten;
		}
		memset(&r->content[tail], '\0', RING_LENGTH - tail);
		r->producer += RING_LENGTH - tail;
		r->tails_zeroed++;
		assert(r->producer >= r->consumer);
	} else if (tail < head) {
		/* Try to write to the free bytes between the producer pointer
		 * and the consumer pointer.  If the message does not fit in
		 * that region, then reclaim a message at the consumer
		 * pointer and advance the consumer pointer over that message.
		 */
		const int nwritten = hlog_msg_vsnprintf(
		    &r->content[tail], head - tail, msg, fmt, ap);
		if (nwritten < 0)
			return nwritten;
		if ((unsigned)nwritten < head - tail) {
			r->producer += (uint64_t)nwritten + 1;
			assert(strlen(&r->content[tail]) == (size_t)nwritten);
			assert(r->producer >= r->consumer);
			return nwritten;
		}
		r->content[tail] = '\0';
		assert(strlen(&r->content[head]) + head <= RING_LENGTH);
		r->consumer += strlen(&r->content[head]) + 1;
		r->heads_erased++;
	} else {
		/* The message will not fit because the buffer is full.
		 * Reclaim a message at the consumer pointer and advance
		 * the consumer pointer over that message.
		 */
		assert(strlen(&r->content[head]) + head <= RING_LENGTH);
		r->consumer += strlen(&r->content[head]) + 1;
		r->times_empty++;
		r->heads_erased++;
	}
	assert(r->producer >= r->consumer);
	*completep = false;
	return RING_LENGTH;
}

/* Empty ring `r` of messages, printing the messages to the standard
 * error stream.  Before the first message, print a header that
 * identifies the ring.  After the last message, print a footer
 * containing ring statistics.  For an empty ring, print a header
 * immediately followed by a footer.
 */
static void
hlog_ring_dump(hlog_ring_t *r)
{
	assert(r->consumer <= r->producer);

	pthread_mutex_lock(&r->mtx);

	fprintf(stderr, "ring %p\n", (void *)r);

	while (r->consumer < r->producer) {
		const uint64_t head = r->consumer % RING_LENGTH;
		const size_t msglen = strlen(&r->content[head]);

		assert(head + msglen + 1 <= RING_LENGTH);

		if (msglen != 0)
			fprintf(stderr, "%s", &r->content[head]);
		r->consumer += msglen + 1;
	}
	assert(r->consumer == r->producer);
	fprintf(stderr,
	    "\n%" PRIu64 " heads erased, %" PRIu64 " tails zeroed, %" PRIu64
	    " times empty, %" PRIu64 " truncations\n", r->heads_erased,
	    r->tails_zeroed, r->times_empty, r->truncations);
	pthread_mutex_unlock(&r->mtx);
}

/* Print all of the message rings for the process to the standard
 * error stream.
 */
static void
hlog_ring_dump_all(void)
{
	hlog_ring_t *first, *last, *r;

	for (first = hlog_ring_head, last = NULL;
	     first != last;
	     last = first, first = hlog_ring_head) {
		for (r = first; r != last; r = r->next)
			hlog_ring_dump(r);
	}
}

/* Print all of the message rings for the process, then print a reason
 * for aborting, then abort the process.
 */
void
hlog_abort(const char *reason)
{
	hlog_ring_dump_all();
	fprintf(stderr, "%s\n", reason);
	fflush(stderr);
	abort();
}

/* Write the formatted message given by `fmt` & `ap` with the message
 * prefix & suffix given by `msg` to the current thread's message ring.
 * Return the number of characters written on success.  Return < 0 on
 * failure.
 */
static int
hlog_msg_ring_vprintf(hlog_msg_t msg, const char *fmt, va_list ap)
{
	bool complete;
	int nwritten;
	hlog_ring_t * const r = hlog_ring_get();

	pthread_mutex_lock(&r->mtx);

	do {
		va_list ap_copy;

		va_copy(ap_copy, ap);
		nwritten = hlog_msg_ring_try_vprintf(r, msg, fmt, ap_copy,
		    &complete);
		va_end(ap_copy);
		if (nwritten < 0)
			break;
	} while (!complete);

	pthread_mutex_unlock(&r->mtx);

	return nwritten;
}

static void
hlog_outlet_state_init(void)
{
	const char *item, *settings0;
	char *iter, *settings;

	if ((settings0 = getenv("HLOG")) == NULL)
		return;

	if ((settings = strdup(settings0)) == NULL) {
		warn("%s: cannot duplicate settings string", __func__);
		return;
	}

	iter = settings;
	while ((item = strsep(&iter, " ,")) != NULL) {
		hlog_outlet_state_t state;
		char key[64 + 1], val[4 + 1];	// + 1 for the terminating NUL
		int nconverted;

		nconverted = sscanf(item, " %64[0-9a-z_] = %4s ", key, val);
		if (nconverted != 2) {
			warnx("%s: malformed HLOG item \"%s\"", __func__, item);
			continue;
		}

		if (strcmp(val, "on") == 0 || strcmp(val, "yes") == 0)
			state = HLOG_OUTLET_S_ON;
		else if (strcmp(val, "off") == 0 || strcmp(val, "no") == 0)
			state = HLOG_OUTLET_S_OFF;
		else if (strcmp(val, "pass") == 0)
			state = HLOG_OUTLET_S_PASS;
		else {
			warnx("%s: bad HLOG value \"%s\" in item \"%s\"",
			    __func__, val, item);
			continue;
		}

		if (hlog_set_state(key, state, true) == -1) {
			warn("%s: could not set state for HLOG item \"%s\"",
			    __func__, item);
		}
	}

	free(settings);
}

static void
hlog_output_state_init(void)
{
	const char *setting;

	if ((setting = getenv("HLOG_OUTPUT")) == NULL)
		return;

	if (strcmp(setting, "stderr") == 0)
		hlog_out = HLOG_OUTPUT_STDERR;
	else if (strcmp(setting, "stdout") == 0)
		hlog_out = HLOG_OUTPUT_STDOUT;
	else if (strcmp(setting, "ring") == 0) {
		hlog_out = HLOG_OUTPUT_RING;
	} else if (strcmp(setting, "null") == 0)
		hlog_out = HLOG_OUTPUT_NULL;
	else
		warn("%s: unknown hlog output \"%s\" selected",
		    __func__, setting);
}

void
hlog_init(void)
{
	hlog_outlet_state_init();
	hlog_output_state_init();
}


static void
hlog_timestamps_init(void)
{
	static bool initialized = false;

	if (initialized)
		return;

	if (clock_gettime(CLOCK_MONOTONIC, &timestamp_zero) == -1)
		err(EXIT_FAILURE, "%s: clock_gettime", __func__);

	initialized = true;
}

static void
hlog_msg_vfprintf(FILE *stream, hlog_msg_t msg, const char *fmt, va_list ap)
{
	if (msg.prefix) {
		(void)fprintf(stream, "%ju.%.9ld ",
		    (uintmax_t)msg.elapsed.tv_sec, msg.elapsed.tv_nsec);
	}
	(void)vfprintf(stream, fmt, ap);
	if (msg.errnum != 0) {
		(void)fprintf(stream, ": %s%s", strerror(msg.errnum),
		              msg.suffix ? "\n" : "");
	} else if (msg.suffix)
		(void)fputc('\n', stream);
}

static hlog_msg_t
hlog_msg_init(const hlog_outlet_t *ls, int errnum)
{
	struct timespec elapsed, now;

	hlog_timestamps_init();

	if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
		err(EXIT_FAILURE, "%s: clock_gettime", __func__);

	timespecsub(&now, &timestamp_zero, &elapsed);

	return (hlog_msg_t){.errnum = errnum, .elapsed = elapsed,
	                    .prefix = (ls != NULL) ? ls->ls_prefix : true,
	                    .suffix = (ls != NULL) ? ls->ls_suffix : true};
}

static void
vhlog_msg(hlog_msg_t msg, const char *fmt, va_list ap)
{
	switch (hlog_out) {
	case HLOG_OUTPUT_STDERR:
		(void)hlog_msg_vfprintf(stderr, msg, fmt, ap);
		return;
	case HLOG_OUTPUT_STDOUT:
		(void)hlog_msg_vfprintf(stdout, msg, fmt, ap);
		return;
	case HLOG_OUTPUT_RING:
		(void)hlog_msg_ring_vprintf(msg, fmt, ap);
		return;
	case HLOG_OUTPUT_NULL:
		return;
	}
}

void
vhlog(const hlog_outlet_t *ls, const char *fmt, va_list ap)
{
	vhlog_msg(hlog_msg_init(ls, 0), fmt, ap);
}

void
vhlog_err(int status, const char *fmt, va_list ap)
{
	vhlog_msg(hlog_msg_init(NULL, errno), fmt, ap);
	exit(status);
}

void
vhlog_errx(int status, const char *fmt, va_list ap)
{
	vhlog_msg(hlog_msg_init(NULL, 0), fmt, ap);
	exit(status);
}

void
vhlog_warn(const char *fmt, va_list ap)
{
	vhlog_msg(hlog_msg_init(NULL, errno), fmt, ap);
}

void
vhlog_warnx(const char *fmt, va_list ap)
{
	vhlog_msg(hlog_msg_init(NULL, 0), fmt, ap);
}

void
hlog_err(int status, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vhlog_err(status, fmt, ap);
	va_end(ap);
}

void
hlog_errx(int status, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vhlog_errx(status, fmt, ap);
	va_end(ap);
}

void
hlog_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vhlog_warn(fmt, ap);
	va_end(ap);
}

void
hlog_warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vhlog_warnx(fmt, ap);
	va_end(ap);
}

struct hlog_outlet *
hlog_outlet_find_active(struct hlog_outlet *ls0)
{
	struct hlog_outlet *ls;

	HLOG_OUTLET_FOREACH(ls, ls0) {
		switch (ls->ls_state) {
		case HLOG_OUTLET_S_PASS:
			continue;
		case HLOG_OUTLET_S_OFF:
			return NULL;
		case HLOG_OUTLET_S_ON:
		default:
			return ls;
		}
	}
	return NULL;
}

void
hlog_always(const hlog_outlet_t *ls, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vhlog(ls, fmt, ap);
	va_end(ap);
}

void
hlog_impl(struct hlog_outlet *ls0, const char *fmt, ...)
{
	struct hlog_outlet *ls;
	va_list ap;

	if ((ls = hlog_outlet_find_active(ls0)) == NULL) {
		ls0->ls_resolved = HLOG_OUTLET_S_OFF;
		return;
	}

	ls0->ls_resolved = HLOG_OUTLET_S_ON;

	va_start(ap, fmt);
	vhlog(ls, fmt, ap);
	va_end(ap);
}

static void
hlog_outlet_reset_all(void)
{
	struct hlog_outlet *ls;

	TAILQ_FOREACH(ls, &hlog_outlets, ls_next)
		ls->ls_resolved = HLOG_OUTLET_S_PASS;
}

struct hlog_outlet *
hlog_outlet_lookup(const char *name)
{
	struct hlog_outlet *ls;

	TAILQ_FOREACH(ls, &hlog_outlets, ls_next) {
		if (strcmp(ls->ls_name, name) == 0)
			return ls;
	}
	return NULL;
}

static struct hlog_outlet *
hlog_outlet_create(const char *name)
{
	struct hlog_outlet *ls;

	if ((ls = calloc(1, sizeof(*ls))) == NULL)
		return NULL;
	else if ((ls->ls_name0 = strdup(name)) == NULL) {
		free(ls);
		return NULL;
	}
	ls->ls_name = ls->ls_name0;
	ls->ls_rendezvous = true;
	return ls;
}

static void
hlog_outlet_destroy(struct hlog_outlet *ls)
{
	/*LINTED*/
	if (ls->ls_name0 != NULL)
		free(ls->ls_name0);
	free(ls);
}

int
hlog_set_output(hlog_output_t out)
{
	switch (out) {
	case HLOG_OUTPUT_STDERR:
	case HLOG_OUTPUT_STDOUT:
	case HLOG_OUTPUT_RING:
	case HLOG_OUTPUT_NULL:
		hlog_out = out;
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}

int
hlog_set_state(const char *name, hlog_outlet_state_t state, bool rendezvous)
{
	struct hlog_outlet *ls;
	errno = 0;

	switch (state) {
	case HLOG_OUTLET_S_PASS:
	case HLOG_OUTLET_S_OFF:
	case HLOG_OUTLET_S_ON:
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	if ((ls = hlog_outlet_lookup(name)) == NULL && !rendezvous) {
		errno = ESRCH;
		return -1;
	} else if (ls == NULL) {
		if ((ls = hlog_outlet_create(name)) == NULL)
			return -1;
		TAILQ_INSERT_TAIL(&hlog_outlets, ls, ls_next);
	}
	ls->ls_state = state;
	hlog_outlet_reset_all();
	return 0;
}

void
hlog_outlet_register(struct hlog_outlet *ls_arg)
{
	struct hlog_outlet *ls;
	if ((ls = hlog_outlet_lookup(ls_arg->ls_name)) == NULL ||
	    ls->ls_rendezvous) {
		TAILQ_INSERT_TAIL(&hlog_outlets, ls_arg, ls_next);
		if (ls == NULL)
			return;
		warnx("%s: rendezvous with log-outlet '%s'", __func__,
		    ls->ls_name);
		ls_arg->ls_state = ls->ls_state;
		TAILQ_REMOVE(&hlog_outlets, ls, ls_next);
		hlog_outlet_destroy(ls);
	} else
		warnx("%s: duplicate log-outlet, '%s'", __func__, ls->ls_name);
}
