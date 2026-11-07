#include "subtitle_display_layout.h"

namespace
{
    bool IsPositiveSize(const RECT& rect)
    {
        return rect.right > rect.left && rect.bottom > rect.top;
    }

    LONG RectWidth(const RECT& rect)
    {
        return rect.right - rect.left;
    }

    LONG RectHeight(const RECT& rect)
    {
        return rect.bottom - rect.top;
    }

    int Gcd(int first, int second)
    {
        while (second != 0) {
            const int remainder = first % second;
            first = second;
            second = remainder;
        }
        return first > 0 ? first : 1;
    }

    // Relative tolerance: consumer display fields routinely omit a taskbar or
    // a few pixels of window decoration. 5% is wide enough for a taskbar and
    // narrow enough to reject a window that merely happens to be larger.
    bool SizesMatchWithinTolerance(
        LONG candidate_width, LONG candidate_height,
        LONG reference_width, LONG reference_height)
    {
        if (candidate_width <= 0 || candidate_height <= 0
            || reference_width <= 0 || reference_height <= 0) {
            return false;
        }

        const LONG tolerance_x = (reference_width + 19) / 20;
        const LONG tolerance_y = (reference_height + 19) / 20;
        return candidate_width >= reference_width - tolerance_x
            && candidate_width <= reference_width + tolerance_x
            && candidate_height >= reference_height - tolerance_y
            && candidate_height <= reference_height + tolerance_y;
    }

    RECT MakeDisplayModeRect(const SIZE& size)
    {
        const RECT rect = { 0, 0, size.cx, size.cy };
        return rect;
    }
}

namespace SubtitleDisplayLayout
{
    FullscreenGateInput::FullscreenGateInput()
        : video_output_rect()
        , subtitle_target_rect()
        , fullscreen_rect()
        , has_fullscreen_rect(false)
        , display_mode_size()
        , has_display_mode_size(false)
        , display_area_size()
        , has_display_area_size(false)
    {
    }

    FullscreenGateResult::FullscreenGateResult()
        : has_display(false)
        , size_matches(false)
        , contains_video(false)
        , larger_than_video(false)
        , fullscreen(false)
    {
    }

    CanvasInput::CanvasInput()
        : enabled(false)
        , aspect_mode(ASPECT_OFF)
        , fullscreen(false)
        , video_output_rect()
        , subtitle_target_rect()
    {
    }

    CanvasResult::CanvasResult()
        : resolved_aspect_mode(ASPECT_OFF)
        , fallback_reason(FALLBACK_DISABLED)
        , uses_display_area(false)
    {
        canvas_rect = RECT();
        layout_size.cx = 0;
        layout_size.cy = 0;
    }

    FrameLayout::FrameLayout()
    {
        output_rect = RECT();
        clip_rect = RECT();
        bitmap_offset.x = 0;
        bitmap_offset.y = 0;
    }

    bool IsValidRect(const RECT& rect)
    {
        return IsPositiveSize(rect);
    }

    bool ContainsRect(const RECT& outer, const RECT& inner)
    {
        return IsPositiveSize(outer)
            && outer.left <= inner.left
            && outer.top <= inner.top
            && outer.right >= inner.right
            && outer.bottom >= inner.bottom;
    }

    RECT FitAspectRect(const RECT& bounds, int width, int height)
    {
        if (!IsPositiveSize(bounds) || width <= 0 || height <= 0) {
            return RECT();
        }

        const __int64 bounds_width = RectWidth(bounds);
        const __int64 bounds_height = RectHeight(bounds);
        __int64 fitted_width = bounds_width;
        __int64 fitted_height = fitted_width * height / width;
        if (fitted_height > bounds_height) {
            fitted_height = bounds_height;
            fitted_width = fitted_height * width / height;
        }

        if (fitted_width <= 0 || fitted_height <= 0) {
            return RECT();
        }

        const LONG left = bounds.left
            + LONG((bounds_width - fitted_width) / 2);
        const LONG top = bounds.top
            + LONG((bounds_height - fitted_height) / 2);
        const RECT result = {
            left, top, left + LONG(fitted_width), top + LONG(fitted_height)
        };
        return result;
    }

    FullscreenGateResult EvaluateFullscreenGate(
        const FullscreenGateInput& input)
    {
        FullscreenGateResult result;
        if (!IsPositiveSize(input.video_output_rect)
            || !IsPositiveSize(input.subtitle_target_rect)) {
            return result;
        }

        RECT display = {};
        if (input.has_fullscreen_rect
            && IsPositiveSize(input.fullscreen_rect)) {
            display = input.fullscreen_rect;
            result.has_display = true;
        }
        else if (input.has_display_mode_size
            && input.display_mode_size.cx > 0
            && input.display_mode_size.cy > 0) {
            display = MakeDisplayModeRect(input.display_mode_size);
            result.has_display = true;
        }
        if (!result.has_display
            && !(input.has_display_area_size
                && input.display_area_size.cx > 0
                && input.display_area_size.cy > 0)) {
            return result;
        }

        // Only the display size is used for matching. The display rect's
        // origin may be a desktop coordinate and must never leak into the
        // comparison or into layout.
        if (result.has_display) {
            result.size_matches = SizesMatchWithinTolerance(
                RectWidth(input.subtitle_target_rect),
                RectHeight(input.subtitle_target_rect),
                RectWidth(display), RectHeight(display));
        }

        // Both rects are consumer display-space rects, so this comparison is
        // meaningful. It rejects a window that is not actually fullscreen.
        result.contains_video = ContainsRect(
            input.subtitle_target_rect, input.video_output_rect);
        result.larger_than_video =
            RectWidth(input.subtitle_target_rect)
                > RectWidth(input.video_output_rect)
            || RectHeight(input.subtitle_target_rect)
                > RectHeight(input.video_output_rect);

        if (result.size_matches
            && result.contains_video
            && result.larger_than_video) {
            result.fullscreen = true;
            return result;
        }

        // Consumers such as madVR report every rect in source video pixels, so
        // subtitleTargetRect equals videoOutputRect even in fullscreen. There
        // the display size comes from the player window instead.
        result.fullscreen = input.has_display_area_size
            && input.display_area_size.cx > 0
            && input.display_area_size.cy > 0
            && result.contains_video;
        return result;
    }

    CanvasResult ResolveCanvas(const CanvasInput& input)
    {
        CanvasResult result;
        result.resolved_aspect_mode = input.aspect_mode;

        if (!input.enabled || input.aspect_mode == ASPECT_OFF) {
            result.fallback_reason = FALLBACK_DISABLED;
            result.canvas_rect = input.video_output_rect;
            return result;
        }
        if (input.aspect_mode < ASPECT_AUTO
            || input.aspect_mode >= ASPECT_COUNT) {
            result.fallback_reason = FALLBACK_INVALID_ASPECT;
            result.canvas_rect = input.video_output_rect;
            return result;
        }
        if (!input.fullscreen) {
            result.fallback_reason = FALLBACK_NOT_FULLSCREEN;
            result.canvas_rect = input.video_output_rect;
            return result;
        }
        if (!IsPositiveSize(input.video_output_rect)) {
            result.fallback_reason = FALLBACK_INVALID_VIDEO_RECT;
            return result;
        }
        if (!IsPositiveSize(input.subtitle_target_rect)) {
            result.fallback_reason = FALLBACK_NO_CONSUMER_TARGET;
            result.canvas_rect = input.video_output_rect;
            return result;
        }
        if (!ContainsRect(input.subtitle_target_rect, input.video_output_rect)) {
            result.fallback_reason = FALLBACK_CANVAS_DOES_NOT_CONTAIN_VIDEO;
            result.canvas_rect = input.video_output_rect;
            return result;
        }

        RECT canvas = input.subtitle_target_rect;
        if (input.aspect_mode != ASPECT_AUTO) {
            canvas = FitAspectRect(
                input.subtitle_target_rect,
                AspectWidth(input.aspect_mode),
                AspectHeight(input.aspect_mode));
            if (!IsPositiveSize(canvas)) {
                result.fallback_reason = FALLBACK_INVALID_ASPECT;
                result.canvas_rect = input.video_output_rect;
                return result;
            }
        }

        // RTS::RenderEx rejects a target that does not fully contain the video
        // rect with E_NOTIMPL, which drops the whole frame. Never propose a
        // canvas that would trip that guard.
        if (!ContainsRect(canvas, input.video_output_rect)) {
            result.fallback_reason = FALLBACK_CANVAS_DOES_NOT_CONTAIN_VIDEO;
            result.canvas_rect = input.video_output_rect;
            return result;
        }

        result.canvas_rect = canvas;
        result.layout_size.cx = RectWidth(canvas);
        result.layout_size.cy = RectHeight(canvas);
        result.uses_display_area = true;
        result.fallback_reason = FALLBACK_NONE;
        return result;
    }

    FrameLayout BuildFrameLayout(
        const RECT& video_output_rect, const RECT& canvas_rect)
    {
        FrameLayout layout;
        // SubRenderIntf places bitmaps in the output rect's space: the
        // consumer maps GetOutputRect onto videoOutputRect. Publishing a
        // zero-based output rect of the video's size keeps that mapping 1:1,
        // so clip and bitmap positions only need the video origin removed.
        layout.output_rect = RECT{
            0, 0,
            video_output_rect.right - video_output_rect.left,
            video_output_rect.bottom - video_output_rect.top
        };
        layout.clip_rect = RECT{
            canvas_rect.left - video_output_rect.left,
            canvas_rect.top - video_output_rect.top,
            canvas_rect.right - video_output_rect.left,
            canvas_rect.bottom - video_output_rect.top
        };
        // A canvas-local coordinate c sits at screen canvas.left + c. The
        // consumer places output-space p at screen videoOutput.left + p, so
        // p = c + (canvas.left - videoOutput.left).
        layout.bitmap_offset.x =
            canvas_rect.left - video_output_rect.left;
        layout.bitmap_offset.y =
            canvas_rect.top - video_output_rect.top;
        return layout;
    }

    CanvasResult ResolveCanvasForAspect(
        bool enabled, int aspect_mode, bool fullscreen,
        const RECT& video_rect, int display_width, int display_height)
    {
        CanvasResult result;
        result.resolved_aspect_mode = aspect_mode;

        if (!enabled || aspect_mode == ASPECT_OFF) {
            result.fallback_reason = FALLBACK_DISABLED;
            result.canvas_rect = video_rect;
            return result;
        }
        if (aspect_mode < ASPECT_AUTO || aspect_mode >= ASPECT_COUNT) {
            result.fallback_reason = FALLBACK_INVALID_ASPECT;
            result.canvas_rect = video_rect;
            return result;
        }
        if (!fullscreen) {
            result.fallback_reason = FALLBACK_NOT_FULLSCREEN;
            result.canvas_rect = video_rect;
            return result;
        }
        if (!IsPositiveSize(video_rect)) {
            result.fallback_reason = FALLBACK_INVALID_VIDEO_RECT;
            return result;
        }
        if (display_width <= 0 || display_height <= 0) {
            result.fallback_reason = FALLBACK_NO_DISPLAY_RECT;
            result.canvas_rect = video_rect;
            return result;
        }

        int aspect_width = AspectWidth(aspect_mode);
        int aspect_height = AspectHeight(aspect_mode);
        if (aspect_mode == ASPECT_AUTO) {
            // The display's own ratio, reduced to keep the arithmetic small.
            const int divisor = Gcd(display_width, display_height);
            aspect_width = display_width / divisor;
            aspect_height = display_height / divisor;
        }
        if (aspect_width <= 0 || aspect_height <= 0) {
            result.fallback_reason = FALLBACK_INVALID_ASPECT;
            result.canvas_rect = video_rect;
            return result;
        }

        const __int64 video_width = RectWidth(video_rect);
        const __int64 video_height = RectHeight(video_rect);

        // Smallest canvas of the requested ratio that still contains the
        // video. A 21:9 request around a 1920x1080 video cannot contain it,
        // so the ratio is widened to the video's own instead of clipping.
        __int64 canvas_width = video_width;
        __int64 canvas_height = canvas_width * aspect_height / aspect_width;
        if (canvas_height < video_height) {
            canvas_height = video_height;
            canvas_width = canvas_height * aspect_width / aspect_height;
        }
        if (canvas_width < video_width) {
            canvas_width = video_width;
        }
        if (canvas_width <= 0 || canvas_height <= 0) {
            result.fallback_reason = FALLBACK_INVALID_ASPECT;
            result.canvas_rect = video_rect;
            return result;
        }

        // Centre on the video rect so the extra space becomes letterbox bars.
        const LONG left = video_rect.left
            + LONG((video_width - canvas_width) / 2);
        const LONG top = video_rect.top
            + LONG((video_height - canvas_height) / 2);
        result.canvas_rect = RECT{
            left, top,
            left + LONG(canvas_width), top + LONG(canvas_height)
        };
        if (!ContainsRect(result.canvas_rect, video_rect)) {
            result.fallback_reason = FALLBACK_CANVAS_DOES_NOT_CONTAIN_VIDEO;
            result.canvas_rect = video_rect;
            return result;
        }

        result.layout_size.cx = RectWidth(result.canvas_rect);
        result.layout_size.cy = RectHeight(result.canvas_rect);
        result.uses_display_area = true;
        result.fallback_reason = FALLBACK_NONE;
        return result;
    }
}
