#pragma once

#include <afxwin.h>
#include <streams.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "flyweight_base_types.h"
#include "RTS.h"
#include "cache_manager.h"

namespace
{
class TestableText : public CText
{
public:
    TestableText(const FwSTSStyle& style, const CStringW& text, TextRendererMode mode)
        : CText(style, text, 0, 0, 0, 1.0, 1.0, mode)
    {
    }

    bool CapturePath(PathData* path)
    {
        return CreatePath(path);
    }
};

static PathData CaptureGdiPath(const STSStyle& style, const CStringW& text, bool* has_missing_glyph)
{
    HDC hdc = CreateCompatibleDC(NULL);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, 0xffffff);
    SetMapMode(hdc, MM_TEXT);
    FwCMyFont font(style);
    HFONT old_font = SelectFont(hdc, font.get());

    std::vector<WORD> glyphs(text.GetLength());
    GetGlyphIndicesW(hdc, text, text.GetLength(), glyphs.data(), GGI_MARK_NONEXISTING_GLYPHS);
    *has_missing_glyph = std::find(glyphs.begin(), glyphs.end(), 0xffff) != glyphs.end();

    PathData path;
    path.BeginPath(hdc);
    TextOutW(hdc, 0, 0, text, text.GetLength());
    path.EndPath(hdc);

    SelectFont(hdc, old_font);
    DeleteDC(hdc);
    return path;
}
}

TEST(FontFallbackTest, NormalizesInvalidModesToLegacyGdi)
{
    EXPECT_EQ(TEXT_RENDERER_LEGACY_GDI, NormalizeTextRendererMode(TEXT_RENDERER_LEGACY_GDI));
    EXPECT_EQ(TEXT_RENDERER_LEGACY_GDI, NormalizeTextRendererMode(-1));
    EXPECT_EQ(TEXT_RENDERER_LEGACY_GDI, NormalizeTextRendererMode(TEXT_RENDERER_MODE_COUNT));
}

TEST(FontFallbackTest, UsesFallbackForOneMissingGlyphInMixedString)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);

    STSStyle style;
    style.fontName = L"Arial";
    style.fontSize = 96;
    FwSTSStyle flyweight_style(style);

    CStringW text(L"ABC\x2919" L"DEF");
    bool has_missing_glyph = false;
    PathData gdi_path = CaptureGdiPath(style, text, &has_missing_glyph);

    TestableText legacy_text(flyweight_style, text, TEXT_RENDERER_LEGACY_GDI);
    TestableText auto_text(flyweight_style, text, TEXT_RENDERER_AUTO_FALLBACK);
    TestableText uniscribe_text(flyweight_style, text, TEXT_RENDERER_UNISCRIBE);
    PathData legacy_path;
    PathData auto_path;
    PathData uniscribe_path;

    ASSERT_TRUE(has_missing_glyph);
    ASSERT_TRUE(legacy_text.CapturePath(&legacy_path));
    ASSERT_TRUE(auto_text.CapturePath(&auto_path));
    ASSERT_TRUE(uniscribe_text.CapturePath(&uniscribe_path));
    EXPECT_TRUE(legacy_path == gdi_path);
    EXPECT_GT(auto_path.mPathPoints, 0);
    EXPECT_FALSE(auto_path == legacy_path);
    EXPECT_TRUE(uniscribe_path == auto_path);
}

TEST(FontFallbackTest, PreservesOutlineWhenSelectedFontHasGlyph)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);

    STSStyle style;
    style.fontName = L"Arial";
    style.fontSize = 96;
    FwSTSStyle flyweight_style(style);

    CStringW text(L"ABCDEF");
    bool has_missing_glyph = false;
    PathData gdi_path = CaptureGdiPath(style, text, &has_missing_glyph);

    TestableText auto_text(flyweight_style, text, TEXT_RENDERER_AUTO_FALLBACK);
    TestableText uniscribe_text(flyweight_style, text, TEXT_RENDERER_UNISCRIBE);
    PathData auto_path;
    PathData uniscribe_path;

    ASSERT_FALSE(has_missing_glyph);
    ASSERT_TRUE(auto_text.CapturePath(&auto_path));
    ASSERT_TRUE(uniscribe_text.CapturePath(&uniscribe_path));
    EXPECT_TRUE(auto_path == gdi_path);
    EXPECT_GT(uniscribe_path.mPathPoints, 0);
}

TEST(FontFallbackTest, SeparatesCachesByRendererMode)
{
    CCritSec lock;
    CRenderedTextSubtitle renderer(&lock);

    STSStyle style;
    style.fontName = L"Arial";
    style.fontSize = 96;
    FwSTSStyle flyweight_style(style);

    TestableText legacy_text(flyweight_style, L"ABC\x2919" L"DEF", TEXT_RENDERER_LEGACY_GDI);
    TestableText auto_text(flyweight_style, L"ABC\x2919" L"DEF", TEXT_RENDERER_AUTO_FALLBACK);
    PathDataCacheKey legacy_key(legacy_text);
    PathDataCacheKey auto_key(auto_text);

    legacy_key.UpdateHashValue();
    auto_key.UpdateHashValue();
    EXPECT_FALSE(legacy_key == auto_key);
    EXPECT_NE(legacy_key.GetHashValue(), auto_key.GetHashValue());

    TextInfoCacheKey legacy_info_key;
    legacy_info_key.m_str_id = 42;
    legacy_info_key.m_style = flyweight_style;
    legacy_info_key.m_text_renderer_mode = TEXT_RENDERER_LEGACY_GDI;
    legacy_info_key.UpdateHashValue();
    TextInfoCacheKey auto_info_key;
    auto_info_key.m_str_id = 42;
    auto_info_key.m_style = flyweight_style;
    auto_info_key.m_text_renderer_mode = TEXT_RENDERER_AUTO_FALLBACK;
    auto_info_key.UpdateHashValue();

    EXPECT_FALSE(legacy_info_key == auto_info_key);
    EXPECT_NE(legacy_info_key.GetHashValue(), auto_info_key.GetHashValue());
}
