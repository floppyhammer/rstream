// Copyright 2022-2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief  Header for the stream client module of the ElectricMaple XR streaming solution
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @ingroup em_client
 */
#pragma once

#include <EGL/egl.h>
#include <glib-object.h>
#include <stdbool.h>
#include "connection.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

struct MySample;

typedef struct _StreamApp StreamApp;

/*!
 * Create a stream client object, providing the connection object
 *
 * @memberof StreamApp
 */
StreamApp *stream_app_new();

/// Initialize the EGL context and surface.
void stream_app_set_egl_context(StreamApp *app, EGLContext context, EGLDisplay display,
                                EGLSurface surface);

/*!
 * Clear a pointer and free the associate stream client, if any.
 *
 * Handles null checking for you.
 */
void stream_app_destroy(StreamApp **ptr_app);

/*!
 * Start the GMainLoop embedded in this object in a new thread
 *
 * @param connection The connection to use
 */
void stream_app_spawn_thread(StreamApp *app, MyConnection *connection);

/*!
 * Stop the pipeline and the mainloop thread.
 */
void stream_app_stop(StreamApp *app);

/*!
 * Attempt to retrieve a sample, if one has been decoded.
 *
 * Non-null return values need to be released with @ref stream_app_release_sample.

* @param app self
* @param[out] out_decode_end struct to populate with decode-end time.
 */
struct MySample *stream_app_try_pull_sample(StreamApp *app, struct timespec *out_decode_end);

/*!
 * Release a sample returned from @ref stream_app_try_pull_sample
 */
void stream_app_release_sample(StreamApp *app, struct MySample *ems);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
