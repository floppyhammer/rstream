add_library(
        gst_stream_app SHARED
        stream_app.c
        connection.c
        thread.c
        render/gl_debug.cpp
        render/gl_error.cpp
        render/gl_swapchain.cpp
        render/render.cpp
)

target_link_libraries(
        gst_stream_app
        PRIVATE ${ANDROID_LOG_LIBRARY}
        PUBLIC
        EGL::EGL
        OpenGLES::OpenGLESv3
        gstreamer_android
)

target_include_directories(
        gst_stream_app
        PRIVATE ${LIBSOUP_INCLUDE_DIRS} ${JSONGLIB_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS}
        PUBLIC .. ${GST_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS}
)
