#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <err.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define N_READERS 1
#define N_WRITERS 7
#define N_ITERS 0x7FFFFFFF
#define ARRAY_SIZE(x) sizeof(x) / sizeof(*x)

#define ERR_FATAL 0xDEADBEEF

typedef struct {
    unsigned int v1, v2, v3;
} config_t;

typedef uintptr_t hp_t;

static config_t *shared_config;
static hp_t hp_ptr;
static pthread_barrier_t barr;

config_t *create_config()
{
    config_t *out = calloc(1, sizeof(config_t));
    if (!out)
        err(EXIT_FAILURE, "calloc");
    return out;
}

void delete_config(void *arg)
{
    config_t *conf = (config_t *) arg;
    assert(conf);
    free(conf);
}

static void print_config(const char *name, const config_t *conf)
{
    printf("%s : { 0x%08x }\n", name, conf->v1);
}

void init()
{
    shared_config = create_config();
    hp_ptr = 0;
}

void deinit()
{
    delete_config(shared_config);
}

/* Only accept one reader */
static void *reader_thread(void *arg)
{
    (void) arg;
    const uintptr_t nullptr = 0;

    pthread_barrier_wait(&barr);

    for (int i = 0; i < N_ITERS; i++) {
        while (1) {
            uintptr_t val =
                (uintptr_t) __atomic_load_n(&shared_config, __ATOMIC_ACQUIRE);

            __atomic_store(&hp_ptr, &val, __ATOMIC_RELEASE);

            if (__atomic_load_n(&shared_config, __ATOMIC_ACQUIRE) ==
                (config_t *) val) {
                assert(((config_t *) val)->v1 != ERR_FATAL);
                assert(((config_t *) val)->v2 != ERR_FATAL);
                assert(((config_t *) val)->v3 != ERR_FATAL);
                break;
            }

            __atomic_store(&hp_ptr, &nullptr, __ATOMIC_RELEASE);
        }
    }

    __atomic_store(&hp_ptr, &nullptr, __ATOMIC_RELEASE);

    return NULL;
}

static void *writer_thread(void *arg)
{
    (void) arg;

    pthread_barrier_wait(&barr);

    for (int i = 0; i < N_ITERS; i++) {
        config_t *new_config = create_config();
        new_config->v1 = rand() & 0x3;
        new_config->v2 = rand() & 0x3;
        new_config->v3 = rand() & 0x3;
        config_t *old_obj =
            __atomic_exchange_n(&shared_config, new_config, __ATOMIC_ACQ_REL);
        while (__atomic_load_n(&hp_ptr, __ATOMIC_ACQUIRE) ==
               (uintptr_t) old_obj)
            ;
        // old_obj is retired
        old_obj->v1 = ERR_FATAL;
        old_obj->v2 = ERR_FATAL;
        old_obj->v3 = ERR_FATAL;
    }

    return NULL;
}

int main()
{
    pthread_t readers[N_READERS], writers[N_WRITERS];
    pthread_barrier_init(&barr, NULL, N_READERS + N_WRITERS);

    init();

    for (size_t i = 0; i < ARRAY_SIZE(writers); ++i) {
        if (pthread_create(writers + i, NULL, writer_thread, NULL))
            warn("pthread_create");
    }

    for (size_t i = 0; i < ARRAY_SIZE(readers); ++i) {
        if (pthread_create(readers + i, NULL, reader_thread, NULL))
            warn("pthread_create");
    }

    for (size_t i = 0; i < ARRAY_SIZE(writers); ++i) {
        if (pthread_join(writers[i], NULL))
            warn("pthread_join");
    }

    for (size_t i = 0; i < ARRAY_SIZE(readers); ++i) {
        if (pthread_join(readers[i], NULL))
            warn("pthread_join");
    }

    deinit();

    return EXIT_SUCCESS;
}
