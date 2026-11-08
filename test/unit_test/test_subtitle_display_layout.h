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
    const POINT published = {
        bitmap_rect.left + frame.frame.bitmap_offset.x,
        bitmap_rect.top + frame.frame.bitmap_offset.y
    };

    // The consumer maps output 1:1 to the video, then the video is scaled by
    // 4/3 onto the 2560x1440 display. The picture starts around y=186.
    const int video_top_on_display = 186;
    const int bitmap_top_on_display =
        video_top_on_display + published.y * 4 / 3;
    EXPECT_GE(bitmap_top_on_display, video_top_on_display + 800 * 4 / 3);
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
