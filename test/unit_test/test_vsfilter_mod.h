#pragma once

#include <gtest/gtest.h>

#include "VsFilterCompatibility.h"
#include "RTS.h"
#include "cache_manager.h"
#include "mod_style.h"
#include "xy_bitmap.h"

namespace
{
const BYTE kModTestPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
    0x89, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47,
    0x42, 0x00, 0xae, 0xce, 0x1c, 0xe9, 0x00, 0x00,
    0x00, 0x04, 0x67, 0x41, 0x4d, 0x41, 0x00, 0x00,
    0xb1, 0x8f, 0x0b, 0xfc, 0x61, 0x05, 0x00, 0x00,
    0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00,
    0x0e, 0xc3, 0x00, 0x00, 0x0e, 0xc3, 0x01, 0xc7,
    0x6f, 0xa8, 0x64, 0x00, 0x00, 0x00, 0x0d, 0x49,
    0x44, 0x41, 0x54, 0x18, 0x57, 0x63, 0x10, 0x54,
    0x32, 0x6e, 0x00, 0x00, 0x01, 0x95, 0x00, 0xe7,
    0x44, 0x5d, 0xe0, 0x78, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

class ScopedTempPng
{
public:
    ScopedTempPng()
    {
        wchar_t directory[MAX_PATH] = {};
        wchar_t path[MAX_PATH] = {};
        if (GetTempPathW(_countof(directory), directory)
                && GetTempFileNameW(directory, L"xym", 0, path)) {
            m_path = path;
            DeleteFileW(m_path);
        }
    }

    ~ScopedTempPng()
    {
        if (!m_path.IsEmpty()) {
            DeleteFileW(m_path);
        }
    }

    bool IsValid() const
    {
        return !m_path.IsEmpty();
    }

    const CStringW& GetPath() const
    {
        return m_path;
    }

    bool Write() const
    {
        HANDLE file = CreateFileW(m_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD written = 0;
        const BOOL succeeded = WriteFile(file, kModTestPng,
            static_cast<DWORD>(sizeof(kModTestPng)), &written, NULL);
        CloseHandle(file);
        return succeeded && written == sizeof(kModTestPng);
    }

private:
    CStringW m_path;
};

class ScopedColorSpace
{
public:
    ScopedColorSpace()
        : m_creator(XySubRenderFrameCreater::GetDefaultCreater())
        , m_previous(XY_CS_ARGB)
    {
        m_creator->GetColorSpace(&m_previous);
        m_creator->SetColorSpace(XY_CS_ARGB);
    }

    ~ScopedColorSpace()
    {
        m_creator->SetColorSpace(m_previous);
    }

private:
    XySubRenderFrameCreater* m_creator;
    XyColorSpace m_previous;
};

SharedPtrCWord FindTextWord(CSubtitle* subtitle, int index)
{
    if (!subtitle) {
        return SharedPtrCWord();
    }

    POSITION position = subtitle->m_words.GetHeadPosition();
    while (position) {
        const SharedPtrCWord word = subtitle->m_words.GetNext(position);
        if (!word->m_fLineBreak && index-- == 0) {
            return word;
        }
    }
    return SharedPtrCWord();
}
}

TEST(VsFilterModTest, NormalizesCompatibilityMode)
{
    EXPECT_EQ(VSFILTER_COMPATIBILITY_XY,
        NormalizeVsFilterCompatibilityMode(VSFILTER_COMPATIBILITY_XY));
    EXPECT_EQ(VSFILTER_COMPATIBILITY_MOD,
        NormalizeVsFilterCompatibilityMode(VSFILTER_COMPATIBILITY_MOD));
    EXPECT_EQ(VSFILTER_COMPATIBILITY_XY, NormalizeVsFilterCompatibilityMode(-1));
    EXPECT_EQ(VSFILTER_COMPATIBILITY_XY,
        NormalizeVsFilterCompatibilityMode(VSFILTER_COMPATIBILITY_COUNT));
}

TEST(VsFilterModTest, DecodesEmbeddedPngOnceAndReusesImmutablePixels)
{
    ModImageCache cache;
    cache.RegisterEmbedded(L"Image.PNG", kModTestPng, sizeof(kModTestPng));

    const SharedPtrConstModImageResource first =
        cache.Resolve(L"image.png", CStringW(), CStringW());
    const SharedPtrConstModImageResource second =
        cache.Resolve(L"image.png", CStringW(), CStringW());

    ASSERT_NE(nullptr, first.get());
    ASSERT_NE(nullptr, second.get());
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(1, first->GetWidth());
    EXPECT_EQ(1, first->GetHeight());
    ASSERT_EQ(4u, first->GetByteCount());
    EXPECT_EQ(17, first->GetPixels()[0]);
    EXPECT_EQ(34, first->GetPixels()[1]);
    EXPECT_EQ(51, first->GetPixels()[2]);
    EXPECT_EQ(128, first->GetPixels()[3]);
}

TEST(VsFilterModTest, NegativeImageResultPersistsUntilResourceInvalidation)
{
    ScopedTempPng file;
    ASSERT_TRUE(file.IsValid());

    ModImageCache cache;
    EXPECT_EQ(nullptr, cache.Resolve(file.GetPath(), CStringW(), CStringW()).get());
    ASSERT_TRUE(file.Write());
    EXPECT_EQ(nullptr, cache.Resolve(file.GetPath(), CStringW(), CStringW()).get());

    cache.ResetDecoded();
    EXPECT_NE(nullptr, cache.Resolve(file.GetPath(), CStringW(), CStringW()).get());
}

TEST(VsFilterModTest, CreatesSeparateGradientAndImagePaintSources)
{
    ScopedColorSpace color_space;

    ModStyleState gradient_state;
    gradient_state.paint[0].mode = MOD_PAINT_GRADIENT;
    gradient_state.paint[0].colors[0] = 0x000000ff;
    gradient_state.paint[0].colors[1] = 0x0000ff00;
    gradient_state.paint[0].colors[2] = 0x00ff0000;
    gradient_state.paint[0].colors[3] = 0x00ffffff;
    const DWORD solid_colors[2] = {0xffffffff, 0};
    const SharedPtrConstModPaintSource gradient =
        CreateModPaintSource(gradient_state, 0, -1, solid_colors, 0);

    ASSERT_NE(nullptr, gradient.get());
    EXPECT_EQ(0xfe7f7f7f, gradient->GetColor(0, 1, 1, 2, 2, 0, 0, 0));

    const BYTE rgba[] = {17, 34, 51, 128};
    ModStyleState image_state;
    image_state.paint[0].mode = MOD_PAINT_IMAGE;
    image_state.paint[0].image.reset(
        new ModImageResource(1, 1, rgba, sizeof(rgba), L"memory.png"));
    const DWORD image_solid_colors[2] = {0x80112233, 0};
    const SharedPtrConstModPaintSource image =
        CreateModPaintSource(image_state, 0, -1, image_solid_colors, 0);

    ASSERT_NE(nullptr, image.get());
    EXPECT_NE(gradient->GetHash(), image->GetHash());
    EXPECT_EQ(0x40112233, image->GetColor(0, 0, 0, 1, 1, 0, 0, 0));

    ModStyleState changed_gradient_state(gradient_state);
    changed_gradient_state.paint[0].colors[3] ^= 0x00010101;
    const SharedPtrConstModPaintSource changed_gradient =
        CreateModPaintSource(changed_gradient_state, 0, -1, solid_colors, 0);
    ASSERT_NE(nullptr, changed_gradient.get());
    EXPECT_NE(gradient->GetHash(), changed_gradient->GetHash());
}

TEST(VsFilterModTest, PaintOnlyStateDoesNotFragmentPathCache)
{
    ModStyleState plain_state;
    plain_state.RecomputeHash();

    ModStyleState gradient_state;
    gradient_state.feature_mask = MOD_FEATURE_GRADIENT;
    gradient_state.paint[0].mode = MOD_PAINT_GRADIENT;
    gradient_state.paint[0].colors[3] = 0x00112233;
    gradient_state.RecomputeHash();

    EXPECT_NE(plain_state.hash, gradient_state.hash);
    EXPECT_EQ(plain_state.path_hash, gradient_state.path_hash);

    gradient_state.feature_mask |= MOD_FEATURE_DISTORT;
    gradient_state.distort_x[1] = 1.25;
    gradient_state.RecomputeHash();
    EXPECT_NE(plain_state.path_hash, gradient_state.path_hash);
}

TEST(VsFilterModTest, SwitchesSemanticsWithoutDiscardingLexicalTagCache)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_XY;
    renderer.m_dstScreenSize = CSize(640, 360);
    renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    renderer.Add(
        L"{\\1vc(&H0000ff&,&H00ff00&,&Hff0000&,&Hffffff&)}A{\\r}B",
        true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List xy_subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &xy_subtitles));
    ASSERT_FALSE(xy_subtitles.IsEmpty());
    const SharedPtrCWord xy_first = FindTextWord(xy_subtitles.GetHead().s, 0);
    ASSERT_NE(nullptr, xy_first.get());
    EXPECT_EQ(nullptr, xy_first->m_mod_style.get());

    AssTagListMruCache* tag_cache = CacheManager::GetAssTagListMruCache();
    const size_t cached_tag_count = tag_cache->GetCurItemNum();
    ASSERT_GT(cached_tag_count, static_cast<size_t>(0));

    renderer.SetVsFilterCompatibilityMode(VSFILTER_COMPATIBILITY_MOD);
    EXPECT_EQ(SUBTITLE_RENDER_BACKEND_VSFILTER, renderer.m_render_backend);
    EXPECT_EQ(VSFILTER_COMPATIBILITY_MOD, renderer.m_vsfilter_compatibility_mode);
    EXPECT_EQ(cached_tag_count, tag_cache->GetCurItemNum());

    CSubtitle2List mod_subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &mod_subtitles));
    ASSERT_FALSE(mod_subtitles.IsEmpty());
    const SharedPtrCWord mod_first = FindTextWord(mod_subtitles.GetHead().s, 0);
    const SharedPtrCWord mod_second = FindTextWord(mod_subtitles.GetHead().s, 1);
    ASSERT_NE(nullptr, mod_first.get());
    ASSERT_NE(nullptr, mod_second.get());
    ASSERT_NE(nullptr, mod_first->m_mod_style.get());
    EXPECT_NE(0u, mod_first->m_mod_style->feature_mask & MOD_FEATURE_GRADIENT);
    EXPECT_EQ(nullptr, mod_second->m_mod_style.get());
    EXPECT_EQ(cached_tag_count, tag_cache->GetCurItemNum());
}

TEST(VsFilterModTest, ModModeWithoutModTagsKeepsSidecarsEmpty)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_MOD;
    renderer.m_dstScreenSize = CSize(640, 360);
    renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    renderer.Add(L"{\\t(0,1000,\\fscx120)\\clip(0,0,640,360)}Plain text",
        true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &subtitles));
    ASSERT_FALSE(subtitles.IsEmpty());

    POSITION position = subtitles.GetHead().s->m_words.GetHeadPosition();
    while (position) {
        const SharedPtrCWord word = subtitles.GetHead().s->m_words.GetNext(position);
        EXPECT_EQ(nullptr, word->m_mod_style.get());
    }
}

TEST(VsFilterModTest, SeparatesRasterCachesByModGeometryState)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_MOD;
    renderer.m_dstScreenSize = CSize(640, 360);
    renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    renderer.Add(L"A", true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &subtitles));
    ASSERT_FALSE(subtitles.IsEmpty());
    const SharedPtrCWord word = FindTextWord(subtitles.GetHead().s, 0);
    ASSERT_NE(nullptr, word.get());

    PathDataCacheKey base_key(*word);
    base_key.UpdateHashValue();

    word->m_mod_scale_x *= 2;
    PathDataCacheKey scale_x_key(*word);
    scale_x_key.UpdateHashValue();
    EXPECT_FALSE(base_key == scale_x_key);
    EXPECT_NE(base_key.GetHashValue(), scale_x_key.GetHashValue());

    word->m_mod_scale_x /= 2;
    word->m_mod_scale_y *= 2;
    PathDataCacheKey scale_y_key(*word);
    scale_y_key.UpdateHashValue();
    EXPECT_FALSE(base_key == scale_y_key);
    EXPECT_NE(base_key.GetHashValue(), scale_y_key.GetHashValue());

    word->m_mod_scale_y /= 2;
    word->m_is_opaque_box = true;
    PathDataCacheKey opaque_box_key(*word);
    opaque_box_key.UpdateHashValue();
    EXPECT_FALSE(base_key == opaque_box_key);
    EXPECT_NE(base_key.GetHashValue(), opaque_box_key.GetHashValue());
}

TEST(VsFilterModTest, AnimatedRandomAmplitudeUsesModIntegerStorage)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_MOD;
    renderer.m_dstScreenSize = CSize(640, 360);
    renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    renderer.Add(L"{\\t(0,1000,\\rnd20)}A", true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(333 * 10000i64, 25.0, &subtitles));
    ASSERT_FALSE(subtitles.IsEmpty());
    const SharedPtrCWord word = FindTextWord(subtitles.GetHead().s, 0);
    ASSERT_NE(nullptr, word.get());
    ASSERT_NE(nullptr, word->m_mod_style.get());
    EXPECT_DOUBLE_EQ(53, word->m_mod_style->random_x);
    EXPECT_DOUBLE_EQ(53, word->m_mod_style->random_y);
    EXPECT_DOUBLE_EQ(53, word->m_mod_style->random_z);
}

// Default mode degrades MOD-only inline tags to the longest shorter prefix that
// names a legacy command, with the re-sliced param left intact (e.g. \rnd20 ->
// \r with "nd20"). The junk param makes the legacy case reset its property,
// matching how a renderer without MOD support would lex the tag. \r with no
// param resets to org, so each degraded word equals its \r-reset control.
TEST(VsFilterModTest, DefaultModeDegradesInlineModTagsToLegacyReset)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_XY;
    renderer.m_dstScreenSize = CSize(640, 360);
    renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    renderer.Add(
        L"{\\fs80\\rnd20}A\\N{\\r}a\\N"
        L"{\\fs80\\fsvp10}B\\N{\\r}b\\N"
        L"{\\frz30\\frs10}C\\N{\\r}c",
        true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &subtitles));
    ASSERT_FALSE(subtitles.IsEmpty());

    const SharedPtrCWord random_word = FindTextWord(subtitles.GetHead().s, 0);
    const SharedPtrCWord random_control = FindTextWord(subtitles.GetHead().s, 1);
    const SharedPtrCWord spacing_word = FindTextWord(subtitles.GetHead().s, 2);
    const SharedPtrCWord spacing_control = FindTextWord(subtitles.GetHead().s, 3);
    const SharedPtrCWord rotation_word = FindTextWord(subtitles.GetHead().s, 4);
    const SharedPtrCWord rotation_control = FindTextWord(subtitles.GetHead().s, 5);
    ASSERT_NE(nullptr, random_word.get());
    ASSERT_NE(nullptr, random_control.get());
    ASSERT_NE(nullptr, spacing_word.get());
    ASSERT_NE(nullptr, spacing_control.get());
    ASSERT_NE(nullptr, rotation_word.get());
    ASSERT_NE(nullptr, rotation_control.get());

    // \rnd20 -> \r "nd20" (style reset), \fsvp10 -> \fs "vp10" (fontSize reset,
    // overriding \fs80), \frs10 -> \fr "s10" (fontAngleZ reset, overriding \frz30).
    EXPECT_EQ(random_control->m_style.get().fontSize, random_word->m_style.get().fontSize);
    EXPECT_EQ(spacing_control->m_style.get().fontSize, spacing_word->m_style.get().fontSize);
    EXPECT_EQ(rotation_control->m_style.get().fontAngleZ, rotation_word->m_style.get().fontAngleZ);
    EXPECT_EQ(nullptr, random_word->m_mod_style.get());
    EXPECT_EQ(nullptr, spacing_word->m_mod_style.get());
    EXPECT_EQ(nullptr, rotation_word->m_mod_style.get());
}

// Empty bracket parameters do not suppress the inline remainder that the old
// longest-prefix lexer appended. With a non-zero style angle, \frs() must still
// degrade to \fr with parameter "s" (numeric value 0), not to an empty \fr
// which would restore the original angle.
TEST(VsFilterModTest, DefaultModeKeepsLegacyRemainderForEmptyBrackets)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_XY;
    renderer.m_dstScreenSize = CSize(640, 360);
    STSStyle* default_style = renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    ASSERT_NE(nullptr, default_style);
    default_style->fontAngleZ = 17;
    renderer.Add(L"{\\frs()}A\\N{\\fr0}a", true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &subtitles));
    ASSERT_FALSE(subtitles.IsEmpty());
    const SharedPtrCWord fallback = FindTextWord(subtitles.GetHead().s, 0);
    const SharedPtrCWord control = FindTextWord(subtitles.GetHead().s, 1);
    ASSERT_NE(nullptr, fallback.get());
    ASSERT_NE(nullptr, control.get());
    EXPECT_DOUBLE_EQ(0, fallback->m_style.get().fontAngleZ);
    EXPECT_EQ(control->m_style.get().fontAngleZ, fallback->m_style.get().fontAngleZ);
    EXPECT_EQ(nullptr, fallback->m_mod_style.get());
}

// \rndx must skip the MOD prefix "rnd" (CMD_rnd) and fall back to \r, not degrade
// into another MOD command. Guards the legacy_type < CMD_1img check.
TEST(VsFilterModTest, DefaultModeDegradesRndxSkippingModPrefix)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_XY;
    renderer.m_dstScreenSize = CSize(640, 360);
    renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    renderer.Add(L"{\\rndx20}A\\N{\\r}a", true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &subtitles));
    ASSERT_FALSE(subtitles.IsEmpty());
    const SharedPtrCWord word = FindTextWord(subtitles.GetHead().s, 0);
    const SharedPtrCWord control = FindTextWord(subtitles.GetHead().s, 1);
    ASSERT_NE(nullptr, word.get());
    ASSERT_NE(nullptr, control.get());
    EXPECT_EQ(control->m_style.get().fontSize, word->m_style.get().fontSize);
    EXPECT_EQ(nullptr, word->m_mod_style.get());
}

// \mover(...) degrades to \move using the bracket params; a 4-arg mover satisfies
// \move's count==4 guard and produces EF_MOVE, without touching MOD sidecars.
TEST(VsFilterModTest, DefaultModeDegradesMoverToMoveEffect)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_XY;
    renderer.m_dstScreenSize = CSize(640, 360);
    renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    renderer.Add(L"{\\mover(10,20,30,40)}A", true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &subtitles));
    ASSERT_FALSE(subtitles.IsEmpty());
    ASSERT_NE(nullptr, subtitles.GetHead().s->m_effects[EF_MOVE]);
    EXPECT_EQ(nullptr, subtitles.GetHead().s->m_mod_effects.get());
}

// MOD-only tags whose shorter prefixes match no legacy command are skipped
// entirely in default mode (legacyCmdType == CMD_COUNT -> continue).
TEST(VsFilterModTest, DefaultModeSkipsModTagsWithoutLegacyPrefix)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);
    renderer.m_render_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
    renderer.m_vsfilter_compatibility_mode = VSFILTER_COMPATIBILITY_XY;
    renderer.m_dstScreenSize = CSize(640, 360);
    renderer.CreateDefaultStyle(DEFAULT_CHARSET);
    renderer.Add(L"{\\z10}A\\N{\\jitter(1,2,3,4,5)}B", true, 0, 1000);
    renderer.Sort();
    ASSERT_TRUE(renderer.Init(CRect(0, 0, 640, 360), CRect(0, 0, 640, 360),
        CSize(640, 360)));

    CSubtitle2List subtitles;
    ASSERT_EQ(S_OK, renderer.ParseScript(500 * 10000i64, 25.0, &subtitles));
    ASSERT_FALSE(subtitles.IsEmpty());
    const SharedPtrCWord z_word = FindTextWord(subtitles.GetHead().s, 0);
    const SharedPtrCWord jitter_word = FindTextWord(subtitles.GetHead().s, 1);
    ASSERT_NE(nullptr, z_word.get());
    ASSERT_NE(nullptr, jitter_word.get());
    EXPECT_EQ(nullptr, z_word->m_mod_style.get());
    EXPECT_EQ(nullptr, jitter_word->m_mod_style.get());
    EXPECT_EQ(nullptr, subtitles.GetHead().s->m_mod_effects.get());
}
