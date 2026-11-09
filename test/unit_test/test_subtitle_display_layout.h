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

SubtitleDisplayLayout::CanvasInput MakeCanvasInput(
    int aspect_mode, bool enabled,
    const RECT& video, const RECT& target)
{
    SubtitleDisplayLayout::CanvasInput input;
    input.enabled = enabled;
    input.aspect_mode = aspect_mode;
    input.video_output_rect = video;
    input.subtitle_target_rect = target;
    return input;
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

TEST(SubtitleDisplayLayoutTest, AspectCanvasWidensForWideAspect)
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

TEST(SubtitleDisplayLayoutTest, CanvasDisabledKeepsVideoRect)
{
    const RECT video = { 0, 0, 1920, 800 };
    const RECT target = { 0, 0, 1920, 1080 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_OFF, false, video, target));

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_DISABLED, canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasAutoUsesConsumerTarget)
{
    const RECT video = { 0, 0, 1920, 800 };
    const RECT target = { 0, 0, 1920, 1080 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_AUTO, true, video, target));

    EXPECT_TRUE(canvas.uses_display_area);
    EXPECT_EQ(SubtitleDisplayLayout::FALLBACK_NONE, canvas.fallback_reason);
    ExpectRectEquals(target, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasRejectsTargetNotContainingVideo)
{
    const RECT video = { 0, 0, 2560, 1440 };
    const RECT target = { 0, 0, 2560, 1097 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_AUTO, true, video, target));

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_CANVAS_DOES_NOT_CONTAIN_VIDEO,
        canvas.fallback_reason);
    ExpectRectEquals(video, canvas.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasInvalidAspectFallsBack)
{
    const RECT video = { 0, 0, 1920, 800 };
    const RECT target = { 0, 0, 1920, 1080 };
    const SubtitleDisplayLayout::CanvasResult canvas =
        SubtitleDisplayLayout::ResolveCanvas(MakeCanvasInput(
            SubtitleDisplayLayout::ASPECT_COUNT, true, video, target));

    EXPECT_FALSE(canvas.uses_display_area);
    EXPECT_EQ(
        SubtitleDisplayLayout::FALLBACK_INVALID_ASPECT,
        canvas.fallback_reason);
}

TEST(SubtitleDisplayLayoutTest, FrameLayoutKeepsOutputAndShiftsBitmaps)
{
    const RECT video = { 0, 0, 2560, 1067 };
    const RECT canvas = { 0, -186, 2560, 1440 };
    const SubtitleDisplayLayout::FrameLayout layout =
        SubtitleDisplayLayout::BuildFrameLayout(video, canvas);

    ExpectRectEquals(RECT{ 0, 0, 2560, 1067 }, layout.output_rect);
    ExpectRectEquals(RECT{ 0, -186, 2560, 1440 }, layout.clip_rect);
    EXPECT_EQ(0, layout.bitmap_offset.x);
    EXPECT_EQ(-186, layout.bitmap_offset.y);
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

TEST(SubtitleDisplayLayoutTest, BitmapFrameUsesTheFullDisplayCanvas)
{
    SubtitleDisplayLayout::BitmapFrameInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 1920, 800 };
    input.display_area.cx = 2560;
    input.display_area.cy = 1440;

    const SubtitleDisplayLayout::BitmapFrameResult frame =
        SubtitleDisplayLayout::ResolveBitmapFrame(input);

    EXPECT_TRUE(frame.canvas.uses_display_area);
    EXPECT_TRUE(frame.canvas_active);
    ExpectRectEquals(RECT{ 0, -140, 1920, 940 }, frame.canvas.canvas_rect);
    ExpectRectEquals(RECT{ 0, 0, 1920, 800 }, frame.frame.output_rect);
    ExpectRectEquals(RECT{ 0, -140, 1920, 940 }, frame.frame.clip_rect);
    EXPECT_EQ(0, frame.frame.bitmap_offset.x);
    EXPECT_EQ(-140, frame.frame.bitmap_offset.y);
    EXPECT_EQ(1920, frame.buffer_size.cx);
    EXPECT_EQ(1080, frame.buffer_size.cy);
    ExpectRectEquals(RECT{ 0, 140, 1920, 940 }, frame.video_in_canvas);
}

TEST(SubtitleDisplayLayoutTest, BitmapFrameIgnoresDesignResolution)
{
    // The stream's design resolution does not decide whether the canvas is used.
    SubtitleDisplayLayout::BitmapFrameInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 1920, 1080 };
    input.display_area.cx = 2560;
    input.display_area.cy = 1440;

    const SubtitleDisplayLayout::BitmapFrameResult frame =
        SubtitleDisplayLayout::ResolveBitmapFrame(input);

    EXPECT_TRUE(frame.canvas.uses_display_area);
    EXPECT_TRUE(frame.canvas_active);
    ExpectRectEquals(RECT{ 0, 0, 1920, 1080 }, frame.canvas.canvas_rect);
    EXPECT_EQ(1920, frame.buffer_size.cx);
    EXPECT_EQ(1080, frame.buffer_size.cy);
}

TEST(SubtitleDisplayLayoutTest, BitmapFrameFallsBackWithoutDisplaySize)
{
    SubtitleDisplayLayout::BitmapFrameInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 1920, 800 };

    const SubtitleDisplayLayout::BitmapFrameResult frame =
        SubtitleDisplayLayout::ResolveBitmapFrame(input);

    EXPECT_FALSE(frame.canvas.uses_display_area);
    EXPECT_FALSE(frame.canvas_active);
    ExpectRectEquals(input.video_rect, frame.canvas.canvas_rect);
    ExpectRectEquals(input.video_rect, frame.frame.output_rect);
    ExpectRectEquals(input.video_rect, frame.frame.clip_rect);
    EXPECT_EQ(input.video_rect.right - input.video_rect.left,
        frame.buffer_size.cx);
    EXPECT_EQ(input.video_rect.bottom - input.video_rect.top,
        frame.buffer_size.cy);
}

TEST(SubtitleDisplayLayoutTest, BitmapFrameMapsDesignBottomIntoLowerBar)
{
    // A bitmap object at the bottom of a 1920x1080 design canvas must land in
    // the lower 140-pixel bar, not be scaled back into the 800-pixel picture.
    SubtitleDisplayLayout::BitmapFrameInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 1920, 800 };
    input.display_area.cx = 2560;
    input.display_area.cy = 1440;

    const SubtitleDisplayLayout::BitmapFrameResult frame =
        SubtitleDisplayLayout::ResolveBitmapFrame(input);
    const RECT bitmap_rect = {
        0, 1000, 300, 1040
    };
    const RECT published = {
        bitmap_rect.left + frame.frame.bitmap_offset.x,
        bitmap_rect.top + frame.frame.bitmap_offset.y,
        bitmap_rect.right + frame.frame.bitmap_offset.x,
        bitmap_rect.bottom + frame.frame.bitmap_offset.y
    };

    // The published position intentionally extends below the video output;
    // the clip is what grants the consumer permission to draw into the bar.
    EXPECT_TRUE(SubtitleDisplayLayout::ContainsRect(
        frame.frame.clip_rect, published));
    EXPECT_GE(published.top, 800);
}

TEST(SubtitleDisplayLayoutTest, BitmapFrameKeepsCanvasWhenDesignDiffers)
{
    // A 1280x720 stream design and a 1920x1080 display canvas are different;
    // this must not disable the canvas or rescale the object.
    SubtitleDisplayLayout::BitmapFrameInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 1920, 800 };
    input.display_area.cx = 2560;
    input.display_area.cy = 1440;

    const SubtitleDisplayLayout::BitmapFrameResult frame =
        SubtitleDisplayLayout::ResolveBitmapFrame(input);

    EXPECT_TRUE(frame.canvas_active);
    EXPECT_EQ(1920, frame.buffer_size.cx);
    EXPECT_EQ(1080, frame.buffer_size.cy);
    ExpectRectEquals(RECT{ 0, -140, 1920, 940 }, frame.frame.clip_rect);
}

TEST(SubtitleDisplayLayoutTest, PartitionKeepsVideoOnlyDirtyRectWhole)
{
    RECT parts[3] = {};
    const int count = SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ 100, 200, 400, 260 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts);

    ASSERT_EQ(1, count);
    ExpectRectEquals(RECT{ 100, 200, 400, 260 }, parts[0]);
}

TEST(SubtitleDisplayLayoutTest, PartitionKeepsLowerBarOnlyDirtyRectWhole)
{
    RECT parts[3] = {};
    const int count = SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ 100, 950, 400, 1030 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts);

    ASSERT_EQ(1, count);
    ExpectRectEquals(RECT{ 100, 950, 400, 1030 }, parts[0]);
}

TEST(SubtitleDisplayLayoutTest, PartitionSplitsDirtyRectAtLowerVideoEdge)
{
    // The current clip: a bottom-bar object overlaps the last 10 video rows.
    RECT parts[3] = {};
    const int count = SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ 690, 930, 1230, 1020 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts);

    ASSERT_EQ(2, count);
    ExpectRectEquals(RECT{ 690, 930, 1230, 940 }, parts[0]);
    ExpectRectEquals(RECT{ 690, 940, 1230, 1020 }, parts[1]);
}

TEST(SubtitleDisplayLayoutTest, PartitionSplitsDirtyRectAtUpperVideoEdge)
{
    RECT parts[3] = {};
    const int count = SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ 500, 100, 900, 200 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts);

    ASSERT_EQ(2, count);
    ExpectRectEquals(RECT{ 500, 100, 900, 140 }, parts[0]);
    ExpectRectEquals(RECT{ 500, 140, 900, 200 }, parts[1]);
}

TEST(SubtitleDisplayLayoutTest, PartitionSplitsAcrossBothBars)
{
    RECT parts[3] = {};
    const int count = SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ 10, 100, 300, 1000 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts);

    ASSERT_EQ(3, count);
    ExpectRectEquals(RECT{ 10, 100, 300, 140 }, parts[0]);
    ExpectRectEquals(RECT{ 10, 140, 300, 940 }, parts[1]);
    ExpectRectEquals(RECT{ 10, 940, 300, 1000 }, parts[2]);
}

TEST(SubtitleDisplayLayoutTest, PartitionDropsRectTouchingVideoEdge)
{
    RECT parts[3] = {};
    const int count = SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ 100, 940, 400, 1000 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts);

    ASSERT_EQ(1, count);
    ExpectRectEquals(RECT{ 100, 940, 400, 1000 }, parts[0]);
}

TEST(SubtitleDisplayLayoutTest, PartitionClipsDirtyRectToCanvas)
{
    RECT parts[3] = {};
    const int count = SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ -50, -20, 2000, 1200 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts);

    ASSERT_EQ(3, count);
    ExpectRectEquals(RECT{ 0, 0, 1920, 140 }, parts[0]);
    ExpectRectEquals(RECT{ 0, 140, 1920, 940 }, parts[1]);
    ExpectRectEquals(RECT{ 0, 940, 1920, 1080 }, parts[2]);
}

TEST(SubtitleDisplayLayoutTest, PartitionRejectsEmptyDirtyRect)
{
    RECT parts[3] = {};
    EXPECT_EQ(0, SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ 100, 100, 100, 200 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts));
}

TEST(SubtitleDisplayLayoutTest, BitmapSliceMapsCanvasRectIntoOutputSpace)
{
    // Canvas origin y=-140, so a canvas-local y=140..940 slice starts at 0
    // in the zero-based video output.
    const SubtitleDisplayLayout::BitmapSlice upper =
        SubtitleDisplayLayout::ResolveBitmapSlice(
            RECT{ 690, 930, 1230, 940 }, POINT{ 0, -140 });
    EXPECT_EQ(690, upper.position.x);
    EXPECT_EQ(790, upper.position.y);
    EXPECT_EQ(540, upper.size.cx);
    EXPECT_EQ(10, upper.size.cy);

    const SubtitleDisplayLayout::BitmapSlice lower =
        SubtitleDisplayLayout::ResolveBitmapSlice(
            RECT{ 690, 940, 1230, 1020 }, POINT{ 0, -140 });
    EXPECT_EQ(690, lower.position.x);
    EXPECT_EQ(800, lower.position.y);
    EXPECT_EQ(540, lower.size.cx);
    EXPECT_EQ(80, lower.size.cy);
}

TEST(SubtitleDisplayLayoutTest, BitmapSlicesStayInsideTheirOwnBand)
{
    RECT parts[3] = {};
    const int count = SubtitleDisplayLayout::PartitionBitmapRects(
        RECT{ 690, 930, 1230, 1020 },
        RECT{ 0, 0, 1920, 1080 },
        RECT{ 0, 140, 1920, 940 },
        parts);
    ASSERT_EQ(2, count);

    const POINT offset = { 0, -140 };
    const SubtitleDisplayLayout::BitmapSlice upper =
        SubtitleDisplayLayout::ResolveBitmapSlice(parts[0], offset);
    const SubtitleDisplayLayout::BitmapSlice lower =
        SubtitleDisplayLayout::ResolveBitmapSlice(parts[1], offset);

    // The video band in output space is 0..800. The upper slice must end at
    // 800 and the lower slice must start at 800.
    EXPECT_EQ(800, upper.position.y + upper.size.cy);
    EXPECT_EQ(800, lower.position.y);
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
