#ifndef __TEST_BITMAP_RESAMPLER_H__
#define __TEST_BITMAP_RESAMPLER_H__

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <vector>

#include "bitmap_resampler.h"

namespace {

using BitmapResampler::FILTER_BILINEAR;
using BitmapResampler::FILTER_SPLINE36;

std::vector<BYTE> MakeSolidBitmap(
    int width, int height, BYTE b, BYTE g, BYTE r, BYTE a)
{
    std::vector<BYTE> pixels((size_t)width * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            BYTE* p = &pixels[((size_t)y * width + x) * 4];
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = a;
        }
    }
    return pixels;
}

TEST(BitmapResamplerTest, ScaleGeometryUpscalesWithSingleFactor)
{
    const BitmapResampler::ScaleGeometry geometry =
        BitmapResampler::ResolveScaleGeometry(
            SIZE{ 1920, 1080 }, SIZE{ 3840, 2160 });

    EXPECT_TRUE(geometry.active);
    EXPECT_DOUBLE_EQ(2.0, geometry.scale);
    EXPECT_EQ(3840, geometry.scaled_size.cx);
    EXPECT_EQ(2160, geometry.scaled_size.cy);
    EXPECT_EQ(0, geometry.offset.x);
    EXPECT_EQ(0, geometry.offset.y);
}

TEST(BitmapResamplerTest, ScaleGeometryPadsMismatchedAspect)
{
    const BitmapResampler::ScaleGeometry geometry =
        BitmapResampler::ResolveScaleGeometry(
            SIZE{ 1440, 1080 }, SIZE{ 1920, 1080 });

    EXPECT_TRUE(geometry.active);
    EXPECT_DOUBLE_EQ(1.0, geometry.scale);
    EXPECT_EQ(1440, geometry.scaled_size.cx);
    EXPECT_EQ(1080, geometry.scaled_size.cy);
    EXPECT_EQ(240, geometry.offset.x);
    EXPECT_EQ(0, geometry.offset.y);
}

TEST(BitmapResamplerTest, SolidColourSurvivesBilinearAndCubic)
{
    const std::vector<BYTE> source =
        MakeSolidBitmap(8, 8, 32, 64, 96, 128);
    for (int filter = FILTER_BILINEAR; filter < BitmapResampler::FILTER_COUNT;
         ++filter) {
        std::vector<BYTE> target((size_t)16 * 16 * 4, 0);
        ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
            source.data(), 8 * 4, SIZE{ 8, 8 },
            target.data(), 16 * 4, SIZE{ 16, 16 },
            RECT{ 0, 0, 8, 8 },
            static_cast<BitmapResampler::Filter>(filter)));

        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const BYTE* p = &target[((size_t)y * 16 + x) * 4];
                EXPECT_NEAR(32, p[0], 2);
                EXPECT_NEAR(64, p[1], 2);
                EXPECT_NEAR(96, p[2], 2);
                EXPECT_NEAR(128, p[3], 2);
            }
        }
    }
}

TEST(BitmapResamplerTest, TransparentAreaHasNoColourFringe)
{
    // MSP_RGBA stores transparency as alpha 255 before FlipAlphaValue.
    std::vector<BYTE> source((size_t)8 * 8 * 4, 0);
    for (size_t i = 0; i < source.size(); i += 4) {
        source[i + 3] = 255;
    }
    // One fully opaque white pixel surrounded by transparent pixels.
    BYTE* center = &source[((size_t)4 * 8 + 4) * 4];
    center[0] = center[1] = center[2] = 255;
    center[3] = 0;
    std::vector<BYTE> target((size_t)16 * 16 * 4, 0);

    ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
        source.data(), 8 * 4, SIZE{ 8, 8 },
        target.data(), 16 * 4, SIZE{ 16, 16 },
        RECT{ 0, 0, 8, 8 }, FILTER_SPLINE36));

    for (size_t i = 0; i < target.size(); i += 4) {
        if (target[i + 3] == 255) {
            EXPECT_EQ(0, target[i]);
            EXPECT_EQ(0, target[i + 1]);
            EXPECT_EQ(0, target[i + 2]);
        }
        EXPECT_LE(target[i], 255 - target[i + 3]);
        EXPECT_LE(target[i + 1], 255 - target[i + 3]);
        EXPECT_LE(target[i + 2], 255 - target[i + 3]);
    }
}

TEST(BitmapResamplerTest, DirtyRectOnlyWritesMappedRegion)
{
    const std::vector<BYTE> source =
        MakeSolidBitmap(8, 8, 10, 20, 30, 40);
    std::vector<BYTE> target((size_t)16 * 16 * 4, 0xEE);

    ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
        source.data(), 8 * 4, SIZE{ 8, 8 },
        target.data(), 16 * 4, SIZE{ 16, 16 },
        RECT{ 2, 2, 4, 4 }, FILTER_BILINEAR));

    const BYTE* untouched = &target[0];
    EXPECT_EQ(0xEE, untouched[0]);
    EXPECT_EQ(0xEE, untouched[1]);
    EXPECT_EQ(0xEE, untouched[2]);
    EXPECT_EQ(0xEE, untouched[3]);

    const BYTE* written = &target[((size_t)8 * 16 + 8) * 4];
    EXPECT_NE(0xEE, written[3]);
}

TEST(BitmapResamplerTest, IdenticalSizeCopiesDirtyRect)
{
    std::vector<BYTE> source =
        MakeSolidBitmap(4, 4, 1, 2, 3, 4);
    std::vector<BYTE> target((size_t)4 * 4 * 4, 0);
    ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
        source.data(), 4 * 4, SIZE{ 4, 4 },
        target.data(), 4 * 4, SIZE{ 4, 4 },
        RECT{ 0, 0, 4, 4 }, FILTER_SPLINE36));
    EXPECT_EQ(source, target);
}

TEST(BitmapResamplerTest, MapRectClipsAndRejectsEmpty)
{
    const BitmapResampler::ScaleGeometry geometry =
        BitmapResampler::ResolveScaleGeometry(
            SIZE{ 100, 100 }, SIZE{ 200, 200 });
    RECT mapped = {};
    ASSERT_TRUE(BitmapResampler::MapRectToTarget(
        RECT{ 10, 20, 30, 40 }, geometry, &mapped));
    EXPECT_EQ(20, mapped.left);
    EXPECT_EQ(40, mapped.top);
    EXPECT_EQ(60, mapped.right);
    EXPECT_EQ(80, mapped.bottom);
    EXPECT_FALSE(BitmapResampler::MapRectToTarget(
        RECT{ 100, 100, 100, 120 }, geometry, &mapped));
}

TEST(BitmapResamplerTest, FourKScalingStaysUnderTwoMilliseconds)
{
    const SIZE design = { 1920, 1080 };
    const SIZE target = { 3840, 2160 };
    const std::vector<BYTE> source =
        MakeSolidBitmap(design.cx, design.cy, 32, 64, 96, 128);
    std::vector<BYTE> output((size_t)target.cx * target.cy * 4, 0);

    const auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
        source.data(), design.cx * 4, design,
        output.data(), target.cx * 4, target,
        RECT{ 0, 0, design.cx, design.cy }, FILTER_SPLINE36));
    const auto stop = std::chrono::steady_clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(stop - start).count();
    std::cout << "spline36 1080p->2160p: " << elapsed_ms << " ms" << std::endl;
    EXPECT_LT(elapsed_ms, 2.0);
}

TEST(BitmapResamplerTest, FourKScaledOutputKeepsSourceGeometry)
{
    // A marker at the top-left and bottom-right of a 1080p design canvas must
    // land at the matching corners of the 4K target, not stay at source size.
    const SIZE design = { 1920, 1080 };
    const SIZE target = { 3840, 2160 };
    std::vector<BYTE> source((size_t)design.cx * design.cy * 4, 0);
    for (size_t i = 0; i < source.size(); i += 4) {
        source[i + 3] = 255;
    }
    const RECT bottom = { 0, 1030, 1920, 1080 };
    for (int y = bottom.top; y < bottom.bottom; ++y) {
        for (int x = 0; x < design.cx; ++x) {
            BYTE* p = &source[((size_t)y * design.cx + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = 0;
        }
    }
    std::vector<BYTE> output((size_t)target.cx * target.cy * 4, 0xFF);
    ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
        source.data(), design.cx * 4, design,
        output.data(), target.cx * 4, target,
        RECT{ 0, 0, design.cx, design.cy }, FILTER_BILINEAR));

    const BYTE* top = &output[0];
    EXPECT_EQ(255, top[3]);
    const BYTE* bottom_out =
        &output[((size_t)(target.cy - 1) * target.cx + target.cx / 2) * 4];
    EXPECT_EQ(0, bottom_out[3]);
}

}

#endif // __TEST_BITMAP_RESAMPLER_H__
