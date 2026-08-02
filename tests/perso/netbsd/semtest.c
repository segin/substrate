/*
 * semtest — pin substrate's NetBSD-personality _ksem_*(2) against a real
 * NetBSD kernel, through the POSIX sem_* API (the path SDL uses).
 *
 * NetBSD's librt implements sem_init/sem_wait/... entirely on top of the
 * _ksem_* syscalls (247..256); sem_init() has no userspace fast path, so
 * SDL_CreateSemaphore died with "Unable to init SDL: sem_init() failed" until
 * the personality wired these up.
 *
 * Build natively on NetBSD i386:  cc -O1 -o semtest semtest.c -lrt -lpthread
 * See README.md — must be ALL PASS on the real host first.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void
ok(const char *name, int pass, const char *detail)
{
	printf("%-30s %s%s%s\n", name, pass ? "PASS" : "FAIL",
	    detail && *detail ? " - " : "", detail ? detail : "");
	fflush(stdout);
	if (!pass)
		failures++;
}

static void *
poster(void *arg)
{
	sem_t *s = arg;
	usleep(150 * 1000);
	(void)sem_post(s);
	return NULL;
}

int
main(void)
{
	sem_t s, *ns;
	struct timespec ts;
	int v, rc;

	/* T1: anonymous sem_init (the SDL path) + initial value. */
	if (sem_init(&s, 0, 2) != 0) {
		ok("T1 sem_init", 0, strerror(errno));
		printf("semtest: FAILED (init) \n");
		return 1;
	}
	v = -1;
	rc = sem_getvalue(&s, &v);
	ok("T1 sem_init value==2", rc == 0 && v == 2, rc ? strerror(errno) : "");

	/* T2: two waits drain it; a third trywait is EAGAIN. */
	rc = (sem_wait(&s) == 0) && (sem_wait(&s) == 0);
	v = -1; sem_getvalue(&s, &v);
	errno = 0;
	int tw = sem_trywait(&s);
	ok("T2 drain + trywait EAGAIN", rc && v == 0 && tw == -1 && errno == EAGAIN,
	    tw == -1 ? strerror(errno) : "trywait unexpectedly succeeded");

	/* T3: post raises the value again. */
	rc = sem_post(&s);
	v = -1; sem_getvalue(&s, &v);
	ok("T3 sem_post value==1", rc == 0 && v == 1, rc ? strerror(errno) : "");
	(void)sem_wait(&s);   /* back to 0 */

	/* T4: timedwait on an empty sem with a past deadline -> ETIMEDOUT. */
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec -= 1;
	errno = 0;
	rc = sem_timedwait(&s, &ts);
	ok("T4 timedwait ETIMEDOUT", rc == -1 && errno == ETIMEDOUT,
	    rc == -1 ? strerror(errno) : "did not time out");

	/* T5: sem_destroy on a fresh, untouched sem.  (We destroy a separate sem
	 * rather than `s`: NetBSD leaves a lingering reference after certain
	 * operations — a completed sem_timedwait, or a thread that blocked-and-woke
	 * — and then returns EBUSY from sem_destroy.  That is a ksem-refcount
	 * timing quirk substrate does not reproduce and apps do not rely on, so we
	 * pin the clean destroy path instead.  `s` itself is left for process-exit
	 * cleanup.) */
	{
		sem_t d;
		errno = 0;
		if (sem_init(&d, 0, 1) != 0)
			ok("T5 sem_destroy", 0, strerror(errno));
		else {
			rc = sem_destroy(&d);
			ok("T5 sem_destroy", rc == 0, rc ? strerror(errno) : "");
		}
	}

	/* T6: a background thread posts; a blocking wait wakes.  Exercises the
	 * kernel block/wake path across LWPs.  Uses its own sem; left for
	 * process-exit cleanup rather than destroyed (see T5). */
	{
		sem_t bs;
		pthread_t th;
		int made;
		if (sem_init(&bs, 0, 0) != 0) {
			ok("T6 blocking wait wakes", 0, strerror(errno));
		} else {
			made = pthread_create(&th, NULL, poster, &bs) == 0;
			rc = sem_wait(&bs);      /* blocks until the poster runs */
			if (made) pthread_join(th, NULL);
			ok("T6 blocking wait wakes", made && rc == 0,
			    !made ? "pthread_create failed" : (rc ? strerror(errno) : ""));
		}
	}

	/* T7: pshared anonymous sem — exercises the ksem id marker encode/decode
	 * (value used within this process, no fork needed to touch the path). */
	{
		sem_t ps;
		if (sem_init(&ps, 1, 1) != 0) {
			ok("T7 pshared sem_init", 0, strerror(errno));
		} else {
			v = -1; sem_getvalue(&ps, &v);
			int w = sem_wait(&ps);
			int p = sem_post(&ps);
			ok("T7 pshared wait/post", v == 1 && w == 0 && p == 0, "");
			(void)sem_destroy(&ps);
		}
	}

	/* T8: named semaphore open/wait/post/close/unlink. */
	(void)sem_unlink("/semtest");
	ns = sem_open("/semtest", O_CREAT | O_EXCL, 0644, 1);
	if (ns == SEM_FAILED) {
		ok("T8 named sem_open", 0, strerror(errno));
	} else {
		int w = sem_wait(ns);
		int p = sem_post(ns);
		int c = sem_close(ns);
		int u = sem_unlink("/semtest");
		ok("T8 named open/wait/post/close/unlink",
		    w == 0 && p == 0 && c == 0 && u == 0, "");
	}

	printf("semtest: %s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
	    failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
