#ifndef __TEST_SUBTITLE_DISPLAY_LAYOUT_H__
#define __TEST_SUBTITLE_DISPLAY_LAYOUT_H__

#include <gtest/gtest.h>

#include "subtitle_display_layout.h"

namespace {

void ExpectRectEquals(const RECT& expected, const RECT& actual)
{
    EXPECT_EQ(expected.left, actual.left);
    EXPECT_EQ(expected.top, actual.top);
    EXPECT_EQ(expected.right, actual.right);
    EXPECT_EQ(expected.bottom, actual.bottom);
}

SubtitleDisplayLayout::ResolveInput MakeLayoutInput(
    int aspect_mode, const RECT& video, const RECT& display)
{
    SubtitleDisplayLayout::ResolveInput input = {};
    input.aspect_mode = aspect_mode;
    input.fullscreen = true;
    input.video_rect = video;
    input.fullscreen_rect = display;
    input.has_fullscreen_rect = true;
    return input;
}

TEST(SubtitleDisplayLayoutTest, DisabledKeepsVideoRect)
{
    const RECT video = { 0, 0, 2560, 1097 };
    const RECT display = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(MakeLayoutInput(
            SubtitleDisplayLayout::ASPECT_OFF, video, display));

    EXPECT_FALSE(result.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_DISABLED, result.fallback_reason);
    ExpectRectEquals(video, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, AutoUsesActualDisplayRect)
{
    const RECT video = { 0, 0, 2560, 1097 };
    const RECT display = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(MakeLayoutInput(
            SubtitleDisplayLayout::ASPECT_AUTO, video, display));

    EXPECT_TRUE(result.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_NONE, result.fallback_reason);
    ExpectRectEquals(display, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, DisplayModeSizeBuildsFallbackRect)
{
    const RECT video = { 0, 0, 1920, 800 };
    SubtitleDisplayLayout::ResolveInput input =
        MakeLayoutInput(SubtitleDisplayLayout::ASPECT_AUTO, video, RECT());
    input.has_fullscreen_rect = false;
    input.has_display_mode_size = true;
    input.display_mode_size.cx = 1920;
    input.display_mode_size.cy = 1080;

    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(input);
    const RECT expected = { 0, 0, 1920, 1080 };
    EXPECT_TRUE(result.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_NONE, result.fallback_reason);
    ExpectRectEquals(expected, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, FullscreenRectWinsOverDisplayModeSize)
{
    const RECT video = { 0, 0, 1920, 800 };
    SubtitleDisplayLayout::ResolveInput input =
        MakeLayoutInput(SubtitleDisplayLayout::ASPECT_AUTO, video,
            RECT{ 0, 0, 1920, 1080 });
    input.has_display_mode_size = true;
    input.display_mode_size.cx = 2560;
    input.display_mode_size.cy = 1440;

    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(input);
    const RECT expected = { 0, 0, 1920, 1080 };
    ExpectRectEquals(expected, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, MissingDisplayFallsBackToVideo)
{
    const RECT video = { 0, 0, 1920, 800 };
    SubtitleDisplayLayout::ResolveInput input =
        MakeLayoutInput(SubtitleDisplayLayout::ASPECT_AUTO, video, RECT());
    input.has_fullscreen_rect = false;

    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(input);
    EXPECT_FALSE(result.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_NO_DISPLAY_RECT,
        result.fallback_reason);
    ExpectRectEquals(video, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, WindowModeFallsBackToVideo)
{
    const RECT video = { 0, 0, 1920, 800 };
    const RECT display = { 0, 0, 1920, 1080 };
    SubtitleDisplayLayout::ResolveInput input =
        MakeLayoutInput(SubtitleDisplayLayout::ASPECT_AUTO, video, display);
    input.fullscreen = false;

    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(input);
    EXPECT_FALSE(result.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_NOT_FULLSCREEN,
        result.fallback_reason);
    ExpectRectEquals(video, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, ExplicitAspectFitsInsideDisplay)
{
    const RECT video = { 0, 0, 2560, 1097 };
    const RECT display = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(MakeLayoutInput(
            SubtitleDisplayLayout::ASPECT_16_9, video, display));

    EXPECT_TRUE(result.uses_display_area);
    ExpectRectEquals(display, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, ExplicitWideAspectLetterboxesInsideScreen)
{
    const RECT video = { 0, 0, 1920, 800 };
    const RECT display = { 0, 0, 1920, 1080 };
    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(MakeLayoutInput(
            SubtitleDisplayLayout::ASPECT_32_9, video, display));
    const RECT expected = { 0, 270, 1920, 810 };

    EXPECT_TRUE(result.uses_display_area);
    ExpectRectEquals(expected, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, InvalidRectFallsBackSafely)
{
    const RECT video = { 0, 0, 1920, 800 };
    const RECT invalid_display = { -10, 0, 1910, 1080 };
    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveTargetRect(MakeLayoutInput(
            SubtitleDisplayLayout::ASPECT_AUTO, video, invalid_display));

    EXPECT_FALSE(result.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_NO_DISPLAY_RECT,
        result.fallback_reason);
    ExpectRectEquals(video, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, AspectMetadataIsComplete)
{
    EXPECT_EQ(16, SubtitleDisplayLayout::AspectWidth(
        SubtitleDisplayLayout::ASPECT_16_9));
    EXPECT_EQ(9, SubtitleDisplayLayout::AspectHeight(
        SubtitleDisplayLayout::ASPECT_16_9));
    EXPECT_EQ(16, SubtitleDisplayLayout::AspectWidth(
        SubtitleDisplayLayout::ASPECT_16_10));
    EXPECT_EQ(10, SubtitleDisplayLayout::AspectHeight(
        SubtitleDisplayLayout::ASPECT_16_10));
    EXPECT_EQ(21, SubtitleDisplayLayout::AspectWidth(
        SubtitleDisplayLayout::ASPECT_21_9));
    EXPECT_EQ(9, SubtitleDisplayLayout::AspectHeight(
        SubtitleDisplayLayout::ASPECT_21_9));
    EXPECT_EQ(32, SubtitleDisplayLayout::AspectWidth(
        SubtitleDisplayLayout::ASPECT_32_9));
    EXPECT_EQ(9, SubtitleDisplayLayout::AspectHeight(
        SubtitleDisplayLayout::ASPECT_32_9));
    EXPECT_EQ(4, SubtitleDisplayLayout::AspectWidth(
        SubtitleDisplayLayout::ASPECT_4_3));
    EXPECT_EQ(3, SubtitleDisplayLayout::AspectHeight(
        SubtitleDisplayLayout::ASPECT_4_3));
}

TEST(SubtitleDisplayLayoutTest, BitmapPlacementKeepsSourceCoordinates)
{
    const RECT video = { 0, 0, 2560, 1097 };
    const RECT display = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::BitmapPlacement placement =
        SubtitleDisplayLayout::ResolveBitmapPlacement(video, display, true);

    ExpectRectEquals(video, placement.output_rect);
    ExpectRectEquals(display, placement.clip_rect);
    EXPECT_TRUE(placement.uses_display_area);
    EXPECT_DOUBLE_EQ(1.0, placement.scale_x);
    EXPECT_DOUBLE_EQ(1440.0 / 1097.0, placement.scale_y);
}

TEST(SubtitleDisplayLayoutTest, BitmapPlacementFallsBackWithoutDisplayArea)
{
    const RECT video = { 0, 0, 2560, 1097 };
    const RECT display = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::BitmapPlacement placement =
        SubtitleDisplayLayout::ResolveBitmapPlacement(video, display, false);

    ExpectRectEquals(video, placement.output_rect);
    ExpectRectEquals(video, placement.clip_rect);
    EXPECT_FALSE(placement.uses_display_area);
    EXPECT_DOUBLE_EQ(1.0, placement.scale_x);
    EXPECT_DOUBLE_EQ(1.0, placement.scale_y);
}

TEST(SubtitleDisplayLayoutTest, ConsumerInputDisabledIgnoresDisplayFields)
{
    SubtitleDisplayLayout::ConsumerInput input;
    input.video_rect = RECT{ 0, 0, 2560, 1097 };
    input.aspect_mode = SubtitleDisplayLayout::ASPECT_AUTO;
    input.fullscreen = true;
    input.fullscreen_rect = RECT{ 0, 0, 2560, 1440 };
    input.has_fullscreen_rect = true;

    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveFromConsumerInput(input);
    EXPECT_FALSE(result.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_DISABLED, result.fallback_reason);
    ExpectRectEquals(input.video_rect, result.target_rect);
}

TEST(SubtitleDisplayLayoutTest, ConsumerInputUsesDisplayWhenAllFieldsValid)
{
    SubtitleDisplayLayout::ConsumerInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 2560, 1097 };
    input.aspect_mode = SubtitleDisplayLayout::ASPECT_AUTO;
    input.fullscreen = true;
    input.fullscreen_rect = RECT{ 0, 0, 2560, 1440 };
    input.has_fullscreen_rect = true;

    const SubtitleDisplayLayout::ResolveResult result =
        SubtitleDisplayLayout::ResolveFromConsumerInput(input);
    EXPECT_TRUE(result.uses_display_area);
    ExpectRectEquals(input.fullscreen_rect, result.target_rect);
}

}

#endif // __TEST_SUBTITLE_DISPLAY_LAYOUT_H__
