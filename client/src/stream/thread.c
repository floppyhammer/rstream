#include "thread.h"

#include <glib.h>
#include <pthread.h>

int os_thread_helper_start(struct os_thread_helper *oth, os_run_func_t func, void *ptr) {
    pthread_mutex_lock(&oth->mutex);

    g_assert(oth->initialized);
    if (oth->running) {
        pthread_mutex_unlock(&oth->mutex);
        return -1;
    }

    int ret = pthread_create(&oth->thread, NULL, func, ptr);
    if (ret != 0) {
        pthread_mutex_unlock(&oth->mutex);
        return ret;
    }

    oth->running = true;

    pthread_mutex_unlock(&oth->mutex);

    return 0;
}

/*!
 * Zeroes the correct amount of memory based on the type pointed-to by the
 * argument.
 *
 * Use instead of memset(..., 0, ...) on a structure or pointer to structure.
 *
 * @ingroup aux_util
 */
#define U_ZERO(PTR) memset((PTR), 0, sizeof(*(PTR)))

int os_thread_helper_init(struct os_thread_helper *oth) {
    U_ZERO(oth);

    int ret = pthread_mutex_init(&oth->mutex, NULL);
    if (ret != 0) {
        return ret;
    }

    ret = pthread_cond_init(&oth->cond, NULL);
    if (ret) {
        pthread_mutex_destroy(&oth->mutex);
        return ret;
    }
    oth->initialized = true;

    return 0;
}
