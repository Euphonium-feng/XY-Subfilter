#include "stdafx.h"
#include "HdmvSub.h"
#include "DVBSub.h"
#include "hdmv_subtitle_provider.h"
#include "../subpic/XySubRenderFrameWrapper.h"
#include "../subpic/diagnostic_log.h"
#include "../subpic/bitmap_resampler.h"
#include "../subpic/subtitle_display_layout.h"
#include "IDirectVobSubXy.h"
#include "xy_malloc.h"

#if ENABLE_XY_LOG_HDMVSUB
#define TRACE_SUB_PROVIDER(msg)		XY_LOG_TRACE(msg)
#else
#define TRACE_SUB_PROVIDER(msg)
#endif

namespace
{
// Bitmaps use the same display canvas as text (D44). The canvas is built from
// the video rect and the persisted display size; the stream's design
// resolution only determines how many pixels the provider has to paint.
void ReadBitmapCanvasInput(
    IXyOptions* consumer, const RECT& video_rect,
    SubtitleDisplayLayout::BitmapFrameInput* input)
{
    if (!consumer || !input) {
        return;
    }

    input->video_rect = video_rect;
    input->display_area.cx = 0;
    input->display_area.cy = 0;
    input->fullscreen_rect = RECT();
    input->enabled = false;

    if (FAILED(consumer->XyGetBool(
            DirectVobSubXyOptions::BOOL_USE_DISPLAY_AREA_FOR_SUBTITLES,
            &input->enabled))
        || !input->enabled) {
        return;
    }

    // Only raw values are read here. Whether the fullscreen rect is usable is
    // decided once, in ResolveCanvasRequest, so bitmaps and text cannot drift.
    consumer->XyGetSize(
        DirectVobSubXyOptions::SIZE_DISPLAY_AREA, &input->display_area);
    consumer->XyGetRect(
        DirectVobSubXyOptions::RECT_FULLSCREEN, &input->fullscreen_rect);
}

// Adapter between IXyOptions and ConsumerIdentity's name reader. The identity
// module owns classification and the bounded retry.
bool ReadConsumerName(void* context, wchar_t* buffer, int capacity)
{
    IXyOptions* consumer = static_cast<IXyOptions*>(context);
    if (!consumer || !buffer || capacity <= 1) {
        return false;
    }
    LPWSTR name = NULL;
    int chars = 0;
    if (FAILED(consumer->XyGetString(
            DirectVobSubXyOptions::STRING_CONNECTED_CONSUMER,
            &name, &chars)) || !name) {
        return false;
    }
    int i = 0;
    for (; i < chars && name[i] && i < capacity - 1; ++i) {
        buffer[i] = name[i];
    }
    buffer[i] = 0;
    LocalFree(name);
    return i > 0;
}
}

//
// HdmvSubtitleProviderImpl
//
HdmvSubtitleProviderImpl::HdmvSubtitleProviderImpl( CBaseSub* pSub, bool cleanOld )
    : m_pSub(pSub)
    , m_cleanOld(cleanOld)
    , m_consumer(NULL)
    , m_use_dst_alpha(false)
    , m_canvas_cached(false)
    , m_canvas_active(false)
    , m_canvas_fallback(0)
    , m_frame_geometry_dirty(false)
    , m_publication_valid(false)
    , m_trace_valid(false)
    , m_diag_presented_bitmap(-1)
    , m_diag_presented_movable(-1)
    , m_diag_max_bitmap_count(-1)
    , m_diag_consumer_pending(true)
    , m_diag_consumer_refresh_budget(8)
{
    m_cur_output_size.cx = 0;
    m_cur_output_size.cy = 0;
    m_canvas_design_size.cx = 0;
    m_canvas_design_size.cy = 0;
    m_render_size.cx = 0;
    m_render_size.cy = 0;
    m_canvas_video_size.cx = 0;
    m_canvas_video_size.cy = 0;
    m_canvas_enabled = false;
    m_canvas_display_area.cx = 0;
    m_canvas_display_area.cy = 0;
    m_canvas_display_source = 0;
    m_prefer_single_bitmap = false;
    m_canvas_rect = RECT();
    m_canvas_video_in_canvas = RECT();
    m_canvas_frame_layout = SubtitleDisplayLayout::FrameLayout();
    m_diag_consumer_fullscreen = RECT();
    m_diag_consumer_display_mode.cx = 0;
    m_diag_consumer_display_mode.cy = 0;
    m_diag_consumer_subtitle_target = RECT();
    m_design_spd = SubPicDesc();
    m_design_spd_size.cx = 0;
    m_design_spd_size.cy = 0;
    m_design_spd_valid = false;
    m_scale_design_canvas = false;
    m_scale_filter = DirectVobSubXyOptions::BITMAP_SCALE_FILTER_BILINEAR;
    m_diag_design_canvas.cx = 0;
    m_diag_design_canvas.cy = 0;
    m_diag_scale_canvas.cx = 0;
    m_diag_scale_canvas.cy = 0;
    m_diag_scale_factor = 1.0;
    m_diag_source_dirty = RECT();
    m_diag_scaled_dirty = RECT();
}

HdmvSubtitleProviderImpl::~HdmvSubtitleProviderImpl( void )
{
    if (m_design_spd_valid) {
        xy_free(m_design_spd.bits);
        m_design_spd.bits = NULL;
    }
}

HdmvSubtitleProviderImpl::FrameTrace::FrameTrace()
    : bitmap_id(0)
    , bitmap_count(0)
    , object_count(0)
    , drawn_objects(0)
    , skipped_objects(0)
    , canvas_active(false)
    , partition_count(0)
    , scale_filter(0)
    , scale_factor(1.0)
    , contract(0)
    , oversized_bitmap(0)
    , consumer_scale_x_milli(0)
    , consumer_scale_y_milli(0)
    , fallback(0)
    , presented_bitmap(-1)
    , presented_movable(-1)
    , max_bitmap_count(-1)
{
    bitmaps[0] = RECT();
    bitmaps[1] = RECT();
    bitmaps[2] = RECT();
    dirty_rect = RECT();
    buffer_size.cx = 0;
    buffer_size.cy = 0;
    output = RECT();
    clip = RECT();
    offset.x = 0;
    offset.y = 0;
    video_in_canvas = RECT();
    design_canvas.cx = 0;
    design_canvas.cy = 0;
    scale_canvas.cx = 0;
    scale_canvas.cy = 0;
    source_dirty = RECT();
    scaled_dirty = RECT();
    consumer_fullscreen = RECT();
    consumer_display_mode.cx = 0;
    consumer_display_mode.cy = 0;
    consumer_subtitle_target = RECT();
}

namespace
{
bool SameRect(const RECT& first, const RECT& second)
{
    return first.left == second.left
        && first.top == second.top
        && first.right == second.right
        && first.bottom == second.bottom;
}

bool SameSize(const SIZE& first, const SIZE& second)
{
    return first.cx == second.cx && first.cy == second.cy;
}
}

bool HdmvSubtitleProviderImpl::FrameTrace::operator==(
    const FrameTrace& other) const
{
    if (bitmap_id != other.bitmap_id
        || bitmap_count != other.bitmap_count
        || object_count != other.object_count
        || drawn_objects != other.drawn_objects
        || skipped_objects != other.skipped_objects
        || canvas_active != other.canvas_active
        || partition_count != other.partition_count
        || scale_filter != other.scale_filter
        || scale_factor != other.scale_factor
        || contract != other.contract
        || oversized_bitmap != other.oversized_bitmap
        || consumer_scale_x_milli != other.consumer_scale_x_milli
        || consumer_scale_y_milli != other.consumer_scale_y_milli
        || fallback != other.fallback
        || presented_bitmap != other.presented_bitmap
        || presented_movable != other.presented_movable
        || max_bitmap_count != other.max_bitmap_count
        || !SameSize(buffer_size, other.buffer_size)
        || !SameSize(design_canvas, other.design_canvas)
        || !SameSize(scale_canvas, other.scale_canvas)
        || !SameSize(consumer_display_mode, other.consumer_display_mode)
        || !SameRect(dirty_rect, other.dirty_rect)
        || !SameRect(output, other.output)
        || !SameRect(clip, other.clip)
        || !SameRect(video_in_canvas, other.video_in_canvas)
        || !SameRect(source_dirty, other.source_dirty)
        || !SameRect(scaled_dirty, other.scaled_dirty)
        || !SameRect(consumer_fullscreen, other.consumer_fullscreen)
        || !SameRect(consumer_subtitle_target, other.consumer_subtitle_target)
        || offset.x != other.offset.x
        || offset.y != other.offset.y
        || consumer_name != other.consumer_name
        || consumer_version != other.consumer_version) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!SameRect(bitmaps[i], other.bitmaps[i])
            || !SameRect(partitions[i], other.partitions[i])) {
            return false;
        }
    }
    return true;
}

STDMETHODIMP HdmvSubtitleProviderImpl::Connect( IXyOptions *consumer )
{
    HRESULT hr = NOERROR;
    if (consumer)
    {
        hr = m_consumer != consumer ? S_OK : S_FALSE;
        m_consumer = consumer;
        m_canvas_cached = false;
        m_trace_valid = false;
        m_publication_valid = false;
        m_diag_consumer_pending = true;
        m_diag_consumer_refresh_budget = 8;
        m_consumer_identity.Reset();
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

void HdmvSubtitleProviderImpl::RefreshConsumerDiagnostics()
{
    m_diag_consumer_version.Empty();
    m_diag_presented_bitmap = -1;
    m_diag_presented_movable = -1;
    m_diag_max_bitmap_count = -1;
    m_diag_consumer_fullscreen = RECT();
    m_diag_consumer_display_mode.cx = 0;
    m_diag_consumer_display_mode.cy = 0;
    m_diag_consumer_subtitle_target = RECT();
    if (!m_consumer) {
        return;
    }

    m_consumer_identity.Refresh(ReadConsumerName, m_consumer);
    LPWSTR version = NULL;
    int version_chars = 0;
    if (SUCCEEDED(m_consumer->XyGetString(
            DirectVobSubXyOptions::STRING_CONSUMER_VERSION,
            &version, &version_chars)) && version) {
        m_diag_consumer_version = version;
        LocalFree(version);
        for (int i = 0; i < m_diag_consumer_version.GetLength(); ++i) {
            const wchar_t ch = m_diag_consumer_version[i];
            if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
                m_diag_consumer_version.SetAt(i, L'_');
            }
        }
    }
    bool is_bitmap = false;
    if (SUCCEEDED(m_consumer->XyGetBool(
            DirectVobSubXyOptions::BOOL_IS_BITMAP, &is_bitmap))) {
        m_diag_presented_bitmap = is_bitmap ? 1 : 0;
    }
    bool is_movable = false;
    if (SUCCEEDED(m_consumer->XyGetBool(
            DirectVobSubXyOptions::BOOL_IS_MOVABLE, &is_movable))) {
        m_diag_presented_movable = is_movable ? 1 : 0;
    }
    int max_bitmap_count = 0;
    if (SUCCEEDED(m_consumer->XyGetInt(
            DirectVobSubXyOptions::INT_MAX_BITMAP_COUNT2,
            &max_bitmap_count))) {
        m_diag_max_bitmap_count = max_bitmap_count;
    }
    RECT fullscreen = {};
    if (SUCCEEDED(m_consumer->XyGetRect(
            DirectVobSubXyOptions::RECT_FULLSCREEN, &fullscreen))) {
        m_diag_consumer_fullscreen = fullscreen;
    }
    SIZE display_mode = {};
    if (SUCCEEDED(m_consumer->XyGetSize(
            DirectVobSubXyOptions::SIZE_DISPLAY_MODE, &display_mode))) {
        m_diag_consumer_display_mode = display_mode;
    }
    RECT subtitle_target = {};
    if (SUCCEEDED(m_consumer->XyGetRect(
            DirectVobSubXyOptions::RECT_SUBTITLE_TARGET, &subtitle_target))) {
        m_diag_consumer_subtitle_target = subtitle_target;
    }
}

STDMETHODIMP HdmvSubtitleProviderImpl::RequestFrame( IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now )
{
    CheckPointer(subRenderFrame, E_POINTER);
    *subRenderFrame = NULL;

    HRESULT hr = NOERROR;

    POSITION pos;
    CSize MaxTextureSize, VideoSize;
    CPoint VideoTopLeft;

    if (CHdmvSub* pHdmvSub = dynamic_cast<CHdmvSub*>(m_pSub)) {
        pos = pHdmvSub->GetStartPosition(now, 0, m_cleanOld);
    } else {
        pos = m_pSub->GetStartPosition(now);
    }

    if (!pos)
    {
        return S_FALSE;
    }

    if (m_diag_consumer_pending && m_diag_consumer_refresh_budget > 0) {
        // The outer filter may not have copied the consumer name into its
        // options yet when Connect() runs; ConsumerIdentity retries a bounded
        // number of frames, so the contract is not lost and there is no COM
        // call on every frame.
        RefreshConsumerDiagnostics();
        --m_diag_consumer_refresh_budget;
        m_diag_consumer_pending = false;
        if (!m_consumer_identity.HasName()
            && m_diag_consumer_refresh_budget > 0) {
            m_diag_consumer_pending = true;
        }
    }

    bool use_dst_alpha = false;
    hr = m_consumer->XyGetBool(DirectVobSubXyOptions::BOOL_SUB_FRAME_USE_DST_ALPHA, &use_dst_alpha);
    if (m_use_dst_alpha!=use_dst_alpha)
    {
        XY_LOG_INFO("Alpha type changed from "<<m_use_dst_alpha<<" to "<<use_dst_alpha);
        Invalidate(-1);
        m_use_dst_alpha = use_dst_alpha;
    }

    hr = m_pSub->GetTextureSize(pos, MaxTextureSize, VideoSize, VideoTopLeft);
    if (SUCCEEDED(hr))
    {
        ASSERT(MaxTextureSize==VideoSize && VideoTopLeft==CPoint(0,0));
        if (!(MaxTextureSize==VideoSize && VideoTopLeft==CPoint(0,0)))
        {
            XY_LOG_ERROR("We don't know how to deal with this sizes."
                <<XY_LOG_VAR_2_STR(MaxTextureSize)<<XY_LOG_VAR_2_STR(VideoSize)
                <<XY_LOG_VAR_2_STR(VideoTopLeft));
            return E_FAIL;
        }
    }
    else
    {
        XY_LOG_ERROR("Failed to get sizes info");
        return hr;
    }
    if (m_cur_output_size != MaxTextureSize)
    {
        m_cur_output_size = MaxTextureSize;
        if (m_cur_output_size.cx==0 || m_cur_output_size.cy==0)
        {
            XY_LOG_WARN("Invalid output size "<<m_cur_output_size);
            return S_FALSE;
        }
    }
    // Resolve the canvas once per input change. The canvas is built from the
    // active video size, not from the stream's design resolution: a PGS
    // design canvas can be 1920x1080 while the picture is 1920x800.
    const SIZE design_size = m_cur_output_size;
    SIZE video_size = design_size;
    if (FAILED(m_consumer->XyGetSize(
            DirectVobSubXyOptions::SIZE_ORIGINAL_VIDEO, &video_size))
        || video_size.cx <= 0
        || video_size.cy <= 0) {
        video_size = design_size;
    }
    const RECT video_rect = { 0, 0, video_size.cx, video_size.cy };
    SubtitleDisplayLayout::BitmapFrameInput input;
    ReadBitmapCanvasInput(m_consumer, video_rect, &input);
    if (!m_canvas_cached
        || m_canvas_design_size.cx != design_size.cx
        || m_canvas_design_size.cy != design_size.cy
        || m_canvas_video_size.cx != video_size.cx
        || m_canvas_video_size.cy != video_size.cy
        || m_canvas_enabled != input.enabled
        || m_canvas_display_area.cx != input.display_area.cx
        || m_canvas_display_area.cy != input.display_area.cy
        || !EqualRect(&m_canvas_input.fullscreen_rect,
            &input.fullscreen_rect))
    {
        const SubtitleDisplayLayout::BitmapFrameResult frame =
            SubtitleDisplayLayout::ResolveBitmapFrame(input);
        // The display canvas is the publication surface. The stream's design
        // canvas is rendered at its own coordinates and then scaled into it.
        m_canvas_active = frame.canvas_active;
        m_canvas_rect = frame.canvas.uses_display_area
            ? frame.canvas.canvas_rect : video_rect;
        m_canvas_video_in_canvas = frame.video_in_canvas;
        m_canvas_frame_layout = frame.frame;
        m_canvas_fallback = int(frame.canvas.fallback_reason);
        m_canvas_input = input;
        m_canvas_enabled = input.enabled;
        m_canvas_display_area = input.display_area;
        // Report where the canvas actually got its display size from, not
        // merely whether the persisted size was non-zero.
        m_canvas_display_source = frame.canvas.uses_display_area
            ? (input.display_area.cx > 0 && input.display_area.cy > 0
                ? 1 : 2)
            : 0;
        m_canvas_design_size = design_size;
        m_canvas_video_size = video_size;
        m_canvas_cached = true;
        m_frame_geometry_dirty = true;
    }

    int filter = DirectVobSubXyOptions::BITMAP_SCALE_FILTER_BILINEAR;
    if (FAILED(m_consumer->XyGetInt(
            DirectVobSubXyOptions::INT_BITMAP_SCALE_FILTER, &filter))
        || filter < DirectVobSubXyOptions::BITMAP_SCALE_FILTER_SPLINE36
        || filter >= DirectVobSubXyOptions::BITMAP_SCALE_FILTER_COUNT) {
        filter = DirectVobSubXyOptions::BITMAP_SCALE_FILTER_BILINEAR;
    }
    if (m_scale_filter != filter) {
        m_scale_filter = filter;
        m_pSubPic = NULL;
        m_xy_sub_render_frame = NULL;
        m_frame_geometry_dirty = true;
    }

    // Presentation contract: resolved here, after the canvas decision, from
    // the consumer name cached at connection time. No per-frame COM call.
    const SubtitleDisplayLayout::ConsumerContract contract =
        SubtitleDisplayLayout::ResolveConsumerContract(
            m_consumer_identity.Family(),
            m_canvas_active, true);
    if (m_prefer_single_bitmap != contract.prefer_single_bitmap) {
        m_prefer_single_bitmap = contract.prefer_single_bitmap;
        m_frame_geometry_dirty = true;
        m_pSubPic = NULL;
        m_xy_sub_render_frame = NULL;
        m_trace_valid = false;
        m_publication_valid = false;
    }

    // The display canvas is the publication surface. Bitmap objects are
    // rendered into the stream's design canvas and scaled into it below.
    const SIZE render_size = m_canvas_active
        ? SIZE{ m_canvas_rect.right - m_canvas_rect.left,
                m_canvas_rect.bottom - m_canvas_rect.top }
        : design_size;
    const bool resample_design =
        m_canvas_active && BitmapResampler::NeedsResample(design_size, render_size);
    m_diag_design_canvas = design_size;
    m_diag_scale_canvas = render_size;
    m_diag_scale_factor =
        BitmapResampler::ResolveScaleGeometry(design_size, render_size).scale;
    if (!m_allocator
        || m_render_size.cx != render_size.cx
        || m_render_size.cy != render_size.cy)
    {
        m_render_size = render_size;
        m_pSubPic = NULL;
        m_xy_sub_render_frame = NULL;
        m_frame_geometry_dirty = true;
        hr = ResetAllocator();
        ASSERT(SUCCEEDED(hr));
    }
    if (resample_design
        && (!m_design_spd_valid
            || m_design_spd_size.cx != design_size.cx
            || m_design_spd_size.cy != design_size.cy)) {
        if (m_design_spd_valid) {
            xy_free(m_design_spd.bits);
            m_design_spd = SubPicDesc();
            m_design_spd_valid = false;
        }
        SubPicDesc source;
        source.type = MSP_RGBA;
        source.w = design_size.cx;
        source.h = design_size.cy;
        source.bpp = 32;
        source.pitch = source.w * 4;
        source.bits = reinterpret_cast<BYTE*>(
            xy_malloc((size_t)source.pitch * source.h));
        source.vidrect = CRect(CPoint(0, 0), CSize(design_size.cx, design_size.cy));
        if (source.bits) {
            m_design_spd = source;
            m_design_spd_size = design_size;
            m_design_spd_valid = true;
        }
    }
    m_scale_design_canvas = resample_design && m_design_spd_valid;
    if (resample_design && !m_design_spd_valid) {
        XY_LOG_ERROR("Failed to allocate design canvas");
        return E_FAIL;
    }
    if (m_frame_geometry_dirty) {
        // Render() returns S_OK early when m_pSubPic still covers this
        // interval. The published geometry changed, so the cached frame must
        // not be reused.
        m_pSubPic = NULL;
        m_xy_sub_render_frame = NULL;
        m_frame_geometry_dirty = false;
    }

    hr = Render( now, pos );
    if (SUCCEEDED(hr) && m_xy_sub_render_frame)
    {
        *subRenderFrame = m_xy_sub_render_frame;
        (*subRenderFrame)->AddRef();
    }

    return hr;
}

void HdmvSubtitleProviderImpl::LogBitmapFrame(
    const wchar_t* event, REFERENCE_TIME now, HRESULT hr,
    const CRect* dirty_rect, int lock_hr, int alloc_hr)
{
    int object_count = 0;
    int drawn_objects = 0;
    int skipped_objects = 0;
    m_pSub->GetAuxRenderStats(
        object_count, drawn_objects, skipped_objects);

    int bitmap_count = 0;
    ULONGLONG bitmap_id = 0;
    RECT bitmap_rects[3] = {};
    POINT bitmap_positions[3] = {};
    SIZE bitmap_sizes[3] = {};
    if (m_xy_sub_render_frame) {
        m_xy_sub_render_frame->GetBitmapCount(&bitmap_count);
        const int wanted = bitmap_count < 3 ? bitmap_count : 3;
        for (int i = 0; i < wanted; ++i) {
            m_xy_sub_render_frame->GetBitmap(
                i, i == 0 ? &bitmap_id : NULL,
                &bitmap_positions[i], &bitmap_sizes[i],
                NULL, NULL);
            bitmap_rects[i] = {
                bitmap_positions[i].x, bitmap_positions[i].y,
                bitmap_positions[i].x + bitmap_sizes[i].cx,
                bitmap_positions[i].y + bitmap_sizes[i].cy
            };
        }
    }

    // The contract is the consumer family, not this frame's geometry. MPCVR
    // stays on the texture contract even when the canvas is disabled; only
    // usesOversizedBitmap reports whether this frame published a full canvas.
    const bool mpcvr_texture = m_prefer_single_bitmap;
    const bool oversized =
        m_publication_valid
        && m_publication.kind
            == SubtitleDisplayLayout::BitmapPublication::SingleCanvas;
    RECT published_output = {};
    RECT published_clip = {};
    POINT published_offset = {};
    if (m_publication_valid) {
        published_output = m_publication.output_rect;
        published_clip = m_publication.clip_rect;
        published_offset = m_publication.bitmap_offset;
    }
    else if (!m_canvas_active) {
        const RECT video_rect = { 0, 0, m_render_size.cx, m_render_size.cy };
        published_output = video_rect;
        published_clip = video_rect;
        published_offset.x = 0;
        published_offset.y = 0;
    }
    if (m_xy_sub_render_frame) {
        RECT frame_output = {};
        RECT frame_clip = {};
        if (SUCCEEDED(m_xy_sub_render_frame->GetOutputRect(&frame_output))) {
            published_output = frame_output;
        }
        if (SUCCEEDED(m_xy_sub_render_frame->GetClipRect(&frame_clip))) {
            published_clip = frame_clip;
        }
    }
    RECT partitions[3] = {};
    int partition_count = 0;
    if (m_publication_valid) {
        partition_count = m_publication.partition_count;
        for (int i = 0; i < 3; ++i) {
            partitions[i] = m_publication.partitions[i];
        }
    }
    const double consumer_scale_x =
        m_canvas_video_size.cx > 0
            ? double(m_render_size.cx) / double(m_canvas_video_size.cx) : 0.0;
    const double consumer_scale_y =
        m_canvas_video_size.cy > 0
            ? double(m_render_size.cy) / double(m_canvas_video_size.cy) : 0.0;
    if (m_xy_sub_render_frame) {
        XySubRenderFrameWrapper* wrapper =
            dynamic_cast<XySubRenderFrameWrapper*>(m_xy_sub_render_frame.p);
        if (wrapper) {
            XySubRenderFrameWrapper::Bitmap bitmap;
            if (SUCCEEDED(wrapper->DescribeBitmap(0, &bitmap))) {
                published_offset.x = bitmap.position.x - bitmap.rect.left;
                published_offset.y = bitmap.position.y - bitmap.rect.top;
            }
        }
    }
    FrameTrace trace;
    trace.bitmap_id = bitmap_id;
    trace.bitmap_count = bitmap_count;
    for (int i = 0; i < 3; ++i) {
        trace.bitmaps[i] = bitmap_rects[i];
        trace.partitions[i] = partitions[i];
    }
    trace.dirty_rect = dirty_rect ? *dirty_rect : RECT();
    trace.buffer_size = m_render_size;
    trace.output = published_output;
    trace.clip = published_clip;
    trace.offset = published_offset;
    trace.object_count = object_count;
    trace.drawn_objects = drawn_objects;
    trace.skipped_objects = skipped_objects;
    trace.canvas_active = m_canvas_active;
    trace.video_in_canvas = m_canvas_video_in_canvas;
    trace.partition_count = partition_count;
    trace.design_canvas = m_diag_design_canvas;
    trace.scale_canvas = m_diag_scale_canvas;
    trace.scale_filter = m_scale_filter;
    trace.scale_factor = m_diag_scale_factor;
    trace.source_dirty = m_diag_source_dirty;
    trace.scaled_dirty = m_diag_scaled_dirty;
    trace.contract = mpcvr_texture ? 1 : 0;
    trace.oversized_bitmap = oversized ? 1 : 0;
    trace.consumer_scale_x_milli = int(consumer_scale_x * 1000.0 + 0.5);
    trace.consumer_scale_y_milli = int(consumer_scale_y * 1000.0 + 0.5);
    trace.fallback = m_canvas_fallback;
    trace.consumer_name =
        m_consumer_identity.HasName() ? m_consumer_identity.Name() : L"?";
    trace.consumer_version =
        m_diag_consumer_version.IsEmpty() ? L"?" : m_diag_consumer_version;
    trace.presented_bitmap = m_diag_presented_bitmap;
    trace.presented_movable = m_diag_presented_movable;
    trace.max_bitmap_count = m_diag_max_bitmap_count;
    trace.consumer_fullscreen = m_diag_consumer_fullscreen;
    trace.consumer_display_mode = m_diag_consumer_display_mode;
    trace.consumer_subtitle_target = m_diag_consumer_subtitle_target;
    if (m_trace_valid && m_last_trace == trace) {
        return;
    }
    m_trace_valid = true;
    m_last_trace = trace;

    CStringW diagnostic;
    diagnostic.Format(
        L"schema=xy-bitmap-frame/1\r\n"
        L"event=%ls rt=%I64d hr=0x%08lX lock=0x%08lX alloc=0x%08lX "
        L"designSize=%ld,%ld videoSize=%ld,%ld "
        L"displaySource=%d "
        L"displayArea=%ld,%ld canvasRect=%ld,%ld,%ld,%ld "
        L"designCanvas=%ld,%ld scaleCanvas=%ld,%ld scaleFactor=%.3f "
        L"scaleFilter=%d sourceDirty=%ld,%ld,%ld,%ld "
        L"scaledDirty=%ld,%ld,%ld,%ld "
        L"canvasActive=%d bufferSize=%ld,%ld "
        L"publishOutput=%ld,%ld,%ld,%ld "
        L"publishClip=%ld,%ld,%ld,%ld bitmapOffset=%ld,%ld "
        L"dirtyRect=%ld,%ld,%ld,%ld "
        L"objectCount=%d drawnObjects=%d skippedObjects=%d "
        L"bitmapCount=%d bitmap0=%ld,%ld,%ld,%ld "
        L"bitmap1=%ld,%ld,%ld,%ld bitmap2=%ld,%ld,%ld,%ld "
        L"videoInCanvas=%ld,%ld,%ld,%ld "
        L"partitionCount=%d "
        L"partition0=%ld,%ld,%ld,%ld "
        L"partition1=%ld,%ld,%ld,%ld "
        L"partition2=%ld,%ld,%ld,%ld "
        L"consumer=%ls consumerVersion=%ls "
        L"presentedBitmap=%d presentedMovable=%d maxBitmapCount=%d "
        L"consumerFullscreen=%ld,%ld,%ld,%ld "
        L"consumerDisplayMode=%ld,%ld "
        L"consumerSubtitleTarget=%ld,%ld,%ld,%ld "
        L"contract=%ls usesOversizedBitmap=%d "
        L"consumerScale=%.3f,%.3f "
        L"canvasFallback=%d\r\n",
        event ? event : L"frame",
        now,
        static_cast<int>(hr),
        lock_hr,
        alloc_hr,
        m_canvas_design_size.cx, m_canvas_design_size.cy,
        m_canvas_video_size.cx, m_canvas_video_size.cy,
        m_canvas_display_source,
        m_canvas_display_area.cx, m_canvas_display_area.cy,
        m_canvas_rect.left, m_canvas_rect.top,
        m_canvas_rect.right, m_canvas_rect.bottom,
        trace.design_canvas.cx, trace.design_canvas.cy,
        trace.scale_canvas.cx, trace.scale_canvas.cy,
        trace.scale_factor,
        trace.scale_filter,
        trace.source_dirty.left, trace.source_dirty.top,
        trace.source_dirty.right, trace.source_dirty.bottom,
        trace.scaled_dirty.left, trace.scaled_dirty.top,
        trace.scaled_dirty.right, trace.scaled_dirty.bottom,
        trace.canvas_active ? 1 : 0,
        trace.buffer_size.cx, trace.buffer_size.cy,
        trace.output.left, trace.output.top,
        trace.output.right, trace.output.bottom,
        trace.clip.left, trace.clip.top,
        trace.clip.right, trace.clip.bottom,
        trace.offset.x, trace.offset.y,
        trace.dirty_rect.left, trace.dirty_rect.top,
        trace.dirty_rect.right, trace.dirty_rect.bottom,
        trace.object_count, trace.drawn_objects, trace.skipped_objects,
        trace.bitmap_count,
        trace.bitmaps[0].left, trace.bitmaps[0].top,
        trace.bitmaps[0].right, trace.bitmaps[0].bottom,
        trace.bitmaps[1].left, trace.bitmaps[1].top,
        trace.bitmaps[1].right, trace.bitmaps[1].bottom,
        trace.bitmaps[2].left, trace.bitmaps[2].top,
        trace.bitmaps[2].right, trace.bitmaps[2].bottom,
        trace.video_in_canvas.left, trace.video_in_canvas.top,
        trace.video_in_canvas.right, trace.video_in_canvas.bottom,
        trace.partition_count,
        trace.partitions[0].left, trace.partitions[0].top,
        trace.partitions[0].right, trace.partitions[0].bottom,
        trace.partitions[1].left, trace.partitions[1].top,
        trace.partitions[1].right, trace.partitions[1].bottom,
        trace.partitions[2].left, trace.partitions[2].top,
        trace.partitions[2].right, trace.partitions[2].bottom,
        trace.consumer_name.GetString(),
        trace.consumer_version.GetString(),
        trace.presented_bitmap,
        trace.presented_movable,
        trace.max_bitmap_count,
        trace.consumer_fullscreen.left, trace.consumer_fullscreen.top,
        trace.consumer_fullscreen.right, trace.consumer_fullscreen.bottom,
        trace.consumer_display_mode.cx, trace.consumer_display_mode.cy,
        trace.consumer_subtitle_target.left,
        trace.consumer_subtitle_target.top,
        trace.consumer_subtitle_target.right,
        trace.consumer_subtitle_target.bottom,
        trace.contract ? L"mpcvr-texture" : L"default",
        trace.oversized_bitmap,
        double(trace.consumer_scale_x_milli) / 1000.0,
        double(trace.consumer_scale_y_milli) / 1000.0,
        trace.fallback);
    AppendXyDiagnostic(diagnostic.GetString());
}

STDMETHODIMP HdmvSubtitleProviderImpl::Invalidate( REFERENCE_TIME rtInvalidate /*= -1*/ )
{
    // Layout options and display size live on the consumer. Re-read them the
    // next time a frame is requested instead of caching the old geometry.
    m_canvas_cached = false;
    m_trace_valid = false;
    m_publication_valid = false;
    m_frame_geometry_dirty = true;
    if (m_pSubPic )
    {
        if (m_pSubPic->GetStart()>=rtInvalidate) {
            m_pSubPic = NULL;

            //fix me: important!
            m_xy_sub_render_frame = NULL;
        }
        else if (m_pSubPic->GetStop()>rtInvalidate)
        {
            m_pSubPic->SetStop(rtInvalidate);
        }
    }
    return S_OK;
}

HRESULT HdmvSubtitleProviderImpl::SetHdrConfig(const HdrColor::Config& config)
{
    HRESULT hr = m_pSub->SetHdrConfig(config);
    if (FAILED(hr)) {
        return hr;
    }

    if (!HdrColor::IsSameRevision(m_applied_hdr_config, config)) {
        Invalidate(-1);
        m_applied_hdr_config = config;
    }
    return S_OK;
}

HRESULT HdmvSubtitleProviderImpl::ResetAllocator()
{
    const int MAX_SUBPIC_QUEUE_LENGTH = 1;

    m_allocator = DEBUG_NEW CPooledSubPicAllocator(MSP_RGB32, m_render_size, MAX_SUBPIC_QUEUE_LENGTH + 1);
    ASSERT(m_allocator);

    m_allocator->SetCurSize(m_render_size);
    m_allocator->SetCurVidRect(CRect(CPoint(0,0),m_render_size));
    return S_OK;
}

HRESULT HdmvSubtitleProviderImpl::Render( REFERENCE_TIME now, POSITION pos )
{
    REFERENCE_TIME start = m_pSub->GetStart(pos);
    REFERENCE_TIME stop = m_pSub->GetStop(pos);

    if (!(start <= now && now < stop))
    {
        return S_FALSE;
    }

    if(m_pSubPic
        && m_pSubPic->GetStart() <= now && now < m_pSubPic->GetStop())
    {
        return S_OK;
    }

    // A failed allocation must not describe the geometry of the previous
    // frame. Clear the publication snapshot before any early error path.
    m_publication_valid = false;
    m_xy_sub_render_frame = NULL;

    //should always re-alloc one for the old be in used by the consumer
    m_pSubPic = NULL;
    ASSERT(m_allocator);
    const HRESULT alloc_hr = m_allocator->AllocDynamicEx(&m_pSubPic);
    if(FAILED(alloc_hr)) {
        XY_LOG_ERROR("Failed to allocate subpic");
        LogBitmapFrame(L"alloc-failed", now, alloc_hr, NULL, S_OK, alloc_hr);
        return E_FAIL;
    }

    ASSERT(m_pSubPic);

    HRESULT hr = E_FAIL;

    CRect dirty_rect(0, 0, 0, 0);
    CMemSubPic * mem_subpic = dynamic_cast<CMemSubPic*>((ISubPicEx *)m_pSubPic);
    ASSERT(mem_subpic);
    SubPicDesc spd;
    const HRESULT lock_hr = m_pSubPic->Lock(spd);
    if(SUCCEEDED(lock_hr))
    {
        // Before FlipAlphaValue the surface stores an inverted, premultiplied
        // alpha: the alpha byte 0 means opaque and 255 means transparent.
        // 0xFF000000 is therefore the transparent clear, and becomes alpha 0
        // in the published (normal) domain.
        const DWORD color = 0xFF000000;
        if (m_canvas_active && m_prefer_single_bitmap) {
            // The whole canvas is published, not just the dirty rect, and a
            // pooled surface may still hold the previous frame. Clear every
            // pixel so the untouched area becomes transparent once flipped.
            memsetd(spd.bits, color, (size_t)spd.pitch * spd.h);
        }
        if(SUCCEEDED(m_pSubPic->ClearDirtyRect(color)))
        {
            dirty_rect.SetRectEmpty();
            if (m_scale_design_canvas) {
                SubPicDesc design_spd = m_design_spd;
                memsetd(
                    design_spd.bits, 0xFF000000,
                    (size_t)design_spd.pitch * design_spd.h);
                m_pSub->Render(design_spd, now, dirty_rect);
                m_diag_source_dirty = dirty_rect;
            } else {
                m_pSub->Render(spd, now, dirty_rect);
            }

            TRACE_SUB_PROVIDER(XY_LOG_VAR_2_STR(start)<<XY_LOG_VAR_2_STR(stop));

            m_pSubPic->SetStart(start);
            m_pSubPic->SetStop(stop);

            if (m_scale_design_canvas && !dirty_rect.IsRectEmpty()) {
                RECT target_dirty = {};
                const BitmapResampler::ScaleGeometry geometry =
                    BitmapResampler::ResolveScaleGeometry(
                        m_design_spd_size, m_render_size);
                const BitmapResampler::Filter filter =
                    m_scale_filter ==
                        DirectVobSubXyOptions::BITMAP_SCALE_FILTER_BILINEAR
                        ? BitmapResampler::FILTER_BILINEAR
                        : BitmapResampler::FILTER_SPLINE36;
                if (!BitmapResampler::MapDirtyRectToTarget(
                        dirty_rect, geometry, filter, &target_dirty)
                    || !BitmapResampler::ScalePremultipliedRgba(
                        m_design_spd.bits, m_design_spd.pitch,
                        m_design_spd_size,
                        spd.bits, spd.pitch, m_render_size,
                        dirty_rect, filter)) {
                    m_xy_sub_render_frame = NULL;
                    LogBitmapFrame(
                        L"scale-failed", now, E_FAIL, &dirty_rect,
                        lock_hr, alloc_hr);
                    m_pSubPic->Unlock(NULL);
                    return E_FAIL;
                }
                m_diag_source_dirty = dirty_rect;
                m_diag_scaled_dirty = target_dirty;
                dirty_rect = target_dirty;
            }
        }
        hr = m_pSubPic->Unlock(&dirty_rect);
        ASSERT(SUCCEEDED(hr));
        if (FAILED(hr))
        {
            XY_LOG_ERROR("Failed to unlock subtitle."
                <<XY_LOG_VAR_2_STR(hr)<<XY_LOG_VAR_2_STR(dirty_rect));
        }
        if (!dirty_rect.IsRectEmpty())
        {
            hr = m_pSubPic->GetDirtyRect(&dirty_rect);
            ASSERT(SUCCEEDED(hr));
            if (!m_use_dst_alpha)
            {
                // A single published canvas must have transparent alpha over
                // its entire area; flipping only the dirty rect would leave
                // the rest of the texture in the inverted (0=opaque) domain.
                const bool whole_canvas =
                    m_canvas_active && m_prefer_single_bitmap;
                const CRect flip_rect = whole_canvas
                    ? CRect(0, 0, spd.w, spd.h) : CRect(dirty_rect);
                hr = mem_subpic->FlipAlphaValue(flip_rect);//fixme: mem_subpic.type is now MSP_RGBA_F, not MSP_RGBA
                ASSERT(SUCCEEDED(hr));
                if (FAILED(hr))
                {
                    XY_LOG_ERROR("Failed. "<<XY_LOG_VAR_2_STR(hr));
                    LogBitmapFrame(L"flip-failed", now, hr, &dirty_rect, lock_hr, alloc_hr);
                    return hr;
                }
            }
            hr = S_OK;
        }
        else
        {
            hr = S_FALSE;
        }
    }
    if (hr == S_OK)
    {
        SubtitleDisplayLayout::BitmapPublicationInput publication_input;
        publication_input.canvas_active = m_canvas_active;
        publication_input.prefer_single_bitmap = m_prefer_single_bitmap;
        publication_input.buffer_size = m_render_size;
        publication_input.frame_output_rect =
            m_canvas_frame_layout.output_rect;
        publication_input.frame_clip_rect =
            m_canvas_frame_layout.clip_rect;
        publication_input.frame_bitmap_offset =
            m_canvas_frame_layout.bitmap_offset;
        publication_input.video_in_canvas = m_canvas_video_in_canvas;
        publication_input.dirty_rect = dirty_rect;
        m_publication_valid =
            SubtitleDisplayLayout::ResolveBitmapPublication(
                publication_input, &m_publication);
        const SubtitleDisplayLayout::BitmapPublication& publication =
            m_publication;
        const bool has_publication = m_publication_valid;
        if (has_publication)
        {
            if (publication.kind
                == SubtitleDisplayLayout::BitmapPublication::SingleCanvas) {
                // MPCVR reads only GetBitmap(0) and drops the clip origin, so
                // the canvas has to be published as one zero-based texture.
                // Its own GetSourceAndDest then maps that texture onto the
                // video rect with independent X/Y scale factors.
                // The wrapper otherwise derives the bitmap rect from the
                // dirty rect; publishing only the drawn band would hand
                // MPCVR a cropped texture, so the whole canvas is declared
                // explicitly.
                HRESULT wrapper_hr = S_OK;
                m_xy_sub_render_frame = DEBUG_NEW XySubRenderFrameWrapper(
                    mem_subpic, publication.output_rect,
                    publication.clip_rect, now,
                    publication.bitmap_rects, publication.bitmap_count,
                    &wrapper_hr, publication.bitmap_offset);
                if (FAILED(wrapper_hr)) {
                    m_xy_sub_render_frame = NULL;
                    hr = wrapper_hr;
                }
            }
            else if (publication.kind
                == SubtitleDisplayLayout::BitmapPublication::Partitioned) {
                HRESULT wrapper_hr = S_OK;
                m_xy_sub_render_frame = DEBUG_NEW XySubRenderFrameWrapper(
                    mem_subpic, publication.output_rect,
                    publication.clip_rect, now, publication.bitmap_rects,
                    publication.bitmap_count, &wrapper_hr,
                    publication.bitmap_offset);
                if (FAILED(wrapper_hr)) {
                    m_xy_sub_render_frame = NULL;
                    hr = wrapper_hr;
                }
            }
            else {
                m_xy_sub_render_frame = DEBUG_NEW XySubRenderFrameWrapper(
                    mem_subpic, CRect(publication.output_rect),
                    CRect(publication.clip_rect), now, &hr,
                    publication.bitmap_offset);
            }
        }
        else
        {
            m_xy_sub_render_frame = NULL;
            dirty_rect.SetRectEmpty();
            hr = S_FALSE;
        }
        LogBitmapFrame(L"frame", now, hr, &dirty_rect, lock_hr, alloc_hr);
    }
    else
    {
        m_xy_sub_render_frame = NULL;
        LogBitmapFrame(L"render-empty", now, hr, &dirty_rect, lock_hr, alloc_hr);
        if (FAILED(hr)) {
            return hr;
        }
    }
    return hr;
}

//
// HdmvSubtitleProvider
//
HdmvSubtitleProvider::HdmvSubtitleProvider( CCritSec* pLock, SUBTITLE_TYPE nType
    , const CString& name, LCID lcid )
    : CUnknown(NAME("HdmvSubtitleProvider"), NULL)
    , m_name(name), m_lcid(lcid)
{
    switch (nType) {
    case ST_DVB :
        m_pSub = DEBUG_NEW CDVBSub();
        if (name.IsEmpty() || (name == _T("Unknown"))) m_name = "DVB Embedded Subtitle";
        m_helper = DEBUG_NEW HdmvSubtitleProviderImpl(m_pSub);
        break;
    case ST_HDMV :
        m_pSub = DEBUG_NEW CHdmvSub();
        if (name.IsEmpty() || (name == _T("Unknown"))) m_name = "HDMV Embedded Subtitle";
        m_helper = DEBUG_NEW HdmvSubtitleProviderImpl(m_pSub);
        break;
    default :
        ASSERT (FALSE);
        m_pSub = NULL;
        m_helper = NULL;
    }
    m_rtStart = 0;
}

HdmvSubtitleProvider::~HdmvSubtitleProvider( void )
{
    delete m_helper;
    delete m_pSub;
}

STDMETHODIMP HdmvSubtitleProvider::NonDelegatingQueryInterface( REFIID riid, void** ppv )
{
    CheckPointer(ppv, E_POINTER);
    *ppv = NULL;

    return
        QI(IPersist)
        QI(ISubStream)
        QI(IXySubRenderProvider)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

// IXySubRenderProvider

STDMETHODIMP HdmvSubtitleProvider::Connect( IXyOptions *consumer )
{
    CAutoLock cAutoLock(&m_csCritSec);
    return m_helper->Connect(consumer);
}

STDMETHODIMP HdmvSubtitleProvider::RequestFrame( IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now )
{
    CAutoLock cAutoLock(&m_csCritSec);
    return m_helper->RequestFrame(subRenderFrame, now - m_rtStart);
}

STDMETHODIMP HdmvSubtitleProvider::Invalidate( REFERENCE_TIME rtInvalidate /*= -1*/ )
{
    CAutoLock cAutoLock(&m_csCritSec);
    return m_helper->Invalidate(rtInvalidate);
}

// IPersist

STDMETHODIMP HdmvSubtitleProvider::GetClassID(CLSID* pClassID)
{
    return pClassID ? *pClassID = __uuidof(this), S_OK : E_POINTER;
}

// ISubStream

STDMETHODIMP_(int) HdmvSubtitleProvider::GetStreamCount()
{
    return (1);
}

STDMETHODIMP HdmvSubtitleProvider::GetStreamInfo(int iStream, WCHAR** ppName, LCID* pLCID)
{
    if(iStream != 0) {
        return E_INVALIDARG;
    }

    if(ppName) {
        *ppName = (WCHAR*)CoTaskMemAlloc((m_name.GetLength()+1)*sizeof(WCHAR));
        if(!(*ppName)) {
            return E_OUTOFMEMORY;
        }

        wcscpy_s (*ppName, m_name.GetLength()+1, CStringW(m_name));
    }

    if(pLCID) {
        *pLCID = m_lcid;
    }

    return S_OK;
}

STDMETHODIMP_(int) HdmvSubtitleProvider::GetStream()
{
    return(0);
}

STDMETHODIMP HdmvSubtitleProvider::SetStream(int iStream)
{
    return iStream == 0 ? S_OK : E_FAIL;
}

STDMETHODIMP HdmvSubtitleProvider::Reload()
{
    return S_OK;
}

HRESULT HdmvSubtitleProvider::ParseSample (IMediaSample* pSample)
{
    CAutoLock cAutoLock(&m_csCritSec);
    HRESULT		hr;

    hr = m_pSub->ParseSample (pSample);
    return hr;
}

HRESULT HdmvSubtitleProvider::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
    CAutoLock cAutoLock(&m_csCritSec);

    m_pSub->Reset();
    m_rtStart = tStart;
    return S_OK;
}

HRESULT HdmvSubtitleProvider::EndOfStream( void )
{
    CAutoLock cAutoLock(&m_csCritSec);

    m_pSub->EndOfStream();
    return S_OK;
}

HRESULT HdmvSubtitleProvider::SetYuvType( CBaseSub::ColorType colorType, CBaseSub::YuvRangeType yuvRangeType )
{
    CAutoLock cAutoLock(&m_csCritSec);
    Invalidate(-1);
    return m_pSub->SetYuvType(colorType, yuvRangeType);
}

HRESULT HdmvSubtitleProvider::SetHdrConfig( const HdrColor::Config& config )
{
    CAutoLock cAutoLock(&m_csCritSec);
    return m_helper->SetHdrConfig(config);
}

//
// SupFileSubtitleProvider
//
SupFileSubtitleProvider::SupFileSubtitleProvider()
    : CUnknown(NAME("SupSubFile"), NULL)
    , m_pSub(NULL)
    , m_helper(NULL)
{
}

SupFileSubtitleProvider::~SupFileSubtitleProvider()
{
    CAMThread::Close();

    delete m_pSub;
    delete m_helper;
}

static UINT64 ReadByte(CFile* mfile, size_t count = 1)
{
    if (count <= 0 || count >= 64) {
        return 0;
    }
    UINT64 ret = 0;
    BYTE buf[64];
    if (mfile->Read(buf, count) != count) {
        return 0;
    }
    for(size_t i = 0; i<count; i++) {
        ret = (ret << 8) + (buf[i] & 0xff);
    }

    return ret;
}

static CString StripPath(CString path)
{
    CString p = path;
    p.Replace('\\', '/');
    p = p.Mid(p.ReverseFind('/')+1);
    return (p.IsEmpty() ? path : p);
}

bool SupFileSubtitleProvider::Open(CString fn, CString subName /*= _T("")*/)
{
    if (!m_fname.IsEmpty())
    {
        return false;
    }

    CFile f;
    if (!f.Open(fn, CFile::modeRead|CFile::typeBinary|CFile::shareDenyNone)) {
        return false;
    }

    WORD sync = 0;
    sync = (WORD)ReadByte(&f, 2);
    if (sync != 'PG') {
        return false;
    }

    m_fname   = fn;
    m_subname = subName;
    if (m_subname.IsEmpty())
    {
        m_subname = fn.Left(fn.ReverseFind('.'));
        m_subname = m_subname.Mid(m_subname.ReverseFind('/')+1);
        m_subname = m_subname.Mid(m_subname.ReverseFind('.')+1);
    }

    {
        CAutoLock cAutoLock(&m_csCritSec);

        delete m_pSub;
        delete m_helper;
        m_pSub    = DEBUG_NEW CHdmvSub();
        m_helper  = DEBUG_NEW HdmvSubtitleProviderImpl(m_pSub, false);
    }

    return CAMThread::Create()==TRUE;
}

DWORD SupFileSubtitleProvider::ThreadProc()
{
    CFile f;
    if (!f.Open(m_fname, CFile::modeRead|CFile::typeBinary|CFile::shareDenyNone)) {
        return 1;
    }

    f.SeekToBegin();

    CMemFile sub;
    sub.SetLength(f.GetLength());
    sub.SeekToBegin();

    int len;
    BYTE buff[65536];
    while ((len = f.Read(buff, sizeof(buff))) > 0) {
        sub.Write(buff, len);
    }
    f.Close();
    sub.SeekToBegin();

    WORD sync              = 0;
    USHORT size            = 0;
    REFERENCE_TIME rtStart = 0;
    REFERENCE_TIME rtStop  = 0;

    CAutoLock cAutoLock(&m_csCritSec);
    while (sub.GetPosition() < (sub.GetLength() - 10)) {
        sync = (WORD)ReadByte(&sub, 2);
        if (sync == 'PG') {
            rtStart = UINT64(ReadByte(&sub, 4) * 1000 / 9);
            rtStop = UINT64(ReadByte(&sub, 4) * 1000 / 9);
            sub.Seek(1, CFile::current); // Segment type
            size = ReadByte(&sub, 2) + 3;    // Segment size
            sub.Seek(-3, CFile::current);
            sub.Read(buff, size);
            m_pSub->ParseSample(buff, size, rtStart, rtStop);
        } else {
            break;
        }
    }

    sub.Close();

    return 0;
}

STDMETHODIMP SupFileSubtitleProvider::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);
    *ppv = NULL;

    return
        QI(IPersist)
        QI(ISubStream)
        QI(IXySubRenderProvider)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

// IXySubRenderProvider

STDMETHODIMP SupFileSubtitleProvider::Connect( IXyOptions *consumer )
{
    CAutoLock cAutoLock(&m_csCritSec);
    return m_helper->Connect(consumer);
}

STDMETHODIMP SupFileSubtitleProvider::RequestFrame( IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now )
{
    CAutoLock cAutoLock(&m_csCritSec);
    return m_helper->RequestFrame(subRenderFrame, now);
}

STDMETHODIMP SupFileSubtitleProvider::Invalidate( REFERENCE_TIME rtInvalidate /*= -1*/ )
{
    CAutoLock cAutoLock(&m_csCritSec);
    return m_helper->Invalidate(rtInvalidate);
}

// IPersist

STDMETHODIMP SupFileSubtitleProvider::GetClassID(CLSID* pClassID)
{
    return pClassID ? *pClassID = __uuidof(this), S_OK : E_POINTER;
}

// ISubStream

STDMETHODIMP_(int) SupFileSubtitleProvider::GetStreamCount()
{
    return 1;
}

STDMETHODIMP SupFileSubtitleProvider::GetStreamInfo(int iStream, WCHAR** ppName, LCID* pLCID)
{
    if (iStream != 0) {
        return E_INVALIDARG;
    }

    if (ppName) {
        *ppName = (WCHAR*)CoTaskMemAlloc((m_subname.GetLength()+1)*sizeof(WCHAR));
        if (!(*ppName)) {
            return E_OUTOFMEMORY;
        }

        wcscpy_s (*ppName, m_subname.GetLength()+1, CStringW(m_subname));
    }

    if (pLCID) {
        *pLCID = 0;
    }

    return S_OK;
}

STDMETHODIMP_(int) SupFileSubtitleProvider::GetStream()
{
    return 0;
}

STDMETHODIMP SupFileSubtitleProvider::SetStream(int iStream)
{
    return iStream == 0 ? S_OK : E_FAIL;
}

STDMETHODIMP SupFileSubtitleProvider::Reload()
{
    CAutoLock cAutoLock(&m_csCritSec);
    return Open(m_fname, m_subname) ? S_OK : E_FAIL;
}

HRESULT SupFileSubtitleProvider::SetYuvType( CBaseSub::ColorType colorType, CBaseSub::YuvRangeType yuvRangeType )
{
    CAutoLock cAutoLock(&m_csCritSec);
    Invalidate(-1);
    return m_pSub->SetYuvType(colorType, yuvRangeType);
}

HRESULT SupFileSubtitleProvider::SetHdrConfig( const HdrColor::Config& config )
{
    CAutoLock cAutoLock(&m_csCritSec);
    return m_helper->SetHdrConfig(config);
}

