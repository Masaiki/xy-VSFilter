#pragma once

enum LibassHintingMode : int
{
    LIBASS_HINTING_NONE = 0,
    LIBASS_HINTING_LIGHT,
    LIBASS_HINTING_NORMAL,
    LIBASS_HINTING_NATIVE,
    LIBASS_HINTING_MODE_COUNT
};

inline LibassHintingMode NormalizeLibassHintingMode(int value)
{
    if (value < LIBASS_HINTING_NONE || value >= LIBASS_HINTING_MODE_COUNT) {
        return LIBASS_HINTING_NONE;
    }

    return static_cast<LibassHintingMode>(value);
}
