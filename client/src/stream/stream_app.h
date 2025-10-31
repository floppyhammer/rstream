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

G_BEGIN_DECLS

struct MySample;

#define MY_TYPE_STREAM_APP my_stream_app_get_type()

G_DECLARE_FINAL_TYPE(MyStreamApp, my_stream_app, MY, STREAM_APP, GObject)

/*!
 * Create a stream client object, providing the connection object
 *
 * @memberof StreamApp
 */
MyStreamApp *my_stream_app_new();

/// Initialize the EGL context and surface.
void stream_app_set_egl_context(MyStreamApp *app, EGLContext context, EGLDisplay display, EGLSurface surface);

/*!
 * Start the GMainLoop embedded in this object in a new thread
 *
 * @param connection The connection to use
 */
void stream_app_spawn_thread(MyStreamApp *app, MyConnection *connection);

/*!
 * Stop the pipeline and the mainloop thread.
 */
void stream_app_stop(MyStreamApp *app);

/*!
 * Attempt to retrieve a sample, if one has been decoded.
 *
 * Non-null return values need to be released with @ref stream_app_release_sample.

* @param app self
* @param[out] out_decode_end struct to populate with decode-end time.
 */
struct MySample *stream_app_try_pull_sample(MyStreamApp *app, struct timespec *out_decode_end);

/*!
 * Release a sample returned from @ref stream_app_try_pull_sample
 */
void stream_app_release_sample(MyStreamApp *app, struct MySample *ems);

uint32_t stream_app_get_video_width(MyStreamApp *app);

uint32_t stream_app_get_video_height(MyStreamApp *app);

G_END_DECLS
