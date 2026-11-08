#include "stdafx.h"
#include "HdmvSub.h"
#include "DVBSub.h"
#include "hdmv_subtitle_provider.h"
#include "../subpic/XySubRenderFrameWrapper.h"
#include "../subpic/subtitle_display_layout.h"
#include "IDirectVobSubXy.h"

#if ENABLE_XY_LOG_HDMVSUB
#define TRACE_SUB_PROVIDER(msg)		XY_LOG_TRACE(msg)
#else
#define TRACE_SUB_PROVIDER(msg)
#endif

namespace
{
bool SameSize(const SIZE& first, const SIZE& second)
{
    return first.cx == second.cx && first.cy == second.cy;
}

// Bitmaps use the same display canvas as text (D44). The canvas is built from
// the video rect and the persisted display size; the stream's design
// resolution only determines how many pixels the provider has to paint.
bool ReadBitmapCanvasInput(
    IXyOptions* consumer, const RECT& video_rect,
    SubtitleDisplayLayout::BitmapFrameInput* input)
{
    if (!consumer || !input) {
        return false;
    }

    input->video_rect = video_rect;
    input->display_area.cx = 0;
    input->display_area.cy = 0;
    input->enabled = false;

    if (FAILED(consumer->XyGetBool(
            DirectVobSubXyOptions::BOOL_USE_DISPLAY_AREA_FOR_SUBTITLES,
            &input->enabled))
        || !input->enabled) {
        return false;
    }

    if (SUCCEEDED(consumer->XyGetSize(
            DirectVobSubXyOptions::SIZE_DISPLAY_AREA, &input->display_area))
        && input->display_area.cx > 0
        && input->display_area.cy > 0) {
        return true;
    }

    RECT fullscreen = {};
    if (SUCCEEDED(consumer->XyGetRect(
            DirectVobSubXyOptions::RECT_FULLSCREEN, &fullscreen))
        && fullscreen.right > fullscreen.left
        && fullscreen.bottom > fullscreen.top
        && fullscreen.right - fullscreen.left > video_rect.right - video_rect.left
        && fullscreen.bottom - fullscreen.top > video_rect.bottom - video_rect.top) {
        input->display_area.cx = fullscreen.right - fullscreen.left;
        input->display_area.cy = fullscreen.bottom - fullscreen.top;
        return true;
    }

    return false;
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
{
    m_canvas_design_size.cx = 0;
    m_canvas_design_size.cy = 0;
    m_canvas_render_size.cx = 0;
    m_canvas_render_size.cy = 0;
    m_canvas_video_size.cx = 0;
    m_canvas_video_size.cy = 0;
    m_design_size.cx = 0;
    m_design_size.cy = 0;
    m_render_size.cx = 0;
    m_render_size.cy = 0;
    m_canvas_enabled = false;
    m_canvas_display_area.cx = 0;
    m_canvas_display_area.cy = 0;
    m_canvas_publish_output = RECT();
    m_canvas_publish_clip = RECT();
    m_canvas_bitmap_offset.x = 0;
    m_canvas_bitmap_offset.y = 0;
}

HdmvSubtitleProviderImpl::~HdmvSubtitleProviderImpl( void )
{
}

STDMETHODIMP HdmvSubtitleProviderImpl::Connect( IXyOptions *consumer )
{
    HRESULT hr = NOERROR;
    if (consumer)
    {
        hr = m_consumer != consumer ? S_OK : S_FALSE;
        m_consumer = consumer;
        m_canvas_cached = false;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
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
    m_design_size = MaxTextureSize;
    if (m_design_size.cx == 0 || m_design_size.cy == 0) {
        XY_LOG_WARN("Invalid design size "<<m_design_size);
        return S_FALSE;
    }

    // Resolve the canvas once per input change. The canvas is built from the
    // active video size, not from the stream's design resolution: a PGS
    // design canvas can be 1920x1080 while the picture is 1920x800.
    SIZE video_size = m_design_size;
    if (FAILED(m_consumer->XyGetSize(
            DirectVobSubXyOptions::SIZE_ORIGINAL_VIDEO, &video_size))
        || video_size.cx <= 0
        || video_size.cy <= 0) {
        video_size = m_design_size;
    }
    const RECT video_rect = { 0, 0, video_size.cx, video_size.cy };
    if (!m_canvas_cached
        || m_canvas_design_size.cx != m_design_size.cx
        || m_canvas_design_size.cy != m_design_size.cy
        || m_canvas_video_size.cx != video_size.cx
        || m_canvas_video_size.cy != video_size.cy
        || m_canvas_enabled != m_canvas_input.enabled
        || m_canvas_display_area.cx != m_canvas_input.display_area.cx
        || m_canvas_display_area.cy != m_canvas_input.display_area.cy)
    {
        SubtitleDisplayLayout::BitmapFrameInput input;
        const bool has_canvas =
            ReadBitmapCanvasInput(m_consumer, video_rect, &input);
        const SubtitleDisplayLayout::BitmapFrameResult frame =
            SubtitleDisplayLayout::ResolveBitmapFrame(input);
        const LONG canvas_width =
            frame.frame.clip_rect.right - frame.frame.clip_rect.left;
        const LONG canvas_height =
            frame.frame.clip_rect.bottom - frame.frame.clip_rect.top;
        m_canvas_active =
            has_canvas
            && frame.canvas.uses_display_area
            && canvas_width >= m_design_size.cx
            && canvas_height >= m_design_size.cy;
        m_canvas_publish_output = frame.frame.output_rect;
        m_canvas_publish_clip = frame.frame.clip_rect;
        m_canvas_bitmap_offset = frame.frame.bitmap_offset;
        m_canvas_input = input;
        m_canvas_enabled = input.enabled;
        m_canvas_display_area = input.display_area;
        m_canvas_design_size = m_design_size;
        m_canvas_video_size = video_size;
        m_canvas_cached = true;
    }

    // The painting buffer is the canvas, not the design resolution. The
    // provider still paints objects at their stream coordinates; a larger
    // canvas simply gives them the room the display actually has.
    SIZE render_size = m_canvas_active
        ? SIZE{ m_canvas_publish_clip.right - m_canvas_publish_clip.left,
                m_canvas_publish_clip.bottom - m_canvas_publish_clip.top }
        : m_design_size;
    if (m_canvas_render_size.cx != render_size.cx
        || m_canvas_render_size.cy != render_size.cy) {
        m_canvas_render_size = render_size;
        m_render_size = render_size;
        hr = ResetAllocator();
        ASSERT(SUCCEEDED(hr));
    }
    else if (!m_allocator) {
        m_render_size = render_size;
        hr = ResetAllocator();
        ASSERT(SUCCEEDED(hr));
    }

    hr = Render( now, pos );
    if (SUCCEEDED(hr) && m_xy_sub_render_frame)
    {
        *subRenderFrame = m_xy_sub_render_frame;
        (*subRenderFrame)->AddRef();
    }

    return hr;
}

STDMETHODIMP HdmvSubtitleProviderImpl::Invalidate( REFERENCE_TIME rtInvalidate /*= -1*/ )
{
    // Layout options and display size live on the consumer. Re-read them the
    // next time a frame is requested instead of caching the old geometry.
    m_canvas_cached = false;
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

    if(m_pSubPic && m_pSubPic->GetStart() <= now && now < m_pSubPic->GetStop())
    {
        return S_OK;
    }

    //should always re-alloc one for the old be in used by the consumer
    m_pSubPic = NULL;
    ASSERT(m_allocator);
    if(FAILED(m_allocator->AllocDynamicEx(&m_pSubPic))) {
        XY_LOG_ERROR("Failed to allocate subpic");
        return E_FAIL;
    }

    ASSERT(m_pSubPic);

    HRESULT hr = E_FAIL;

    CMemSubPic * mem_subpic = dynamic_cast<CMemSubPic*>((ISubPicEx *)m_pSubPic);
    ASSERT(mem_subpic);
    SubPicDesc spd;
    if(SUCCEEDED(m_pSubPic->Lock(spd)))
    {
        CRect dirty_rect;
        DWORD color = 0xFF000000;
        if(SUCCEEDED(m_pSubPic->ClearDirtyRect(color)))
        {
            m_pSub->Render(spd, now, dirty_rect);

            TRACE_SUB_PROVIDER(XY_LOG_VAR_2_STR(start)<<XY_LOG_VAR_2_STR(stop));

            m_pSubPic->SetStart(start);
            m_pSubPic->SetStop(stop);
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
                hr = mem_subpic->FlipAlphaValue(dirty_rect);//fixme: mem_subpic.type is now MSP_RGBA_F, not MSP_RGBA
                ASSERT(SUCCEEDED(hr));
                if (FAILED(hr))
                {
                    XY_LOG_ERROR("Failed. "<<XY_LOG_VAR_2_STR(hr));
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
        CRect video_rect(CPoint(0,0), m_render_size);
        if (m_canvas_active)
        {
            m_xy_sub_render_frame = DEBUG_NEW XySubRenderFrameWrapper(
                mem_subpic, m_canvas_publish_output, m_canvas_publish_clip,
                now, &hr, m_canvas_bitmap_offset);
        }
        else
        {
            m_xy_sub_render_frame = DEBUG_NEW XySubRenderFrameWrapper(
                mem_subpic, video_rect, video_rect, now, &hr);
        }
    }
    else
    {
        m_xy_sub_render_frame = NULL;
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

