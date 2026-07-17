#include "stdafx.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "xy_bitmap.h"

namespace
{
    const char kBitmapTestAss[] =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "PlayResX: 640\n"
        "PlayResY: 360\n"
        "ScaledBorderAndShadow: yes\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
        "Style: Default,Arial,32,&H001122EE,&H000000FF,&H00EE4411,&H00000000,0,0,0,0,100,100,0,0,1,4,0,7,0,0,0,1\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        // One outlined vector shape. Its outline and fill overlap and are separate ASS_Image nodes.
        "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\p1\\pos(80,60)\\bord6\\shad0}m 0 0 l 120 0 120 60 0 60{\\p0}\n"
        // More raw tiles than the limit, all in one overlap-connected component.
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\p1\\pos(80,60)\\bord2\\shad0}m 0 0 l 80 0 80 40 0 40{\\p0}\n"
        "Dialogue: 1,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\p1\\pos(100,70)\\bord2\\shad0}m 0 0 l 80 0 80 40 0 40{\\p0}\n"
        "Dialogue: 2,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\p1\\pos(120,80)\\bord2\\shad0}m 0 0 l 80 0 80 40 0 40{\\p0}\n"
        "Dialogue: 3,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\p1\\pos(140,90)\\bord2\\shad0}m 0 0 l 80 0 80 40 0 40{\\p0}\n"
        "Dialogue: 4,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\p1\\pos(160,100)\\bord2\\shad0}m 0 0 l 80 0 80 40 0 40{\\p0}\n"
        "Dialogue: 5,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\p1\\pos(180,110)\\bord2\\shad0}m 0 0 l 80 0 80 40 0 40{\\p0}\n"
        // Eight disconnected components, forcing the second-stage merge when the limit is four.
        "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(20,30)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 1,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(100,30)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 2,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(180,30)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 3,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(260,30)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 4,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(20,130)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 5,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(100,130)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 6,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(180,130)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 7,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(260,130)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n";

    const RECT kFrameRect = { 0, 0, 640, 360 };
    const SIZE kFrameSize = { 640, 360 };
    const double kFps = 25.0;

    struct FormatCase
    {
        int spd_type;
        XyColorSpace color_space;
        const char *name;
    };

    const FormatCase kFormats[] = {
        { MSP_RGBA,       XY_CS_ARGB,       "ARGB" },
        { MSP_RGBA_F,     XY_CS_ARGB_F,     "ARGB_F" },
        { MSP_AYUV_PLANAR,XY_CS_AYUV_PLANAR,"AYUV_PLANAR" },
        { MSP_AYUV,       XY_CS_AYUV,       "AYUV" },
        { MSP_XY_AUYV,    XY_CS_AUYV,       "AUYV" },
    };

    class LibassBitmapTest : public testing::Test
    {
    protected:
        CCritSec lock_;
        CComPtr<CRenderedTextSubtitle> subtitle_;

        void SetUp() override
        {
            subtitle_ = DEBUG_NEW CRenderedTextSubtitle(&lock_);
            ASSERT_TRUE(subtitle_ != NULL);

            std::vector<BYTE> script(kBitmapTestAss, kBitmapTestAss + strlen(kBitmapTestAss));
            ASSERT_TRUE(subtitle_->Open(script.data(), static_cast<int>(script.size()),
                                        DEFAULT_CHARSET, _T("CSRI memory subtitles")));
            ASSERT_TRUE(subtitle_->m_ass_context.m_assloaded);
            subtitle_->m_render_backend = SUBTITLE_RENDER_BACKEND_LIBASS;
        }

        int CountRawImages(REFERENCE_TIME time)
        {
            ass_set_storage_size(subtitle_->m_ass_context.m_renderer.get(), kFrameSize.cx, kFrameSize.cy);
            ass_set_frame_size(subtitle_->m_ass_context.m_renderer.get(), kFrameSize.cx, kFrameSize.cy);
            int changed = 0;
            ASS_Image *images = ass_render_frame(subtitle_->m_ass_context.m_renderer.get(),
                                                 subtitle_->m_ass_context.m_track.get(),
                                                 time / 10000, &changed);
            int count = 0;
            for (ASS_Image *image = images; image; image = image->next)
                ++count;
            return count;
        }

        CComPtr<IXySubRenderFrame> Render(REFERENCE_TIME time, int spd_type, int max_bitmap_count)
        {
            EXPECT_HRESULT_SUCCEEDED(subtitle_->SetMaxBitmapCount(max_bitmap_count));
            CComPtr<IXySubRenderFrame> frame;
            EXPECT_HRESULT_SUCCEEDED(subtitle_->RenderEx(&frame, spd_type, kFrameRect, kFrameRect,
                                                         kFrameSize, time, kFps));
            EXPECT_TRUE(frame != NULL);
            return frame;
        }

        void ExpectColorSpace(IXySubRenderFrame *frame, XyColorSpace expected)
        {
            int actual = -1;
            EXPECT_HRESULT_SUCCEEDED(frame->GetXyColorSpace(&actual));
            EXPECT_EQ(static_cast<int>(expected), actual);
        }
    };

    int BitmapCount(IXySubRenderFrame *frame)
    {
        int count = -1;
        EXPECT_HRESULT_SUCCEEDED(frame->GetBitmapCount(&count));
        return count;
    }

    struct Canvas
    {
        int width;
        int height;
        bool planar;
        std::vector<BYTE> bytes;
    };

    Canvas FlattenFrame(IXySubRenderFrame *frame)
    {
        int color_space_value = -1;
        EXPECT_HRESULT_SUCCEEDED(frame->GetXyColorSpace(&color_space_value));
        const XyColorSpace color_space = static_cast<XyColorSpace>(color_space_value);
        const bool planar = color_space == XY_CS_AYUV_PLANAR;

        const int count = BitmapCount(frame);

        Canvas canvas = { kFrameSize.cx, kFrameSize.cy, planar,
                          std::vector<BYTE>(static_cast<size_t>(kFrameSize.cx) * kFrameSize.cy * 4, 0) };
        if (color_space != XY_CS_ARGB_F) {
            if (planar) {
                std::fill(canvas.bytes.begin(), canvas.bytes.begin() +
                          static_cast<size_t>(canvas.width) * canvas.height, 0xFF);
            } else {
                for (size_t i = 3; i < canvas.bytes.size(); i += 4)
                    canvas.bytes[i] = 0xFF;
            }
        }

        for (int i = 0; i < count; ++i) {
            POINT position = {};
            SIZE size = {};
            LPCVOID pixels = NULL;
            int pitch = 0;
            EXPECT_HRESULT_SUCCEEDED(frame->GetBitmap(i, NULL, &position, &size, &pixels, &pitch));
            if (!pixels) {
                ADD_FAILURE() << "Bitmap " << i << " has no pixel buffer";
                return canvas;
            }

            if (planar) {
                XyPlannerFormatExtra extra = {};
                EXPECT_HRESULT_SUCCEEDED(frame->GetBitmapExtra(i, &extra));
                const BYTE *planes[] = {
                    static_cast<const BYTE *>(extra.plans[0]),
                    static_cast<const BYTE *>(extra.plans[1]),
                    static_cast<const BYTE *>(extra.plans[2]),
                    static_cast<const BYTE *>(extra.plans[3])
                };
                for (int y = 0; y < size.cy; ++y) {
                    for (int x = 0; x < size.cx; ++x) {
                        if (planes[0][y * pitch + x] == 0xFF)
                            continue;
                        const size_t output = static_cast<size_t>(position.y + y) * canvas.width + position.x + x;
                        for (int plane = 0; plane < 4; ++plane)
                            canvas.bytes[static_cast<size_t>(plane) * canvas.width * canvas.height + output] =
                                planes[plane][y * pitch + x];
                    }
                }
            } else {
                const BYTE *source = static_cast<const BYTE *>(pixels);
                const BYTE transparent_alpha = color_space == XY_CS_ARGB_F ? 0x00 : 0xFF;
                for (int y = 0; y < size.cy; ++y) {
                    for (int x = 0; x < size.cx; ++x) {
                        const BYTE *pixel = source + y * pitch + x * 4;
                        if (pixel[3] == transparent_alpha)
                            continue;
                        BYTE *output = canvas.bytes.data() +
                            (static_cast<size_t>(position.y + y) * canvas.width + position.x + x) * 4;
                        memcpy(output, pixel, 4);
                    }
                }
            }
        }
        return canvas;
    }

    void ExpectPixelEquivalent(IXySubRenderFrame *expected, IXySubRenderFrame *actual,
                               const char *format_name)
    {
        SCOPED_TRACE(format_name);
        const Canvas expected_canvas = FlattenFrame(expected);
        const Canvas actual_canvas = FlattenFrame(actual);
        EXPECT_EQ(expected_canvas.planar, actual_canvas.planar);
        EXPECT_EQ(expected_canvas.bytes, actual_canvas.bytes);
    }
}

TEST_F(LibassBitmapTest, OverlappingOutlineAndFillImagesAreCompositedExactly)
{
    const REFERENCE_TIME time = 5000000;
    ASSERT_GT(CountRawImages(time), 1);

    for (const FormatCase &format : kFormats) {
        SCOPED_TRACE(format.name);
        CComPtr<IXySubRenderFrame> original = Render(time, format.spd_type, 1);
        CComPtr<IXySubRenderFrame> split = Render(time, format.spd_type, 16);
        ExpectColorSpace(split, format.color_space);
        ASSERT_EQ(1, BitmapCount(original));
        ExpectPixelEquivalent(original, split, format.name);
    }
}

TEST_F(LibassBitmapTest, OverLimitTilesCollapseToFewerThanTheLimitByConnectivity)
{
    const REFERENCE_TIME time = 15000000;
    const int limit = 4;
    ASSERT_GT(CountRawImages(time), limit);

    for (const FormatCase &format : kFormats) {
        SCOPED_TRACE(format.name);
        CComPtr<IXySubRenderFrame> original = Render(time, format.spd_type, 1);
        CComPtr<IXySubRenderFrame> grouped = Render(time, format.spd_type, limit);
        ExpectColorSpace(grouped, format.color_space);
        EXPECT_LT(BitmapCount(grouped), limit);
        ExpectPixelEquivalent(original, grouped, format.name);
    }
}

TEST_F(LibassBitmapTest, OverLimitComponentsAreMergedDownToTheLimit)
{
    const REFERENCE_TIME time = 25000000;
    const int limit = 4;
    ASSERT_GT(CountRawImages(time), limit);

    for (const FormatCase &format : kFormats) {
        SCOPED_TRACE(format.name);
        CComPtr<IXySubRenderFrame> original = Render(time, format.spd_type, 1);
        CComPtr<IXySubRenderFrame> grouped = Render(time, format.spd_type, limit);
        ExpectColorSpace(grouped, format.color_space);
        EXPECT_EQ(limit, BitmapCount(grouped));
        ExpectPixelEquivalent(original, grouped, format.name);
    }
}

TEST_F(LibassBitmapTest, CombineBitmapsOptionHasSingleAndMultiBitmapModes)
{
    const REFERENCE_TIME time = 25000000;
    for (const FormatCase &format : kFormats) {
        SCOPED_TRACE(format.name);
        // combineBitmaps=false exposes the configured multi-bitmap limit.
        CComPtr<IXySubRenderFrame> separate = Render(time, format.spd_type, 16);
        ExpectColorSpace(separate, format.color_space);
        ASSERT_GT(BitmapCount(separate), 1);

        // combineBitmaps=true maps to max_bitmap_count == 1 in the provider options.
        CComPtr<IXySubRenderFrame> combined = Render(time, format.spd_type, 1);
        ASSERT_EQ(1, BitmapCount(combined));
        ExpectPixelEquivalent(combined, separate, format.name);
    }
}
