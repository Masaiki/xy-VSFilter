#pragma once

enum VsFilterCompatibilityMode : int
{
    VSFILTER_COMPATIBILITY_XY = 0,
    VSFILTER_COMPATIBILITY_MOD,
    VSFILTER_COMPATIBILITY_COUNT
};

inline VsFilterCompatibilityMode NormalizeVsFilterCompatibilityMode(int value)
{
    if (value < VSFILTER_COMPATIBILITY_XY || value >= VSFILTER_COMPATIBILITY_COUNT) {
        return VSFILTER_COMPATIBILITY_XY;
    }

    return static_cast<VsFilterCompatibilityMode>(value);
}
