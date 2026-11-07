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

SubtitleDisplayLayout::FullscreenGateInput MakeGateInput(
    const RECT& video, const RECT& target, const RECT& display)
{
    SubtitleDisplayLayout::FullscreenGateInput input;
    input.video_output_rect = video;
    input.subtitle_target_rect = target;
    input.fullscreen_rect = display;
    input.has_fullscreen_rect = true;
    return input;
}

SubtitleDisplayLayout::CanvasInput MakeCanvasInput(
    int aspect_mode, bool enabled, bool fullscreen,
    const RECT& video, const RECT& target)
{
    SubtitleDisplayLayout::CanvasInput input;
    input.enabled = enabled;
    input.aspect_mode = aspect_mode;
    input.fullscreen = fullscreen;
    input.video_output_rect = video;
    input.subtitle_target_rect = target;
    return input;
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateAcceptsMatchingDisplay)
{
    // 16:9 display, 1920x800 video scaled to 2560x1066, target is full screen.
    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(MakeGateInput(
            RECT{ 0, 187, 2560, 1253 },
            RECT{ 0, 0, 2560, 1440 },
            RECT{ 0, 0, 2560, 1440 }));

    EXPECT_TRUE(gate.has_display);
    EXPECT_TRUE(gate.size_matches);
    EXPECT_TRUE(gate.contains_video);
    EXPECT_TRUE(gate.larger_than_video);
    EXPECT_TRUE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateUsesDisplayModeSize)
{
    SubtitleDisplayLayout::FullscreenGateInput input = MakeGateInput(
        RECT{ 0, 187, 2560, 1253 },
        RECT{ 0, 0, 2560, 1440 },
        RECT());
    input.has_fullscreen_rect = false;
    input.has_display_mode_size = true;
    input.display_mode_size.cx = 2560;
    input.display_mode_size.cy = 1440;

    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(input);
    EXPECT_TRUE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateAcceptsTaskbarSizedTarget)
{
    // 2560x1400 vs 2560x1440 is within 5%, as a taskbar would be.
    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(MakeGateInput(
            RECT{ 0, 187, 2560, 1213 },
            RECT{ 0, 0, 2560, 1400 },
            RECT{ 0, 0, 2560, 1440 }));
    EXPECT_TRUE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateAcceptsNonZeroOrigin)
{
    // Secondary monitor: the display rect has a non-zero origin.
    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(MakeGateInput(
            RECT{ 2560, 187, 5120, 1253 },
            RECT{ 2560, 0, 5120, 1440 },
            RECT{ 2560, 0, 5120, 1440 }));
    EXPECT_TRUE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateRejectsWindowSizedTarget)
{
    // A window whose target equals the video rect is not fullscreen.
    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(MakeGateInput(
            RECT{ 0, 0, 1920, 800 },
            RECT{ 0, 0, 1920, 800 },
            RECT{ 0, 0, 2560, 1440 }));
    EXPECT_FALSE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateRejectsTargetSmallerThanVideo)
{
    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(MakeGateInput(
            RECT{ 0, 0, 2560, 1440 },
            RECT{ 0, 0, 2560, 1097 },
            RECT{ 0, 0, 2560, 1440 }));
    EXPECT_FALSE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateRejectsMissingDisplay)
{
    SubtitleDisplayLayout::FullscreenGateInput input = MakeGateInput(
        RECT{ 0, 187, 2560, 1253 },
        RECT{ 0, 0, 2560, 1440 },
        RECT());
    input.has_fullscreen_rect = false;

    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(input);
    EXPECT_FALSE(gate.has_display);
    EXPECT_FALSE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, CanvasDisabledKeepsVideoRect)
{
    const RECT video = { 0, 187, 2560, 1253 };
    const RECT target = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_OFF, false, true, video, target));

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_DISABLED, canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasAutoUsesConsumerTarget)
{
    const RECT video = { 0, 187, 2560, 1253 };
    const RECT target = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_AUTO, true, true, video, target));

    EXPECT_TRUE(canvas.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_NONE, canvas.fallback_reason);
    ExpectRectEquals(target, canvas.canvas_rect);
    EXPECT_EQ(2560, canvas.layout_size.cx);
    EXPECT_EQ(1440, canvas.layout_size.cy);
}

TEST(SubtitleDisplayLayoutTest, CanvasAutoKeepsNonZeroOrigin)
{
    const RECT video = { 2560, 187, 5120, 1253 };
    const RECT target = { 2560, 0, 5120, 1440 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_AUTO, true, true, video, target));

    EXPECT_TRUE(canvas.uses_display_area);
    ExpectRectEquals(target, canvas.canvas_rect);
    EXPECT_EQ(2560, canvas.layout_size.cx);
    EXPECT_EQ(1440, canvas.layout_size.cy);
}

TEST(SubtitleDisplayLayoutTest, Canvas16x9FitsInsideTarget)
{
    // 2560x1440 target is already 16:9.
    const RECT video = { 0, 187, 2560, 1253 };
    const RECT target = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_16_9, true, true, video, target));

    EXPECT_TRUE(canvas.uses_display_area);
    ExpectRectEquals(target, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasWideAspectLetterboxesTarget)
{
    const RECT video = { 0, 187, 2560, 1253 };
    const RECT target = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_32_9, true, true, video, target));

    // 32:9 inside 2560x1440 is 2560x720, and it cannot contain the video, so
    // the resolver must fall back instead of returning a clipping canvas.
    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_CANVAS_DOES_NOT_CONTAIN_VIDEO,
        canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, Canvas4x3FitsInsideTarget)
{
    const RECT video = { 0, 187, 2560, 1253 };
    const RECT target = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_4_3, true, true, video, target));

    // 4:3 inside 2560x1440 is 1920x1440, which does not contain a 2560-wide
    // video; the resolver must refuse it rather than drop the frame.
    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_CANVAS_DOES_NOT_CONTAIN_VIDEO,
        canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasWindowModeFallsBack)
{
    const RECT video = { 0, 0, 1920, 800 };
    const RECT target = { 0, 0, 1920, 800 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_AUTO, true, false, video, target));

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_NOT_FULLSCREEN,
        canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasRejectsTargetNotContainingVideo)
{
    const RECT video = { 0, 0, 2560, 1440 };
    const RECT target = { 0, 0, 2560, 1097 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_AUTO, true, true, video, target));

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_CANVAS_DOES_NOT_CONTAIN_VIDEO,
        canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasInvalidAspectFallsBack)
{
    const RECT video = { 0, 187, 2560, 1253 };
    const RECT target = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_COUNT, true, true, video, target));

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_INVALID_ASPECT,
        canvas.fallback_reason);
}

TEST(SubtitleDisplayLayoutTest, AspectCanvasExtendsVideoToDisplayRatio)
{
    // 1920x800 on a 16:9 display: the canvas is 1920x1080, 140px per bar.
    const RECT video = { 0, 0, 1920, 800 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvasForAspect(
            true, SubtitleDisplayLayout::ASPECT_AUTO, true,
            video, 2560, 1440);

    EXPECT_TRUE(canvas.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_NONE, canvas.fallback_reason);
    ExpectRectEquals(RECT{ 0, -140, 1920, 940 }, canvas.canvas_rect);
    EXPECT_EQ(1920, canvas.layout_size.cx);
    EXPECT_EQ(1080, canvas.layout_size.cy);
}

TEST(SubtitleDisplayLayoutTest, AspectCanvasHonoursExplicitAspect)
{
    const RECT video = { 0, 0, 1920, 800 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvasForAspect(
            true, SubtitleDisplayLayout::ASPECT_4_3, true,
            video, 2560, 1440);

    // 4:3 around a 1920-wide video is 1920x1440.
    ExpectRectEquals(RECT{ 0, -320, 1920, 1120 }, canvas.canvas_rect);
    EXPECT_EQ(1920, canvas.layout_size.cx);
    EXPECT_EQ(1440, canvas.layout_size.cy);
}

TEST(SubtitleDisplayLayoutTest, AspectCanvasWideModeCannotClipVideo)
{
    const RECT video = { 0, 0, 1920, 1080 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvasForAspect(
            true, SubtitleDisplayLayout::ASPECT_21_9, true,
            video, 2560, 1440);

    // 21:9 is wider than 16:9, so the canvas grows to 2520x1080 to contain the
    // video: 300 px of side bars instead of clipping the picture.
    EXPECT_TRUE(canvas.uses_display_area);
    ExpectRectEquals(RECT{ -300, 0, 2220, 1080 }, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, AspectCanvasRequiresFullscreen)
{
    const RECT video = { 0, 0, 1920, 800 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvasForAspect(
            true, SubtitleDisplayLayout::ASPECT_AUTO, false,
            video, 2560, 1440);

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_NOT_FULLSCREEN,
        canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, AspectCanvasNeedsDisplaySize)
{
    const RECT video = { 0, 0, 1920, 800 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvasForAspect(
            true, SubtitleDisplayLayout::ASPECT_AUTO, true,
            video, 0, 0);

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_NO_DISPLAY_RECT,
        canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateAcceptsSourcePixelConsumer)
{
    // madVR reports every rect as 1920x800 even in fullscreen.
    SubtitleDisplayLayout::FullscreenGateInput input;
    input.video_output_rect = RECT{ 0, 0, 1920, 800 };
    input.subtitle_target_rect = RECT{ 0, 0, 1920, 800 };
    input.has_display_area_size = true;
    input.display_area_size.cx = 2560;
    input.display_area_size.cy = 1440;

    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(input);
    EXPECT_FALSE(gate.size_matches);
    EXPECT_TRUE(gate.contains_video);
    EXPECT_FALSE(gate.larger_than_video);
    EXPECT_TRUE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, FullscreenGateRejectsWindowedSourcePixels)
{
    // No display area means the player does not cover the monitor.
    SubtitleDisplayLayout::FullscreenGateInput input;
    input.video_output_rect = RECT{ 0, 0, 1920, 800 };
    input.subtitle_target_rect = RECT{ 0, 0, 1920, 800 };

    const SubtitleDisplayLayout::FullscreenGateResult gate =
        SubtitleDisplayLayout::EvaluateFullscreenGate(input);
    EXPECT_FALSE(gate.fullscreen);
}

TEST(SubtitleDisplayLayoutTest, FrameLayoutKeepsOutputAndShiftsBitmaps)
{
    const RECT video = { 0, 187, 2560, 1253 };
    const RECT canvas = { 0, 0, 2560, 1440 };
    const SubtitleDisplayLayout::FrameLayout layout =
        SubtitleDisplayLayout::BuildFrameLayout(video, canvas);

    // Output is the zero-based video rect; clip extends into the bars.
    ExpectRectEquals(RECT{ 0, 0, 2560, 1066 }, layout.output_rect);
    ExpectRectEquals(RECT{ 0, -187, 2560, 1253 }, layout.clip_rect);
    EXPECT_EQ(0, layout.bitmap_offset.x);
    EXPECT_EQ(-187, layout.bitmap_offset.y);
}

TEST(SubtitleDisplayLayoutTest, FrameLayoutShiftsByCanvasOrigin)
{
    const RECT video = { 2560, 187, 5120, 1253 };
    const RECT canvas = { 2560, 0, 5120, 1440 };
    const SubtitleDisplayLayout::FrameLayout layout =
        SubtitleDisplayLayout::BuildFrameLayout(video, canvas);

    ExpectRectEquals(RECT{ 0, 0, 2560, 1066 }, layout.output_rect);
    ExpectRectEquals(RECT{ 0, -187, 2560, 1253 }, layout.clip_rect);
    EXPECT_EQ(0, layout.bitmap_offset.x);
    EXPECT_EQ(-187, layout.bitmap_offset.y);
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

}

#endif // __TEST_SUBTITLE_DISPLAY_LAYOUT_H__
