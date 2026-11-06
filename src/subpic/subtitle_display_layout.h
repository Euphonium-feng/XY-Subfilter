/*
 * Pure subtitle display-area selection shared by the renderer wrappers.
 * It does not own DirectShow or subtitle-provider state.
 */

#pragma once

#include <WTypes.h>

namespace SubtitleDisplayLayout
{
    enum AspectMode
    {
        ASPECT_OFF = 0,
        ASPECT_AUTO,
        ASPECT_16_9,
        ASPECT_16_10,
        ASPECT_21_9,
        ASPECT_32_9,
        ASPECT_4_3,
        ASPECT_COUNT
    };

    enum FallbackReason
    {
        FALLBACK_NONE = 0,
        FALLBACK_DISABLED,
        FALLBACK_NOT_FULLSCREEN,
        FALLBACK_NO_DISPLAY_RECT,
        FALLBACK_INVALID_DISPLAY_RECT,
        FALLBACK_INVALID_VIDEO_RECT,
        FALLBACK_INVALID_ASPECT
    };

    struct ResolveInput
    {
        int aspect_mode;
        bool fullscreen;
        RECT video_rect;
        RECT fullscreen_rect;
        bool has_fullscreen_rect;
        SIZE display_mode_size;
        bool has_display_mode_size;

        ResolveInput();
    };

    struct ResolveResult
    {
        RECT target_rect;
        int resolved_aspect_mode;
        FallbackReason fallback_reason;
        bool uses_display_area;

        ResolveResult();
    };

    struct BitmapPlacement
    {
        RECT output_rect;
        RECT clip_rect;
        double scale_x;
        double scale_y;
        bool uses_display_area;

        BitmapPlacement();
    };

    // A valid rect is non-empty and entirely non-negative in display coordinates.
    bool IsValidRect(const RECT& rect);

    // Returns the largest centered rect with the requested aspect inside bounds.
    RECT FitAspectRect(const RECT& bounds, int width, int height);

    // Resolves the subtitle target rect without depending on DirectShow.
    ResolveResult ResolveTargetRect(const ResolveInput& input);

    struct ConsumerInput
    {
        bool enabled;
        int aspect_mode;
        bool fullscreen;
        RECT video_rect;
        RECT fullscreen_rect;
        bool has_fullscreen_rect;
        SIZE display_mode_size;
        bool has_display_mode_size;

        ConsumerInput();
    };

    ResolveResult ResolveFromConsumerInput(const ConsumerInput& input);

    // Maps a bitmap rendered in video coordinates onto the display target.
    // The bitmap remains in video coordinates; output/clip express the
    // transform the consumer must apply.
    BitmapPlacement ResolveBitmapPlacement(
        const RECT& video_rect, const RECT& display_target_rect,
        bool use_display_area);

    // Aspect label metadata for property-page combo boxes.
    inline int AspectWidth(int aspect_mode)
    {
        switch (aspect_mode) {
        case ASPECT_16_9:
            return 16;
        case ASPECT_16_10:
            return 16;
        case ASPECT_21_9:
            return 21;
        case ASPECT_32_9:
            return 32;
        case ASPECT_4_3:
            return 4;
        default:
            return 0;
        }
    }

    inline int AspectHeight(int aspect_mode)
    {
        switch (aspect_mode) {
        case ASPECT_16_9:
            return 9;
        case ASPECT_16_10:
            return 10;
        case ASPECT_21_9:
            return 9;
        case ASPECT_32_9:
            return 9;
        case ASPECT_4_3:
            return 3;
        default:
            return 0;
        }
    }
}
