#pragma once

#include <gtest/gtest.h>

#include "LibassHinting.h"
#include "libass_context.h"

TEST(LibassHintingTest, NormalizesValidModes)
{
    EXPECT_EQ(LIBASS_HINTING_NONE, NormalizeLibassHintingMode(LIBASS_HINTING_NONE));
    EXPECT_EQ(LIBASS_HINTING_LIGHT, NormalizeLibassHintingMode(LIBASS_HINTING_LIGHT));
    EXPECT_EQ(LIBASS_HINTING_NORMAL, NormalizeLibassHintingMode(LIBASS_HINTING_NORMAL));
    EXPECT_EQ(LIBASS_HINTING_NATIVE, NormalizeLibassHintingMode(LIBASS_HINTING_NATIVE));
}

TEST(LibassHintingTest, NormalizesInvalidModesToNone)
{
    EXPECT_EQ(LIBASS_HINTING_NONE, NormalizeLibassHintingMode(-1));
    EXPECT_EQ(LIBASS_HINTING_NONE, NormalizeLibassHintingMode(LIBASS_HINTING_MODE_COUNT));
    EXPECT_EQ(LIBASS_HINTING_NONE, NormalizeLibassHintingMode(100));
}

TEST(LibassHintingTest, ContextDefaultsToNoneAndRetainsConfiguredMode)
{
    ASS_Context context{};

    EXPECT_EQ(LIBASS_HINTING_NONE, context.m_options.hinting_mode);

    LibassRenderOptions options;
    options.hinting_mode = LIBASS_HINTING_LIGHT;
    context.ApplyRenderOptions(options, nullptr);
    EXPECT_EQ(LIBASS_HINTING_LIGHT, context.m_options.hinting_mode);

    options.hinting_mode = static_cast<LibassHintingMode>(100);
    context.ApplyRenderOptions(options, nullptr);
    EXPECT_EQ(LIBASS_HINTING_NONE, context.m_options.hinting_mode);
}
