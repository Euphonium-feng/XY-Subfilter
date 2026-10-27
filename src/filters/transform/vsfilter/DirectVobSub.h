/* 
 *	Copyright (C) 2003-2006 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#include "IDirectVobSub.h"
#include "IDirectVobSubXy.h"
#include "..\..\..\..\include\IFilterVersion.h"
#include "version.h"
#include "XyOptionsImpl.h"
#include "../../../subpic/hdr_color.h"
#include "../../../subpic/hdr_evidence.h"

class DirectVobSubImpl : public IDirectVobSub2, public XyOptionsImpl, public IFilterVersion
{
public:
    typedef XyOptionsImpl::Option Option;
    typedef DirectVobSubXyOptions::SubStyle SubStyle;

    enum ColorSpaceOption
    {
        YuvMatrix_AUTO = 0
        ,BT_601
        ,BT_709
        ,BT_2020
        ,GUESS
    };
    enum YuvRange
    {
        YuvRange_Auto = 0
        ,YuvRange_TV
        ,YuvRange_PC
    };
    enum LoadLevel
    {
        LOADLEVEL_WHEN_NEEDED = 0,
        LOADLEVEL_ALWAYS = 1,
        LOADLEVEL_DISABLED = 2,
        LOADLEVEL_COUNT
    };

    static const int REQUIRED_CONFIG_VERSION = 706;
    static const int CUR_SUPPORTED_FILTER_VERSION = 39;

    typedef DirectVobSubXyOptions::CachesInfo CachesInfo;
    typedef DirectVobSubXyOptions::XyFlyWeightInfo XyFlyWeightInfo;
    typedef DirectVobSubXyOptions::ColorSpaceOpt ColorSpaceOpt;

    struct FilterInfo
    {
        CStringW guid, friendly_name;
    };
protected:
    DirectVobSubImpl(const Option *options, CCritSec * pLock);
    virtual ~DirectVobSubImpl();

    bool is_compatible();
    UINT GetCompatibleProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nDefault);
    CString GetCompatibleProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszDefault);

    virtual HRESULT GetCurStyles(SubStyle sub_style[], int count) { return E_NOTIMPL; }
    virtual HRESULT SetCurStyles(const SubStyle sub_style[], int count) { return E_NOTIMPL; }
protected:
    CCritSec *m_propsLock;

    volatile LONG m_hdr_generation;
    HdrColor::HdrSignal m_graph_hdr_signal;
    HdrColor::HdrSignal m_container_hdr_signal;
    bool m_video_side_data_hdr;
    int m_consumer_transfer_function;
    DWORD m_consumer_control_flags;
    bool m_graph_bt2020_matrix;
    bool m_container_bt2020_matrix;
    bool m_video_filename_hdr;

    int m_bt601Width, m_bt601Height;//for AUTO_GUESS

    static int const MAX_COLOR_SPACE = 256;
    ColorSpaceOpt m_outputColorSpace[MAX_COLOR_SPACE];
    ColorSpaceOpt m_inputColorSpace[MAX_COLOR_SPACE];

	STSStyle m_defStyle;

	bool m_fAdvancedRenderer;
	int m_nReloaderDisableCount;
	int m_SubtitleDelay, m_SubtitleSpeedMul, m_SubtitleSpeedDiv;
	NORMALIZEDRECT m_ZoomRect;

    CAtlArray<CStringW> m_known_source_filters_guid;
    CAtlArray<CStringW> m_known_source_filters_name;

    int m_supported_filter_verion;
    int m_config_info_version;

	CComPtr<ISubClock> m_pSubClock;

public:
    static void LoadKnownSourceFilters( CAtlArray<CStringW> *filter_guid,
                                        CAtlArray<CStringW> *filter_name );
    static void SaveKnownSourceFilters( const CAtlArray<CStringW>& filter_guid, 
                                        const CAtlArray<CStringW>& filter_name );
public:
    // XyOptionsImpl
    HRESULT DoGetField(unsigned field, void *value);
    HRESULT DoSetField(unsigned field, void const *value);
    virtual HRESULT OnOptionChanged(unsigned field);

    // Resolves INT_PGS_HDR_MODE / INT_PGS_HDR_WHITE_NITS into a mapping config.
    // AUTO uses the active graph path, optional local MKV metadata, filename,
    // and the consumer's YCbCr matrix as a final fallback.
    HdrColor::Config ResolvePgsHdrConfig() const;

    // Same mapping rules for ASS/SRT text, driven by INT_TEXT_HDR_MODE and the
    // shared white point.
    HdrColor::Config ResolveTextHdrConfig() const;

    // Applies INT_UI_LANGUAGE to the calling thread's resource language.
    static void ApplyUiLanguage(int ui_language);

    // Shared implementation behind the PGS and text resolvers.
    HdrColor::Config ResolveHdrMode(int mode) const;

    // Monotonic revision changed whenever an HDR option or consumer HDR hint
    // changes. It is carried into every renderer cache and bitmap id.
    unsigned long long GetHdrGeneration() const;

    // Property-page hooks. XySubFilter overrides these to coalesce refreshes
    // and report consumer cache status; other filters keep the defaults.
    virtual void BeginHdrOptionBatch() {}
    virtual void EndHdrOptionBatch() {}
    virtual CStringW GetHdrStatusText();
    virtual CStringW GetHdrDiagnosticPath() const { return CStringW(); }

    // Resolves INT_UI_LANGUAGE into the LANGID used for Win32 resource lookup.
    static LANGID ResolveUiLanguage(int ui_language);

    // Resource language currently in effect for a thread, honouring the user
    // choice so property pages (created on the host UI thread) can pass it to
    // LoadString/FindResource style lookups.
    static LANGID GetActiveResourceLanguage();

    // Applies the stored UI language to the calling thread. Property pages run
    // on the host UI thread, which does not inherit the filter thread's
    // language, so call this before any LoadString/CreateDialogParam there.
    static void ApplyActiveUiLanguageToCurrentThread();

    // Last INT_UI_LANGUAGE value seen, shared with property pages that run on
    // a different thread than the filter.
    static volatile LONG s_active_ui_language;

    // IXyOptions
    STDMETHODIMP XyGetBin      (unsigned field, LPVOID    *value, int *size );
    STDMETHODIMP XyGetBin2     (unsigned field, void      *value, int size );

    STDMETHODIMP XySetBin      (unsigned field, LPVOID    value, int size );

	// IDirectVobSub

    STDMETHODIMP get_FileName(WCHAR* fn);
    STDMETHODIMP put_FileName(WCHAR* fn);
    STDMETHODIMP get_LanguageCount(int* nLangs);
    STDMETHODIMP get_LanguageName(int iLanguage, WCHAR** ppName);
    STDMETHODIMP get_SelectedLanguage(int* iSelected);
    STDMETHODIMP put_SelectedLanguage(int iSelected);
    STDMETHODIMP get_HideSubtitles(bool* fHideSubtitles);
    STDMETHODIMP put_HideSubtitles(bool fHideSubtitles);
    STDMETHODIMP get_PreBuffering(bool* fDoPreBuffering);
    STDMETHODIMP put_PreBuffering(bool fDoPreBuffering);

    
    STDMETHODIMP get_SubPictToBuffer(unsigned int* uSubPictToBuffer)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP put_SubPictToBuffer(unsigned int uSubPictToBuffer)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP get_AnimWhenBuffering(bool* fAnimWhenBuffering)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP put_AnimWhenBuffering(bool fAnimWhenBuffering)
    {
        return E_NOTIMPL;
    }
    STDMETHODIMP get_Placement(bool* fOverridePlacement, int* xperc, int* yperc);
    STDMETHODIMP put_Placement(bool fOverridePlacement, int xperc, int yperc);
    STDMETHODIMP get_VobSubSettings(bool* fBuffer, bool* fOnlyShowForcedSubs, bool* fPolygonize);
    STDMETHODIMP put_VobSubSettings(bool fBuffer, bool fOnlyShowForcedSubs, bool fPolygonize);
    STDMETHODIMP get_TextSettings(void* lf, int lflen, COLORREF* color, bool* fShadow, bool* fOutline, bool* fAdvancedRenderer);
    STDMETHODIMP put_TextSettings(void* lf, int lflen, COLORREF color, bool fShadow, bool fOutline, bool fAdvancedRenderer);
    STDMETHODIMP get_Flip(bool* fPicture, bool* fSubtitles);
    STDMETHODIMP put_Flip(bool fPicture, bool fSubtitles);
    STDMETHODIMP get_OSD(bool* fShowOSD);
    STDMETHODIMP put_OSD(bool fShowOSD);
	STDMETHODIMP get_SaveFullPath(bool* fSaveFullPath);
	STDMETHODIMP put_SaveFullPath(bool fSaveFullPath);
    STDMETHODIMP get_SubtitleTiming(int* delay, int* speedmul, int* speeddiv);
    STDMETHODIMP put_SubtitleTiming(int delay, int speedmul, int speeddiv);
    STDMETHODIMP get_MediaFPS(bool* fEnabled, double* fps);
    STDMETHODIMP put_MediaFPS(bool fEnabled, double fps);
	STDMETHODIMP get_ZoomRect(NORMALIZEDRECT* rect);
    STDMETHODIMP put_ZoomRect(NORMALIZEDRECT* rect);
	STDMETHODIMP get_ColorFormat(int* iPosition) {return E_NOTIMPL;}
    STDMETHODIMP put_ColorFormat(int iPosition) {return E_NOTIMPL;}

    STDMETHOD (get_CachesInfo)(CachesInfo* caches_info);
    STDMETHOD (get_XyFlyWeightInfo)(XyFlyWeightInfo* xy_fw_info);

	STDMETHODIMP HasConfigDialog(int iSelected);
	STDMETHODIMP ShowConfigDialog(int iSelected, HWND hWndParent);

	// settings for the rest are stored in the registry

	STDMETHODIMP IsSubtitleReloaderLocked(bool* fLocked);
    STDMETHODIMP LockSubtitleReloader(bool fLock);
	STDMETHODIMP get_SubtitleReloader(bool* fDisabled);
    STDMETHODIMP put_SubtitleReloader(bool fDisable);

	// the followings need a partial or full reloading of the filter

	STDMETHODIMP get_ExtendPicture(int* horizontal, int* vertical, int* resx2, int* resx2minw, int* resx2minh);
	STDMETHODIMP put_ExtendPicture(int horizontal, int vertical, int resx2, int resx2minw, int resx2minh);
	STDMETHODIMP get_LoadSettings(int* level, bool* fExternalLoad, bool* fWebLoad, bool* fEmbeddedLoad);
	STDMETHODIMP put_LoadSettings(int level, bool fExternalLoad, bool fWebLoad, bool fEmbeddedLoad);
        
	// IDirectVobSub2

	STDMETHODIMP AdviseSubClock(ISubClock* pSubClock);
	STDMETHODIMP_(bool) get_Forced();
	STDMETHODIMP put_Forced(bool fForced);
    STDMETHODIMP get_TextSettings(STSStyle* pDefStyle);
    STDMETHODIMP put_TextSettings(STSStyle* pDefStyle);
	STDMETHODIMP get_AspectRatioSettings(CSimpleTextSubtitle::EPARCompensationType* ePARCompensationType);
	STDMETHODIMP put_AspectRatioSettings(CSimpleTextSubtitle::EPARCompensationType* ePARCompensationType);

	// IFilterVersion
	
	STDMETHODIMP_(DWORD) GetFilterVersion();
};

// For DirectVobSubFilter
class CDirectVobSub: public DirectVobSubImpl
{
public:
    CDirectVobSub(const Option *options, CCritSec * pLock);

protected:
    STDMETHODIMP UpdateRegistry();
};

// For XySubFilter
class CDVS4XySubFilter: public DirectVobSubImpl
{
public:
    CDVS4XySubFilter(const Option *options, CCritSec * pLock);

protected:
    STDMETHODIMP UpdateRegistry();
};
