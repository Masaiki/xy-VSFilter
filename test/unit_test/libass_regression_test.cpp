#include "stdafx.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Bcrypt.lib")

#include "xy_bitmap.h"

namespace
{
    const wchar_t kFixturePath[] =
        L"test\\unit_test\\fixtures\\libass_regression.ass";
    const wchar_t kFontPath[] =
        L"SMP\\libass\\compare\\test\\font1.ttf";
    const wchar_t kGoldenPath[] =
        L"test\\unit_test\\fixtures\\libass_regression.golden.tsv";
    const double kFps = 24000.0 / 1001.0;

    struct FormatCase
    {
        int spd_type;
        XyColorSpace color_space;
        const char *name;
    };

    const FormatCase kFormats[] = {
        { MSP_RGBA,        XY_CS_ARGB,        "ARGB" },
        { MSP_RGBA_F,      XY_CS_ARGB_F,      "ARGB_F" },
        { MSP_AYUV_PLANAR, XY_CS_AYUV_PLANAR, "AYUV_PLANAR" },
        { MSP_AYUV,        XY_CS_AYUV,        "AYUV" },
        { MSP_XY_AUYV,     XY_CS_AUYV,        "AUYV" },
    };

    struct RenderCase
    {
        int width;
        int height;
        int time_ms;
    };

    const RenderCase kRenderCases[] = {
        { 640, 360, 1500 },
        { 640, 360, 2500 },
        { 640, 360, 3500 },
        { 640, 360, 4500 },
        { 640, 360, 5500 },
        { 640, 360, 6500 },
        { 1920, 1080, 6500 },
    };

    struct Canvas
    {
        int width;
        int height;
        bool planar;
        std::vector<BYTE> bytes;
    };

    struct BitmapInfo
    {
        POINT position;
        SIZE size;
        int pitch;
    };

    struct FrameSnapshot
    {
        int color_space;
        std::vector<BitmapInfo> bitmaps;
        std::string metadata;
        std::vector<BYTE> visible_bytes;
        Canvas canvas;
    };

    struct GoldenEntry
    {
        int color_space;
        int bitmap_count;
        std::string metadata_sha256;
        std::string visible_sha256;
        std::string canvas_sha256;
    };

    struct GoldenManifest
    {
        std::string baseline_sha;
        std::string fixture_sha256;
        std::string font_sha256;
        std::map<std::string, GoldenEntry> entries;
    };

    std::wstring EnvironmentString(const wchar_t *name)
    {
        const DWORD length = GetEnvironmentVariableW(name, NULL, 0);
        if (!length)
            return std::wstring();
        std::vector<wchar_t> value(length, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, value.data(), length);
        if (!written || written >= length)
            return std::wstring();
        return std::wstring(value.data(), written);
    }

    bool ReadBinaryFile(const wchar_t *path, std::vector<BYTE> *bytes)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return false;
        input.seekg(0, std::ios::end);
        const std::streamoff size = input.tellg();
        input.seekg(0, std::ios::beg);
        if (size <= 0)
            return false;
        bytes->resize(static_cast<size_t>(size));
        input.read(reinterpret_cast<char *>(bytes->data()), size);
        return input.good();
    }

    std::string Hex(const std::vector<BYTE> &bytes)
    {
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (BYTE value : bytes)
            output << std::setw(2) << static_cast<unsigned int>(value);
        return output.str();
    }

    std::string Sha256(const BYTE *data, size_t size)
    {
        BCRYPT_ALG_HANDLE algorithm = NULL;
        BCRYPT_HASH_HANDLE hash = NULL;
        DWORD object_size = 0;
        DWORD digest_size = 0;
        DWORD result_size = 0;

        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                        NULL, 0) < 0)
            return std::string();
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size),
                              sizeof(object_size), &result_size, 0) < 0 ||
            BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                              reinterpret_cast<PUCHAR>(&digest_size),
                              sizeof(digest_size), &result_size, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return std::string();
        }

        std::vector<BYTE> object(object_size);
        std::vector<BYTE> digest(digest_size);
        if (BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                             NULL, 0, 0) < 0 ||
            BCryptHashData(hash, const_cast<PUCHAR>(data),
                           static_cast<ULONG>(size), 0) < 0 ||
            BCryptFinishHash(hash, digest.data(), digest_size, 0) < 0) {
            if (hash)
                BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return std::string();
        }

        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return Hex(digest);
    }

    std::string Sha256(const std::vector<BYTE> &bytes)
    {
        return Sha256(bytes.empty() ? NULL : bytes.data(), bytes.size());
    }

    std::string Sha256(const std::string &value)
    {
        return Sha256(reinterpret_cast<const BYTE *>(value.data()), value.size());
    }

    std::vector<std::string> Split(const std::string &value, char separator)
    {
        std::vector<std::string> parts;
        size_t begin = 0;
        while (begin <= value.size()) {
            const size_t end = value.find(separator, begin);
            parts.push_back(value.substr(begin, end == std::string::npos
                ? std::string::npos : end - begin));
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
        return parts;
    }

    std::string CaseId(const RenderCase &render_case, const FormatCase &format,
                       int max_bitmap_count)
    {
        std::ostringstream output;
        output << render_case.width << 'x' << render_case.height
               << "_t" << render_case.time_ms
               << '_' << format.name
               << "_max" << max_bitmap_count;
        return output.str();
    }

    BYTE Div255(unsigned int value)
    {
        return static_cast<BYTE>((value + 1 + ((value + 1) >> 8)) >> 8);
    }

    Canvas EmptyCanvas(int width, int height, XyColorSpace color_space)
    {
        const bool planar = color_space == XY_CS_AYUV_PLANAR;
        Canvas canvas = {
            width,
            height,
            planar,
            std::vector<BYTE>(static_cast<size_t>(width) * height * 4, 0)
        };
        const size_t plane_size = static_cast<size_t>(width) * height;
        if (planar) {
            std::fill(canvas.bytes.begin(), canvas.bytes.begin() + plane_size, 0xFF);
        } else if (color_space != XY_CS_ARGB_F) {
            for (size_t i = 3; i < canvas.bytes.size(); i += 4)
                canvas.bytes[i] = 0xFF;
        }
        return canvas;
    }

    Canvas CompositeReference(ASS_Image *images, int width, int height,
                              XyColorSpace color_space)
    {
        Canvas canvas = EmptyCanvas(width, height, color_space);
        const bool planar = canvas.planar;
        const size_t plane_size = static_cast<size_t>(width) * height;
        XySubRenderFrameCreater *frame_creater =
            XySubRenderFrameCreater::GetDefaultCreater();

        for (ASS_Image *image = images; image; image = image->next) {
            const DWORD argb =
                (image->color << 24) ^ (image->color >> 8) ^ 0xFF000000;
            const DWORD color = frame_creater->TransColor(argb);
            const BYTE color_a = static_cast<BYTE>(color >> 24);

            for (int y = 0; y < image->h; ++y) {
                for (int x = 0; x < image->w; ++x) {
                    const int output_x = image->dst_x + x;
                    const int output_y = image->dst_y + y;
                    if (output_x < 0 || output_x >= width ||
                        output_y < 0 || output_y >= height)
                        continue;

                    const BYTE coverage =
                        image->bitmap[y * image->stride + x];
                    const size_t output =
                        static_cast<size_t>(output_y) * width + output_x;

                    if (planar) {
                        BYTE &dst_a = canvas.bytes[output];
                        BYTE &dst_y = canvas.bytes[plane_size + output];
                        BYTE &dst_u = canvas.bytes[plane_size * 2 + output];
                        BYTE &dst_v = canvas.bytes[plane_size * 3 + output];
                        if (x < (image->w & ~15)) {
                            const unsigned int src_a =
                                ((static_cast<unsigned int>(coverage) + 1) *
                                 color_a) >> 8;
                            const unsigned int comp_a = 0x100 - src_a;
                            const unsigned int dst_blend =
                                ((dst_a ^ 0xFF) * comp_a + 0x80) >> 8;

                            dst_a = static_cast<BYTE>(
                                0xFF - min(src_a + dst_blend, 0xFFu));
                            dst_y = static_cast<BYTE>(
                                (((color >> 16) & 0xFF) * src_a +
                                 dst_y * comp_a + 0x80) >> 8);
                            dst_u = static_cast<BYTE>(
                                (((color >> 8) & 0xFF) * src_a +
                                 dst_u * comp_a + 0x80) >> 8);
                            dst_v = static_cast<BYTE>(
                                ((color & 0xFF) * src_a +
                                 dst_v * comp_a + 0x80) >> 8);
                        } else {
                            const BYTE src_a = Div255(
                                static_cast<unsigned int>(coverage) * color_a);
                            const BYTE comp_a = static_cast<BYTE>(~src_a);

                            dst_a = static_cast<BYTE>(
                                (src_a + Div255(
                                    static_cast<unsigned int>(dst_a ^ 0xFF) *
                                    comp_a)) ^ 0xFF);
                            dst_y = Div255(
                                ((color >> 16) & 0xFF) * src_a +
                                static_cast<unsigned int>(dst_y) * comp_a);
                            dst_u = Div255(
                                ((color >> 8) & 0xFF) * src_a +
                                static_cast<unsigned int>(dst_u) * comp_a);
                            dst_v = Div255(
                                (color & 0xFF) * src_a +
                                static_cast<unsigned int>(dst_v) * comp_a);
                        }
                    } else {
                        BYTE *dst = canvas.bytes.data() + output * 4;
                        const unsigned int src_a =
                            ((static_cast<unsigned int>(coverage) + 1) *
                             color_a) >> 8;
                        const unsigned int comp_a = 0x100 - src_a;
                        const unsigned int dst_opacity =
                            color_space == XY_CS_ARGB_F
                            ? dst[3] : dst[3] ^ 0xFF;

                        dst[0] = static_cast<BYTE>(
                            (static_cast<unsigned int>(dst[0]) * comp_a +
                             (color & 0xFF) * (src_a + 1)) >> 8);
                        dst[1] = static_cast<BYTE>(
                            (static_cast<unsigned int>(dst[1]) * comp_a +
                             ((color >> 8) & 0xFF) * (src_a + 1)) >> 8);
                        dst[2] = static_cast<BYTE>(
                            (static_cast<unsigned int>(dst[2]) * comp_a +
                             ((color >> 16) & 0xFF) * (src_a + 1)) >> 8);
                        const BYTE output_opacity = static_cast<BYTE>(
                            (dst_opacity * comp_a >> 8) + src_a);
                        dst[3] = color_space == XY_CS_ARGB_F
                            ? output_opacity : output_opacity ^ 0xFF;
                    }
                }
            }
        }
        return canvas;
    }

    FrameSnapshot CaptureFrame(IXySubRenderFrame *frame, int width, int height)
    {
        FrameSnapshot snapshot = {};
        EXPECT_HRESULT_SUCCEEDED(frame->GetXyColorSpace(&snapshot.color_space));
        const XyColorSpace color_space =
            static_cast<XyColorSpace>(snapshot.color_space);
        const bool planar = color_space == XY_CS_AYUV_PLANAR;
        snapshot.canvas = EmptyCanvas(width, height, color_space);
        const size_t plane_size = static_cast<size_t>(width) * height;

        int count = 0;
        EXPECT_HRESULT_SUCCEEDED(frame->GetBitmapCount(&count));
        std::ostringstream metadata;
        metadata << snapshot.color_space << '|' << count;

        for (int i = 0; i < count; ++i) {
            BitmapInfo info = {};
            LPCVOID pixels = NULL;
            EXPECT_HRESULT_SUCCEEDED(frame->GetBitmap(
                i, NULL, &info.position, &info.size, &pixels, &info.pitch));
            EXPECT_TRUE(pixels != NULL);
            if (!pixels)
                continue;

            snapshot.bitmaps.push_back(info);
            metadata << '|' << i << ':' << info.position.x << ','
                     << info.position.y << ',' << info.size.cx << ','
                     << info.size.cy << ',' << info.pitch;

            if (planar) {
                XyPlannerFormatExtra extra = {};
                EXPECT_HRESULT_SUCCEEDED(frame->GetBitmapExtra(i, &extra));
                const BYTE *planes[] = {
                    static_cast<const BYTE *>(extra.plans[0]),
                    static_cast<const BYTE *>(extra.plans[1]),
                    static_cast<const BYTE *>(extra.plans[2]),
                    static_cast<const BYTE *>(extra.plans[3])
                };
                for (int plane = 0; plane < 4; ++plane) {
                    for (int y = 0; y < info.size.cy; ++y) {
                        const BYTE *row = planes[plane] + y * info.pitch;
                        snapshot.visible_bytes.insert(snapshot.visible_bytes.end(),
                                                      row, row + info.size.cx);
                    }
                }
                for (int y = 0; y < info.size.cy; ++y) {
                    for (int x = 0; x < info.size.cx; ++x) {
                        if (planes[0][y * info.pitch + x] == 0xFF)
                            continue;
                        const int output_x = info.position.x + x;
                        const int output_y = info.position.y + y;
                        if (output_x < 0 || output_x >= width ||
                            output_y < 0 || output_y >= height)
                            continue;
                        const size_t output =
                            static_cast<size_t>(output_y) * width + output_x;
                        for (int plane = 0; plane < 4; ++plane) {
                            snapshot.canvas.bytes[
                                static_cast<size_t>(plane) * plane_size + output] =
                                planes[plane][y * info.pitch + x];
                        }
                    }
                }
            } else {
                const BYTE *source = static_cast<const BYTE *>(pixels);
                for (int y = 0; y < info.size.cy; ++y) {
                    const BYTE *row = source + y * info.pitch;
                    snapshot.visible_bytes.insert(snapshot.visible_bytes.end(),
                                                  row, row + info.size.cx * 4);
                }

                const BYTE transparent_alpha =
                    color_space == XY_CS_ARGB_F ? 0x00 : 0xFF;
                for (int y = 0; y < info.size.cy; ++y) {
                    for (int x = 0; x < info.size.cx; ++x) {
                        const BYTE *pixel =
                            source + y * info.pitch + x * 4;
                        if (pixel[3] == transparent_alpha)
                            continue;
                        const int output_x = info.position.x + x;
                        const int output_y = info.position.y + y;
                        if (output_x < 0 || output_x >= width ||
                            output_y < 0 || output_y >= height)
                            continue;
                        BYTE *output = snapshot.canvas.bytes.data() +
                            (static_cast<size_t>(output_y) * width + output_x) * 4;
                        memcpy(output, pixel, 4);
                    }
                }
            }
        }
        snapshot.metadata = metadata.str();
        return snapshot;
    }

    std::string OutputRegion(const FrameSnapshot &snapshot, int x, int y)
    {
        for (size_t i = 0; i < snapshot.bitmaps.size(); ++i) {
            const BitmapInfo &info = snapshot.bitmaps[i];
            if (x < info.position.x || y < info.position.y ||
                x >= info.position.x + info.size.cx ||
                y >= info.position.y + info.size.cy)
                continue;
            const int local_x = x - info.position.x;
            std::ostringstream output;
            output << "bitmap=" << i << " local_x=" << local_x
                   << " region="
                   << (local_x < (info.size.cx & ~15) ? "SIMD" : "tail");
            return output.str();
        }
        return "bitmap=none region=background";
    }

    testing::AssertionResult CanvasEquals(
        const Canvas &expected, const FrameSnapshot &actual,
        const std::string &case_id)
    {
        if (expected.width != actual.canvas.width ||
            expected.height != actual.canvas.height ||
            expected.planar != actual.canvas.planar ||
            expected.bytes.size() != actual.canvas.bytes.size()) {
            return testing::AssertionFailure()
                << case_id << " canvas shape differs";
        }

        const auto mismatch = std::mismatch(
            expected.bytes.begin(), expected.bytes.end(),
            actual.canvas.bytes.begin());
        if (mismatch.first == expected.bytes.end())
            return testing::AssertionSuccess();

        const size_t index =
            static_cast<size_t>(mismatch.first - expected.bytes.begin());
        const size_t plane_size =
            static_cast<size_t>(expected.width) * expected.height;
        int x = 0;
        int y = 0;
        int channel = 0;
        if (expected.planar) {
            channel = static_cast<int>(index / plane_size);
            const size_t pixel = index % plane_size;
            x = static_cast<int>(pixel % expected.width);
            y = static_cast<int>(pixel / expected.width);
        } else {
            const size_t pixel = index / 4;
            channel = static_cast<int>(index % 4);
            x = static_cast<int>(pixel % expected.width);
            y = static_cast<int>(pixel / expected.width);
        }

        return testing::AssertionFailure()
            << case_id << " first pixel mismatch at (" << x << ',' << y
            << ") byte=" << channel
            << " expected=" << static_cast<unsigned int>(*mismatch.first)
            << " actual=" << static_cast<unsigned int>(*mismatch.second)
            << ' ' << OutputRegion(actual, x, y);
    }

    bool ReadGoldenManifest(GoldenManifest *manifest)
    {
        std::ifstream input(kGoldenPath, std::ios::binary);
        if (!input)
            return false;

        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            if (line.compare(0, 13, "baseline_sha=") == 0) {
                manifest->baseline_sha = line.substr(13);
                continue;
            }
            if (line.compare(0, 15, "fixture_sha256=") == 0) {
                manifest->fixture_sha256 = line.substr(15);
                continue;
            }
            if (line.compare(0, 12, "font_sha256=") == 0) {
                manifest->font_sha256 = line.substr(12);
                continue;
            }

            const std::vector<std::string> fields = Split(line, '\t');
            if (fields.size() != 6)
                return false;
            GoldenEntry entry = {};
            entry.color_space = atoi(fields[1].c_str());
            entry.bitmap_count = atoi(fields[2].c_str());
            entry.metadata_sha256 = fields[3];
            entry.visible_sha256 = fields[4];
            entry.canvas_sha256 = fields[5];
            manifest->entries[fields[0]] = entry;
        }
        return input.eof() && !manifest->baseline_sha.empty() &&
               !manifest->fixture_sha256.empty() &&
               !manifest->font_sha256.empty() &&
               !manifest->entries.empty();
    }

    bool WriteGoldenManifest(
        const std::string &baseline_sha,
        const std::string &fixture_sha256,
        const std::string &font_sha256,
        const std::map<std::string, GoldenEntry> &entries)
    {
        std::ofstream output(kGoldenPath, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output << "# xy-vsfilter-libass-regression-v1\n"
               << "baseline_sha=" << baseline_sha << '\n'
               << "fixture_sha256=" << fixture_sha256 << '\n'
               << "font_sha256=" << font_sha256 << '\n'
               << "# id\tcolor_space\tbitmap_count\tmetadata_sha256"
                  "\tvisible_sha256\tcanvas_sha256\n";
        for (const auto &item : entries) {
            const GoldenEntry &entry = item.second;
            output << item.first << '\t'
                   << entry.color_space << '\t'
                   << entry.bitmap_count << '\t'
                   << entry.metadata_sha256 << '\t'
                   << entry.visible_sha256 << '\t'
                   << entry.canvas_sha256 << '\n';
        }
        return output.good();
    }

    void WriteActualSnapshot(const std::string &case_id,
                             const FrameSnapshot &snapshot)
    {
        const std::wstring results = EnvironmentString(L"XY_TEST_RESULTS");
        if (results.empty())
            return;
        const std::wstring filename(case_id.begin(), case_id.end());
        const std::wstring path =
            results + L"\\actual_" + filename + L".bin";
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            return;

        const uint32_t metadata_size =
            static_cast<uint32_t>(snapshot.metadata.size());
        const uint32_t visible_size =
            static_cast<uint32_t>(snapshot.visible_bytes.size());
        const uint32_t canvas_size =
            static_cast<uint32_t>(snapshot.canvas.bytes.size());
        output.write(reinterpret_cast<const char *>(&metadata_size),
                     sizeof(metadata_size));
        output.write(snapshot.metadata.data(), snapshot.metadata.size());
        output.write(reinterpret_cast<const char *>(&visible_size),
                     sizeof(visible_size));
        output.write(
            reinterpret_cast<const char *>(snapshot.visible_bytes.data()),
            snapshot.visible_bytes.size());
        output.write(reinterpret_cast<const char *>(&canvas_size),
                     sizeof(canvas_size));
        output.write(
            reinterpret_cast<const char *>(snapshot.canvas.bytes.data()),
            snapshot.canvas.bytes.size());
    }

    class LibassRegressionTest : public testing::Test
    {
    protected:
        CCritSec lock_;
        CComPtr<CRenderedTextSubtitle> subtitle_;
        std::vector<BYTE> fixture_bytes_;
        std::vector<BYTE> font_bytes_;

        void SetUp() override
        {
            ASSERT_TRUE(ReadBinaryFile(kFixturePath, &fixture_bytes_))
                << "Run unit_test.exe from the repository root";
            ASSERT_TRUE(ReadBinaryFile(kFontPath, &font_bytes_))
                << "Pinned libass submodules are not initialized";

            subtitle_ = DEBUG_NEW CRenderedTextSubtitle(&lock_);
            ASSERT_TRUE(subtitle_ != NULL);
            ASSERT_TRUE(subtitle_->Open(
                fixture_bytes_.data(),
                static_cast<int>(fixture_bytes_.size()),
                DEFAULT_CHARSET,
                _T("CSRI memory subtitles")));
            ASSERT_TRUE(subtitle_->m_ass_context.m_assloaded);

            ass_add_font(
                subtitle_->m_ass_context.m_ass.get(),
                "font1.ttf",
                reinterpret_cast<char *>(font_bytes_.data()),
                static_cast<int>(font_bytes_.size()));
            ass_set_fonts(
                subtitle_->m_ass_context.m_renderer.get(),
                NULL,
                "Pixel Operator Mono",
                ASS_FONTPROVIDER_NONE,
                NULL,
                0);
            subtitle_->m_render_backend = SUBTITLE_RENDER_BACKEND_LIBASS;
        }

        ASS_Image *RawImages(const RenderCase &render_case)
        {
            ass_set_storage_size(
                subtitle_->m_ass_context.m_renderer.get(),
                render_case.width,
                render_case.height);
            ass_set_frame_size(
                subtitle_->m_ass_context.m_renderer.get(),
                render_case.width,
                render_case.height);
            int changed = 0;
            return ass_render_frame(
                subtitle_->m_ass_context.m_renderer.get(),
                subtitle_->m_ass_context.m_track.get(),
                render_case.time_ms,
                &changed);
        }

        CComPtr<IXySubRenderFrame> Render(
            const RenderCase &render_case,
            const FormatCase &format,
            int max_bitmap_count)
        {
            EXPECT_HRESULT_SUCCEEDED(
                subtitle_->SetMaxBitmapCount(max_bitmap_count));
            subtitle_->m_last_frame = NULL;
            CComPtr<IXySubRenderFrame> frame;
            const RECT target = {
                0, 0, render_case.width, render_case.height
            };
            const SIZE frame_size = {
                render_case.width, render_case.height
            };
            EXPECT_EQ(S_OK, subtitle_->RenderEx(
                &frame,
                format.spd_type,
                target,
                target,
                frame_size,
                static_cast<REFERENCE_TIME>(render_case.time_ms) * 10000,
                kFps));
            EXPECT_TRUE(frame != NULL);
            return frame;
        }
    };
}

TEST_F(LibassRegressionTest, FixtureContainsAnEmptyFrame)
{
    const RenderCase empty = { 640, 360, 500 };
    EXPECT_TRUE(RawImages(empty) == NULL);
}

TEST_F(LibassRegressionTest, OverlapExercisesSimdAndScalarTail)
{
    const RenderCase overlap = { 640, 360, 2500 };
    ASS_Image *images = RawImages(overlap);
    ASSERT_TRUE(images != NULL);

    bool overlap_in_simd = false;
    bool overlap_in_tail = false;
    std::ostringstream image_layout;
    for (ASS_Image *a = images; a; a = a->next) {
        int nonzero_left = a->w;
        int nonzero_right = -1;
        for (int y = 0; y < a->h; ++y) {
            for (int x = 0; x < a->w; ++x) {
                if (a->bitmap[y * a->stride + x]) {
                    nonzero_left = min(nonzero_left, x);
                    nonzero_right = max(nonzero_right, x);
                }
            }
        }
        image_layout << '[' << a->dst_x << ',' << a->dst_y << ' '
                     << a->w << 'x' << a->h << " stride=" << a->stride
                     << " nonzero-x=" << nonzero_left << ".."
                     << nonzero_right
                     << "] ";
        for (ASS_Image *b = a->next; b; b = b->next) {
            const int left = max(a->dst_x, b->dst_x);
            const int top = max(a->dst_y, b->dst_y);
            const int right = min(a->dst_x + a->w, b->dst_x + b->w);
            const int bottom = min(a->dst_y + a->h, b->dst_y + b->h);
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    const int ax = x - a->dst_x;
                    const int ay = y - a->dst_y;
                    const int bx = x - b->dst_x;
                    const int by = y - b->dst_y;
                    if (!a->bitmap[ay * a->stride + ax] ||
                        !b->bitmap[by * b->stride + bx])
                        continue;
                    if (ax < (a->w & ~15) && bx < (b->w & ~15))
                        overlap_in_simd = true;
                    if (ax >= (a->w & ~15) && bx >= (b->w & ~15))
                        overlap_in_tail = true;
                }
            }
        }
    }
    EXPECT_TRUE(overlap_in_simd);
    EXPECT_TRUE(overlap_in_tail) << image_layout.str();
}

TEST_F(LibassRegressionTest, ScalarReferenceAndGoldenOutputsMatch)
{
    const bool update_golden =
        EnvironmentString(L"XY_UPDATE_GOLDEN") == L"1";
    const std::string fixture_sha256 = Sha256(fixture_bytes_);
    const std::string font_sha256 = Sha256(font_bytes_);
    ASSERT_FALSE(fixture_sha256.empty());
    ASSERT_FALSE(font_sha256.empty());

    GoldenManifest manifest;
    if (!update_golden) {
        ASSERT_TRUE(ReadGoldenManifest(&manifest))
            << "Golden manifest is missing or invalid. Use "
               "Run-RegressionTests.ps1 -UpdateGolden explicitly.";
        ASSERT_EQ(fixture_sha256, manifest.fixture_sha256)
            << "Fixture changed without a deliberate golden update";
        ASSERT_EQ(font_sha256, manifest.font_sha256)
            << "Font changed without a deliberate golden update";
    }

    std::map<std::string, GoldenEntry> generated;
    for (const RenderCase &render_case : kRenderCases) {
        for (const FormatCase &format : kFormats) {
            for (const int max_bitmap_count : { 1, 16 }) {
                const std::string case_id =
                    CaseId(render_case, format, max_bitmap_count);
                SCOPED_TRACE(case_id);

                CComPtr<IXySubRenderFrame> frame =
                    Render(render_case, format, max_bitmap_count);
                ASSERT_TRUE(frame != NULL);
                const FrameSnapshot snapshot =
                    CaptureFrame(frame, render_case.width, render_case.height);

                ASS_Image *images = RawImages(render_case);
                ASSERT_TRUE(images != NULL);
                const Canvas expected = CompositeReference(
                    images,
                    render_case.width,
                    render_case.height,
                    format.color_space);
                ASSERT_TRUE(CanvasEquals(expected, snapshot, case_id));

                GoldenEntry actual = {};
                actual.color_space = snapshot.color_space;
                actual.bitmap_count =
                    static_cast<int>(snapshot.bitmaps.size());
                actual.metadata_sha256 = Sha256(snapshot.metadata);
                actual.visible_sha256 = Sha256(snapshot.visible_bytes);
                actual.canvas_sha256 = Sha256(snapshot.canvas.bytes);
                ASSERT_FALSE(actual.metadata_sha256.empty());
                ASSERT_FALSE(actual.visible_sha256.empty());
                ASSERT_FALSE(actual.canvas_sha256.empty());
                generated[case_id] = actual;

                if (update_golden)
                    continue;

                const auto found = manifest.entries.find(case_id);
                ASSERT_TRUE(found != manifest.entries.end())
                    << "Golden entry is missing";
                const GoldenEntry &golden = found->second;
                if (actual.color_space != golden.color_space ||
                    actual.bitmap_count != golden.bitmap_count ||
                    actual.metadata_sha256 != golden.metadata_sha256 ||
                    actual.visible_sha256 != golden.visible_sha256 ||
                    actual.canvas_sha256 != golden.canvas_sha256) {
                    WriteActualSnapshot(case_id, snapshot);
                    FAIL()
                        << "Golden mismatch for " << case_id
                        << "\nactual metadata: " << snapshot.metadata
                        << "\nexpected color/count: "
                        << golden.color_space << '/' << golden.bitmap_count
                        << "\nactual color/count: "
                        << actual.color_space << '/' << actual.bitmap_count
                        << "\nexpected metadata/visible/canvas: "
                        << golden.metadata_sha256 << '/'
                        << golden.visible_sha256 << '/'
                        << golden.canvas_sha256
                        << "\nactual metadata/visible/canvas: "
                        << actual.metadata_sha256 << '/'
                        << actual.visible_sha256 << '/'
                        << actual.canvas_sha256;
                }
            }
        }
    }

    if (update_golden) {
        const std::wstring baseline_wide =
            EnvironmentString(L"XY_GOLDEN_BASELINE_SHA");
        const std::string baseline(
            baseline_wide.begin(), baseline_wide.end());
        ASSERT_EQ(40u, baseline.size())
            << "XY_GOLDEN_BASELINE_SHA must be a full commit SHA";
        ASSERT_TRUE(WriteGoldenManifest(
            baseline, fixture_sha256, font_sha256, generated));
    } else {
        EXPECT_EQ(generated.size(), manifest.entries.size())
            << "Golden manifest contains stale entries";
    }
}
