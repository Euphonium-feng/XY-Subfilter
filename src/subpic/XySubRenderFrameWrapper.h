#pragma once

#include "../subtitles/flyweight_base_types.h"

class CMemSubPic;
interface IXySubRenderFrame;

class XySubRenderFrameWrapper : public CUnknown, public IXySubRenderFrame
{
public:
    XySubRenderFrameWrapper(CMemSubPic * inner_obj, const CRect& output_rect, const CRect& clip_rect, 
        ULONGLONG id,
        HRESULT *phr=NULL);
    virtual ~XySubRenderFrameWrapper(){}

    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

    STDMETHODIMP GetOutputRect(RECT *outputRect);
    STDMETHODIMP GetClipRect(RECT *clipRect);
    STDMETHODIMP GetXyColorSpace(int *xyColorSpace);
    STDMETHODIMP GetBitmapCount(int *count);
    STDMETHODIMP GetBitmap(int index, ULONGLONG *id, POINT *position, SIZE *size, LPCVOID *pixels, int *pitch);
    STDMETHODIMP GetBitmapExtra(int index, LPVOID extra_info);
private:
    CComPtr<CMemSubPic> m_inner_obj;
    CRect m_output_rect, m_clip_rect;
    ULONGLONG m_id;
};

class XySubRenderFrameWrapper2 : public CUnknown, public IXySubRenderFrame
{
public:
    XySubRenderFrameWrapper2(
        IXySubRenderFrame *inner_obj, ULONGLONG context_id,
        ULONGLONG hdr_generation = 0, bool remap_bitmap_ids = true);
    virtual ~XySubRenderFrameWrapper2(){}

    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

    STDMETHODIMP GetOutputRect(RECT *outputRect);
    STDMETHODIMP GetClipRect(RECT *clipRect);
    STDMETHODIMP GetXyColorSpace(int *xyColorSpace);
    STDMETHODIMP GetBitmapCount(int *count);
    STDMETHODIMP GetBitmap(int index, ULONGLONG *id, POINT *position, SIZE *size, LPCVOID *pixels, int *pitch);
    STDMETHODIMP GetBitmapExtra(int index, LPVOID extra_info);

    // Optional frame geometry override. Used when the provider rendered into a
    // display canvas that is larger than the consumer's video output rect.
    // All rectangles and the bitmap offset use the inner frame's coordinate
    // space, matching SubRenderIntf's "same space as GetOutputRect" contract.
    void SetFrameGeometry(
        const RECT& output_rect, const RECT& clip_rect, POINT bitmap_offset);

private:
    CComPtr<IXySubRenderFrame> m_inner_obj;
    CAtlArray<ULONGLONG> m_ids;
    bool m_has_geometry_override;
    bool m_remap_bitmap_ids;
    RECT m_output_rect;
    RECT m_clip_rect;
    POINT m_bitmap_offset;
};

#ifdef SUBTITLE_FRAME_DUMP_FILE
HRESULT DumpSubRenderFrame( IXySubRenderFrame *sub, const char * filename );
HRESULT DumpToBitmap( IXySubRenderFrame *sub, LPCTSTR filename, bool dump_alpha_channel, bool dump_rgb_bmp
    , DWORD background_color );
#endif
