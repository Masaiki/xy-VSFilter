#pragma once

#include <vector>
#include <atlstr.h>

#include <ass/ass.h>

#include "LibassHinting.h"

enum LibassShaper : int
{
    LIBASS_SHAPER_SIMPLE = 0,
    LIBASS_SHAPER_COMPLEX,
    LIBASS_SHAPER_COUNT
};

inline LibassShaper NormalizeLibassShaper(int value)
{
    if (value < LIBASS_SHAPER_SIMPLE || value >= LIBASS_SHAPER_COUNT) {
        return LIBASS_SHAPER_COMPLEX;
    }

    return static_cast<LibassShaper>(value);
}

enum LibassStyleOverride : int
{
    LIBASS_STYLE_OVERRIDE_NO = 0,
    LIBASS_STYLE_OVERRIDE_YES,
    LIBASS_STYLE_OVERRIDE_SCALE,
    LIBASS_STYLE_OVERRIDE_FORCE,
    LIBASS_STYLE_OVERRIDE_COUNT
};

inline LibassStyleOverride NormalizeLibassStyleOverride(int value)
{
    if (value < LIBASS_STYLE_OVERRIDE_NO || value >= LIBASS_STYLE_OVERRIDE_COUNT) {
        return LIBASS_STYLE_OVERRIDE_SCALE;
    }

    return static_cast<LibassStyleOverride>(value);
}

struct LibassRenderOptions
{
    LibassHintingMode hinting_mode = LIBASS_HINTING_NONE;
    double font_scale = 1.0;            // mpv --sub-scale, range [0, 100]
    double line_spacing = 0.0;          // mpv --sub-line-spacing, range [-1000, 1000]
    double line_position = 100.0;       // mpv --sub-pos, 0=top 100=bottom(default), range [0, 150]
    LibassShaper shaper = LIBASS_SHAPER_COMPLEX;
    LibassStyleOverride style_override = LIBASS_STYLE_OVERRIDE_SCALE;
    bool scale_signs = false;           // mpv --sub-scale-signs
    bool justify = false;               // mpv --sub-ass-justify
    double prune_delay = -1.0;          // mpv --sub-ass-prune-delay in seconds, -1=disabled, range [-1, 10000]
    int glyph_cache_limit = 0;          // mpv --sub-glyph-limit, 0=libass default
    int bitmap_cache_max_size = 0;      // mpv --sub-bitmap-max-size in MB, 0=libass default
    bool use_embedded_fonts = true;     // mpv --embeddedfonts
    CStringW style_overrides;           // mpv --sub-ass-style-overrides ("[Style.]Param=Value,...")
    CStringW styles_file;               // mpv --sub-ass-styles
    CStringW fonts_dir;                 // mpv --sub-fonts-dir
};

inline double ClampLibassDouble(double value, double min_value, double max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

inline LibassRenderOptions NormalizeLibassRenderOptions(LibassRenderOptions options)
{
    options.hinting_mode = NormalizeLibassHintingMode(static_cast<int>(options.hinting_mode));
    options.font_scale = ClampLibassDouble(options.font_scale, 0.0, 100.0);
    options.line_spacing = ClampLibassDouble(options.line_spacing, -1000.0, 1000.0);
    options.line_position = ClampLibassDouble(options.line_position, 0.0, 150.0);
    options.shaper = NormalizeLibassShaper(static_cast<int>(options.shaper));
    options.style_override = NormalizeLibassStyleOverride(static_cast<int>(options.style_override));
    options.prune_delay = ClampLibassDouble(options.prune_delay, -1.0, 10000.0);
    if (options.glyph_cache_limit < 0) options.glyph_cache_limit = 0;
    if (options.bitmap_cache_max_size < 0) options.bitmap_cache_max_size = 0;
    return options;
}

// Bit mask of ASS_OVERRIDE_BIT_* for ass_set_selective_style_override_enabled,
// following mpv's --sub-ass-override semantics.
inline int LibassOverrideBits(LibassStyleOverride mode, bool justify, bool scale_signs)
{
    mode = NormalizeLibassStyleOverride(static_cast<int>(mode));
    int bits = 0;
    if ((mode == LIBASS_STYLE_OVERRIDE_SCALE || mode == LIBASS_STYLE_OVERRIDE_FORCE) && !scale_signs) {
        bits |= ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE;
    }
    if (mode == LIBASS_STYLE_OVERRIDE_FORCE) {
        bits |= ASS_OVERRIDE_BIT_FONT_NAME
            | ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS
            | ASS_OVERRIDE_BIT_COLORS
            | ASS_OVERRIDE_BIT_BORDER;
#if LIBASS_VERSION >= 0x01703020
        bits |= ASS_OVERRIDE_BIT_BLUR;
#endif
    }
    if (mode != LIBASS_STYLE_OVERRIDE_NO && justify) {
        bits |= ASS_OVERRIDE_BIT_JUSTIFY;
    }
    return bits;
}

// Parse a mpv --sub-ass-style-overrides style list: "[Style.]Param=Value[,...]".
std::vector<CStringA> ParseLibassStyleOverrideString(const CStringW &str);
