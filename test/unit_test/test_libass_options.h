#pragma once

#include <gtest/gtest.h>

#include "LibassRenderOptions.h"
#include "libass_context.h"

TEST(LibassOptionsTest, NormalizesShaper)
{
    EXPECT_EQ(LIBASS_SHAPER_SIMPLE, NormalizeLibassShaper(LIBASS_SHAPER_SIMPLE));
    EXPECT_EQ(LIBASS_SHAPER_COMPLEX, NormalizeLibassShaper(LIBASS_SHAPER_COMPLEX));
    EXPECT_EQ(LIBASS_SHAPER_COMPLEX, NormalizeLibassShaper(-1));
    EXPECT_EQ(LIBASS_SHAPER_COMPLEX, NormalizeLibassShaper(LIBASS_SHAPER_COUNT));
    EXPECT_EQ(LIBASS_SHAPER_COMPLEX, NormalizeLibassShaper(100));
}

TEST(LibassOptionsTest, NormalizesStyleOverride)
{
    EXPECT_EQ(LIBASS_STYLE_OVERRIDE_NO, NormalizeLibassStyleOverride(LIBASS_STYLE_OVERRIDE_NO));
    EXPECT_EQ(LIBASS_STYLE_OVERRIDE_YES, NormalizeLibassStyleOverride(LIBASS_STYLE_OVERRIDE_YES));
    EXPECT_EQ(LIBASS_STYLE_OVERRIDE_SCALE, NormalizeLibassStyleOverride(LIBASS_STYLE_OVERRIDE_SCALE));
    EXPECT_EQ(LIBASS_STYLE_OVERRIDE_FORCE, NormalizeLibassStyleOverride(LIBASS_STYLE_OVERRIDE_FORCE));
    EXPECT_EQ(LIBASS_STYLE_OVERRIDE_SCALE, NormalizeLibassStyleOverride(-1));
    EXPECT_EQ(LIBASS_STYLE_OVERRIDE_SCALE, NormalizeLibassStyleOverride(LIBASS_STYLE_OVERRIDE_COUNT));
}

TEST(LibassOptionsTest, DefaultsMatchMpv)
{
    LibassRenderOptions options;

    EXPECT_EQ(LIBASS_HINTING_NONE, options.hinting_mode);
    EXPECT_DOUBLE_EQ(1.0, options.font_scale);
    EXPECT_DOUBLE_EQ(0.0, options.line_spacing);
    EXPECT_DOUBLE_EQ(100.0, options.line_position);
    EXPECT_EQ(LIBASS_SHAPER_COMPLEX, options.shaper);
    EXPECT_EQ(LIBASS_STYLE_OVERRIDE_SCALE, options.style_override);
    EXPECT_FALSE(options.scale_signs);
    EXPECT_FALSE(options.justify);
    EXPECT_DOUBLE_EQ(-1.0, options.prune_delay);
    EXPECT_EQ(0, options.glyph_cache_limit);
    EXPECT_EQ(0, options.bitmap_cache_max_size);
    EXPECT_TRUE(options.use_embedded_fonts);
    EXPECT_TRUE(options.style_overrides.IsEmpty());
    EXPECT_TRUE(options.styles_file.IsEmpty());
    EXPECT_TRUE(options.fonts_dir.IsEmpty());
}

TEST(LibassOptionsTest, ClampsDoubleValues)
{
    LibassRenderOptions options;

    options.font_scale = 1000.0;
    options.line_spacing = 2000.0;
    options.line_position = 200.0;
    options.prune_delay = 20000.0;
    options.glyph_cache_limit = -5;
    options.bitmap_cache_max_size = -5;
    options = NormalizeLibassRenderOptions(options);

    EXPECT_DOUBLE_EQ(100.0, options.font_scale);
    EXPECT_DOUBLE_EQ(1000.0, options.line_spacing);
    EXPECT_DOUBLE_EQ(150.0, options.line_position);
    EXPECT_DOUBLE_EQ(10000.0, options.prune_delay);
    EXPECT_EQ(0, options.glyph_cache_limit);
    EXPECT_EQ(0, options.bitmap_cache_max_size);

    options.font_scale = -1.0;
    options.line_spacing = -2000.0;
    options.line_position = -1.0;
    options.prune_delay = -2.0;
    options = NormalizeLibassRenderOptions(options);

    EXPECT_DOUBLE_EQ(0.0, options.font_scale);
    EXPECT_DOUBLE_EQ(-1000.0, options.line_spacing);
    EXPECT_DOUBLE_EQ(0.0, options.line_position);
    EXPECT_DOUBLE_EQ(-1.0, options.prune_delay);
}

TEST(LibassOptionsTest, OverrideBitsFollowMpvSemantics)
{
    const int selective = ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE;
    const int force_fields = ASS_OVERRIDE_BIT_FONT_NAME
        | ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS
        | ASS_OVERRIDE_BIT_COLORS
        | ASS_OVERRIDE_BIT_BORDER
        | ASS_OVERRIDE_BIT_BLUR;

    EXPECT_EQ(0, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_NO, false, false));
    EXPECT_EQ(0, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_NO, true, true));
    EXPECT_EQ(0, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_YES, false, false));
    EXPECT_EQ(ASS_OVERRIDE_BIT_JUSTIFY, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_YES, true, false));
    EXPECT_EQ(selective, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_SCALE, false, false));
    EXPECT_EQ(0, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_SCALE, false, true));
    EXPECT_EQ(selective | ASS_OVERRIDE_BIT_JUSTIFY, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_SCALE, true, false));
    EXPECT_EQ(selective | force_fields, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_FORCE, false, false));
    EXPECT_EQ(selective | force_fields | ASS_OVERRIDE_BIT_JUSTIFY, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_FORCE, true, false));
    EXPECT_EQ(force_fields, LibassOverrideBits(LIBASS_STYLE_OVERRIDE_FORCE, false, true));
    // Invalid modes normalize to SCALE (the default), where justify still applies.
    EXPECT_EQ(ASS_OVERRIDE_BIT_JUSTIFY, LibassOverrideBits(static_cast<LibassStyleOverride>(100), true, true));
}

TEST(LibassOptionsTest, ParsesStyleOverrideString)
{
    EXPECT_TRUE(ParseLibassStyleOverrideString(L"").empty());
    EXPECT_TRUE(ParseLibassStyleOverrideString(L"   ").empty());
    EXPECT_TRUE(ParseLibassStyleOverrideString(L",,,").empty());

    std::vector<CStringA> result = ParseLibassStyleOverrideString(L"FontName=Arial");
    ASSERT_EQ(1u, result.size());
    EXPECT_STREQ("FontName=Arial", result[0].GetString());

    result = ParseLibassStyleOverrideString(L"FontName=Arial,Default.Bold=1");
    ASSERT_EQ(2u, result.size());
    EXPECT_STREQ("FontName=Arial", result[0].GetString());
    EXPECT_STREQ("Default.Bold=1", result[1].GetString());

    result = ParseLibassStyleOverrideString(L" FontName=Arial ,, ScaledBorderAndShadow=yes ,");
    ASSERT_EQ(2u, result.size());
    EXPECT_STREQ("FontName=Arial", result[0].GetString());
    EXPECT_STREQ("ScaledBorderAndShadow=yes", result[1].GetString());
}

TEST(LibassOptionsTest, ContextRetainsNormalizedRenderOptions)
{
    ASS_Context context{};

    LibassRenderOptions options;
    options.font_scale = 500.0;
    options.line_position = 200.0;
    options.shaper = static_cast<LibassShaper>(-3);
    options.style_override = static_cast<LibassStyleOverride>(99);
    options.prune_delay = 20000.0;
    context.ApplyRenderOptions(options, nullptr);

    EXPECT_DOUBLE_EQ(100.0, context.m_options.font_scale);
    EXPECT_DOUBLE_EQ(150.0, context.m_options.line_position);
    EXPECT_EQ(LIBASS_SHAPER_COMPLEX, context.m_options.shaper);
    EXPECT_EQ(LIBASS_STYLE_OVERRIDE_SCALE, context.m_options.style_override);
    EXPECT_DOUBLE_EQ(10000.0, context.m_options.prune_delay);

    context.SetExtraStyleOverrides({"FontName=Arial"});
    context.SetExtraStyleOverrides({});
}
