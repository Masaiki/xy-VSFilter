#ifndef __TEST_COLOR_CONV_TABLE_H__
#define __TEST_COLOR_CONV_TABLE_H__

#include <gtest/gtest.h>
#include "color_conv_table.h"

namespace {

int ScaleRangeReference(int value, int output_size, int input_size)
{
    return (value * output_size + input_size / 2) / input_size;
}

TEST(ColorConvTableTest, RgbPcToTvRoundsToNearest)
{
    for (int value = 0; value <= 255; ++value) {
        const DWORD argb = 0xA5000000 | (value << 16) | (value << 8) | value;
        const DWORD result = ColorConvTable::RGB_PC_TO_TV(argb);
        const int expected = ScaleRangeReference(value, 219, 255) + 16;

        EXPECT_EQ(0xA5, (result >> 24) & 0xFF) << "input=" << value;
        EXPECT_EQ(expected, (result >> 16) & 0xFF) << "input=" << value;
        EXPECT_EQ(expected, (result >> 8) & 0xFF) << "input=" << value;
        EXPECT_EQ(expected, result & 0xFF) << "input=" << value;
    }

    EXPECT_EQ(0xFF477EB5, ColorConvTable::RGB_PC_TO_TV(0xFF4080C0));
}

TEST(ColorConvTableTest, AyuvPcToTvRoundsToNearest)
{
    for (int value = 0; value <= 255; ++value) {
        const DWORD result = ColorConvTable::A8Y8U8V8_PC_To_TV(0xA5, value, value, value);

        EXPECT_EQ(0xA5, (result >> 24) & 0xFF) << "input=" << value;
        EXPECT_EQ(ScaleRangeReference(value, 219, 255) + 16, (result >> 16) & 0xFF)
            << "input=" << value;
        EXPECT_EQ(ScaleRangeReference(value, 224, 255) + 16, (result >> 8) & 0xFF)
            << "input=" << value;
        EXPECT_EQ(ScaleRangeReference(value, 224, 255) + 16, result & 0xFF)
            << "input=" << value;
    }
}

TEST(ColorConvTableTest, AyuvTvToPcRoundsToNearest)
{
    for (int value = 16; value <= 235; ++value) {
        const DWORD result = ColorConvTable::A8Y8U8V8_TV_To_PC(0xA5, value, 16, 16);

        EXPECT_EQ(0xA5, (result >> 24) & 0xFF) << "input=" << value;
        EXPECT_EQ(ScaleRangeReference(value - 16, 255, 219), (result >> 16) & 0xFF)
            << "input=" << value;
    }

    for (int value = 16; value <= 240; ++value) {
        const DWORD result = ColorConvTable::A8Y8U8V8_TV_To_PC(0xA5, 16, value, value);
        const int expected = ScaleRangeReference(value - 16, 255, 224);

        EXPECT_EQ(expected, (result >> 8) & 0xFF) << "input=" << value;
        EXPECT_EQ(expected, result & 0xFF) << "input=" << value;
    }
}

TEST(ColorConvTableTest, RgbToYuvRoundsIntermediateFixedPointValues)
{
    ColorConvTable::SetDefaultConvType(ColorConvTable::BT709, ColorConvTable::RANGE_TV);

    EXPECT_EQ(0xFF14997E, ColorConvTable::Argb2Ayuv(0xFF000039));
    EXPECT_EQ(0xFF108480, ColorConvTable::Argb2Ayuv(0xFF000008));
    EXPECT_EQ(0xFF12907F, ColorConvTable::Argb2Ayuv(0xFF000025));
    EXPECT_EQ(20, ColorConvTable::Rgb2Y(0, 0, 57));

    ColorConvTable::SetDefaultConvType(ColorConvTable::BT601, ColorConvTable::RANGE_TV);
}

}

#endif // __TEST_COLOR_CONV_TABLE_H__
