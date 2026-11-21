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

TEST(SubtitleDisplayLayoutTest, BitmapFrameCanvasSizeDrivesMpcVrVirtualSize)
{
    // MPCVR sets VirtualSize from GetClipRect. The canvas is the buffer and
    // therefore the texture MPCVR receives; its video rect is what the
    // texture is scaled onto, so the two must be reported independently.
    SubtitleDisplayLayout::BitmapFrameInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 3840, 1600 };
    input.display_area.cx = 3840;
    input.display_area.cy = 2160;

    const SubtitleDisplayLayout::BitmapFrameResult frame =
        SubtitleDisplayLayout::ResolveBitmapFrame(input);

    ASSERT_TRUE(frame.canvas.uses_display_area);
    EXPECT_EQ(3840, frame.buffer_size.cx);
    EXPECT_EQ(2160, frame.buffer_size.cy);
    // The concurrent X/Y factors are what MPCVR applies to the single texture.
    const double scale_x =
        double(frame.buffer_size.cx) / input.video_rect.right;
    const double scale_y =
        double(frame.buffer_size.cy) / input.video_rect.bottom;
    EXPECT_NEAR(1.0, scale_x, 1e-9);
    EXPECT_NEAR(1.35, scale_y, 1e-9);
}

TEST(SubtitleDisplayLayoutTest, MpcVrSingleBitmapPublishesWholeCanvas)
{
    // The MPCVR branch publishes the full canvas as bitmap 0. Its position is
    // the canvas origin (0,0) and its size is the buffer, which is also what
    // MPCVR reports back as VirtualSize through GetClipRect.
    SubtitleDisplayLayout::BitmapFrameInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 1920, 800 };
    input.display_area.cx = 2560;
    input.display_area.cy = 1440;

    const SubtitleDisplayLayout::BitmapFrameResult frame =
        SubtitleDisplayLayout::ResolveBitmapFrame(input);
    ASSERT_TRUE(frame.canvas_active);

    const RECT canvas = { 0, 0, frame.buffer_size.cx, frame.buffer_size.cy };
    const SubtitleDisplayLayout::BitmapSlice slice =
        SubtitleDisplayLayout::ResolveBitmapSlice(canvas, POINT{ 0, 0 });
    EXPECT_EQ(0, slice.position.x);
    EXPECT_EQ(0, slice.position.y);
    EXPECT_EQ(frame.buffer_size.cx, slice.size.cx);
    EXPECT_EQ(frame.buffer_size.cy, slice.size.cy);

    // The madVR path keeps its video-based output and shifted clip; the two
    // contracts must not be conflated.
    ExpectRectEquals(RECT{ 0, 0, 1920, 800 }, frame.frame.output_rect);
    ExpectRectEquals(RECT{ 0, -140, 1920, 940 }, frame.frame.clip_rect);
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

TEST(SubtitleDisplayLayoutTest, BitmapPublicationPartitionsMadVrCanvas)
{
    SubtitleDisplayLayout::BitmapPublicationInput input;
    input.canvas_active = true;
    input.prefer_single_bitmap = false;
    input.buffer_size.cx = 1920;
    input.buffer_size.cy = 1080;
    input.frame_output_rect = RECT{ 0, 0, 1920, 800 };
    input.frame_clip_rect = RECT{ 0, -140, 1920, 940 };
    input.frame_bitmap_offset = POINT{ 0, -140 };
    input.video_in_canvas = RECT{ 0, 140, 1920, 940 };
    input.dirty_rect = RECT{ 690, 930, 1230, 1020 };

    SubtitleDisplayLayout::BitmapPublication publication;
    ASSERT_TRUE(SubtitleDisplayLayout::ResolveBitmapPublication(
        input, &publication));
    EXPECT_EQ(
        SubtitleDisplayLayout::BitmapPublication::Partitioned,
        publication.kind);
    ExpectRectEquals(RECT{ 0, 0, 1920, 800 }, publication.output_rect);
    ExpectRectEquals(RECT{ 0, -140, 1920, 940 }, publication.clip_rect);
    EXPECT_EQ(0, publication.bitmap_offset.x);
    EXPECT_EQ(-140, publication.bitmap_offset.y);
    EXPECT_EQ(2, publication.partition_count);
    EXPECT_EQ(2, publication.bitmap_count);
    ExpectRectEquals(
        RECT{ 690, 930, 1230, 940 }, publication.partitions[0]);
    ExpectRectEquals(
        RECT{ 690, 940, 1230, 1020 }, publication.partitions[1]);
    ExpectRectEquals(
        RECT{ 690, 930, 1230, 940 }, publication.bitmap_rects[0]);
    ExpectRectEquals(
        RECT{ 690, 940, 1230, 1020 }, publication.bitmap_rects[1]);

    const SubtitleDisplayLayout::BitmapSlice first =
        SubtitleDisplayLayout::ResolveBitmapSlice(
            publication.bitmap_rects[0], publication.bitmap_offset);
    const SubtitleDisplayLayout::BitmapSlice second =
        SubtitleDisplayLayout::ResolveBitmapSlice(
            publication.bitmap_rects[1], publication.bitmap_offset);
    EXPECT_EQ(690, first.position.x);
    EXPECT_EQ(790, first.position.y);
    EXPECT_EQ(690, second.position.x);
    EXPECT_EQ(800, second.position.y);
}

TEST(SubtitleDisplayLayoutTest, BitmapPublicationPublishesOneMpcVrCanvas)
{
    SubtitleDisplayLayout::BitmapPublicationInput input;
    input.canvas_active = true;
    input.prefer_single_bitmap = true;
    input.buffer_size.cx = 1920;
    input.buffer_size.cy = 1080;
    input.frame_output_rect = RECT{ 0, 0, 1920, 800 };
    input.frame_clip_rect = RECT{ 0, -140, 1920, 940 };
    input.frame_bitmap_offset = POINT{ 0, -140 };
    input.video_in_canvas = RECT{ 0, 140, 1920, 940 };
    input.dirty_rect = RECT{ 690, 930, 1230, 1020 };

    SubtitleDisplayLayout::BitmapPublication publication;
    ASSERT_TRUE(SubtitleDisplayLayout::ResolveBitmapPublication(
        input, &publication));
    EXPECT_EQ(
        SubtitleDisplayLayout::BitmapPublication::SingleCanvas,
        publication.kind);
    ExpectRectEquals(RECT{ 0, 0, 1920, 1080 }, publication.output_rect);
    ExpectRectEquals(RECT{ 0, 0, 1920, 1080 }, publication.clip_rect);
    EXPECT_EQ(0, publication.bitmap_offset.x);
    EXPECT_EQ(0, publication.bitmap_offset.y);
    EXPECT_EQ(0, publication.partition_count);
    EXPECT_EQ(1, publication.bitmap_count);
    ExpectRectEquals(
        RECT{ 0, 0, 1920, 1080 }, publication.bitmap_rects[0]);
}

TEST(SubtitleDisplayLayoutTest, BitmapPublicationWithoutCanvasIsVideoSized)
{
    SubtitleDisplayLayout::BitmapPublicationInput input;
    input.canvas_active = false;
    input.prefer_single_bitmap = true;
    input.buffer_size.cx = 1920;
    input.buffer_size.cy = 800;
    input.frame_output_rect = RECT{ 0, 0, 1920, 800 };
    input.frame_clip_rect = RECT{ 0, 0, 1920, 800 };
    input.dirty_rect = RECT{ 690, 700, 1230, 780 };

    SubtitleDisplayLayout::BitmapPublication publication;
    ASSERT_TRUE(SubtitleDisplayLayout::ResolveBitmapPublication(
        input, &publication));
    EXPECT_EQ(
        SubtitleDisplayLayout::BitmapPublication::SingleRect,
        publication.kind);
    ExpectRectEquals(RECT{ 0, 0, 1920, 800 }, publication.output_rect);
    ExpectRectEquals(RECT{ 0, 0, 1920, 800 }, publication.clip_rect);
    EXPECT_EQ(0, publication.partition_count);
    EXPECT_EQ(1, publication.bitmap_count);
    ExpectRectEquals(
        RECT{ 690, 700, 1230, 780 }, publication.bitmap_rects[0]);
}

TEST(SubtitleDisplayLayoutTest, BitmapPublicationRejectsEmptyDirtyRect)
{
    SubtitleDisplayLayout::BitmapPublicationInput input;
    input.canvas_active = true;
    input.prefer_single_bitmap = true;
    input.buffer_size.cx = 1920;
    input.buffer_size.cy = 1080;
    input.dirty_rect = RECT{ 690, 940, 690, 1020 };

    SubtitleDisplayLayout::BitmapPublication publication;
    EXPECT_FALSE(SubtitleDisplayLayout::ResolveBitmapPublication(
        input, &publication));
    EXPECT_EQ(0, publication.bitmap_count);
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


TEST(SubtitleDisplayLayoutTest, ClassifyConsumerRecognizesMadVr)
{
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MADVR,
        SubtitleDisplayLayout::ClassifyConsumer(L"madVR"));
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MADVR,
        SubtitleDisplayLayout::ClassifyConsumer(L"MadVR Video Renderer"));
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MADVR,
        SubtitleDisplayLayout::ClassifyConsumer(L"potplayer - madvr"));
}

TEST(SubtitleDisplayLayoutTest, ClassifyConsumerRecognizesMpcVr)
{
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MPC_VR,
        SubtitleDisplayLayout::ClassifyConsumer(L"MPC Video Renderer"));
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MPC_VR,
        SubtitleDisplayLayout::ClassifyConsumer(L"mpcvr"));
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MPC_VR,
        SubtitleDisplayLayout::ClassifyConsumer(L"MPC-BE"));
}

TEST(SubtitleDisplayLayoutTest, ClassifyConsumerRejectsUnknownNames)
{
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_UNKNOWN,
        SubtitleDisplayLayout::ClassifyConsumer(L""));
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_UNKNOWN,
        SubtitleDisplayLayout::ClassifyConsumer(NULL));
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_UNKNOWN,
        SubtitleDisplayLayout::ClassifyConsumer(L"EVR"));
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_UNKNOWN,
        SubtitleDisplayLayout::ClassifyConsumer(L"VMR-9"));
}

TEST(SubtitleDisplayLayoutTest, ClassifyConsumerPrefersMadVrPrefix)
{
    // A name mentioning both must not be misrouted to the MPC path.
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MADVR,
        SubtitleDisplayLayout::ClassifyConsumer(L"madVR (MPC-HC)"));
}

TEST(SubtitleDisplayLayoutTest, CanvasRequestPrefersPersistedDisplaySize)
{
    SubtitleDisplayLayout::CanvasRequest request;
    request.enabled = true;
    request.aspect_mode = SubtitleDisplayLayout::ASPECT_AUTO;
    request.video_rect = RECT{ 0, 0, 1920, 800 };
    request.display_area.cx = 2560;
    request.display_area.cy = 1440;
    // A consumer rect that would also be usable must not win over the
    // persisted size.
    request.fullscreen_rect = RECT{ 0, 0, 2560, 1440 };

    const SubtitleDisplayLayout::CanvasPlan plan =
        SubtitleDisplayLayout::ResolveCanvasRequest(request);

    EXPECT_TRUE(plan.active);
    EXPECT_EQ(SubtitleDisplayLayout::DISPLAY_SIZE_PERSISTED,
        plan.display_size_source);
    EXPECT_EQ(2560, plan.display_size.cx);
    ExpectRectEquals(RECT{ 0, -140, 1920, 940 }, plan.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasRequestUsesConsumerFullscreenWhenLarger)
{
    SubtitleDisplayLayout::CanvasRequest request;
    request.enabled = true;
    request.aspect_mode = SubtitleDisplayLayout::ASPECT_AUTO;
    request.video_rect = RECT{ 0, 0, 1920, 800 };
    request.fullscreen_rect = RECT{ 0, 0, 2560, 1440 };

    const SubtitleDisplayLayout::CanvasPlan plan =
        SubtitleDisplayLayout::ResolveCanvasRequest(request);

    EXPECT_TRUE(plan.active);
    EXPECT_EQ(SubtitleDisplayLayout::DISPLAY_SIZE_CONSUMER,
        plan.display_size_source);
    ExpectRectEquals(RECT{ 0, -140, 1920, 940 }, plan.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasRequestRejectsFullscreenEqualToVideo)
{
    // A consumer that reports source pixels in every rect gives fullscreen ==
    // video; that must not be taken as a display size.
    SubtitleDisplayLayout::CanvasRequest request;
    request.enabled = true;
    request.aspect_mode = SubtitleDisplayLayout::ASPECT_AUTO;
    request.video_rect = RECT{ 0, 0, 1920, 800 };
    request.fullscreen_rect = RECT{ 0, 0, 1920, 800 };
    request.subtitle_target_rect = RECT{ 0, 0, 1920, 800 };

    const SubtitleDisplayLayout::CanvasPlan plan =
        SubtitleDisplayLayout::ResolveCanvasRequest(request);

    EXPECT_FALSE(plan.active);
    EXPECT_EQ(SubtitleDisplayLayout::DISPLAY_SIZE_NONE,
        plan.display_size_source);
    ExpectRectEquals(request.video_rect, plan.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, CanvasRequestRejectsFullscreenNotContainingVideo)
{
    SubtitleDisplayLayout::CanvasRequest request;
    request.enabled = true;
    request.aspect_mode = SubtitleDisplayLayout::ASPECT_AUTO;
    request.video_rect = RECT{ 100, 100, 2020, 900 };
    request.fullscreen_rect = RECT{ 0, 0, 1920, 800 };

    const SubtitleDisplayLayout::CanvasPlan plan =
        SubtitleDisplayLayout::ResolveCanvasRequest(request);

    EXPECT_FALSE(plan.active);
    ExpectRectEquals(request.video_rect, plan.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, BitmapFrameUsesConsumerFullscreenRect)
{
    // The bitmap entry point must accept the same fullscreen rect the text
    // path does; previously only a strictly larger rect on both axes counted.
    SubtitleDisplayLayout::BitmapFrameInput input;
    input.enabled = true;
    input.video_rect = RECT{ 0, 0, 1920, 800 };
    input.fullscreen_rect = RECT{ 0, 0, 2560, 1440 };

    const SubtitleDisplayLayout::BitmapFrameResult frame =
        SubtitleDisplayLayout::ResolveBitmapFrame(input);

    EXPECT_TRUE(frame.canvas.uses_display_area);
    EXPECT_TRUE(frame.canvas_active);
    EXPECT_EQ(1920, frame.buffer_size.cx);
    EXPECT_EQ(1080, frame.buffer_size.cy);
}

TEST(SubtitleDisplayLayoutTest, CanvasRequestTextFallsBackToTargetWithoutDisplaySize)
{
    SubtitleDisplayLayout::CanvasRequest request;
    request.enabled = true;
    request.aspect_mode = SubtitleDisplayLayout::ASPECT_AUTO;
    request.use_subtitle_target_when_off = true;
    request.video_rect = RECT{ 0, 0, 1920, 800 };
    request.subtitle_target_rect = RECT{ 0, -140, 1920, 940 };

    const SubtitleDisplayLayout::CanvasPlan plan =
        SubtitleDisplayLayout::ResolveCanvasRequest(request);

    EXPECT_TRUE(plan.active);
    ExpectRectEquals(request.subtitle_target_rect, plan.canvas_rect);
}

TEST(SubtitleDisplayLayoutTest, MadVrContractMasksBitmapOnlyWithCanvas)
{
    const SubtitleDisplayLayout::ConsumerContract bitmap =
        SubtitleDisplayLayout::ResolveConsumerContract(
            SubtitleDisplayLayout::CONSUMER_MADVR, true, true);
    EXPECT_TRUE(bitmap.present_oversized_bitmap);
    EXPECT_FALSE(bitmap.prefer_single_bitmap);
    EXPECT_FALSE(bitmap.hdr_owned_by_consumer);

    // No canvas: madVR stays on the real bitmap contract with baseline
    // geometry, exactly as before this change.
    const SubtitleDisplayLayout::ConsumerContract no_canvas =
        SubtitleDisplayLayout::ResolveConsumerContract(
            SubtitleDisplayLayout::CONSUMER_MADVR, false, true);
    EXPECT_FALSE(no_canvas.present_oversized_bitmap);
    EXPECT_FALSE(no_canvas.prefer_single_bitmap);
    EXPECT_FALSE(no_canvas.hdr_owned_by_consumer);

    // Text subtitles never take the bitmap overlay route.
    const SubtitleDisplayLayout::ConsumerContract text =
        SubtitleDisplayLayout::ResolveConsumerContract(
            SubtitleDisplayLayout::CONSUMER_MADVR, true, false);
    EXPECT_FALSE(text.present_oversized_bitmap);
    EXPECT_FALSE(text.prefer_single_bitmap);
    EXPECT_FALSE(text.hdr_owned_by_consumer);
}

TEST(SubtitleDisplayLayoutTest, MpcVrContractOwnsHdrAndPrefersOneBitmap)
{
    const SubtitleDisplayLayout::ConsumerContract bitmap =
        SubtitleDisplayLayout::ResolveConsumerContract(
            SubtitleDisplayLayout::CONSUMER_MPC_VR, true, true);
    // MPCVR reads only bitmap 0 and converts SDR subtitle textures itself.
    EXPECT_FALSE(bitmap.present_oversized_bitmap);
    EXPECT_TRUE(bitmap.prefer_single_bitmap);
    EXPECT_TRUE(bitmap.hdr_owned_by_consumer);

    // The renderer applies its own transfer function for text too, so the
    // provider must leave both subtitle kinds in SDR.
    const SubtitleDisplayLayout::ConsumerContract text =
        SubtitleDisplayLayout::ResolveConsumerContract(
            SubtitleDisplayLayout::CONSUMER_MPC_VR, true, false);
    EXPECT_FALSE(text.present_oversized_bitmap);
    EXPECT_FALSE(text.prefer_single_bitmap);
    EXPECT_TRUE(text.hdr_owned_by_consumer);

    // Even with the layout off the HDR hand-over is unchanged.
    const SubtitleDisplayLayout::ConsumerContract no_canvas =
        SubtitleDisplayLayout::ResolveConsumerContract(
            SubtitleDisplayLayout::CONSUMER_MPC_VR, false, true);
    EXPECT_TRUE(no_canvas.prefer_single_bitmap);
    EXPECT_TRUE(no_canvas.hdr_owned_by_consumer);
}

TEST(SubtitleDisplayLayoutTest, UnknownConsumerKeepsConservativeContract)
{
    const SubtitleDisplayLayout::ConsumerContract bitmap =
        SubtitleDisplayLayout::ResolveConsumerContract(
            SubtitleDisplayLayout::CONSUMER_UNKNOWN, true, true);
    EXPECT_FALSE(bitmap.present_oversized_bitmap);
    EXPECT_FALSE(bitmap.prefer_single_bitmap);
    EXPECT_FALSE(bitmap.hdr_owned_by_consumer);

    const SubtitleDisplayLayout::ConsumerContract text =
        SubtitleDisplayLayout::ResolveConsumerContract(
            SubtitleDisplayLayout::CONSUMER_UNKNOWN, false, false);
    EXPECT_FALSE(text.present_oversized_bitmap);
    EXPECT_FALSE(text.prefer_single_bitmap);
    EXPECT_FALSE(text.hdr_owned_by_consumer);
}

TEST(SubtitleDisplayLayoutTest, MpcVrPassesSdrThroughRegardlessOfMode)
{
    // MPCVR applies its own SDR-to-PQ conversion, so the provider must keep
    // the SDR code values even when the user selects PQ or Auto.
    EXPECT_TRUE(SubtitleDisplayLayout::ShouldPassThroughSdrForConsumer(
        SubtitleDisplayLayout::CONSUMER_MPC_VR));
    EXPECT_FALSE(SubtitleDisplayLayout::ShouldPassThroughSdrForConsumer(
        SubtitleDisplayLayout::CONSUMER_MADVR));
    EXPECT_FALSE(SubtitleDisplayLayout::ShouldPassThroughSdrForConsumer(
        SubtitleDisplayLayout::CONSUMER_UNKNOWN));
}

namespace
{
struct FakeNameSource
{
    const wchar_t* name;
    int calls;
};

bool ReadFakeName(void* context, wchar_t* buffer, int capacity)
{
    FakeNameSource* source = static_cast<FakeNameSource*>(context);
    if (!source || !source->name || capacity <= 1) {
        return false;
    }
    ++source->calls;
    int i = 0;
    for (; source->name[i] && i < capacity - 1; ++i) {
        buffer[i] = source->name[i];
    }
    buffer[i] = 0;
    return i > 0;
}
}

TEST(SubtitleDisplayLayoutTest, ConsumerIdentitySamplesNameAndNormalizes)
{
    FakeNameSource source = { L"MPC Video Renderer", 0 };
    SubtitleDisplayLayout::ConsumerIdentity identity;
    identity.Refresh(ReadFakeName, &source);

    EXPECT_TRUE(identity.HasName());
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MPC_VR, identity.Family());
    // Diagnostics expect a single-token name.
    EXPECT_STREQ(L"MPC_Video_Renderer", identity.Name());
    EXPECT_TRUE(identity.OwnsHdrConversion());

    const int calls = source.calls;
    identity.Refresh(ReadFakeName, &source);
    EXPECT_EQ(calls, source.calls) << "a known name must not be re-read";
}

TEST(SubtitleDisplayLayoutTest, ConsumerIdentityRetryIsBounded)
{
    FakeNameSource source = { NULL, 0 };
    SubtitleDisplayLayout::ConsumerIdentity identity;
    for (int i = 0; i < 32; ++i) {
        identity.Refresh(ReadFakeName, &source);
    }
    EXPECT_FALSE(identity.HasName());
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_UNKNOWN, identity.Family());
    // Retries stop after the module's bounded budget rather than every frame.
    EXPECT_LE(source.calls, 8);

    // A name that appears later is picked up once by Sample.
    source.name = L"madVR";
    EXPECT_TRUE(identity.Sample(ReadFakeName, &source));
    EXPECT_EQ(SubtitleDisplayLayout::CONSUMER_MADVR, identity.Family());
}

TEST(SubtitleDisplayLayoutTest, ConsumerIdentityContractMatchesFamilyRules)
{
    FakeNameSource source = { L"MPC Video Renderer", 0 };
    SubtitleDisplayLayout::ConsumerIdentity identity;
    identity.Sample(ReadFakeName, &source);

    const SubtitleDisplayLayout::ConsumerContract bitmap =
        identity.Contract(true, true);
    EXPECT_FALSE(bitmap.present_oversized_bitmap);
    EXPECT_TRUE(bitmap.prefer_single_bitmap);
    EXPECT_TRUE(bitmap.hdr_owned_by_consumer);

    const SubtitleDisplayLayout::ConsumerContract text =
        identity.Contract(true, false);
    EXPECT_FALSE(text.prefer_single_bitmap);
    EXPECT_TRUE(text.hdr_owned_by_consumer);
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
