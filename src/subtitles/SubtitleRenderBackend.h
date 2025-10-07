#pragma once

enum SubtitleRenderBackend : int
{
    SUBTITLE_RENDER_BACKEND_LIBASS = 0,
    SUBTITLE_RENDER_BACKEND_VSFILTER,
    SUBTITLE_RENDER_BACKEND_CSRI,
    SUBTITLE_RENDER_BACKEND_COUNT
};

inline SubtitleRenderBackend NormalizeBackend(int value)
{
    const int min_backend = static_cast<int>(SUBTITLE_RENDER_BACKEND_LIBASS);
    const int max_backend = static_cast<int>(SUBTITLE_RENDER_BACKEND_COUNT) - 1;
    if (value < min_backend) {
        return static_cast<SubtitleRenderBackend>(min_backend);
    }
    if (value > max_backend) {
        return static_cast<SubtitleRenderBackend>(max_backend);
    }
    return static_cast<SubtitleRenderBackend>(value);
}