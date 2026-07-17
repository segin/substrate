/*
 * lwptest — exercise substrate's NetBSD-personality LWP semantics directly
 * against the raw _lwp_* syscalls (no libpthread bookkeeping in the way).
 *
 * Covers the paths added by the "full LWP semantics" work.  Expectations were
 * established by running this same binary on a real NetBSD 10.1/i386 host:
 *   T1  _lwp_wait(0) with no waitable sibling   -> ESRCH  (NetBSD nfound==0)
 *   T2  _lwp_create + _lwp_wait(0)              -> any-LWP reap, departed == lid
 *   T3  _lwp_create(LWP_DETACHED) x N           -> kernel self-reap (no join)
 *   T4  _lwp_create(LWP_SUSPENDED)              -> parked until _lwp_continue
 *   T5  _lwp_suspend / _lwp_continue on a live  -> stops/resumes progress
 *   T6  _lwp_wait(lid) on a detached LWP        -> EINVAL
 *
 * Raw LWPs get no libpthread TCB, so their entry points touch only volatile
 * globals + atomics and then _lwp_exit(); no stdio/TLS from a raw LWP.
 *
 * Build natively on NetBSD i386:  cc -O1 -o lwptest lwptest.c
 */
#include <sys/lwp.h>

#include <errno.h>
#include <lwp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#define STACKSZ   (64 * 1024)
#define NDETACHED 200

static int failures;

static void
ok(const char *name, int pass, const char *detail)
{
	printf("%-28s %s%s%s\n", name, pass ? "PASS" : "FAIL",
	    detail && *detail ? " - " : "", detail ? detail : "");
	fflush(stdout);
	if (!pass)
		failures++;
}

/* ---- raw LWP entry points (no TLS, no stdio) ---- */

static volatile int  t_ran;
static volatile int  spin_stop;
static volatile unsigned long spin_ctr;
static volatile int  detached_done;

static void
lwp_trivial(void *arg)
{
	(void)arg;
	t_ran = 1;
	_lwp_exit();
}

static void
lwp_spin(void *arg)
{
	(void)arg;
	t_ran = 1;
	while (!spin_stop)
		spin_ctr++;
	_lwp_exit();
}

static void
lwp_detached(void *arg)
{
	(void)arg;
	__sync_fetch_and_add(&detached_done, 1);
	_lwp_exit();
}

/* A detached LWP that stays alive until told to go, so T6 can try to wait on
 * it while it still exists. */
static volatile int held_stop;
static void
lwp_detached_held(void *arg)
{
	(void)arg;
	t_ran = 1;
	while (!held_stop)
		;
	_lwp_exit();
}

/* Spawn one raw LWP.  Returns 0 on success and stores its lid. */
static int
spawn(void (*fn)(void *), unsigned long flags, lwpid_t *lid)
{
	ucontext_t uc;
	void *stack, *priv;

	stack = malloc(STACKSZ);
	priv  = malloc(256);          /* dummy TLS base; never dereferenced */
	if (stack == NULL || priv == NULL)
		return -1;
	memset(&uc, 0, sizeof(uc));
	_lwp_makecontext(&uc, fn, NULL, priv, stack, STACKSZ);
	return _lwp_create(&uc, flags, lid);
}

int
main(void)
{
	lwpid_t lid, departed;
	unsigned long a, b;
	int i, rc;

	printf("lwptest: pid %d lwp %d\n", (int)getpid(), (int)_lwp_self());

	/* T1: sole LWP -> nothing waitable.  NetBSD's lwp_wait() reports ESRCH for
	 * its `nfound == 0` case (EDEADLK is reserved for "every LWP is blocked in
	 * _lwp_wait", which this is not). */
	errno = 0;
	rc = _lwp_wait(0, &departed);
	ok("T1 wait(0) no siblings", rc == -1 && errno == ESRCH,
	    rc == -1 ? strerror(errno) : "returned success");

	/* T2: create one joinable LWP, reap it via the any-LWP wait. */
	t_ran = 0;
	lid = 0;
	if (spawn(lwp_trivial, 0, &lid) != 0) {
		ok("T2 create+wait(0)", 0, strerror(errno));
	} else {
		departed = 0;
		rc = _lwp_wait(0, &departed);
		ok("T2 create+wait(0)",
		    rc == 0 && departed == lid && t_ran == 1,
		    rc != 0 ? strerror(errno) : "");
	}

	/* T3: detached LWPs must be reaped by the kernel with no join. */
	detached_done = 0;
	rc = 0;
	for (i = 0; i < NDETACHED; i++) {
		if (spawn(lwp_detached, LWP_DETACHED, &lid) != 0) {
			rc = -1;
			break;
		}
	}
	if (rc == 0) {
		for (i = 0; i < 1000 && detached_done < NDETACHED; i++)
			usleep(1000);
	}
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%d/%d ran", detached_done, NDETACHED);
		ok("T3 detached self-reap", rc == 0 && detached_done == NDETACHED, buf);
	}
	/* A leaked detached zombie is invisible from userspace; the signal here is
	 * that 200 create/exit/reap cycles neither crash nor wedge the kernel. */

	/* T4: LWP_SUSPENDED must not run until _lwp_continue. */
	t_ran = 0;
	spin_stop = 0;
	if (spawn(lwp_trivial, LWP_SUSPENDED, &lid) != 0) {
		ok("T4 create suspended", 0, strerror(errno));
	} else {
		usleep(200 * 1000);
		if (t_ran != 0) {
			ok("T4 create suspended", 0, "ran while suspended");
		} else {
			rc = _lwp_continue(lid);
			for (i = 0; i < 500 && t_ran == 0; i++)
				usleep(1000);
			ok("T4 create suspended",
			    rc == 0 && t_ran == 1,
			    rc != 0 ? strerror(errno)
			            : (t_ran ? "" : "never ran after continue"));
		}
		(void)_lwp_wait(lid, NULL);
	}

	/* T5: suspend a running LWP -> progress stops; continue -> resumes. */
	t_ran = 0;
	spin_stop = 0;
	spin_ctr = 0;
	if (spawn(lwp_spin, 0, &lid) != 0) {
		ok("T5 suspend/continue live", 0, strerror(errno));
	} else {
		for (i = 0; i < 500 && t_ran == 0; i++)
			usleep(1000);
		if (_lwp_suspend(lid) != 0) {
			ok("T5 suspend/continue live", 0, strerror(errno));
			spin_stop = 1;
		} else {
			usleep(100 * 1000);
			a = spin_ctr;
			usleep(100 * 1000);
			b = spin_ctr;
			if (a != b) {
				ok("T5 suspend/continue live", 0, "kept running while suspended");
			} else {
				if (_lwp_continue(lid) != 0) {
					ok("T5 suspend/continue live", 0, strerror(errno));
				} else {
					usleep(200 * 1000);
					ok("T5 suspend/continue live", spin_ctr != b,
					    spin_ctr != b ? "" : "did not resume");
				}
			}
			spin_stop = 1;
		}
		(void)_lwp_wait(lid, NULL);
	}

	/* T6: a detached LWP is not waitable -> EINVAL (not ESRCH, not a hang). */
	t_ran = 0;
	held_stop = 0;
	if (spawn(lwp_detached_held, LWP_DETACHED, &lid) != 0) {
		ok("T6 wait on detached", 0, strerror(errno));
	} else {
		for (i = 0; i < 500 && t_ran == 0; i++)
			usleep(1000);
		errno = 0;
		rc = _lwp_wait(lid, NULL);
		ok("T6 wait on detached", rc == -1 && errno == EINVAL,
		    rc == -1 ? strerror(errno) : "returned success");
		held_stop = 1;
		usleep(100 * 1000);   /* let it exit + be reaped by the kernel */
	}

	printf("lwptest: %s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
	    failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
