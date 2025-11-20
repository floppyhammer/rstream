#pragma once

#include <optional>
#include <string>

#include "egl_data.hpp"
#include "stream/connection.h"
#include "stream/render/render.hpp"
#include "stream/stream_app.h"

struct MyState {
    // Window size.
    int32_t window_width;
    int32_t window_height;
    // Video size.
    int32_t render_width;
    int32_t render_height;

    int32_t h_margin;
    int32_t v_margin;

    bool pressed;
    float press_pos_x;
    float press_pos_y;
    float prev_pos_x;
    float prev_pos_y;
    bool scrolling;

    // Scroll
    float prev_pos_center_x;
    float prev_pos_center_y;

    std::optional<int64_t> press_time;

    int64_t last_time_cursor_down;
    float last_cursor_down_pos_x;
    float last_cursor_down_pos_y;

    MyConnection *connection;
    MyStreamApp *stream_app;

    std::string host_ip;
    std::string video_quality;
    uint32_t framerate;
    uint32_t bitrate;

    std::unique_ptr<Renderer> renderer;

    std::unique_ptr<EglData> egl_data;

    pthread_t listener_tid;

    float prev_lt = 0;
    float prev_rt = 0;
    float prev_lx = 0;
    float prev_ly = 0;
    float prev_rx = 0;
    float prev_ry = 0;
};
