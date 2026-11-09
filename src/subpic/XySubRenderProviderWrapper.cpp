#include "stdafx.h"
#include "XySubRenderProviderWrapper.h"
#include "IDirectVobSubXy.h"
#include "diagnostic_log.h"

#if ENABLE_XY_LOG_RENDERER_REQUEST2
#  define TRACE_RENDERER_REQUEST(msg) XY_LOG_TRACE(msg)
#  define TRACE_RENDERER_REQUEST_TIMING(msg) XY_AUTO_TIMING(msg)
#else
#  define TRACE_RENDERER_REQUEST(msg)
#  define TRACE_RENDERER_REQUEST_TIMING(msg)
#endif

namespace
{
SubtitleDisplayLayout::CanvasResult ResolveConsumerCanvas(
    IXyOptions* consumer, const RECT& video_output_rect,
    const RECT& subtitle_target_rect)
{
    SubtitleDisplayLayout::CanvasInput input;
    input.video_output_rect = video_output_rect;
    input.subtitle_target_rect = subtitle_target_rect;

    if (!consumer
        || FAILED(consumer->XyGetBool(
            DirectVobSubXyOptions::BOOL_USE_DISPLAY_AREA_FOR_SUBTITLES,
            &input.enabled))
        || !input.enabled) {
        {
            CStringW diag;
            diag.Format(
                L"schema=xy-canvas-input/1\r\nevent=disabled enabled=%d "
                L"videoRect=%ld,%ld,%ld,%ld targetRect=%ld,%ld,%ld,%ld\r\n",
                input.enabled ? 1 : 0,
                video_output_rect.left, video_output_rect.top,
                video_output_rect.right, video_output_rect.bottom,
                subtitle_target_rect.left, subtitle_target_rect.top,
                subtitle_target_rect.right, subtitle_target_rect.bottom);
            AppendXyDiagnostic(diag.GetString());
        }
        return SubtitleDisplayLayout::ResolveCanvas(input);
    }

    if (FAILED(consumer->XyGetInt(
            DirectVobSubXyOptions::INT_SUBTITLE_DISPLAY_ASPECT,
            &input.aspect_mode))
        || input.aspect_mode == DirectVobSubXyOptions::SUBTITLE_DISPLAY_ASPECT_OFF) {
        {
            CStringW diag;
            diag.Format(
                L"schema=xy-canvas-input/1\r\nevent=aspect-off aspect=%d "
                L"videoRect=%ld,%ld,%ld,%ld targetRect=%ld,%ld,%ld,%ld\r\n",
                input.aspect_mode,
                video_output_rect.left, video_output_rect.top,
                video_output_rect.right, video_output_rect.bottom,
                subtitle_target_rect.left, subtitle_target_rect.top,
                subtitle_target_rect.right, subtitle_target_rect.bottom);
            AppendXyDiagnostic(diag.GetString());
        }
        return SubtitleDisplayLayout::ResolveCanvas(input);
    }

    // Exactly one decision: the display size. madVR publishes the real monitor
    // rect in fullscreenRect; MPC VR and windowed playback fall back to the
    // size persisted by the property page. No window or monitor query happens
    // here, so this is safe on the playback thread.
    SIZE display_area = {};
    bool has_display_area =
        SUCCEEDED(consumer->XyGetSize(
            DirectVobSubXyOptions::SIZE_DISPLAY_AREA, &display_area))
        && display_area.cx > 0
        && display_area.cy > 0;

    if (!has_display_area) {
        RECT fullscreen = {};
        if (SUCCEEDED(consumer->XyGetRect(
                DirectVobSubXyOptions::RECT_FULLSCREEN, &fullscreen))
            && SubtitleDisplayLayout::IsValidRect(fullscreen)
            && SubtitleDisplayLayout::ContainsRect(
                fullscreen, video_output_rect)
            && (fullscreen.right - fullscreen.left
                    > video_output_rect.right - video_output_rect.left
                || fullscreen.bottom - fullscreen.top
                    > video_output_rect.bottom - video_output_rect.top)) {
            display_area.cx = fullscreen.right - fullscreen.left;
            display_area.cy = fullscreen.bottom - fullscreen.top;
            has_display_area = true;
        }
    }

    if (!has_display_area) {
        return SubtitleDisplayLayout::ResolveCanvas(input);
    }

    // A display size is known, so lay out on a canvas of its ratio centred on
    // the video instead of trusting consumer rects that may all be source
    // pixels.
    return SubtitleDisplayLayout::ResolveCanvasForAspect(
        true, input.aspect_mode, true, video_output_rect,
        display_area.cx, display_area.cy);
}
}

XySubRenderProviderWrapper::XySubRenderProviderWrapper( ISubPicProviderEx *provider
    , HRESULT* phr/*=NULL*/ )
    : CUnknown(NAME("XySubRenderProviderWrapper"), NULL, phr)
    , m_provider(provider)
    , m_consumer(NULL)
    , m_use_dst_alpha(false)
{
    HRESULT hr = NOERROR;
    if (!provider)
    {
        hr = E_INVALIDARG;
    }

    if (phr)
    {
        *phr = hr;
    }
}

STDMETHODIMP XySubRenderProviderWrapper::NonDelegatingQueryInterface( REFIID riid, void** ppv )
{
    return
        QI(IXySubRenderProvider)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP XySubRenderProviderWrapper::Connect( IXyOptions *consumer )
{
    HRESULT hr = NOERROR;
    if (consumer)
    {
        hr = m_consumer != consumer ? S_OK : S_FALSE;
        m_consumer = consumer;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

STDMETHODIMP XySubRenderProviderWrapper::RequestFrame( IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now  )
{
    ASSERT(m_consumer);
    double fps;
    CheckPointer(subRenderFrame, E_POINTER);
    *subRenderFrame = NULL;

    HRESULT hr = m_consumer->XyGetDouble(DirectVobSubXyOptions::DOUBLE_FPS, &fps);
    if (FAILED(hr))
    {
        XY_LOG_ERROR("Failed to get fps. "<<XY_LOG_VAR_2_STR(hr));
        return hr;
    }

    CRect output_rect, subtitle_target_rect;
    CSize original_video_size;
    bool use_dst_alpha = false;
    ASSERT(m_consumer);
    hr = m_consumer->XyGetSize(DirectVobSubXyOptions::SIZE_ORIGINAL_VIDEO, &original_video_size);
    ASSERT(SUCCEEDED(hr));
    const RECT video_rect = {
        0, 0, original_video_size.cx, original_video_size.cy
    };
    hr = m_consumer->XyGetBool(DirectVobSubXyOptions::BOOL_SUB_FRAME_USE_DST_ALPHA, &use_dst_alpha);

    if (m_original_video_size!=original_video_size
        || m_use_dst_alpha!=use_dst_alpha)
    {
        if (m_use_dst_alpha==use_dst_alpha)
        {
            XY_LOG_WARN("Original video size changed from "<<m_original_video_size<<" to "<<original_video_size);
        }
        else
        {
            XY_LOG_INFO("'Use dst alpha' option changed from "<<m_use_dst_alpha<<" to "<<use_dst_alpha);
        }
        Invalidate();
        m_original_video_size = original_video_size;
        m_use_dst_alpha = use_dst_alpha;
    }
    if(!m_pSubPic)
    {
        if (!m_allocator)
        {
            ResetAllocator();
        }
    }

    POSITION pos = m_provider->GetStartPosition(now, fps);
    if (!pos)
    {
        return S_FALSE;
    }
    REFERENCE_TIME start = m_provider->GetStart(pos, fps);
    REFERENCE_TIME stop = m_provider->GetStop(pos, fps); //fixme: have to get start stop twice
    if (!(start <= now && now < stop))
    {
        return S_FALSE;
    }

    hr = Render( now, pos, fps );
    if (FAILED(hr))
    {
        return hr;
    }
    if (m_xy_sub_render_frame)
    {
        *subRenderFrame = m_xy_sub_render_frame;
        (*subRenderFrame)->AddRef();
    }

    return hr;
}

HRESULT XySubRenderProviderWrapper::Render( REFERENCE_TIME now, POSITION pos, double fps )
{
    if(m_pSubPic && m_pSubPic->GetStart() <= now && now < m_pSubPic->GetStop())
    {
        return S_OK;
    }
    HRESULT hr = E_FAIL;

    //should always re-alloc one for the old be in used by the consumer
    m_pSubPic = NULL;
    ASSERT(m_allocator);
    if(FAILED(m_allocator->AllocDynamicEx(&m_pSubPic))) {
        XY_LOG_ERROR("Failed to allocate subpic");
        return E_FAIL;
    }
    ASSERT(m_pSubPic);

    if(FAILED(m_provider->Lock())) {
        return hr;
    }

    CMemSubPic * mem_subpic = dynamic_cast<CMemSubPic*>((ISubPicEx *)m_pSubPic);
    ASSERT(mem_subpic);
    SubPicDesc spd;
    if(SUCCEEDED(m_pSubPic->Lock(spd)))
    {
        CAtlList<CRect> rectList;
        DWORD color = 0xFF000000;
        if(SUCCEEDED(m_pSubPic->ClearDirtyRect(color)))
        {
            hr = m_provider->RenderEx(spd, now, fps, rectList);

            if (!m_provider->IsAnimated(pos))
            {
                REFERENCE_TIME start = m_provider->GetStart(pos, fps);
                REFERENCE_TIME stop = m_provider->GetStop(pos, fps);
                XY_LOG_TRACE(XY_LOG_VAR_2_STR(start)<<XY_LOG_VAR_2_STR(stop));
                m_pSubPic->SetStart(start);
                m_pSubPic->SetStop(stop);
            }
            else
            {
                m_pSubPic->SetStart(now);
                m_pSubPic->SetStop(now+1);
            }
        }
        else
        {
            rectList.AddHead(CRect(CPoint(0,0),m_original_video_size));
            XY_LOG_ERROR("Failed to clear subpic!");
        }
        hr = m_pSubPic->UnlockEx(&rectList);
        ASSERT(SUCCEEDED(hr));
        if (FAILED(hr))
        {
            XY_LOG_ERROR("Failed to unlock subpic. "<<XY_LOG_VAR_2_STR(hr));
        }
        CRect dirty_rect;
        hr = m_pSubPic->GetDirtyRect(&dirty_rect);
        ASSERT(SUCCEEDED(hr));
        if (!dirty_rect.IsRectEmpty())
        {
            if (!m_use_dst_alpha)
            {
                hr = mem_subpic->FlipAlphaValue(dirty_rect);//fixme: mem_subpic.type is now MSP_RGBA_F, not MSP_RGBA
                ASSERT(SUCCEEDED(hr));
                if (FAILED(hr))
                {
                    XY_LOG_ERROR("Failed. "<<XY_LOG_VAR_2_STR(hr));
                    return hr;
                }
            }
        }
        else
        {
            hr = S_FALSE;
        }
    }

    m_provider->Unlock();
    if (hr == S_OK)
    {
        CRect output_rect(CPoint(0,0), m_original_video_size);
        CRect clip_rect = output_rect;
        m_xy_sub_render_frame = DEBUG_NEW XySubRenderFrameWrapper(
            mem_subpic, output_rect, clip_rect, now, &hr);
    }
    else
    {
        m_xy_sub_render_frame = NULL;
    }
    return hr;
}

HRESULT XySubRenderProviderWrapper::ResetAllocator()
{
    const int MAX_SUBPIC_QUEUE_LENGTH = 1;

    m_allocator = DEBUG_NEW CPooledSubPicAllocator(MSP_RGB32, m_original_video_size, MAX_SUBPIC_QUEUE_LENGTH + 1);
    ASSERT(m_allocator);

    m_allocator->SetCurSize(m_original_video_size);
    m_allocator->SetCurVidRect(CRect(CPoint(0,0),m_original_video_size));
    return S_OK;
}

STDMETHODIMP XySubRenderProviderWrapper::Invalidate( REFERENCE_TIME rtInvalidate /*= -1*/ )
{
    if (m_pSubPic )
    {
        if (m_pSubPic->GetStart()>=rtInvalidate) {
            m_pSubPic = NULL;

            //fix me: important!
            m_xy_sub_render_frame = NULL;
            m_allocator = NULL;
        }
        else if (m_pSubPic->GetStop()>rtInvalidate)
        {
            m_pSubPic->SetStop(rtInvalidate);
        }
    }

    return S_OK;
}

//
// XySubRenderProviderWrapper2
//
XySubRenderProviderWrapper2::XySubRenderProviderWrapper2( ISubPicProviderEx2 *provider
    , HRESULT* phr/*=NULL*/ )
    : CUnknown(NAME("XySubRenderProviderWrapper2"), NULL, phr)
    , m_provider(provider)
    , m_consumer(NULL)
    , m_start(0), m_stop(0)
    , m_fps(0)
    , m_max_bitmap_count2(0)
    , m_use_dst_alpha(0)
    , m_render_to_original_video_size(0)
    , m_use_display_canvas(false)
    , m_canvas_cached(false)
{
    m_canvas_video_rect = RECT();
    m_canvas_target_rect = RECT();
    m_canvas_video_size.cx = 0;
    m_canvas_video_size.cy = 0;
    HRESULT hr = NOERROR;
    if (!provider)
    {
        hr = E_INVALIDARG;
    }

    if (phr)
    {
        *phr = hr;
    }
}

STDMETHODIMP XySubRenderProviderWrapper2::NonDelegatingQueryInterface( REFIID riid, void** ppv )
{
    return
        QI(IXySubRenderProvider)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP XySubRenderProviderWrapper2::Connect( IXyOptions *consumer )
{
    HRESULT hr = NOERROR;
    if (consumer)
    {
        hr = m_consumer != consumer ? S_OK : S_FALSE;
        m_consumer = consumer;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

STDMETHODIMP XySubRenderProviderWrapper2::RequestFrame( IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now )
{
    ASSERT(m_consumer);
    double fps;
    CheckPointer(subRenderFrame, E_POINTER);
    *subRenderFrame = NULL;

    HRESULT hr = m_consumer->XyGetDouble(DirectVobSubXyOptions::DOUBLE_FPS, &fps);
    if (FAILED(hr))
    {
        XY_LOG_ERROR("Failed to get fps. "<<XY_LOG_VAR_2_STR(hr));
        return hr;
    }
    m_fps = fps;//fix me: invalidate

    CRect output_rect, subtitle_target_rect;
    CSize original_video_size;
    bool use_dst_alpha = false;
    bool combine_bitmap = false;
    bool render_to_original_video_size = false;

    ASSERT(m_consumer);
    hr = m_consumer->XyGetRect(DirectVobSubXyOptions::RECT_VIDEO_OUTPUT, &output_rect);
    ASSERT(SUCCEEDED(hr));
    hr = m_consumer->XyGetRect(DirectVobSubXyOptions::RECT_SUBTITLE_TARGET, &subtitle_target_rect);
    ASSERT(SUCCEEDED(hr));
    ASSERT(output_rect.Width()<=subtitle_target_rect.Width() && output_rect.Height()<=subtitle_target_rect.Height());
    hr = m_consumer->XyGetSize(DirectVobSubXyOptions::SIZE_LAYOUT_WITH, &original_video_size);
    ASSERT(SUCCEEDED(hr));
    // The canvas only depends on the video rect, the consumer target and the
    // injected display size. All three are stable for a whole playback
    // session, so resolve once instead of on every frame request.
    const bool canvas_inputs_changed =
        !m_canvas_cached
        || !EqualRect(&m_canvas_video_rect, &output_rect)
        || !EqualRect(&m_canvas_target_rect, &subtitle_target_rect);
    if (canvas_inputs_changed)
    {
        m_canvas_value = ResolveConsumerCanvas(
            m_consumer, output_rect, subtitle_target_rect);
        m_canvas_video_rect = output_rect;
        m_canvas_target_rect = subtitle_target_rect;
        m_canvas_cached = true;
    }
    const SubtitleDisplayLayout::CanvasResult& display_canvas =
        m_canvas_value;
    hr = m_consumer->XyGetInt(DirectVobSubXyOptions::INT_MAX_BITMAP_COUNT2, &m_max_bitmap_count2);
    ASSERT(SUCCEEDED(hr));
    hr = m_consumer->XyGetBool(DirectVobSubXyOptions::BOOL_SUB_FRAME_USE_DST_ALPHA, &use_dst_alpha);
    hr = m_consumer->XyGetBool(DirectVobSubXyOptions::BOOL_RENDER_TO_ORIGINAL_VIDEO_SIZE, &render_to_original_video_size);

    bool use_display_canvas = display_canvas.uses_display_area;
    CRect render_video_rect = output_rect;
    CRect render_target_rect = subtitle_target_rect;
    CSize render_video_size = original_video_size;
    SubtitleDisplayLayout::FrameLayout frame_layout;

    if (render_to_original_video_size)
    {
        output_rect = CRect(CPoint(), original_video_size);
        subtitle_target_rect = output_rect;
        render_video_rect = output_rect;
        render_target_rect = output_rect;
        use_display_canvas = false;
    }
    else if (use_display_canvas)
    {
        // Render into a zero-based canvas the size of the display target:
        // libass, CSRI and the legacy renderer then share one layout space
        // and the font scale follows the display height.
        render_video_rect = CRect(CPoint(0, 0), display_canvas.layout_size);
        render_target_rect = render_video_rect;
        render_video_size = display_canvas.layout_size;
        frame_layout = SubtitleDisplayLayout::BuildFrameLayout(
            output_rect, display_canvas.canvas_rect);
    }

    m_render_to_original_video_size = render_to_original_video_size;

    bool should_invalidate = false;
    bool should_invalidate_allocator = false;
    if (m_original_video_size!=original_video_size
        || m_use_dst_alpha!=use_dst_alpha
        || m_output_rect!=output_rect
        || m_subtitle_target_rect!=subtitle_target_rect
        || m_render_video_rect!=render_video_rect
        || m_render_target_rect!=render_target_rect
        || m_render_video_size!=render_video_size
        || m_use_display_canvas!=use_display_canvas
        || !EqualRect(&m_frame_layout.output_rect, &frame_layout.output_rect)
        || !EqualRect(&m_frame_layout.clip_rect, &frame_layout.clip_rect))
    {
        should_invalidate = true;
        should_invalidate_allocator =
            (m_render_target_rect!=render_target_rect)==TRUE;

        m_original_video_size  = original_video_size;
        m_use_dst_alpha        = use_dst_alpha;
        m_output_rect          = output_rect;
        m_subtitle_target_rect = subtitle_target_rect;
        m_render_video_rect    = render_video_rect;
        m_render_target_rect   = render_target_rect;
        m_render_video_size    = render_video_size;
        m_use_display_canvas   = use_display_canvas;
        m_frame_layout         = frame_layout;
    }

    if (m_xy_sub_render_frame)
    {
        int count = 0;
        hr = m_xy_sub_render_frame->GetBitmapCount(&count);
        should_invalidate = (count>m_max_bitmap_count2);
    }
    if (should_invalidate)
    {
        XY_LOG_INFO("Output rects or max_bitmap_count or alpha type changed.");
        Invalidate();
    }
    if (should_invalidate_allocator)
    {
        CSize max_size = m_render_target_rect.Size();

        m_allocator = DEBUG_NEW CPooledSubPicAllocator(MSP_RGB32, max_size,2);
        ASSERT(m_allocator);

        m_allocator->SetCurSize(max_size);
        m_allocator->SetCurVidRect(CRect(CPoint(0,0),max_size));
    }

    POSITION pos = m_provider->GetStartPosition(now, fps);
    if (!pos)
    {
        XY_LOG_TRACE("No subtitles at "<<XY_LOG_VAR_2_STR(now));
        return S_FALSE;
    }
    REFERENCE_TIME start = m_provider->GetStart(pos, fps);
    REFERENCE_TIME stop = m_provider->GetStop(pos, fps); //fixme: have to get start stop twice
    if (!(start <= now && now < stop))
    {
        XY_LOG_TRACE("No subtitles at "<<XY_LOG_VAR_2_STR(now));
        return S_FALSE;
    }

    hr = Render( now, pos );
    if (FAILED(hr))
    {
        return hr;
    }
    CComPtr<IXySubRenderFrame> published_frame = m_xy_sub_render_frame;
    if (published_frame)
    {
        *subRenderFrame = published_frame;
        (*subRenderFrame)->AddRef();
    }
    return hr;
}

STDMETHODIMP XySubRenderProviderWrapper2::Invalidate( REFERENCE_TIME rtInvalidate /*= -1*/ )
{
    if (m_start>=rtInvalidate)
    {
        m_xy_sub_render_frame = NULL;
        m_start = m_stop = 0;
    }
    else if (m_stop>rtInvalidate)
    {
        m_stop = rtInvalidate;
    }


    return S_OK;
}

HRESULT XySubRenderProviderWrapper2::Render( REFERENCE_TIME now, POSITION pos )
{
    if(m_start <= now && now < m_stop && m_xy_sub_render_frame)
    {
        return S_FALSE;
    }

    m_xy_sub_render_frame = NULL;
    HRESULT hr = E_FAIL;

    if(FAILED(m_provider->Lock())) {
        return hr;
    }

    int spd_type = m_use_dst_alpha ? MSP_RGBA : MSP_RGBA_F;
    CComQIPtr<ISubPicProviderEx3> provider3 = m_provider;
    if (provider3)
        provider3->SetMaxBitmapCount(m_max_bitmap_count2);
    hr = m_provider->RenderEx(&m_xy_sub_render_frame, spd_type, 
        m_render_video_rect, m_render_target_rect,
        m_render_video_size, now, m_fps);
    ASSERT(SUCCEEDED(hr));

    if (m_use_display_canvas && m_xy_sub_render_frame)
    {
        // Write the published geometry straight into the rendered frame. A
        // per-frame wrapper allocation was the source of the fullscreen
        // stutter and crashes.
        XySubRenderFrame* xy_frame =
            dynamic_cast<XySubRenderFrame*>(m_xy_sub_render_frame.p);
        if (xy_frame)
        {
            xy_frame->m_output_rect = m_frame_layout.output_rect;
            xy_frame->m_clip_rect = m_frame_layout.clip_rect;
            xy_frame->m_bitmap_offset_x = m_frame_layout.bitmap_offset.x;
            xy_frame->m_bitmap_offset_y = m_frame_layout.bitmap_offset.y;
        }
    }

    bool should_combine = false;
    if (m_xy_sub_render_frame)
    {
        int count = 0;
        hr = m_xy_sub_render_frame->GetBitmapCount(&count);
        ASSERT(SUCCEEDED(hr));
        should_combine = count > m_max_bitmap_count2;
    }
    if (should_combine)
    {
        hr = CombineBitmap(now);
        ASSERT(SUCCEEDED(hr));
        TRACE_RENDERER_REQUEST("Finished combine bitmap");
    }

    if (m_xy_sub_render_frame)
    {
        RECT out_rect = {};
        RECT clip_rect = {};
        POINT first_pos = {};
        SIZE first_size = {};
        int count = 0;
        if (SUCCEEDED(m_xy_sub_render_frame->GetOutputRect(&out_rect))
            && SUCCEEDED(m_xy_sub_render_frame->GetClipRect(&clip_rect))
            && SUCCEEDED(m_xy_sub_render_frame->GetBitmapCount(&count))
            && count > 0
            && SUCCEEDED(m_xy_sub_render_frame->GetBitmap(
                0, NULL, &first_pos, &first_size, NULL, NULL))) {
            CStringW diagnostic;
            diagnostic.Format(
                L"schema=xy-text-frame/1\r\n"
                L"event=frame canvas=%d maxBitmap=%d bitmapCount=%d "
                L"videoRect=%ld,%ld,%ld,%ld canvasRect=%ld,%ld,%ld,%ld "
                L"targetRect=%ld,%ld,%ld,%ld "
                L"output=%ld,%ld,%ld,%ld clip=%ld,%ld,%ld,%ld "
                L"bitmap0=%ld,%ld,%ld,%ld\r\n",
                m_use_display_canvas ? 1 : 0,
                m_max_bitmap_count2,
                count,
                m_output_rect.left, m_output_rect.top,
                m_output_rect.right, m_output_rect.bottom,
                m_render_target_rect.left, m_render_target_rect.top,
                m_render_target_rect.right, m_render_target_rect.bottom,
                m_subtitle_target_rect.left, m_subtitle_target_rect.top,
                m_subtitle_target_rect.right, m_subtitle_target_rect.bottom,
                out_rect.left, out_rect.top, out_rect.right, out_rect.bottom,
                clip_rect.left, clip_rect.top, clip_rect.right, clip_rect.bottom,
                first_pos.x, first_pos.y,
                first_pos.x + first_size.cx, first_pos.y + first_size.cy);
            AppendXyDiagnostic(diagnostic.GetString());
        }
    }

    if (!m_provider->IsAnimated(pos))
    {
        m_start = m_provider->GetStart(pos, m_fps);
        m_stop = m_provider->GetStop(pos, m_fps);
    }
    else
    {
        m_start = now;
        m_stop = now+1;
    }
    XY_LOG_TRACE(XY_LOG_VAR_2_STR(m_start)<<XY_LOG_VAR_2_STR(m_stop));

    m_provider->Unlock();
    return hr;
}

HRESULT XySubRenderProviderWrapper2::CombineBitmap(REFERENCE_TIME now)
{
    static int s_combined_bitmap_id = 0x10000000;//fixme: important! Uncombined bitmap id MUST < 0x10000000.

    XY_LOG_TRACE(now);
    HRESULT hr = NOERROR;
    if (m_xy_sub_render_frame)
    {
        m_subpic = NULL;
        hr = m_allocator->AllocDynamicEx(&m_subpic);
        if(FAILED(hr) || !m_subpic) {
            XY_LOG_FATAL("Failed to allocate subpic. "<<XY_LOG_VAR_2_STR(hr));
            return hr;
        }

        int count = 0;
        hr = m_xy_sub_render_frame->GetBitmapCount(&count);
        ASSERT(SUCCEEDED(hr));
        if (count==1)
        {
            return S_OK;
        }
        SubPicDesc spd;
        hr = m_subpic->Lock(spd);
        if (FAILED(hr))
        {
            XY_LOG_ERROR("Failed to lock spd. "<<XY_LOG_VAR_2_STR(hr));
            return hr;
        }
        DWORD color = 0x00000000;
        hr = m_subpic->ClearDirtyRect(color);
        if(FAILED(hr))
        {
            XY_LOG_ERROR("Failed to clear dirty rect. "<<XY_LOG_VAR_2_STR(hr));
            return hr;
        }
        // The canvas lives outside the video rect: clipping the combined
        // dirty rect (or publishing the render rects) to m_render_target_rect
        // discards everything in the letterbox bars. Keep the union intact
        // and publish the canvas-space geometry instead.
        const CRect canvas_bounds = m_use_display_canvas
            ? CRect(CPoint(0, 0), m_render_target_rect.Size())
            : m_render_target_rect;
        CRect dirty_rect;
        for (int i=0;i<count;i++)
        {
            POINT pos;
            SIZE size;
            LPCVOID pixels;
            int pitch;
            hr = m_xy_sub_render_frame->GetBitmap(i, NULL, &pos, &size, &pixels, &pitch );
            if (FAILED(hr))
            {
                XY_LOG_ERROR("Failed to get bitmap. "<<XY_LOG_VAR_2_STR(hr));
                return hr;
            }
            ASSERT(SUCCEEDED(hr));
            // GetBitmap() already reports positions with the canvas offset
            // applied. The merged frame is built from the buffer itself, so
            // undo the offset when blitting and re-apply it via the wrapper.
            POINT local_pos = pos;
            if (m_use_display_canvas) {
                local_pos.x -= m_frame_layout.bitmap_offset.x;
                local_pos.y -= m_frame_layout.bitmap_offset.y;
            }
            dirty_rect |= CRect(local_pos, size);
            XyBitmap::BltPack(spd, local_pos, size, pixels, pitch);
        }
        dirty_rect &= canvas_bounds;
        hr = m_subpic->Unlock(&dirty_rect);
        if (FAILED(hr))
        {
            XY_LOG_ERROR("Failed to unlock. "<<XY_LOG_VAR_2_STR(dirty_rect)<<XY_LOG_VAR_2_STR(hr));
            return hr;
        }
        hr = m_subpic->GetDirtyRect(&dirty_rect);
        ASSERT(SUCCEEDED(hr));
        CMemSubPic * mem_subpic = dynamic_cast<CMemSubPic*>((ISubPicEx *)m_subpic);
        ASSERT(mem_subpic);

        const CRect output_rect = m_use_display_canvas
            ? m_frame_layout.output_rect : m_render_video_rect;
        const CRect clip_rect = m_use_display_canvas
            ? m_frame_layout.clip_rect : m_render_target_rect;
        m_xy_sub_render_frame = DEBUG_NEW XySubRenderFrameWrapper(
            mem_subpic, output_rect, clip_rect,
            s_combined_bitmap_id++, &hr,
            m_use_display_canvas ? m_frame_layout.bitmap_offset : POINT());
        return hr;
    }
    return hr;
}
