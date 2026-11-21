#include "subtitle_display_layout.h"

#include <string.h>
#include <wchar.h>

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

    CanvasRequest::CanvasRequest()
        : enabled(false)
        , aspect_mode(ASPECT_OFF)
        , use_subtitle_target_when_off(false)
    {
        video_rect = RECT();
        subtitle_target_rect = RECT();
        display_area.cx = 0;
        display_area.cy = 0;
        fullscreen_rect = RECT();
    }

    CanvasPlan::CanvasPlan()
        : display_size_source(DISPLAY_SIZE_NONE)
        , active(false)
    {
        canvas_rect = RECT();
        display_size.cx = 0;
        display_size.cy = 0;
    }

    CanvasPlan ResolveCanvasRequest(const CanvasRequest& request)
    {
        CanvasPlan plan;

        // Persisted display size wins: it is the only source that works for
        // every consumer, including those that report source-pixel rects.
        if (request.display_area.cx > 0 && request.display_area.cy > 0) {
            plan.display_size = request.display_area;
            plan.display_size_source = DISPLAY_SIZE_PERSISTED;
        }
        else if (IsValidRect(request.fullscreen_rect)
            && ContainsRect(request.fullscreen_rect, request.video_rect)
            && (RectWidth(request.fullscreen_rect) > RectWidth(request.video_rect)
                || RectHeight(request.fullscreen_rect) > RectHeight(request.video_rect))) {
            plan.display_size.cx = RectWidth(request.fullscreen_rect);
            plan.display_size.cy = RectHeight(request.fullscreen_rect);
            plan.display_size_source = DISPLAY_SIZE_CONSUMER;
        }

        if (plan.display_size_source != DISPLAY_SIZE_NONE) {
            plan.canvas = ResolveCanvasForAspect(
                request.enabled, request.aspect_mode, true, request.video_rect,
                plan.display_size.cx, plan.display_size.cy);
        }
        else {
            CanvasInput input;
            // Without a display size the consumer target is the only canvas
            // information available. Text falls back to it when ASPECT_OFF;
            // bitmaps keep the video rect because their objects carry their
            // own coordinates.
            input.enabled = request.use_subtitle_target_when_off;
            input.aspect_mode = request.enabled
                ? request.aspect_mode : ASPECT_OFF;
            input.video_output_rect = request.video_rect;
            input.subtitle_target_rect = request.subtitle_target_rect;
            plan.canvas = ResolveCanvas(input);
        }

        plan.active = plan.canvas.uses_display_area;
        plan.canvas_rect = plan.active
            ? plan.canvas.canvas_rect : request.video_rect;
        return plan;
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
        fullscreen_rect = RECT();
    }

    BitmapFrameResult::BitmapFrameResult()
        : canvas_active(false)
    {
        buffer_size.cx = 0;
        buffer_size.cy = 0;
        video_in_canvas = RECT();
    }

    BitmapFrameResult ResolveBitmapFrame(const BitmapFrameInput& input)
    {
        BitmapFrameResult result;
        // The bitmap canvas is the display area expressed in source pixels,
        // exactly like the text path: 1920x800 video on a 16:9 display gets a
        // 1920x1080 canvas. ASPECT_AUTO keeps the display's own ratio; bitmap
        // subtitles never use an explicit letterbox aspect.
        CanvasRequest request;
        request.enabled = input.enabled;
        request.aspect_mode = ASPECT_AUTO;
        request.video_rect = input.video_rect;
        request.display_area = input.display_area;
        request.fullscreen_rect = input.fullscreen_rect;
        const CanvasPlan plan = ResolveCanvasRequest(request);
        result.canvas = plan.canvas;
        const RECT canvas = plan.canvas_rect;
        // Match the verified text path exactly: the output stays the video
        // reference rect, the clip is the centred canvas extending into the
        // bars, and bitmap positions are moved by the canvas origin. Each
        // bitmap must also stay entirely inside the video band or one of the
        // bars; see PartitionBitmapRects.
        result.frame = BuildFrameLayout(input.video_rect, canvas);
        result.buffer_size.cx = canvas.right - canvas.left;
        result.buffer_size.cy = canvas.bottom - canvas.top;
        result.video_in_canvas = RECT{
            input.video_rect.left - canvas.left,
            input.video_rect.top - canvas.top,
            input.video_rect.right - canvas.left,
            input.video_rect.bottom - canvas.top
        };
        result.canvas_active = result.canvas.uses_display_area;
        return result;
    }

    int PartitionBitmapRects(
        const RECT& dirty, const RECT& canvas_bounds,
        const RECT& video_in_canvas, RECT out[3])
    {
        if (!out || !IsPositiveSize(dirty) || !IsPositiveSize(canvas_bounds)) {
            return 0;
        }

        RECT clipped = dirty;
        if (clipped.left < canvas_bounds.left) {
            clipped.left = canvas_bounds.left;
        }
        if (clipped.top < canvas_bounds.top) {
            clipped.top = canvas_bounds.top;
        }
        if (clipped.right > canvas_bounds.right) {
            clipped.right = canvas_bounds.right;
        }
        if (clipped.bottom > canvas_bounds.bottom) {
            clipped.bottom = canvas_bounds.bottom;
        }
        if (!IsPositiveSize(clipped)) {
            return 0;
        }

        // An empty or inverted video band means there is no split point.
        if (!IsPositiveSize(video_in_canvas)) {
            out[0] = clipped;
            return 1;
        }

        int count = 0;
        const LONG video_top = video_in_canvas.top;
        const LONG video_bottom = video_in_canvas.bottom;

        // Above the video.
        if (clipped.top < video_top) {
            RECT upper = clipped;
            if (upper.bottom > video_top) {
                upper.bottom = video_top;
            }
            if (IsPositiveSize(upper)) {
                out[count++] = upper;
            }
        }

        // The video band itself. Horizontal extent is left untouched so a
        // bitmap in a side bar is never dropped; the consumer clips it.
        if (clipped.bottom > video_top && clipped.top < video_bottom) {
            RECT middle = clipped;
            if (middle.top < video_top) {
                middle.top = video_top;
            }
            if (middle.bottom > video_bottom) {
                middle.bottom = video_bottom;
            }
            if (IsPositiveSize(middle)) {
                out[count++] = middle;
            }
        }

        // Below the video.
        if (clipped.bottom > video_bottom) {
            RECT lower = clipped;
            if (lower.top < video_bottom) {
                lower.top = video_bottom;
            }
            if (IsPositiveSize(lower)) {
                out[count++] = lower;
            }
        }

        return count;
    }

    BitmapSlice::BitmapSlice()
    {
        position.x = 0;
        position.y = 0;
        size.cx = 0;
        size.cy = 0;
    }

    BitmapSlice ResolveBitmapSlice(
        const RECT& bitmap_rect, const POINT& bitmap_offset)
    {
        BitmapSlice result;
        result.position.x = bitmap_rect.left + bitmap_offset.x;
        result.position.y = bitmap_rect.top + bitmap_offset.y;
        result.size.cx = bitmap_rect.right - bitmap_rect.left;
        result.size.cy = bitmap_rect.bottom - bitmap_rect.top;
        return result;
    }

    BitmapPublication::BitmapPublication()
        : kind(SingleRect)
        , bitmap_count(0)
        , partition_count(0)
    {
        output_rect = RECT();
        clip_rect = RECT();
        bitmap_offset.x = 0;
        bitmap_offset.y = 0;
        for (int i = 0; i < 3; ++i) {
            bitmap_rects[i] = RECT();
            partitions[i] = RECT();
        }
    }

    BitmapPublicationInput::BitmapPublicationInput()
        : canvas_active(false)
        , prefer_single_bitmap(false)
        , frame_output_rect()
        , frame_clip_rect()
        , video_in_canvas()
        , dirty_rect()
    {
        buffer_size.cx = 0;
        buffer_size.cy = 0;
        frame_bitmap_offset.x = 0;
        frame_bitmap_offset.y = 0;
    }

    bool ResolveBitmapPublication(
        const BitmapPublicationInput& input, BitmapPublication* publication)
    {
        if (!publication) {
            return false;
        }
        BitmapPublication result;
        if (!IsPositiveSize(input.dirty_rect)) {
            *publication = result;
            return false;
        }

        if (!input.canvas_active) {
            result.kind = BitmapPublication::SingleRect;
            result.output_rect = input.frame_output_rect;
            result.clip_rect = input.frame_clip_rect;
            result.bitmap_offset = input.frame_bitmap_offset;
            result.bitmap_rects[0] = input.dirty_rect;
            result.bitmap_count = 1;
            *publication = result;
            return true;
        }

        const RECT canvas_bounds = {
            0, 0, input.buffer_size.cx, input.buffer_size.cy
        };
        if (input.prefer_single_bitmap) {
            // MPCVR reads only bitmap 0 and ignores the clip origin. Publish
            // the complete zero-based canvas so the texture is not cropped.
            if (!IsPositiveSize(canvas_bounds)) {
                *publication = result;
                return false;
            }
            result.kind = BitmapPublication::SingleCanvas;
            result.output_rect = canvas_bounds;
            result.clip_rect = canvas_bounds;
            result.bitmap_rects[0] = canvas_bounds;
            result.bitmap_count = 1;
            *publication = result;
            return true;
        }

        RECT partitions[3] = {};
        const int count = PartitionBitmapRects(
            input.dirty_rect, canvas_bounds, input.video_in_canvas,
            partitions);
        if (count <= 0) {
            *publication = result;
            return false;
        }
        result.kind = BitmapPublication::Partitioned;
        result.output_rect = input.frame_output_rect;
        result.clip_rect = input.frame_clip_rect;
        result.bitmap_offset = input.frame_bitmap_offset;
        result.partition_count = count;
        for (int i = 0; i < count; ++i) {
            result.partitions[i] = partitions[i];
            result.bitmap_rects[i] = partitions[i];
        }
        result.bitmap_count = count;
        *publication = result;
        return true;
    }

    namespace
    {
        bool ContainsNoCase(const wchar_t* haystack, const wchar_t* needle)
        {
            if (!haystack || !needle || !*needle) {
                return false;
            }
            const size_t needle_len = wcslen(needle);
            for (const wchar_t* start = haystack; *start; ++start) {
                if (_wcsnicmp(start, needle, needle_len) == 0) {
                    return true;
                }
            }
            return false;
        }
    }

    ConsumerFamily ClassifyConsumer(const wchar_t* consumer_name)
    {
        if (!consumer_name || !*consumer_name) {
            return CONSUMER_UNKNOWN;
        }
        if (ContainsNoCase(consumer_name, L"madvr")) {
            return CONSUMER_MADVR;
        }
        if (ContainsNoCase(consumer_name, L"mpc")) {
            return CONSUMER_MPC_VR;
        }
        return CONSUMER_UNKNOWN;
    }

    ConsumerContract::ConsumerContract()
        : present_oversized_bitmap(false)
        , prefer_single_bitmap(false)
        , hdr_owned_by_consumer(false)
    {
    }

    ConsumerContract ResolveConsumerContract(
        ConsumerFamily family, bool canvas_active, bool is_bitmap)
    {
        ConsumerContract contract;
        switch (family)
        {
        case CONSUMER_MADVR:
            // madVR composites a canvas-positioned bitmap against the video
            // rect when it is presented as a bitmap, so the verified overlay
            // contract masks isBitmap while the canvas is active.
            contract.present_oversized_bitmap =
                canvas_active && is_bitmap;
            break;
        case CONSUMER_MPC_VR:
            // MPCVR reads only bitmap 0 and scales the published texture onto
            // its video rect with independent X/Y factors. A single opaque
            // canvas texture keeps the geometry in one piece; MPCVR converts
            // the SDR subtitle texture to PQ for HDR output itself.
            contract.prefer_single_bitmap = is_bitmap;
            contract.hdr_owned_by_consumer = true;
            break;
        case CONSUMER_UNKNOWN:
        default:
            break;
        }
        return contract;
    }

    bool ShouldPassThroughSdrForConsumer(ConsumerFamily family)
    {
        // MPCVR applies its own SDR-to-PQ conversion on the HDR output path.
        // Pre-encoding the subtitle would apply the transfer function twice.
        return ResolveConsumerContract(family, true, true)
            .hdr_owned_by_consumer;
    }

    ConsumerIdentity::ConsumerIdentity()
        : m_retries(0)
    {
        m_name[0] = 0;
    }

    void ConsumerIdentity::Reset()
    {
        m_name[0] = 0;
        m_retries = 0;
    }

    bool ConsumerIdentity::Sample(NameReader reader, void* context)
    {
        if (!reader) {
            return false;
        }
        wchar_t raw[kMaxNameChars] = {};
        if (!reader(context, raw, kMaxNameChars) || raw[0] == 0) {
            return false;
        }
        int i = 0;
        for (; raw[i] && i < kMaxNameChars - 1; ++i) {
            const wchar_t ch = raw[i];
            m_name[i] = (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n')
                ? L'_' : ch;
        }
        m_name[i] = 0;
        return true;
    }

    void ConsumerIdentity::Refresh(NameReader reader, void* context)
    {
        if (HasName() || m_retries >= kMaxRetries) {
            return;
        }
        if (Sample(reader, context)) {
            return;
        }
        ++m_retries;
    }

    bool ConsumerIdentity::HasName() const
    {
        return m_name[0] != 0;
    }

    const wchar_t* ConsumerIdentity::Name() const
    {
        return m_name;
    }

    ConsumerFamily ConsumerIdentity::Family() const
    {
        return ClassifyConsumer(m_name);
    }

    ConsumerContract ConsumerIdentity::Contract(
        bool canvas_active, bool is_bitmap) const
    {
        return ResolveConsumerContract(Family(), canvas_active, is_bitmap);
    }

    bool ConsumerIdentity::OwnsHdrConversion() const
    {
        return ShouldPassThroughSdrForConsumer(Family());
    }
}
