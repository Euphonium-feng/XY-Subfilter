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

    struct CanvasInput
    {
        bool enabled;
        int aspect_mode;
        RECT video_output_rect;
        RECT subtitle_target_rect;

        CanvasInput();
    };

    struct CanvasResult
    {
        RECT canvas_rect;
        SIZE layout_size;
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

    // Resolves the canvas used to lay out text subtitles. AUTO uses the
    // consumer target; explicit aspect modes fit inside it. Never returns a
    // canvas that is outside the target.
    //
    // Some consumers (notably madVR) report every consumer rect in source
    // pixels, so the target is identical to the video even in fullscreen. When
    // a real display size is supplied, the canvas is therefore built from the
    // display ratio instead: it stays in video pixel space, contains the video
    // and is centred on it, e.g. 1920x800 on a 16:9 display becomes 1920x1080
    // at y=-140.
    CanvasResult ResolveCanvas(const CanvasInput& input);

    CanvasResult ResolveCanvasForAspect(
        bool enabled, int aspect_mode, bool fullscreen,
        const RECT& video_rect, int display_width, int display_height);

    // Bitmap (PGS/DVB) subtitles use the full display canvas. The stream's
    // design resolution is not a publication canvas, and the aspect mode is
    // deliberately ignored here: bitmaps are drawn in their own coordinates
    // and only need the canvas they are rendered into.
    struct BitmapFrameInput
    {
        bool enabled;
        RECT video_rect;
        SIZE display_area;

        BitmapFrameInput();
    };

    struct BitmapFrameResult
    {
        CanvasResult canvas;
        FrameLayout frame;
        SIZE buffer_size;
        // The video rect expressed in canvas-local coordinates. A bitmap
        // frame is published as separate bitmap bands on either side of this
        // rect so no single bitmap straddles the video/letterbox boundary.
        RECT video_in_canvas;
        bool canvas_active;

        BitmapFrameResult();
    };

    BitmapFrameResult ResolveBitmapFrame(const BitmapFrameInput& input);

    // Splits a canvas-local dirty rect into at most three horizontal bands:
    // the area above the video, the video band itself and the area below it.
    // Each returned rect is clipped to canvas_bounds and strictly non-empty.
    // Returns the number of rects written to out[3].
    int PartitionBitmapRects(
        const RECT& dirty, const RECT& canvas_bounds,
        const RECT& video_in_canvas, RECT out[3]);

    // Merges adjacent partition rects until at most max_count remain. A limit
    // of 1 returns the union of everything as a single rect. Used to honour
    // the consumer's announced maxBitmapCount without changing geometry.
    int MergePartitionsToLimit(
        const RECT* parts, int count, int max_count, RECT out[3]);

    // Resolves the published position and size of one partitioned bitmap:
    // the rect is a pixel sub-range of the canvas buffer, so its position is
    // canvasOrigin + rect.topLeft and its size is rect.Size(). The canvas
    // origin is encoded by bitmap_offset, which is already relative to the
    // zero-based video output.
    struct BitmapSlice
    {
        POINT position;
        SIZE size;

        BitmapSlice();
    };

    BitmapSlice ResolveBitmapSlice(
        const RECT& bitmap_rect, const POINT& bitmap_offset);

    // Maps a design-space bitmap coordinate into the consumer's rect space.
    // Some consumers (MPC VR) report their rects in display pixels rather
    // than source pixels; a frame published in source space then lands in a
    // different coordinate system and the letterbox part is discarded.
    // scale/offset are derived from consumerVideoOutput / designSize.
    struct BitmapDisplayTransform
    {
        bool active;
        SIZE design_size;
        SIZE target_size;
        POINT offset;

        BitmapDisplayTransform();
    };

    // Builds the transform when target_size is strictly larger than
    // design_size in either axis (i.e. the consumer is not reporting source
    // pixels). An inactive transform is an identity mapping.
    BitmapDisplayTransform ResolveBitmapDisplayTransform(
        const SIZE& design_size, const SIZE& target_size);

    // Maps one rect through the transform. Runs and widths are rounded so
    // adjacent spans stay contiguous.
    RECT MapRectToTarget(
        const RECT& rect, const BitmapDisplayTransform& transform);

    enum ConsumerFamily
    {
        CONSUMER_UNKNOWN = 0,
        CONSUMER_MADVR,
        CONSUMER_MPC_VR
    };

    // Identifies the connected subtitle consumer from its self-reported
    // name. Comparison is case-insensitive substring matching; an empty or
    // unrecognized name maps to CONSUMER_UNKNOWN.
    ConsumerFamily ClassifyConsumer(const wchar_t* consumer_name);

    // Builds the published text frame geometry from the consumer's video rect
    // and the resolved canvas. Text positions are shifted by
    // canvas_rect.topLeft because the text renderer keeps a video-sized output.
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
