#pragma once

struct StreamConfig {
    int video_width;
    int video_height;
    int framerate;
    int bitrate; // Mbps
    char pin[5]; // Ends in /0
};
