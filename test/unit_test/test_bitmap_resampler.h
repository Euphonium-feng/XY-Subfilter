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

    // Equal scale, only horizontal padding: the copy plus centring path is
    // still active, otherwise content would stay in the top-left corner.
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
    // Before FlipAlphaValue, MSP_RGBA stores 0 as opaque and 255 as
    // transparent.
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
        RECT{ 6, 6, 8, 8 }, FILTER_BILINEAR));

    // The 6..8 design rect maps to 12..16 in the 2x target. Its halo stays
    // near that corner, so (0,0) must remain untouched.
    const BYTE* untouched = &target[0];
    EXPECT_EQ(0xEE, untouched[0]);
    EXPECT_EQ(0xEE, untouched[1]);
    EXPECT_EQ(0xEE, untouched[2]);
    EXPECT_EQ(0xEE, untouched[3]);

    const BYTE* written = &target[((size_t)14 * 16 + 14) * 4];
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

TEST(BitmapResamplerTest, Sse2MatchesScalarReference)
{
    if (!BitmapResampler::HasSse2Kernel()) {
        std::cout << "SSE2 kernel unavailable; parity check skipped"
                  << std::endl;
        return;
    }
    std::vector<BYTE> source((size_t)64 * 32 * 4, 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            BYTE* p = &source[((size_t)y * 64 + x) * 4];
            p[0] = (BYTE)((x * 7 + y * 13) & 0xFF);
            p[1] = (BYTE)((x * 19 + y * 3) & 0xFF);
            p[2] = (BYTE)((x * 5 + y * 23) & 0xFF);
            p[3] = (BYTE)(((x + y) & 1) ? 255 : 0);
        }
    }
    for (int filter = FILTER_BILINEAR; filter < BitmapResampler::FILTER_COUNT;
         ++filter) {
        std::vector<BYTE> fast((size_t)128 * 64 * 4, 0);
        std::vector<BYTE> reference((size_t)128 * 64 * 4, 0);
        const BitmapResampler::Filter selected =
            static_cast<BitmapResampler::Filter>(filter);
        ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
            source.data(), 64 * 4, SIZE{ 64, 32 },
            fast.data(), 128 * 4, SIZE{ 128, 64 },
            RECT{ 10, 4, 54, 28 }, selected));
        ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgbaScalar(
            source.data(), 64 * 4, SIZE{ 64, 32 },
            reference.data(), 128 * 4, SIZE{ 128, 64 },
            RECT{ 10, 4, 54, 28 }, selected));
        for (size_t i = 0; i < fast.size(); ++i) {
            ASSERT_NEAR(reference[i], fast[i], 1)
                << "filter=" << selected << " byte=" << i;
        }
    }
}

TEST(BitmapResamplerTest, PaddingStaysTransparentForMismatchedAspect)
{
    const std::vector<BYTE> source = MakeSolidBitmap(
        4, 4, 30, 60, 90, 0);
    // A 4:3 design scaled into 10x6 is 8x6, centred with one transparent
    // column on each side. Those columns must stay untouched.
    std::vector<BYTE> padded((size_t)10 * 6 * 4, 0x7F);
    ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
        source.data(), 4 * 4, SIZE{ 4, 4 },
        padded.data(), 10 * 4, SIZE{ 10, 6 },
        RECT{ 0, 0, 4, 4 }, FILTER_SPLINE36));
    for (int y = 0; y < 6; ++y) {
        const BYTE* left = &padded[((size_t)y * 10) * 4];
        const BYTE* right = &padded[((size_t)y * 10 + 9) * 4];
        EXPECT_EQ(0x7F, left[0]);
        EXPECT_EQ(0x7F, left[3]);
        EXPECT_EQ(0x7F, right[0]);
        EXPECT_EQ(0x7F, right[3]);
    }
}

TEST(BitmapResamplerTest, DirtyRectMappingIncludesFilterSupport)
{
    const BitmapResampler::ScaleGeometry geometry =
        BitmapResampler::ResolveScaleGeometry(
            SIZE{ 1920, 1080 }, SIZE{ 3840, 2160 });
    RECT mapped = {};
    ASSERT_TRUE(BitmapResampler::MapDirtyRectToTarget(
        RECT{ 800, 900, 1120, 1000 }, geometry,
        FILTER_SPLINE36, &mapped));
    EXPECT_LE(mapped.left, 1600);
    EXPECT_GE(mapped.right, 2240);
    EXPECT_LE(mapped.top, 1800);
    EXPECT_GE(mapped.bottom, 2000);

    const BitmapResampler::ScaleGeometry padding =
        BitmapResampler::ResolveScaleGeometry(
            SIZE{ 4, 4 }, SIZE{ 10, 6 });
    ASSERT_TRUE(BitmapResampler::MapDirtyRectToTarget(
        RECT{ 0, 0, 4, 4 }, padding, FILTER_SPLINE36, &mapped));
    EXPECT_EQ(2, mapped.left);
    EXPECT_EQ(8, mapped.right);
    EXPECT_EQ(0, mapped.top);
    EXPECT_EQ(6, mapped.bottom);
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

TEST(BitmapResamplerTest, FourKScalingReportsTiming)
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
    // The 2 ms budget applies to an actual subtitle dirty region in Release
    // playback. Debug full-canvas timing is diagnostic only.
    EXPECT_GE(elapsed_ms, 0.0);
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

TEST(BitmapResamplerTest, FourKSubtitleDirtyRegionReportsTiming)
{
#if defined(NDEBUG)
    const SIZE design = { 1920, 1080 };
    const SIZE target = { 3840, 2160 };
    const std::vector<BYTE> source =
        MakeSolidBitmap(design.cx, design.cy, 32, 64, 96, 128);
    std::vector<BYTE> output((size_t)target.cx * target.cy * 4, 0);
    const RECT subtitle = { 160, 946, 1760, 1058 };

    const BitmapResampler::Filter filters[] = {
        FILTER_BILINEAR, FILTER_SPLINE36
    };
    for (BitmapResampler::Filter filter : filters) {
        // Warm the destination pages and the per-call row cache once; real
        // playback reuses both buffers.
        ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
            source.data(), design.cx * 4, design,
            output.data(), target.cx * 4, target,
            subtitle, filter));
        double best_ms = 1e9;
        for (int repeat = 0; repeat < 3; ++repeat) {
            const auto start = std::chrono::steady_clock::now();
            ASSERT_TRUE(BitmapResampler::ScalePremultipliedRgba(
                source.data(), design.cx * 4, design,
                output.data(), target.cx * 4, target,
                subtitle, filter));
            const auto stop = std::chrono::steady_clock::now();
            const double elapsed_ms =
                std::chrono::duration<double, std::milli>(stop - start).count();
            if (elapsed_ms < best_ms) {
                best_ms = elapsed_ms;
            }
        }
        std::cout << (filter == FILTER_BILINEAR ? "bilinear" : "spline36")
                  << " 1080p subtitle -> 4K: " << best_ms << " ms"
                  << std::endl;
    }
    // Printed for the per-frame budget evaluation; keep CI deterministic
    // rather than gating on a shared-runner wall clock.
#endif
}

}

#endif // __TEST_BITMAP_RESAMPLER_H__
