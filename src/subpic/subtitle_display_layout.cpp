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

}

namespace SubtitleDisplayLayout
{
    CanvasInput::CanvasInput()
        : enabled(false)
        , aspect_mode(ASPECT_OFF)
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

    FrameLayout BuildBitmapFrameLayout(
        const RECT& video_output_rect, const RECT& canvas_rect)
    {
        FrameLayout layout;
        // GetOutputRect is the video-sized reference rect. Publishing the
        // canvas size here makes the consumer scale the whole canvas back
        // into the video and squash the subtitles.
        layout.output_rect = RECT{
            0, 0,
            video_output_rect.right - video_output_rect.left,
            video_output_rect.bottom - video_output_rect.top
        };
        // The clip is the display canvas in the same output space. PGS
        // objects are authored from the video top-left, so the canvas must
        // start at that origin (not at the centred canvas y=-140).
        layout.clip_rect = RECT{
            0, 0,
            canvas_rect.right - canvas_rect.left,
            canvas_rect.bottom - canvas_rect.top
        };
        layout.bitmap_offset.x = 0;
        layout.bitmap_offset.y = 0;
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

    BitmapFrameInput::BitmapFrameInput()
        : enabled(false)
        , video_rect()
    {
        display_area.cx = 0;
        display_area.cy = 0;
    }

    BitmapFrameResult::BitmapFrameResult()
        : canvas_active(false)
    {
        buffer_size.cx = 0;
        buffer_size.cy = 0;
    }

    BitmapFrameResult ResolveBitmapFrame(const BitmapFrameInput& input)
    {
        BitmapFrameResult result;
        // The bitmap canvas is the display area expressed in source pixels,
        // exactly like the text path: 1920x800 video on a 16:9 display gets a
        // 1920x1080 canvas. The aspect mode is not consulted here; bitmap
        // subtitles always use the whole display area.
        result.canvas = ResolveCanvasForAspect(
            input.enabled, ASPECT_AUTO, true, input.video_rect,
            input.display_area.cx, input.display_area.cy);
        const RECT canvas = result.canvas.uses_display_area
            ? result.canvas.canvas_rect : input.video_rect;
        result.frame = BuildBitmapFrameLayout(input.video_rect, canvas);
        result.buffer_size.cx = canvas.right - canvas.left;
        result.buffer_size.cy = canvas.bottom - canvas.top;
        result.canvas_active = result.canvas.uses_display_area;
        return result;
    }
}
