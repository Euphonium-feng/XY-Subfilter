#ifndef __TEST_COMPOSITION_HDR_H__
#define __TEST_COMPOSITION_HDR_H__

#include <gtest/gtest.h>
#include <vector>

#include "CompositionObject.h"

namespace {

BYTE RenderWhitePixel(const HdrColor::Config& config)
{
    CompositionObject object;
    object.m_horizontal_position = 0;
    object.m_vertical_position = 0;
    object.m_width = 4;
    object.m_height = 1;

    const BYTE rle[] = { 0x00, 0x84, 0x01 }; // four pixels using palette entry 1
    object.SetRLEData(rle, sizeof(rle), sizeof(rle));

    HDMV_PALETTE palette = { 1, 235, 128, 128, 255 };
    object.SetPalette(
        1, &palette, CompositionObject::YUV_Rec709,
        CompositionObject::RANGE_TV, config);

    std::vector<DWORD> pixels(4, 0);
    SubPicDesc spd;
    spd.type = MSP_RGBA;
    spd.w = 4;
    spd.h = 1;
    spd.bpp = 32;
    spd.pitch = 4 * sizeof(DWORD);
    spd.bits = reinterpret_cast<BYTE*>(pixels.data());

    CompositionObject::m_output_tv_range_rgb = false;
    object.InitColor(spd);
    object.RenderHdmv(spd);
    return static_cast<BYTE>((pixels[0] >> 16) & 0xFF);
}

TEST(CompositionObjectHdrTest, PqPreEncodesSdrPaletteWhite)
{
    EXPECT_EQ(148, RenderWhitePixel(
        HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 203.0, 1)));
    EXPECT_EQ(112, RenderWhitePixel(
        HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 50.0, 2)));
    EXPECT_EQ(130, RenderWhitePixel(
        HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 100.0, 5)));
    EXPECT_EQ(255, RenderWhitePixel(
        HdrColor::NormalizeConfig(HdrColor::MAP_OFF, 203.0, 3)));
    EXPECT_EQ(255, RenderWhitePixel(
        HdrColor::NormalizeConfig(HdrColor::MAP_RELATIVE_WHITE, 203.0, 4)));
}

TEST(CompositionObjectHdrTest, ReRendersWhenHdrConfigurationChanges)
{
    EXPECT_EQ(255, RenderWhitePixel(
        HdrColor::NormalizeConfig(HdrColor::MAP_OFF, 203.0, 10)));
    EXPECT_EQ(148, RenderWhitePixel(
        HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 203.0, 11)));
}

}

#endif // __TEST_COMPOSITION_HDR_H__
