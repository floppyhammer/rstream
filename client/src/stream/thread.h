#pragma once

#include <linux/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*!
 * All in one helper that handles locking, waiting for change and starting a
 * thread.
 */
struct os_thread_helper {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool initialized;
    bool running;
};

/*!
 * Run function.
 *
 * @public @memberof os_thread
 */
typedef void *(*os_run_func_t)(void *);

int os_thread_helper_start(struct os_thread_helper *oth, os_run_func_t func, void *ptr);

/*!
 * Initialize the thread helper.
 *
 * @public @memberof os_thread_helper
 */
int os_thread_helper_init(struct os_thread_helper *oth);

/**
 * Signals a thread to stop and waits for it to exit.
 *
 * @param oth Pointer to the os_thread_helper struct.
 * @return 0 on success, or an error code.
 */
int os_thread_helper_stop(struct os_thread_helper *oth);
