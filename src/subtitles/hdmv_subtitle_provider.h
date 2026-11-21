#pragma once

#include "Rasterizer.h"
#include "../SubPic/SubPicProviderImpl.h"
#include "../subpic/bitmap_resampler.h"
#include "../subpic/subtitle_display_layout.h"
#include "HdmvSub.h"
#include "BaseSub.h"

class HdmvSubtitleProviderImpl
{
public:
    HdmvSubtitleProviderImpl (CBaseSub* pSub, bool cleanOld = true);
    ~HdmvSubtitleProviderImpl(void);

    STDMETHODIMP Connect     (IXyOptions *consumer);
    STDMETHODIMP RequestFrame(IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now);
    STDMETHODIMP Invalidate  (REFERENCE_TIME rtInvalidate = -1);
    HRESULT SetHdrConfig(const HdrColor::Config& config);
private:
    struct FrameTrace
    {
        ULONGLONG bitmap_id;
        int bitmap_count;
        RECT bitmaps[3];
        RECT dirty_rect;
        SIZE buffer_size;
        RECT output;
        RECT clip;
        POINT offset;
        int object_count;
        int drawn_objects;
        int skipped_objects;
        bool canvas_active;
        RECT video_in_canvas;
        int partition_count;
        RECT partitions[3];
        SIZE design_canvas;
        SIZE scale_canvas;
        int scale_filter;
        double scale_factor;
        RECT source_dirty;
        RECT scaled_dirty;
        int contract;
        int oversized_bitmap;
        int consumer_scale_x_milli;
        int consumer_scale_y_milli;
        int fallback;
        CStringW consumer_name;
        CStringW consumer_version;
        int presented_bitmap;
        int presented_movable;
        int max_bitmap_count;
        RECT consumer_fullscreen;
        SIZE consumer_display_mode;
        RECT consumer_subtitle_target;

        FrameTrace();
        bool operator==(const FrameTrace& other) const;
    };

    HRESULT ResetAllocator();
    HRESULT Render(REFERENCE_TIME now, POSITION pos);
    void RefreshConsumerDiagnostics();
    void LogBitmapFrame(
        const wchar_t* event, REFERENCE_TIME now, HRESULT hr,
        const CRect* dirty_rect, int lock_hr, int alloc_hr);
private :
    CBaseSub*                   m_pSub;
    bool                        m_cleanOld;

    IXyOptions                 *m_consumer;
    CSize                       m_cur_output_size;
    CSize                       m_render_size;
    bool m_use_dst_alpha;
    HdrColor::Config m_applied_hdr_config;

    // Cached canvas geometry. The bitmap buffer is the complete display
    // canvas; stream coordinates stay unchanged and are clipped to the
    // buffer. Recomputed only when an input changes.
    bool m_canvas_cached;
    SIZE m_canvas_design_size;
    SIZE m_canvas_video_size;
    SubtitleDisplayLayout::BitmapFrameInput m_canvas_input;
    bool m_canvas_enabled;
    SIZE m_canvas_display_area;
    // 0 = none, 1 = persisted SIZE_DISPLAY_AREA, 2 = consumer fullscreen rect.
    int m_canvas_display_source;
    RECT m_canvas_rect;
    RECT m_canvas_video_in_canvas;
    SubtitleDisplayLayout::FrameLayout m_canvas_frame_layout;
    bool m_canvas_active;
    int m_canvas_fallback;
    bool m_frame_geometry_dirty;
    // Resolved once per rendered frame. Render() publishes from it and
    // LogBitmapFrame() serializes it, so geometry cannot drift between the
    // frame handed to the consumer and the frame described in the log.
    SubtitleDisplayLayout::BitmapPublication m_publication;
    bool m_publication_valid;
    // Consumer contract (resolved in RequestFrame): MPCVR reads only the
    // first bitmap, so canvas frames are published as one canvas-sized
    // texture instead of the madVR band partition.
    bool m_prefer_single_bitmap;

    bool m_trace_valid;
    FrameTrace m_last_trace;
    SubtitleDisplayLayout::ConsumerIdentity m_consumer_identity;
    CStringW m_diag_consumer_version;
    int m_diag_presented_bitmap;
    int m_diag_presented_movable;
    int m_diag_max_bitmap_count;
    RECT m_diag_consumer_fullscreen;
    SIZE m_diag_consumer_display_mode;
    RECT m_diag_consumer_subtitle_target;
    bool m_diag_consumer_pending;
    int m_diag_consumer_refresh_budget;

    CComPtr<ISubPicExAllocator> m_allocator;
    CComPtr<ISubPicEx>          m_pSubPic;
    CComPtr<IXySubRenderFrame>  m_xy_sub_render_frame;

    SubPicDesc m_design_spd;
    SIZE m_design_spd_size;
    bool m_design_spd_valid;
    bool m_scale_design_canvas;
    int m_scale_filter;
    // Scale statistics are updated by Render() and copied into FrameTrace.
    // They are not publication geometry.
    SIZE m_diag_design_canvas;
    SIZE m_diag_scale_canvas;
    double m_diag_scale_factor;
    RECT m_diag_source_dirty;
    RECT m_diag_scaled_dirty;
};

//This is for embedded subtitles
class __declspec(uuid("F316F98F-BDB5-42AA-A253-8D6C8AA20754"))
HdmvSubtitleProvider : public CUnknown, public IXySubRenderProvider, public ISubStream
{
public:
    HdmvSubtitleProvider(CCritSec* pLock, SUBTITLE_TYPE nType, const CString& name, LCID lcid);
    ~HdmvSubtitleProvider(void);

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

    // IXySubRenderProvider
    STDMETHODIMP Connect     (IXyOptions *consumer);
    STDMETHODIMP RequestFrame(IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now);
    STDMETHODIMP Invalidate  (REFERENCE_TIME rtInvalidate = -1);

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClassID);

    // ISubStream
    STDMETHODIMP_(int) GetStreamCount();
    STDMETHODIMP GetStreamInfo(int i, WCHAR** ppName, LCID* pLCID);
    STDMETHODIMP_(int) GetStream();
    STDMETHODIMP SetStream(int iStream);
    STDMETHODIMP Reload();

    //fix me: add to an interface
    HRESULT ParseSample(IMediaSample* pSample);
    HRESULT NewSegment (REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);
    HRESULT EndOfStream(void);

    HRESULT SetYuvType(CBaseSub::ColorType colorType, CBaseSub::YuvRangeType yuvRangeType);
    HRESULT SetHdrConfig(const HdrColor::Config& config);
private :
    CString         m_name;
    LCID            m_lcid;
    REFERENCE_TIME  m_rtStart;

    CBaseSub*       m_pSub;
    CCritSec        m_csCritSec;
    HdmvSubtitleProviderImpl *m_helper;
};

//This is for external subtitles
class __declspec(uuid("B5C880E5-A702-4F1B-B79D-410149874726"))
SupFileSubtitleProvider : public CUnknown, public IXySubRenderProvider, public ISubStream, protected CAMThread
{
public:
    SupFileSubtitleProvider();
    virtual ~SupFileSubtitleProvider();

    bool Open(CString fn, CString subName = _T(""));

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

    // IXySubRenderProvider
    STDMETHODIMP Connect     (IXyOptions *consumer);
    STDMETHODIMP RequestFrame(IXySubRenderFrame**subRenderFrame, REFERENCE_TIME now);
    STDMETHODIMP Invalidate  (REFERENCE_TIME rtInvalidate = -1);

    // IPersist
    STDMETHODIMP GetClassID(CLSID* pClassID);

    // ISubStream
    STDMETHODIMP_(int) GetStreamCount();
    STDMETHODIMP GetStreamInfo(int i, WCHAR** ppName, LCID* pLCID);
    STDMETHODIMP_(int) GetStream();
    STDMETHODIMP SetStream(int iStream);
    STDMETHODIMP Reload();

    HRESULT SetYuvType(CBaseSub::ColorType colorType, CBaseSub::YuvRangeType yuvRangeType);
    HRESULT SetHdrConfig(const HdrColor::Config& config);
protected:
    DWORD ThreadProc();
private:
    CString     m_fname, m_subname;
    CHdmvSub*   m_pSub;
    CCritSec    m_csCritSec;
    HdmvSubtitleProviderImpl *m_helper;
};

