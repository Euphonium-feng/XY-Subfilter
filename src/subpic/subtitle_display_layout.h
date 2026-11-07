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
        FALLBACK_INVALID_ASPECT,
        FALLBACK_NO_CONSUMER_TARGET,
        FALLBACK_CANVAS_DOES_NOT_CONTAIN_VIDEO
    };

    struct FullscreenGateInput
    {
        RECT video_output_rect;
        RECT subtitle_target_rect;
        RECT fullscreen_rect;
        bool has_fullscreen_rect;
        SIZE display_mode_size;
        bool has_display_mode_size;

        FullscreenGateInput();
    };

    struct FullscreenGateResult
    {
        bool has_display;
        bool size_matches;
        bool contains_video;
        bool larger_than_video;
        bool fullscreen;

        FullscreenGateResult();
    };

    struct CanvasInput
    {
        bool enabled;
        int aspect_mode;
        bool fullscreen;
        RECT video_output_rect;
        RECT subtitle_target_rect;

        CanvasInput();
    };

    struct CanvasResult
    {
        RECT canvas_rect;
        SIZE layout_size;
        POINT canvas_origin;
        int resolved_aspect_mode;
        FallbackReason fallback_reason;
        bool uses_display_area;

        CanvasResult();
    };

    // Layout of a published frame in the provider's coordinate space:
    // output is the zero-based video rect, clip may extend into the bars and
    // bitmap_offset removes the video's on-screen origin.
    struct FrameLayout
    {
        RECT output_rect;
        RECT clip_rect;
        POINT bitmap_offset;

        FrameLayout();
    };

    // A valid rect has non-zero width and height. Origins may be negative on
    // secondary monitors, so only the size is constrained.
    bool IsValidRect(const RECT& rect);

    // True when outer fully contains inner.
    bool ContainsRect(const RECT& outer, const RECT& inner);

    // Returns the largest centered rect with the requested aspect inside bounds.
    RECT FitAspectRect(const RECT& bounds, int width, int height);

    // Strict fullscreen gate. SubRenderIntf has no fullscreen flag, so the
    // consumer's target is only accepted as fullscreen when its size matches
    // the reported display mode (within 5%), it contains the video rect and it
    // is strictly larger than the video rect.
    FullscreenGateResult EvaluateFullscreenGate(
        const FullscreenGateInput& input);

    // Resolves the canvas used to lay out text subtitles. AUTO uses the
    // consumer target; explicit aspect modes fit inside it. Never returns a
    // canvas that is outside the target.
    CanvasResult ResolveCanvas(const CanvasInput& input);

    // Builds the published frame geometry from the consumer's video rect and
    // the resolved canvas. Bitmap positions are canvas-local and are shifted
    // by canvas_rect.topLeft.
    FrameLayout BuildFrameLayout(
        const RECT& video_output_rect, const RECT& canvas_rect);

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
