/*
 * tlsstatic — thread-local storage in a statically linked program.
 *
 * libc set up a static program's TLS only on amd64; an i386 one had no
 * TLS image (its __thread variables read garbage instead of their
 * initial values) and its threads got a NULL TLS base, so libpthread's
 * own TLS -- thread-specific data, cleanup handlers -- faulted.
 *
 * Checks the initial values in the main thread, that each of four threads
 * gets its own initialised copy, that a key's value and a cleanup handler
 * work in a thread, and that the main thread's copy is untouched.
 *
 * Built static (the Makefile's cross rule); runs on the host too.
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static __thread int tval = 42;
static __thread char tname[16] = "initial";
static __thread int tzero;           /* .tbss */

static pthread_key_t key;
static int fails;
static int cleaned[4];

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static void cleanup(void *arg)
{
    cleaned[(int)(long)arg] = 1;
}

static void *thread(void *arg)
{
    int id = (int)(long)arg;
    int ok = tval == 42 && strcmp(tname, "initial") == 0 && tzero == 0;

    tval = 100 + id;
    tzero = id + 1;
    snprintf(tname, sizeof(tname), "thread%d", id);
    pthread_setspecific(key, &tval);
    pthread_cleanup_push(cleanup, arg);
    ok = ok && pthread_getspecific(key) == &tval && *(int *)pthread_getspecific(key) == 100 + id;
    pthread_cleanup_pop(1);
    return (void *)(long)ok;
}

int main(void)
{
    pthread_t t[4];

    check(tval == 42 && strcmp(tname, "initial") == 0 && tzero == 0,
          "main thread sees the initial values");
    tval = 7;
    check(pthread_key_create(&key, NULL) == 0, "pthread_key_create");
    for (long i = 0; i < 4; i++)
        check(pthread_create(&t[i], NULL, thread, (void *)i) == 0, "pthread_create");
    for (int i = 0; i < 4; i++) {
        void *r = NULL;
        pthread_join(t[i], &r);
        check(r == (void *)1L, "thread had its own initialised TLS and key value");
        check(cleaned[i], "cleanup handler ran");
    }
    check(tval == 7 && strcmp(tname, "initial") == 0 && tzero == 0,
          "main thread's copy untouched");
    printf("tlsstatic: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
