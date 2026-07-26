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
        // Overlapping tiles with origins of different parity. Their individually padded rectangles
        // are even-sized, but their component bounding box is odd-sized before normalization.
        "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\p1\\pos(80,60)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 1,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\p1\\pos(81,61)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        // Eight disconnected components, forcing the second-stage merge when the limit is four.
        "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(20,30)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 1,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(100,30)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 2,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(180,30)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 3,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(260,30)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 4,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(20,130)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 5,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(100,130)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 6,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(180,130)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 7,0:00:02.00,0:00:03.00,Default,,0,0,0,,{\\p1\\pos(260,130)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        // Single tiles that exercise final single-bitmap rectangle normalization.
        "Dialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,{\\p1\\pos(81,61)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 1,0:00:04.00,0:00:05.00,Default,,0,0,0,,{\\p1\\pos(82,62)\\bord0\\shad0}m 0 0 l 35 0 35 25 0 25{\\p0}\n"
        "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,{\\p1\\pos(80,60)\\bord0\\shad0}m 0 0 l 36 0 36 26 0 26{\\p0}\n";

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

        RECT RawBoundingRect(REFERENCE_TIME time)
        {
            ass_set_storage_size(subtitle_->m_ass_context.m_renderer.get(), kFrameSize.cx, kFrameSize.cy);
            ass_set_frame_size(subtitle_->m_ass_context.m_renderer.get(), kFrameSize.cx, kFrameSize.cy);
            int changed = 0;
            ASS_Image *images = ass_render_frame(subtitle_->m_ass_context.m_renderer.get(),
                                                 subtitle_->m_ass_context.m_track.get(),
                                                 time / 10000, &changed);
            if (!images) {
                ADD_FAILURE() << "libass returned no images";
                return RECT{};
            }

            RECT bounds = { images->dst_x, images->dst_y,
                            images->dst_x + images->w, images->dst_y + images->h };
            for (ASS_Image *image = images->next; image; image = image->next) {
                bounds.left = min(bounds.left, image->dst_x);
                bounds.top = min(bounds.top, image->dst_y);
                bounds.right = max(bounds.right, image->dst_x + image->w);
                bounds.bottom = max(bounds.bottom, image->dst_y + image->h);
            }
            return bounds;
        }

        CComPtr<IXySubRenderFrame> Render(REFERENCE_TIME time, int spd_type, int max_bitmap_count)
        {
            EXPECT_HRESULT_SUCCEEDED(subtitle_->SetMaxBitmapCount(max_bitmap_count));
            // Render each requested format instead of reusing libass's cached frame from a
            // preceding call at the same timestamp.
            subtitle_->m_last_frame = NULL;
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

    BYTE Div255(unsigned int value)
    {
        return static_cast<BYTE>((value + 1 + ((value + 1) >> 8)) >> 8);
    }

    Canvas CompositeReference(ASS_Image *images, XyColorSpace color_space)
    {
        const bool planar = color_space == XY_CS_AYUV_PLANAR;
        Canvas canvas = { kFrameSize.cx, kFrameSize.cy, planar,
                          std::vector<BYTE>(static_cast<size_t>(kFrameSize.cx) * kFrameSize.cy * 4, 0) };
        const size_t plane_size = static_cast<size_t>(canvas.width) * canvas.height;

        if (planar) {
            std::fill(canvas.bytes.begin(), canvas.bytes.begin() + plane_size, 0xFF);
        } else if (color_space != XY_CS_ARGB_F) {
            for (size_t i = 3; i < canvas.bytes.size(); i += 4)
                canvas.bytes[i] = 0xFF;
        }

        XySubRenderFrameCreater *frame_creater = XySubRenderFrameCreater::GetDefaultCreater();
        for (ASS_Image *image = images; image; image = image->next) {
            const DWORD argb = (image->color << 24) ^ (image->color >> 8) ^ 0xFF000000;
            const DWORD color = frame_creater->TransColor(argb);
            const BYTE color_a = static_cast<BYTE>(color >> 24);

            for (int y = 0; y < image->h; ++y) {
                for (int x = 0; x < image->w; ++x) {
                    const BYTE coverage = image->bitmap[y * image->stride + x];
                    const size_t output =
                        static_cast<size_t>(image->dst_y + y) * canvas.width + image->dst_x + x;

                    if (planar) {
                        BYTE &dst_a = canvas.bytes[output];
                        BYTE &dst_y = canvas.bytes[plane_size + output];
                        BYTE &dst_u = canvas.bytes[plane_size * 2 + output];
                        BYTE &dst_v = canvas.bytes[plane_size * 3 + output];
                        if (x < (image->w & ~15)) {
                            const unsigned int src_a =
                                ((static_cast<unsigned int>(coverage) + 1) * color_a) >> 8;
                            const unsigned int comp_a = 0x100 - src_a;
                            const unsigned int dst_blend =
                                ((dst_a ^ 0xFF) * comp_a + 0x80) >> 8;

                            dst_a = static_cast<BYTE>(
                                0xFF - min(src_a + dst_blend, 0xFFu));
                            dst_y = static_cast<BYTE>(
                                (((color >> 16) & 0xFF) * src_a + dst_y * comp_a + 0x80) >> 8);
                            dst_u = static_cast<BYTE>(
                                (((color >> 8) & 0xFF) * src_a + dst_u * comp_a + 0x80) >> 8);
                            dst_v = static_cast<BYTE>(
                                ((color & 0xFF) * src_a + dst_v * comp_a + 0x80) >> 8);
                        } else {
                            const BYTE src_a =
                                Div255(static_cast<unsigned int>(coverage) * color_a);
                            const BYTE comp_a = static_cast<BYTE>(~src_a);

                            dst_a = static_cast<BYTE>(
                                (src_a + Div255(static_cast<unsigned int>(dst_a ^ 0xFF) * comp_a))
                                 ^ 0xFF);
                            dst_y = Div255(((color >> 16) & 0xFF) * src_a
                                         + static_cast<unsigned int>(dst_y) * comp_a);
                            dst_u = Div255(((color >> 8) & 0xFF) * src_a
                                         + static_cast<unsigned int>(dst_u) * comp_a);
                            dst_v = Div255((color & 0xFF) * src_a
                                         + static_cast<unsigned int>(dst_v) * comp_a);
                        }
                    } else {
                        BYTE *dst = canvas.bytes.data() + output * 4;
                        const unsigned int src_a =
                            ((static_cast<unsigned int>(coverage) + 1) * color_a) >> 8;
                        const unsigned int comp_a = 0x100 - src_a;
                        const unsigned int dst_opacity =
                            color_space == XY_CS_ARGB_F ? dst[3] : dst[3] ^ 0xFF;

                        dst[0] = static_cast<BYTE>(
                            (static_cast<unsigned int>(dst[0]) * comp_a
                             + (color & 0xFF) * (src_a + 1)) >> 8);
                        dst[1] = static_cast<BYTE>(
                            (static_cast<unsigned int>(dst[1]) * comp_a
                             + ((color >> 8) & 0xFF) * (src_a + 1)) >> 8);
                        dst[2] = static_cast<BYTE>(
                            (static_cast<unsigned int>(dst[2]) * comp_a
                             + ((color >> 16) & 0xFF) * (src_a + 1)) >> 8);
                        const BYTE output_opacity = static_cast<BYTE>(
                            (dst_opacity * comp_a >> 8) + src_a);
                        dst[3] = color_space == XY_CS_ARGB_F
                            ? output_opacity
                            : output_opacity ^ 0xFF;
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

TEST_F(LibassBitmapTest, CompositeMatchesScalarReference)
{
    const REFERENCE_TIME time = 5000000;
    ASSERT_GT(CountRawImages(time), 1);

    for (const FormatCase &format : kFormats) {
        SCOPED_TRACE(format.name);
        CComPtr<IXySubRenderFrame> frame = Render(time, format.spd_type, 1);
        const Canvas actual = FlattenFrame(frame);

        int changed = 0;
        ASS_Image *images = ass_render_frame(subtitle_->m_ass_context.m_renderer.get(),
                                             subtitle_->m_ass_context.m_track.get(),
                                             time / 10000, &changed);
        ASSERT_TRUE(images != NULL);
        const Canvas expected = CompositeReference(images, format.color_space);

        EXPECT_EQ(expected.planar, actual.planar);
        EXPECT_EQ(expected.bytes, actual.bytes);
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

TEST_F(LibassBitmapTest, MergedComponentBitmapsHaveEvenDimensions)
{
    const REFERENCE_TIME time = 35000000;
    ASSERT_EQ(2, CountRawImages(time));

    for (const FormatCase &format : kFormats) {
        SCOPED_TRACE(format.name);
        CComPtr<IXySubRenderFrame> frame = Render(time, format.spd_type, 16);
        ASSERT_EQ(1, BitmapCount(frame));

        SIZE size = {};
        EXPECT_HRESULT_SUCCEEDED(frame->GetBitmap(0, NULL, NULL, &size, NULL, NULL));
        EXPECT_EQ(0, size.cx & 1);
        EXPECT_EQ(0, size.cy & 1);
    }
}

TEST_F(LibassBitmapTest, SingleBitmapNormalizesOddDimensions)
{
    const REFERENCE_TIME time = 45000000;
    ASSERT_EQ(2, CountRawImages(time));
    const RECT raw = RawBoundingRect(time);
    const LONG raw_width = raw.right - raw.left;
    const LONG raw_height = raw.bottom - raw.top;
    ASSERT_EQ(0, raw.left & 1);
    ASSERT_EQ(0, raw.top & 1);
    ASSERT_EQ(1, raw_width & 1);
    ASSERT_EQ(1, raw_height & 1);

    for (const FormatCase &format : kFormats) {
        SCOPED_TRACE(format.name);
        CComPtr<IXySubRenderFrame> frame = Render(time, format.spd_type, 1);
        ASSERT_EQ(1, BitmapCount(frame));

        POINT position = {};
        SIZE size = {};
        EXPECT_HRESULT_SUCCEEDED(frame->GetBitmap(0, NULL, &position, &size, NULL, NULL));
        EXPECT_EQ(raw.left, position.x);
        EXPECT_EQ(raw.top, position.y);
        EXPECT_EQ(raw_width + 1, size.cx);
        EXPECT_EQ(raw_height + 1, size.cy);
    }
}

TEST_F(LibassBitmapTest, SingleYuvBitmapNormalizesOddOffset)
{
    const REFERENCE_TIME time = 55000000;
    ASSERT_EQ(1, CountRawImages(time));
    const RECT raw = RawBoundingRect(time);
    const LONG raw_width = raw.right - raw.left;
    const LONG raw_height = raw.bottom - raw.top;
    ASSERT_EQ(1, raw.left & 1);
    ASSERT_EQ(1, raw.top & 1);
    ASSERT_EQ(0, raw_width & 1);
    ASSERT_EQ(0, raw_height & 1);

    for (const FormatCase &format : kFormats) {
        if (format.color_space == XY_CS_ARGB || format.color_space == XY_CS_ARGB_F)
            continue;

        SCOPED_TRACE(format.name);
        CComPtr<IXySubRenderFrame> frame = Render(time, format.spd_type, 1);
        ASSERT_EQ(1, BitmapCount(frame));

        POINT position = {};
        SIZE size = {};
        EXPECT_HRESULT_SUCCEEDED(frame->GetBitmap(0, NULL, &position, &size, NULL, NULL));
        EXPECT_EQ(raw.left - 1, position.x);
        EXPECT_EQ(raw.top - 1, position.y);
        EXPECT_EQ(raw_width + 2, size.cx);
        EXPECT_EQ(raw_height + 2, size.cy);
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
