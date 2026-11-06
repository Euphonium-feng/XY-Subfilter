#include "subtitle_display_layout.h"

namespace
{
    bool IsPositiveSize(const RECT& rect)
    {
        return rect.right > rect.left && rect.bottom > rect.top;
    }

    RECT MakeDisplayRect(const SubtitleDisplayLayout::ResolveInput& input)
    {
        if (input.has_fullscreen_rect
            && SubtitleDisplayLayout::IsValidRect(input.fullscreen_rect)) {
            return input.fullscreen_rect;
        }
        if (input.has_display_mode_size
            && input.display_mode_size.cx > 0
            && input.display_mode_size.cy > 0) {
            const RECT display = {
                0, 0, input.display_mode_size.cx, input.display_mode_size.cy
            };
            return display;
        }
        return RECT();
    }
}

namespace SubtitleDisplayLayout
{
    ResolveInput::ResolveInput()
        : aspect_mode(ASPECT_OFF)
        , fullscreen(false)
        , video_rect()
        , fullscreen_rect()
        , has_fullscreen_rect(false)
        , display_mode_size()
        , has_display_mode_size(false)
    {
    }

    ResolveResult::ResolveResult()
        : target_rect()
        , resolved_aspect_mode(ASPECT_OFF)
        , fallback_reason(FALLBACK_DISABLED)
        , uses_display_area(false)
    {
    }

    BitmapPlacement::BitmapPlacement()
        : output_rect()
        , clip_rect()
        , scale_x(1.0)
        , scale_y(1.0)
        , uses_display_area(false)
    {
    }

    ConsumerInput::ConsumerInput()
        : enabled(false)
        , aspect_mode(ASPECT_OFF)
        , fullscreen(false)
        , video_rect()
        , fullscreen_rect()
        , has_fullscreen_rect(false)
        , display_mode_size()
        , has_display_mode_size(false)
    {
    }

    bool IsValidRect(const RECT& rect)
    {
        return IsPositiveSize(rect)
            && rect.left >= 0
            && rect.top >= 0;
    }

    RECT FitAspectRect(const RECT& bounds, int width, int height)
    {
        if (!IsValidRect(bounds) || width <= 0 || height <= 0) {
            return RECT();
        }

        const __int64 bounds_width = bounds.right - bounds.left;
        const __int64 bounds_height = bounds.bottom - bounds.top;
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
        RECT result = {
            left, top, left + LONG(fitted_width), top + LONG(fitted_height)
        };
        return result;
    }

    ResolveResult ResolveTargetRect(const ResolveInput& input)
    {
        ResolveResult result;
        result.resolved_aspect_mode = input.aspect_mode;

        if (input.aspect_mode == ASPECT_OFF) {
            result.fallback_reason = FALLBACK_DISABLED;
            result.target_rect = input.video_rect;
            return result;
        }
        if (input.aspect_mode < ASPECT_AUTO || input.aspect_mode >= ASPECT_COUNT) {
            result.fallback_reason = FALLBACK_INVALID_ASPECT;
            result.target_rect = input.video_rect;
            return result;
        }
        if (!input.fullscreen) {
            result.fallback_reason = FALLBACK_NOT_FULLSCREEN;
            result.target_rect = input.video_rect;
            return result;
        }
        if (!IsValidRect(input.video_rect)) {
            result.fallback_reason = FALLBACK_INVALID_VIDEO_RECT;
            return result;
        }

        const RECT display = MakeDisplayRect(input);
        if (!IsPositiveSize(display) || display.left < 0 || display.top < 0) {
            result.fallback_reason = FALLBACK_NO_DISPLAY_RECT;
            result.target_rect = input.video_rect;
            return result;
        }
        if (!IsValidRect(display)) {
            result.fallback_reason = FALLBACK_INVALID_DISPLAY_RECT;
            result.target_rect = input.video_rect;
            return result;
        }

        if (input.aspect_mode == ASPECT_AUTO) {
            result.target_rect = display;
        }
        else {
            result.target_rect = FitAspectRect(
                display, AspectWidth(input.aspect_mode),
                AspectHeight(input.aspect_mode));
            if (!IsPositiveSize(result.target_rect)) {
                result.fallback_reason = FALLBACK_INVALID_ASPECT;
                result.target_rect = input.video_rect;
                return result;
            }
        }

        result.uses_display_area = true;
        result.fallback_reason = FALLBACK_NONE;
        return result;
    }

    BitmapPlacement ResolveBitmapPlacement(
        const RECT& video_rect, const RECT& display_target_rect,
        bool use_display_area)
    {
        BitmapPlacement placement;
        placement.output_rect = video_rect;
        placement.clip_rect = video_rect;

        if (!use_display_area
            || !IsValidRect(video_rect)
            || !IsValidRect(display_target_rect)) {
            return placement;
        }

        const LONG video_width = video_rect.right - video_rect.left;
        const LONG video_height = video_rect.bottom - video_rect.top;
        const LONG target_width =
            display_target_rect.right - display_target_rect.left;
        const LONG target_height =
            display_target_rect.bottom - display_target_rect.top;

        // GetOutputRect describes the bitmap's own coordinate space. The
        // consumer scales that rectangle onto GetClipRect.
        placement.output_rect = video_rect;
        placement.clip_rect = display_target_rect;
        placement.scale_x =
            static_cast<double>(target_width) / video_width;
        placement.scale_y =
            static_cast<double>(target_height) / video_height;
        placement.uses_display_area = true;
        return placement;
    }

    ResolveResult ResolveFromConsumerInput(const ConsumerInput& input)
    {
        ResolveResult result;
        result.resolved_aspect_mode = input.aspect_mode;

        if (!input.enabled
            || input.aspect_mode == ASPECT_OFF) {
            result.fallback_reason = FALLBACK_DISABLED;
            result.target_rect = input.video_rect;
            return result;
        }

        ResolveInput resolve_input;
        resolve_input.aspect_mode = input.aspect_mode;
        resolve_input.fullscreen = input.fullscreen;
        resolve_input.video_rect = input.video_rect;
        resolve_input.fullscreen_rect = input.fullscreen_rect;
        resolve_input.has_fullscreen_rect = input.has_fullscreen_rect;
        resolve_input.display_mode_size = input.display_mode_size;
        resolve_input.has_display_mode_size = input.has_display_mode_size;
        return ResolveTargetRect(resolve_input);
    }

}
