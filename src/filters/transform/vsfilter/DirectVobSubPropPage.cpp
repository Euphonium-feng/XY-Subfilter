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

#include "stdafx.h"
#include <commdlg.h>
#include <afxdlgs.h>
#include "DirectVobSubFilter.h"
#include "DirectVobSubPropPage.h"
#include "VSFilter.h"
#include "StyleEditorDialog.h"

#include "../../../DSUtil/DSUtil.h"
#include "../../../DSUtil/MediaTypes.h"

#include "../../../subtitles/SubtitleRenderBackend.h"
#include "../../../subtitles/TextRendererMode.h"
#include "../../../subpic/hdr_color.h"

#include <shellapi.h>

namespace
{
struct EditSelection
{
    HWND hwnd = NULL;
    DWORD start = 0;
    DWORD end = 0;
};

EditSelection CaptureEditSelection(HWND dialog)
{
    EditSelection selection;
    HWND focus = ::GetFocus();
    if (!focus || !::IsChild(dialog, focus)) {
        return selection;
    }

    WCHAR class_name[16] = {};
    if (!::GetClassNameW(focus, class_name, _countof(class_name))
        || _wcsicmp(class_name, L"Edit") != 0) {
        return selection;
    }

    selection.hwnd = focus;
    ::SendMessageW(focus, EM_GETSEL,
        reinterpret_cast<WPARAM>(&selection.start),
        reinterpret_cast<LPARAM>(&selection.end));
    return selection;
}

void RestoreEditSelection(HWND dialog, const EditSelection& selection)
{
    if (!selection.hwnd || !::IsWindow(selection.hwnd)
        || !::IsChild(dialog, selection.hwnd)) {
        return;
    }

    const int text_length = ::GetWindowTextLengthW(selection.hwnd);
    const DWORD start = min(selection.start, static_cast<DWORD>(text_length));
    const DWORD end = min(selection.end, static_cast<DWORD>(text_length));
    ::SetFocus(selection.hwnd);
    ::SendMessageW(selection.hwnd, EM_SETSEL, start, end);
}

void SetEditTextIfChanged(CEdit& edit, const CString& text)
{
    CString current;
    edit.GetWindowText(current);
    if (current != text) {
        edit.SetWindowText(text);
    }
}

struct ComboEntry
{
    UINT label_id;
    DWORD_PTR value;
};

void PopulateCombo(
    CComboBox& combo, const ComboEntry* entries, size_t count, DWORD_PTR selected)
{
    combo.ResetContent();
    int selected_index = 0;
    for (size_t i = 0; i < count; ++i) {
        const int index = combo.AddString(ResStr(entries[i].label_id));
        combo.SetItemData(index, entries[i].value);
        if (entries[i].value == selected) {
            selected_index = index;
        }
    }
    combo.SetCurSel(selected_index);
}

void PopulateTextRendererModeCombo(CComboBox& combo, int selected_mode)
{
    const ComboEntry entries[] = {
        { IDS_XY_UI_RENDERER_LEGACY_GDI, TEXT_RENDERER_LEGACY_GDI },
        { IDS_XY_UI_RENDERER_AUTO_FALLBACK, TEXT_RENDERER_AUTO_FALLBACK },
        { IDS_XY_UI_RENDERER_UNISCRIBE, TEXT_RENDERER_UNISCRIBE },
    };

    selected_mode = NormalizeTextRendererMode(selected_mode);
    PopulateCombo(combo, entries, _countof(entries), selected_mode);
}

int GetTextRendererModeComboValue(const CComboBox& combo)
{
    const int index = combo.GetCurSel();
    return index == CB_ERR ? TEXT_RENDERER_LEGACY_GDI
                           : NormalizeTextRendererMode(static_cast<int>(combo.GetItemData(index)));
}

void PopulateSubpixelPositionCombo(CComboBox& combo, int selected)
{
    const ComboEntry entries[] = {
        { IDS_XY_UI_SUBPIXEL_NONE, 0 },
        { IDS_XY_UI_SUBPIXEL_2X2, 1 },
        { IDS_XY_UI_SUBPIXEL_4X4, 2 },
        { IDS_XY_UI_SUBPIXEL_8X8_VSFILTER, 3 },
        { IDS_XY_UI_SUBPIXEL_8X8_BILINEAR, 4 },
    };
    PopulateCombo(combo, entries, _countof(entries), selected);
}

void PopulateLayoutSizeCombo(CComboBox& combo, int selected)
{
    const ComboEntry entries[] = {
        { IDS_XY_UI_LAYOUT_ORIGINAL,
            DirectVobSubXyOptions::LAYOUT_SIZE_OPT_ORIGINAL_VIDEO_SIZE },
        { IDS_XY_UI_LAYOUT_AR_ADJUSTED,
            DirectVobSubXyOptions::LAYOUT_SIZE_OPT_AR_ADJUSTED_VIDEO_SIZE },
        { IDS_XY_UI_LAYOUT_CUSTOM,
            DirectVobSubXyOptions::LAYOUT_SIZE_OPT_USER_SPECIFIED },
    };
    PopulateCombo(combo, entries, _countof(entries), selected);
}

void PopulateColorSpaceCombo(CComboBox& combo, int selected)
{
    const ComboEntry entries[] = {
        { IDS_XY_UI_AUTO, CDirectVobSub::YuvMatrix_AUTO },
        { IDS_XY_UI_BT601, CDirectVobSub::BT_601 },
        { IDS_XY_UI_BT709, CDirectVobSub::BT_709 },
        { IDS_XY_UI_BT2020, CDirectVobSub::BT_2020 },
        { IDS_XY_UI_GUESS, CDirectVobSub::GUESS },
    };
    PopulateCombo(combo, entries, _countof(entries), selected);
}

void PopulateYuvRangeCombo(CComboBox& combo, int selected)
{
    const ComboEntry entries[] = {
        { IDS_XY_UI_AUTO, CDirectVobSub::YuvRange_Auto },
        { IDS_XY_UI_TV, CDirectVobSub::YuvRange_TV },
        { IDS_XY_UI_PC, CDirectVobSub::YuvRange_PC },
    };
    PopulateCombo(combo, entries, _countof(entries), selected);
}

void PopulateRgbLevelCombo(CComboBox& combo, int selected)
{
    const ComboEntry entries[] = {
        { IDS_XY_UI_AUTO, DirectVobSubXyOptions::RGB_OUTPUT_LEVEL_AUTO },
        { IDS_XY_UI_PC, DirectVobSubXyOptions::RGB_OUTPUT_LEVEL_PC },
        { IDS_XY_UI_FORCE_TV, DirectVobSubXyOptions::RGB_OUTPUT_LEVEL_FORCE_TV },
    };
    PopulateCombo(combo, entries, _countof(entries), selected);
}

void PopulateHdrModeCombo(CComboBox& combo, int selected)
{
    const ComboEntry entries[] = {
        { IDS_XY_UI_AUTO, DirectVobSubXyOptions::PGS_HDR_MODE_AUTO },
        { IDS_XY_UI_HDR_OFF, DirectVobSubXyOptions::PGS_HDR_MODE_DISABLED },
        { IDS_XY_UI_HDR_PQ, DirectVobSubXyOptions::PGS_HDR_MODE_PQ },
    };
    PopulateCombo(combo, entries, _countof(entries), selected);
}

void PopulateUiLanguageCombo(CComboBox& combo, int selected)
{
    const ComboEntry entries[] = {
        { IDS_XY_UI_AUTO, DirectVobSubXyOptions::UI_LANGUAGE_AUTO },
        { IDS_XY_UI_ENGLISH, DirectVobSubXyOptions::UI_LANGUAGE_ENGLISH },
        { IDS_XY_UI_CHINESE, DirectVobSubXyOptions::UI_LANGUAGE_CHINESE_SIMPLIFIED },
    };
    PopulateCombo(combo, entries, _countof(entries), selected);
}

CString FormatCachesMessage(
    const DirectVobSubXyOptions::CachesInfo& caches_info,
    const DirectVobSubXyOptions::XyFlyWeightInfo& xy_fw_info)
{
    CString msg;
    msg.Format(
        ResStr(IDS_XY_UI_CACHES_INFO_FORMAT),
        caches_info.text_info_cache_cur_item_num, caches_info.text_info_cache_hit_count, caches_info.text_info_cache_query_count,
        caches_info.word_info_cache_cur_item_num, caches_info.word_info_cache_hit_count, caches_info.word_info_cache_query_count,
        caches_info.path_cache_cur_item_num, caches_info.path_cache_hit_count, caches_info.path_cache_query_count,
        caches_info.scanline_cache2_cur_item_num, caches_info.scanline_cache2_hit_count, caches_info.scanline_cache2_query_count,
        caches_info.non_blur_cache_cur_item_num, caches_info.non_blur_cache_hit_count, caches_info.non_blur_cache_query_count,
        caches_info.overlay_cache_cur_item_num, caches_info.overlay_cache_hit_count, caches_info.overlay_cache_query_count,
        caches_info.interpolate_cache_cur_item_num, caches_info.interpolate_cache_hit_count, caches_info.interpolate_cache_query_count,
        caches_info.bitmap_cache_cur_item_num, caches_info.bitmap_cache_hit_count, caches_info.bitmap_cache_query_count,
        caches_info.scanline_cache_cur_item_num, caches_info.scanline_cache_hit_count, caches_info.scanline_cache_query_count,
        caches_info.overlay_key_cache_cur_item_num, caches_info.overlay_key_cache_hit_count, caches_info.overlay_key_cache_query_count,
        caches_info.clipper_cache_cur_item_num, caches_info.clipper_cache_hit_count, caches_info.clipper_cache_query_count,
        xy_fw_info.xy_fw_string_w.cur_item_num, xy_fw_info.xy_fw_string_w.hit_count, xy_fw_info.xy_fw_string_w.query_count,
        xy_fw_info.xy_fw_grouped_draw_items_hash_key.cur_item_num, xy_fw_info.xy_fw_grouped_draw_items_hash_key.hit_count, xy_fw_info.xy_fw_grouped_draw_items_hash_key.query_count);
    return msg;
}

}

BOOL WINAPI MyGetDialogSize(int iResourceID, DLGPROC pDlgProc, LPARAM lParam, SIZE* pResult)
{
    HWND hwnd = CreateDialogParam(AfxGetResourceHandle(),
        MAKEINTRESOURCE(iResourceID),
        GetDesktopWindow(),
        pDlgProc,
        lParam);

    if(hwnd == NULL) return FALSE;

    RECT rc;
    GetWindowRect(hwnd, &rc);
    pResult->cx = rc.right - rc.left;
    pResult->cy = rc.bottom - rc.top;

    DestroyWindow(hwnd);

    return TRUE;
}

/************************************************************************/

DirectVobSubXyOptions::SubStyle * NewSubStyles(int count);
void DeleteSubStyles(DirectVobSubXyOptions::SubStyle* style, int count);
HRESULT GetSubStyles(IXyOptions *filter, DirectVobSubXyOptions::SubStyle* *style, int *count);
HRESULT CreateSubStyleEditDialog(const STSStyle& default_style
    , const DirectVobSubXyOptions::SubStyle sub_styles[], int count
    , CPropertySheet& dlg, /*out*/CStyleEditorPPage **pages);
void EditStyle(HWND hWnd, IXyOptions *filter, /* in-out */STSStyle* default_style);

/********************/

DirectVobSubXyOptions::SubStyle * NewSubStyles(int count)
{
    using namespace DirectVobSubXyOptions;
    SubStyle * ret = DEBUG_NEW SubStyle[count];
    if (ret==NULL)
    {
        XY_LOG_FATAL("Overflow");
        return NULL;
    }
    for (int i=0;i<count;i++)
    {
        ret[i].style = NULL;
    }
    for (int i=0;i<count;i++)
    {
        ret[i].style = DEBUG_NEW STSStyle();
        if (ret[i].style==NULL)
        {
            XY_LOG_FATAL("Overflow");
            DeleteSubStyles(ret, count);
            return NULL;
        }
    }
    return ret;
}

void DeleteSubStyles(DirectVobSubXyOptions::SubStyle* style, int count)
{
    if (!style)
    {
        return;
    }
    for (int i=0; i<count; i++)
    {
        delete style[i].style;
    }
    delete[] style;
}

HRESULT GetSubStyles(IXyOptions *filter, DirectVobSubXyOptions::SubStyle* *style, int *count)
{
    ASSERT(style && count);
    *style = NULL;
    HRESULT hr = filter->XyGetInt(DirectVobSubXyOptions::INT_CUR_STYLES_COUNT, count);
    *style = NewSubStyles(*count+1);
    if (!*style)
    {
        return E_FAIL;
    }
    if (*count)
    {
        hr = filter->XyGetBin2(DirectVobSubXyOptions::BIN2_CUR_STYLES, *style + 1, *count);
        if (hr != S_OK)
        {
            DeleteSubStyles(*style, *count + 1);
            *style = NULL;
            *count = 0;
        }
    }
    return hr;
}

HRESULT CreateSubStyleEditDialog(const STSStyle& default_style
    , const DirectVobSubXyOptions::SubStyle sub_styles[], int count
    , CPropertySheet& dlg, /*out*/CStyleEditorPPage **pages)
{
    ASSERT(pages);
    *pages = DEBUG_NEW CStyleEditorPPage[count+1];
    if (!*pages)
    {
        return E_FAIL;
    }
    (*pages)[0].init(ResStr(IDS_XY_UI_GLOBAL_DEFAULT), &default_style);
    dlg.AddPage(*pages);
    for (int i=0;i<count;i++)
    {
        ASSERT(sub_styles);
        CString title = sub_styles[i].name;
        (*pages)[i+1].init(title, static_cast<STSStyle*>(sub_styles[i].style));
        dlg.AddPage((*pages)+i+1);
    }
    return S_OK;
}

void EditStyle(HWND hWnd, IXyOptions *filter, /* in-out */STSStyle* default_style)
{
    int style_count = 0;
    DirectVobSubXyOptions::SubStyle* styles = NULL;
    HRESULT hr = GetSubStyles(filter, &styles, &style_count);
    CHECK_N_LOG(hr, "Failed to get styles."<<XY_LOG_VAR_2_STR(hr));
    CStyleEditorPPage* pages = NULL;
    CPropertySheet dlg(ResStr(IDS_XY_UI_STYLES_TITLE), CWnd::FromHandle(hWnd));
    hr = CreateSubStyleEditDialog(*default_style, styles+1, style_count, dlg, &pages);
    if (FAILED(hr))
    {
        MessageBoxW(hWnd,
            ResStr(IDS_XY_UI_STYLE_EDITOR_FAILED),
            ResStr(IDS_XY_UI_FATAL_TITLE),
            MB_OK | MB_ICONERROR | MB_APPLMODAL
            );
        DeleteSubStyles(styles, style_count+1);
        return;
    }
    if(dlg.DoModal() == IDOK)
    {
        *default_style = pages[0].m_stss;
        *static_cast<STSStyle *>(styles[0].style) = pages[0].m_stss;
        if (style_count>0)
        {
            for (int i=0;i<style_count;i++)
            {
                *static_cast<STSStyle*>(styles[i+1].style) = pages[i+1].m_stss;
            }
            hr = filter->XySetBin(DirectVobSubXyOptions::BIN2_CUR_STYLES, styles, style_count);
            CHECK_N_LOG(hr, "Failed to set option "<<XY_LOG_VAR_2_STR(DirectVobSubXyOptions::BIN2_CUR_STYLES));
        }
    }
    DeleteSubStyles(styles, style_count+1);
    delete []pages;
    return;
}

/************************************************************************/

STDMETHODIMP CDVSBasePPage::GetPageInfo(LPPROPPAGEINFO pPageInfo)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    // Property pages are created on the host UI thread, which does not inherit
    // the filter thread's language. Switch it here so LoadString and
    // CreateDialogParam pick the matching resource block.
    DirectVobSubImpl::ApplyActiveUiLanguageToCurrentThread();

    CString str;
    if(!str.LoadString(m_TitleId)) return E_FAIL;

    WCHAR wszTitle[STR_MAX_LENGTH];
#ifdef UNICODE
    wcscpy_s(wszTitle, STR_MAX_LENGTH, str);
    wszTitle[STR_MAX_LENGTH-1]=0;
#else
    mbstowcs(wszTitle, str, str.GetLength()+1);
#endif

    CheckPointer(pPageInfo, E_POINTER);

    // Allocate dynamic memory for the property page title

    LPOLESTR pszTitle;
    HRESULT hr = AMGetWideString(wszTitle, &pszTitle);
    if(FAILED(hr)) {NOTE("No caption memory"); return hr;}

    pPageInfo->cb               = sizeof(PROPPAGEINFO);
    pPageInfo->pszTitle         = pszTitle;
    pPageInfo->pszDocString     = NULL;
    pPageInfo->pszHelpFile      = NULL;
    pPageInfo->dwHelpContext    = 0;
    // Set defaults in case GetDialogSize fails
    pPageInfo->size.cx          = 340;
    pPageInfo->size.cy          = 150;

    MyGetDialogSize(m_DialogId, DialogProc, 0L, &pPageInfo->size);

    return NOERROR;
}

STDMETHODIMP CDVSBasePPage::Activate(HWND hwndParent, LPCRECT pRect, BOOL fModal)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    DirectVobSubImpl::ApplyActiveUiLanguageToCurrentThread();

    CheckPointer(pRect,E_POINTER);
/*
    // Return failure if SetObject has not been called.
    if (m_bObjectSet == FALSE) {
        return E_UNEXPECTED;
    }
*/
    if(m_hwnd) return E_UNEXPECTED;

    m_hwnd = CreateDialogParam(AfxGetResourceHandle(), MAKEINTRESOURCE(m_DialogId), hwndParent, DialogProc, (LPARAM)this);
    if(m_hwnd == NULL) return E_OUTOFMEMORY;

    OnActivate();
    Move(pRect);
    return Show(SW_SHOWNORMAL);
}

/* CDVSBasePPage */

CDVSBasePPage::CDVSBasePPage(TCHAR* pName, LPUNKNOWN lpunk, int DialogId, int TitleId) 
    : CBasePropertyPage(pName, lpunk, DialogId, TitleId)
    , m_bIsInitialized(FALSE)
    , m_fAttached(false)
    , m_fDisableInstantUpdate(false)
{
}

INT_PTR CDVSBasePPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
    case WM_COMMAND:
        {
            if(m_bIsInitialized)
            {
                m_bDirty = TRUE;
                if(m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);

                switch(HIWORD(wParam))
                {
                case BN_CLICKED: 
                case CBN_SELCHANGE:
                case EN_CHANGE:
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());

                        if(!m_fDisableInstantUpdate 
                            && !(HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_INSTANTUPDATE)
                            && !!theApp.GetProfileInt(ResStr(IDS_R_GENERAL), ResStr(IDS_RG_INSTANTUPDATE), 1)) 
                            OnApplyChanges();
                    }
                }
            }
        }
        break;

    case WM_NCDESTROY:
        DetachControls();
        break;
    }

    return OnMessage(uMsg, wParam, lParam) 
        ? 0 
        : CBasePropertyPage::OnReceiveMessage(hwnd,uMsg,wParam,lParam);
}

HRESULT CDVSBasePPage::OnConnect(IUnknown* pUnknown)
{
    if(!(m_pDirectVobSub = pUnknown)) return E_NOINTERFACE;
    if(!(m_pDirectVobSubXy = pUnknown)) return E_NOINTERFACE;

    m_pDirectVobSub->LockSubtitleReloader(true); // *

    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    UpdateObjectData(false);

    m_bIsInitialized = FALSE;

    return NOERROR;
}

HRESULT CDVSBasePPage::OnDisconnect()
{
    if(m_pDirectVobSub == NULL) return E_UNEXPECTED;

    m_pDirectVobSub->LockSubtitleReloader(false); // *

    // for some reason OnDisconnect() will be called twice, that's why we 
    // need to release m_pDirectVobSub manually on the first call to avoid 
    // a second "m_pDirectVobSub->LockSubtitleReloader(false);"

    m_pDirectVobSub.Release(); 

    return NOERROR;
}

HRESULT CDVSBasePPage::OnActivate()
{
    ASSERT(m_pDirectVobSub);

    AttachControls();

    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    UpdateControlData(false);

    m_bIsInitialized = TRUE;

    return NOERROR;
}

HRESULT CDVSBasePPage::OnDeactivate()
{
    ASSERT(m_pDirectVobSub);

    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    UpdateControlData(true);

    m_bIsInitialized = FALSE;

    return NOERROR;
}

HRESULT CDVSBasePPage::OnApplyChanges()
{
    ASSERT(m_pDirectVobSub);

    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if(m_bIsInitialized)
    {
        // Instant-apply rebuilds the controls, which would otherwise move the
        // caret in a numeric edit box back to the start after every keystroke.
        const EditSelection selection = CaptureEditSelection(m_Dlg);
        OnDeactivate();
        UpdateObjectData(true);
        m_pDirectVobSub->UpdateRegistry(); // *
        OnActivate();
        RestoreEditSelection(m_Dlg, selection);
    }

    return NOERROR;
}

void CDVSBasePPage::AttachControls()
{
    DetachControls();

    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    POSITION pos = m_controls.GetStartPosition();
    while(pos)
    {
        UINT id;
        CWnd* pControl;
        m_controls.GetNextAssoc(pos, id, pControl);
        if(pControl) 
        {
            BOOL fRet = pControl->Attach(GetDlgItem(m_Dlg, id));
            ASSERT(fRet);
        }
    }

    m_fAttached = true;
}

void CDVSBasePPage::DetachControls()
{
    if(!m_fAttached) return;

    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    POSITION pos = m_controls.GetStartPosition();
    while(pos)
    {
        UINT id;
        CWnd* pControl;
        m_controls.GetNextAssoc(pos, id, pControl);
        if(pControl) pControl->Detach();
    }

    m_fAttached = false;
}

void CDVSBasePPage::BindControl(UINT id, CWnd& control)
{
    m_controls[id] = &control;
}

/* CDVSMainPPage */

CDVSMainPPage::CDVSMainPPage(LPUNKNOWN pUnk, HRESULT* phr) 
    : CDVSBasePPage(NAME("DirectVobSub Property Page (main)"), pUnk, IDD_DVSMAINPAGE, IDD_DVSMAINPAGE)
    , m_nLangs(0)
    , m_ppLangs(NULL)
{
    BindControl(IDC_FILENAME, m_fnedit);
    BindControl(IDC_LANGCOMBO, m_langs);
    BindControl(IDC_OVERRIDEPLACEMENT, m_oplacement);
    BindControl(IDC_SPIN1, m_subposx);
    BindControl(IDC_SPIN2, m_subposy);
    BindControl(IDC_STYLES, m_styles);
    BindControl(IDC_ONLYSHOWFORCEDSUBS, m_forcedsubs);
    BindControl(IDC_PARCOMBO, m_PARCombo);
    BindControl(IDC_CHECKBOX_HideTrayIcon, m_hide_tray_icon);
}

CDVSMainPPage::~CDVSMainPPage()
{
    FreeLangs();
}

void CDVSMainPPage::FreeLangs()
{
    if(m_nLangs > 0 && m_ppLangs) 
    {
        for(int i = 0; i < m_nLangs; i++) CoTaskMemFree(m_ppLangs[i]);
        CoTaskMemFree(m_ppLangs);
        m_nLangs = 0;
        m_ppLangs = NULL;
    }
}

void CDVSMainPPage::AllocLangs(int nLangs)
{
    m_ppLangs = (WCHAR**)CoTaskMemRealloc(m_ppLangs, sizeof(WCHAR*)*nLangs);
    m_nLangs = nLangs;
}

bool CDVSMainPPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
    case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case BN_CLICKED:
                {
                    if(LOWORD(wParam) == IDC_OPEN)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());

                        CFileDialog fd(TRUE, NULL, NULL, 
                            OFN_EXPLORER|OFN_ENABLESIZING|OFN_HIDEREADONLY|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST,
                            _T(".idx .smi .sub .srt .psb .ssa .ass .usf .ssf|*.idx;*.smi;*.sub;*.srt;*.psb;*.ssa;*.ass;*.usf;*.ssf|"),
                            ResStr(IDS_XY_UI_ALL_FILES_FILTER),
                            CDialog::FromHandle(m_Dlg), 0);

                        if(fd.DoModal() == IDOK)
                        {
                            m_fnedit.SetWindowText(fd.GetPathName());
                        }

                        return(true);
                    }
                    else if(LOWORD(wParam) == IDC_STYLES)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());
                        EditStyle(m_hwnd, m_pDirectVobSubXy, &m_defStyle);
                        return(true);
                    }
                }
                break;
            }
        }
        break;
    }

    return(false);
}

void CDVSMainPPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        hr = m_pDirectVobSub->put_FileName(m_fn);
        CHECK_N_LOG(hr, "Failed to set option");
        if(hr == S_OK)
        {
            int nLangs = 0;
            hr = m_pDirectVobSub->get_LanguageCount(&nLangs);
            CHECK_N_LOG(hr, "Failed to get option");
            AllocLangs(nLangs);
            for(int i = 0; i < m_nLangs; i++) {
                hr = m_pDirectVobSub->get_LanguageName(i, &m_ppLangs[i]);
                CHECK_N_LOG(hr, "Failed to get option");
            }
            hr = m_pDirectVobSub->get_SelectedLanguage(&m_iSelectedLanguage);
            CHECK_N_LOG(hr, "Failed to get option");
        }

        hr = m_pDirectVobSub->put_SelectedLanguage(m_iSelectedLanguage);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_Placement(m_fOverridePlacement, m_PlacementXperc, m_PlacementYperc);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_VobSubSettings(true, m_fOnlyShowForcedVobSubs, false);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_TextSettings(&m_defStyle);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_AspectRatioSettings(&m_ePARCompensationType);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetBool(DirectVobSubXyOptions::BOOL_HIDE_TRAY_ICON, m_fHideTrayIcon);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        hr = m_pDirectVobSub->get_FileName(m_fn);
        CHECK_N_LOG(hr, "Failed to get option");
        int nLangs = 0;
        hr = m_pDirectVobSub->get_LanguageCount(&nLangs); 
        CHECK_N_LOG(hr, "Failed to get option");
        AllocLangs(nLangs);
        for(int i = 0; i < m_nLangs; i++) {
            hr = m_pDirectVobSub->get_LanguageName(i, &m_ppLangs[i]);
            CHECK_N_LOG(hr, "Failed to get option");
        }
        hr = m_pDirectVobSub->get_SelectedLanguage(&m_iSelectedLanguage);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_Placement(&m_fOverridePlacement, &m_PlacementXperc, &m_PlacementYperc);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_VobSubSettings(NULL, &m_fOnlyShowForcedVobSubs, NULL);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_TextSettings(&m_defStyle);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_AspectRatioSettings(&m_ePARCompensationType);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetBool(DirectVobSubXyOptions::BOOL_HIDE_TRAY_ICON, &m_fHideTrayIcon);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

void CDVSMainPPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        CString fn;
        m_fnedit.GetWindowText(fn);
#ifdef UNICODE
        wcscpy_s(m_fn, MAX_PATH, fn);
        m_fn[MAX_PATH-1]=0;
#else
        mbstowcs(m_fn, fn, fn.GetLength()+1);
#endif
        m_iSelectedLanguage = m_langs.GetCurSel();
        m_fOverridePlacement = !!m_oplacement.GetCheck();
        m_PlacementXperc = m_subposx.GetPos();
        m_PlacementYperc = m_subposy.GetPos();
        m_fOnlyShowForcedVobSubs = !!m_forcedsubs.GetCheck();
        if (m_PARCombo.GetCurSel() != CB_ERR)
            m_ePARCompensationType = static_cast<CSimpleTextSubtitle::EPARCompensationType>(m_PARCombo.GetItemData(m_PARCombo.GetCurSel()));
        else
            m_ePARCompensationType = CSimpleTextSubtitle::EPCTDisabled;
        m_fHideTrayIcon = !!m_hide_tray_icon.GetCheck();
    }
    else
    {
        m_fnedit.SetWindowText(CString(m_fn));
        m_oplacement.SetCheck(m_fOverridePlacement);
        m_subposx.SetRange(-20, 120);
        m_subposx.SetPos(m_PlacementXperc);
        m_subposx.EnableWindow(m_fOverridePlacement);
        m_subposy.SetRange(-20, 120);
        m_subposy.SetPos(m_PlacementYperc);
        m_subposy.EnableWindow(m_fOverridePlacement);
        m_forcedsubs.SetCheck(m_fOnlyShowForcedVobSubs);
        m_langs.ResetContent();
        m_langs.EnableWindow(m_nLangs > 0);
        for(int i = 0; i < m_nLangs; i++) m_langs.AddString(CString(m_ppLangs[i]));
        m_langs.SetCurSel(m_iSelectedLanguage);

        m_PARCombo.ResetContent();
        m_PARCombo.InsertString(0, ResStr(IDS_RT_PAR_DISABLED));
        m_PARCombo.SetItemData(0, CSimpleTextSubtitle::EPCTDisabled);
        if (m_ePARCompensationType == CSimpleTextSubtitle::EPCTDisabled)
            m_PARCombo.SetCurSel(0);

        m_PARCombo.InsertString(1, ResStr(IDS_RT_PAR_DOWNSCALE));
        m_PARCombo.SetItemData(1, CSimpleTextSubtitle::EPCTDownscale);
        if (m_ePARCompensationType == CSimpleTextSubtitle::EPCTDownscale)
            m_PARCombo.SetCurSel(1);

        m_PARCombo.InsertString(2, ResStr(IDS_RT_PAR_UPSCALE));
        m_PARCombo.SetItemData(2, CSimpleTextSubtitle::EPCTUpscale);
        if (m_ePARCompensationType == CSimpleTextSubtitle::EPCTUpscale)
            m_PARCombo.SetCurSel(2);

        m_PARCombo.InsertString(3, ResStr(IDS_RT_PAR_ACCURATE_SIZE));
        m_PARCombo.SetItemData(3, CSimpleTextSubtitle::EPCTAccurateSize);
        if (m_ePARCompensationType == CSimpleTextSubtitle::EPCTAccurateSize)
            m_PARCombo.SetCurSel(3);
        m_hide_tray_icon.SetCheck(m_fHideTrayIcon);
    }
}

/* CDVSGeneralPPage */

CDVSGeneralPPage::CDVSGeneralPPage(LPUNKNOWN pUnk, HRESULT* phr, TCHAR *pName /* = NAME("DirectVobSub Property Page (general settings)") */)
    : CDVSBasePPage(pName, pUnk, IDD_DVSGENERALPAGE, IDD_DVSGENERALPAGE)
{
    BindControl(IDC_VEREXTCOMBO, m_verext);
    BindControl(IDC_MOD32FIX, m_mod32fix);
    BindControl(IDC_RESX2COMBO, m_resx2);
    BindControl(IDC_SPIN3, m_resx2w);
    BindControl(IDC_SPIN4, m_resx2h);
    BindControl(IDC_LOADCOMBO, m_load);
    BindControl(IDC_EXTLOAD, m_extload);
    BindControl(IDC_WEBLOAD, m_webload);
    BindControl(IDC_EMBLOAD, m_embload);
}

bool CDVSGeneralPPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case CBN_SELCHANGE:
                {
                    AFX_MANAGE_STATE(AfxGetStaticModuleState());

                    if(LOWORD(wParam) == IDC_RESX2COMBO)
                    {
                        m_resx2w.EnableWindow(m_resx2.GetCurSel() == 2);
                        m_resx2h.EnableWindow(m_resx2.GetCurSel() == 2);
                        return(true);
                    }
                    else if(LOWORD(wParam) == IDC_LOADCOMBO)
                    {
                        m_extload.EnableWindow(m_load.GetCurSel() == 1);
                        m_webload.EnableWindow(m_load.GetCurSel() == 1);
                        m_embload.EnableWindow(m_load.GetCurSel() == 1);
                        return(true);
                    }
                }
                break;
            }
        }
        break;
    }

    return(false);
}

void CDVSGeneralPPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        hr = m_pDirectVobSub->put_ExtendPicture(m_HorExt, m_VerExt, m_ResX2, m_ResX2minw, m_ResX2minh);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_LoadSettings(m_LoadLevel, m_fExternalLoad, m_fWebLoad, m_fEmbeddedLoad);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        hr = m_pDirectVobSub->get_ExtendPicture(&m_HorExt, &m_VerExt, &m_ResX2, &m_ResX2minw, &m_ResX2minh);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_LoadSettings(&m_LoadLevel, &m_fExternalLoad, &m_fWebLoad, &m_fEmbeddedLoad);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

void CDVSGeneralPPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        if(m_verext.GetCurSel() >= 0) m_VerExt = m_verext.GetItemData(m_verext.GetCurSel());
        m_HorExt = !!m_mod32fix.GetCheck();
        if(m_resx2.GetCurSel() >= 0) m_ResX2 = m_resx2.GetItemData(m_resx2.GetCurSel());
        m_ResX2minw = m_resx2w.GetPos(); 
        m_ResX2minh = m_resx2h.GetPos();
        if(m_load.GetCurSel() >= 0) m_LoadLevel = m_load.GetItemData(m_load.GetCurSel());
        m_fExternalLoad = !!m_extload.GetCheck();
        m_fWebLoad = !!m_webload.GetCheck();
        m_fEmbeddedLoad = !!m_embload.GetCheck();
    }
    else
    {
        m_verext.ResetContent();
        m_verext.AddString(ResStr(IDS_ORGHEIGHT)); m_verext.SetItemData(0, 0);
        m_verext.AddString(ResStr(IDS_EXTTO169)); m_verext.SetItemData(1, 1);
        m_verext.AddString(ResStr(IDS_EXTTO43)); m_verext.SetItemData(2, 2);
        m_verext.AddString(ResStr(IDS_EXTTO480)); m_verext.SetItemData(3, 3);
        m_verext.AddString(ResStr(IDS_EXTTO576)); m_verext.SetItemData(4, 4);
        m_verext.AddString(ResStr(IDS_CROPTO169)); m_verext.SetItemData(5, 0x81);
        m_verext.AddString(ResStr(IDS_CROPTO43)); m_verext.SetItemData(6, 0x82);
        m_verext.SetCurSel((m_VerExt&0x7f) + ((m_VerExt&0x80)?4:0));
        m_mod32fix.SetCheck(m_HorExt&1);
        m_resx2.ResetContent();
        m_resx2.AddString(ResStr(IDS_ORGRES)); m_resx2.SetItemData(0, 0);
        m_resx2.AddString(ResStr(IDS_DBLRES)); m_resx2.SetItemData(1, 1);
        m_resx2.AddString(ResStr(IDS_DBLRESIF)); m_resx2.SetItemData(2, 2);
        m_resx2.SetCurSel(m_ResX2);
        m_resx2w.SetRange(0, 2048);
        m_resx2w.SetPos(m_ResX2minw);
        m_resx2w.EnableWindow(m_ResX2 == 2);
        m_resx2h.SetRange(0, 2048);
        m_resx2h.SetPos(m_ResX2minh);
        m_resx2h.EnableWindow(m_ResX2 == 2);
        m_load.ResetContent();
        m_load.AddString(ResStr(IDS_DONOTLOAD)); m_load.SetItemData(0, 2);
        m_load.AddString(ResStr(IDS_LOADWHENNEEDED)); m_load.SetItemData(1, 0);
        m_load.AddString(ResStr(IDS_ALWAYSLOAD)); m_load.SetItemData(2, 1);
        m_load.SetCurSel(!m_LoadLevel?1:m_LoadLevel==1?2:0);
        m_extload.SetCheck(m_fExternalLoad);
        m_webload.SetCheck(m_fWebLoad);
        m_embload.SetCheck(m_fEmbeddedLoad);
        m_extload.EnableWindow(m_load.GetCurSel() == 1);
        m_webload.EnableWindow(m_load.GetCurSel() == 1);
        m_embload.EnableWindow(m_load.GetCurSel() == 1);
    }
}

/* CDVSMiscPPage */

CDVSMiscPPage::CDVSMiscPPage(LPUNKNOWN pUnk, HRESULT* phr
    , TCHAR *pName /* = NAME("DirectVobSub Property Page (misc settings)") */)
    : CDVSBasePPage(pName, pUnk, IDD_DVSMISCPAGE, IDD_DVSMISCPAGE)
{
    BindControl(IDC_FLIP, m_flippic);
    BindControl(IDC_FLIPSUB, m_flipsub);
    BindControl(IDC_HIDE, m_hidesub);
    BindControl(IDC_SHOWOSDSTATS, m_showosd);
    //BindControl(IDC_PREBUFFERING, m_prebuff);
    BindControl(IDC_COMBO_COLOR_SPACE, m_colorSpaceDropList);
    BindControl(IDC_COMBO_YUV_RANGE, m_yuvRangeDropList);
    BindControl(IDC_AUTORELOAD, m_autoreload);
    BindControl(IDC_SAVEFULLPATH, m_savefullpath);
    BindControl(IDC_INSTANTUPDATE, m_instupd);
}

bool CDVSMiscPPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case BN_CLICKED:
                {
                    if(LOWORD(wParam) == IDC_INSTANTUPDATE)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());
                        theApp.WriteProfileInt(ResStr(IDS_R_GENERAL), ResStr(IDS_RG_INSTANTUPDATE), !!m_instupd.GetCheck());
                        return(true);
                    }
                }
                break;
            }
        }
        break;
    }

    return(false);
}

void CDVSMiscPPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        hr = m_pDirectVobSub->put_Flip(m_fFlipPicture, m_fFlipSubtitles);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_HideSubtitles(m_fHideSubtitles);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_OSD(m_fOSD);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_PreBuffering(m_fDoPreBuffering);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_COLOR_SPACE, m_colorSpace);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_YUV_RANGE, m_yuvRange);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_SubtitleReloader(m_fReloaderDisabled);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_SaveFullPath(m_fSaveFullPath);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        hr = m_pDirectVobSub->get_Flip(&m_fFlipPicture, &m_fFlipSubtitles);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_HideSubtitles(&m_fHideSubtitles);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_OSD(&m_fOSD);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_PreBuffering(&m_fDoPreBuffering);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_COLOR_SPACE, &m_colorSpace);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_YUV_RANGE, &m_yuvRange);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_SubtitleReloader(&m_fReloaderDisabled);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_SaveFullPath(&m_fSaveFullPath);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

void CDVSMiscPPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        m_fFlipPicture = !!m_flippic.GetCheck();
        m_fFlipSubtitles = !!m_flipsub.GetCheck();
        m_fHideSubtitles = !!m_hidesub.GetCheck();
        m_fSaveFullPath = !!m_savefullpath.GetCheck();
        //m_fDoPreBuffering = !!m_prebuff.GetCheck();

        if (m_colorSpaceDropList.GetCurSel() != CB_ERR)
        {
            m_colorSpace = static_cast<int>(
                m_colorSpaceDropList.GetItemData(m_colorSpaceDropList.GetCurSel()));
        }
        else
        {
            m_colorSpace = CDirectVobSub::YuvMatrix_AUTO;
        }

        if (m_yuvRangeDropList.GetCurSel() != CB_ERR)
        {
            m_yuvRange = static_cast<int>(
                m_yuvRangeDropList.GetItemData(m_yuvRangeDropList.GetCurSel()));
        }
        else
        {
            m_yuvRange = CDirectVobSub::YuvRange_Auto;
        }

        m_fOSD = !!m_showosd.GetCheck();
        m_fReloaderDisabled = !m_autoreload.GetCheck();
    }
    else
    {
        m_flippic.SetCheck(m_fFlipPicture);
        m_flipsub.SetCheck(m_fFlipSubtitles);
        m_hidesub.SetCheck(m_fHideSubtitles);
        m_savefullpath.SetCheck(m_fSaveFullPath);
        //m_prebuff.SetCheck(m_fDoPreBuffering);

        //CString str;str.Format(_T("m_colorSpace:%d"),m_colorSpace);
        if( m_colorSpace != CDirectVobSub::YuvMatrix_AUTO && 
            m_colorSpace != CDirectVobSub::BT_601 && 
            m_colorSpace != CDirectVobSub::BT_709 &&
            m_colorSpace != CDirectVobSub::BT_2020 &&
            m_colorSpace != CDirectVobSub::GUESS )
        {
            m_colorSpace = CDirectVobSub::YuvMatrix_AUTO;
        }
        PopulateColorSpaceCombo(m_colorSpaceDropList, m_colorSpace);

        if( m_yuvRange != CDirectVobSub::YuvRange_Auto &&
            m_yuvRange != CDirectVobSub::YuvRange_PC &&
            m_yuvRange != CDirectVobSub::YuvRange_TV )
        {
            m_yuvRange = CDirectVobSub::YuvRange_Auto;
        }
        PopulateYuvRangeCombo(m_yuvRangeDropList, m_yuvRange);

        m_showosd.SetCheck(m_fOSD);
        m_autoreload.SetCheck(!m_fReloaderDisabled);
        m_instupd.SetCheck(!!theApp.GetProfileInt(ResStr(IDS_R_GENERAL), ResStr(IDS_RG_INSTANTUPDATE), 1));
    }
}

/* CDVSTimingPPage */

CDVSTimingPPage::CDVSTimingPPage( LPUNKNOWN lpunk, HRESULT* phr
    , TCHAR *pName /*= NAME("DirectVobSub Timing Property Page")*/, int dialogId )
    : CDVSBasePPage(pName, lpunk
        , dialogId ? dialogId : IDD_DVSTIMINGPAGE
        , dialogId ? dialogId : IDD_DVSTIMINGPAGE)
{
    BindControl(IDC_SPIN5, m_subdelay);
    BindControl(IDC_SPIN6, m_subspeedmul);
    BindControl(IDC_SPIN9, m_subspeeddiv);
}

void CDVSTimingPPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        hr = m_pDirectVobSub->put_SubtitleTiming(m_SubtitleDelay, m_SubtitleSpeedMul, m_SubtitleSpeedDiv);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        hr = m_pDirectVobSub->get_SubtitleTiming(&m_SubtitleDelay, &m_SubtitleSpeedMul, &m_SubtitleSpeedDiv);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

void CDVSTimingPPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
#if _MFC_VER >= 0x0700
        m_SubtitleDelay = m_subdelay.GetPos32();
        m_SubtitleSpeedMul = m_subspeedmul.GetPos32();
        m_SubtitleSpeedDiv = m_subspeeddiv.GetPos32();
#else
        m_SubtitleDelay = SendMessage(GetDlgItem(m_Dlg, IDC_SPIN5), UDM_GETPOS32, 0, 0);
        m_SubtitleSpeedMul = SendMessage(GetDlgItem(m_Dlg, IDC_SPIN6), UDM_GETPOS32, 0, 0);
        m_SubtitleSpeedDiv = SendMessage(GetDlgItem(m_Dlg, IDC_SPIN9), UDM_GETPOS32, 0, 0);
#endif
    }
    else
    {
        m_subdelay.SetRange32(-180*60*1000, 180*60*1000);
        m_subspeedmul.SetRange32(0, 1000000);
        m_subspeeddiv.SetRange32(1, 1000000);
#if _MFC_VER >= 0x0700
        m_subdelay.SetPos32(m_SubtitleDelay);
        m_subspeedmul.SetPos32(m_SubtitleSpeedMul);
        m_subspeeddiv.SetPos32(m_SubtitleSpeedDiv);
#else
        SendMessage(GetDlgItem(m_Dlg, IDC_SPIN5), UDM_SETPOS32, 0, (LPARAM)m_SubtitleDelay);
        SendMessage(GetDlgItem(m_Dlg, IDC_SPIN6), UDM_SETPOS32, 0, (LPARAM)m_SubtitleSpeedMul);
        SendMessage(GetDlgItem(m_Dlg, IDC_SPIN9), UDM_SETPOS32, 0, (LPARAM)m_SubtitleSpeedDiv);
#endif
    }
}

/* CDVSMorePPage */
CDVSMorePPage::CDVSMorePPage(LPUNKNOWN pUnk, HRESULT* phr) :
CDVSBasePPage(NAME("DirectVobSub More Property Page"), pUnk, IDD_DVSMOREPAGE, IDD_DVSMOREPAGE)
{
    BindControl(IDC_SPINPathCache, m_path_cache);
    BindControl(IDC_SPINScanlineCache, m_scanline_cache);
    BindControl(IDC_SPINOverlayNoBlurCache, m_overlay_no_blur_cache);
    BindControl(IDC_SPINOverlayCache, m_overlay_cache);
    BindControl(IDC_COMBO_SUBPIXEL_POS, m_combo_subpixel_pos);

    BindControl(IDC_COMBO_LAYOUT_SIZE_OPT, m_combo_layout_size_opt);
    BindControl(IDC_SPIN_LAYOUT_SIZE_X, m_layout_size_x);
    BindControl(IDC_SPIN_LAYOUT_SIZE_Y, m_layout_size_y);

    BindControl(IDC_CHECKBOX_VS_ASS_RENDERING, m_vsassrendering);
    BindControl(IDC_COMBO_TEXT_RENDERER_MODE, m_text_renderer_mode_combo);
}

bool CDVSMorePPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HRESULT hr = NOERROR;
    switch(uMsg)
    {
    case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case BN_CLICKED:
                {
                    if(LOWORD(wParam) == IDC_CACHES_INFO_BTN)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());

                        DirectVobSubXyOptions::CachesInfo caches_info;
                        DirectVobSubXyOptions::XyFlyWeightInfo xy_fw_info;
                        hr = m_pDirectVobSubXy->XyGetBin2(DirectVobSubXyOptions::BIN2_CACHES_INFO, reinterpret_cast<LPVOID>(&caches_info), sizeof(caches_info));
                        CHECK_N_LOG(hr, "Failed to get option");
                        hr = m_pDirectVobSubXy->XyGetBin2(DirectVobSubXyOptions::BIN2_XY_FLY_WEIGHT_INFO, reinterpret_cast<LPVOID>(&xy_fw_info), sizeof(xy_fw_info));
                        CHECK_N_LOG(hr, "Failed to get option");
                        CString msg = FormatCachesMessage(caches_info, xy_fw_info);
                        MessageBox(
                            m_hwnd,
                            msg,
                            ResStr(IDS_XY_UI_CACHES_INFO_TITLE),
                            MB_OK | MB_ICONINFORMATION | MB_APPLMODAL
                            );
                        return(true);
                    }
                }
                break;
            }
        }
        break;
    }
    return(false);
}

void CDVSMorePPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_OVERLAY_CACHE_MAX_ITEM_NUM, m_overlay_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_OVERLAY_NO_BLUR_CACHE_MAX_ITEM_NUM, m_overlay_no_blur_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_SCAN_LINE_DATA_CACHE_MAX_ITEM_NUM, m_scan_line_data_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_PATH_DATA_CACHE_MAX_ITEM_NUM, m_path_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_SUBPIXEL_POS_LEVEL, m_subpixel_pos_level);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_LAYOUT_SIZE_OPT, m_layout_size_opt);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetSize(DirectVobSubXyOptions::SIZE_USER_SPECIFIED_LAYOUT_SIZE, m_layout_size);
        CHECK_N_LOG(hr, "Failed to set option");

        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_SUBTITLE_RENDER_BACKEND, m_fVSAssRendering ? SUBTITLE_RENDER_BACKEND_VSFILTER : SUBTITLE_RENDER_BACKEND_LIBASS);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_TEXT_RENDERER_MODE, m_text_renderer_mode);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_OVERLAY_CACHE_MAX_ITEM_NUM, &m_overlay_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_OVERLAY_NO_BLUR_CACHE_MAX_ITEM_NUM, &m_overlay_no_blur_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_SCAN_LINE_DATA_CACHE_MAX_ITEM_NUM, &m_scan_line_data_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_PATH_DATA_CACHE_MAX_ITEM_NUM, &m_path_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_SUBPIXEL_POS_LEVEL, &m_subpixel_pos_level);
        CHECK_N_LOG(hr, "Failed to get option");

        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_LAYOUT_SIZE_OPT, &m_layout_size_opt);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetSize(DirectVobSubXyOptions::SIZE_USER_SPECIFIED_LAYOUT_SIZE, &m_layout_size);
        CHECK_N_LOG(hr, "Failed to get option");

        int backend;
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_SUBTITLE_RENDER_BACKEND, &backend);
        m_fVSAssRendering = (backend == SUBTITLE_RENDER_BACKEND_VSFILTER);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_TEXT_RENDERER_MODE, &m_text_renderer_mode);
        m_text_renderer_mode = NormalizeTextRendererMode(m_text_renderer_mode);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

void CDVSMorePPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        m_overlay_cache_max_item_num = m_overlay_cache.GetPos32();
        m_overlay_no_blur_cache_max_item_num = m_overlay_no_blur_cache.GetPos32();
        m_scan_line_data_cache_max_item_num = m_scanline_cache.GetPos32();
        m_path_cache_max_item_num = m_path_cache.GetPos32();

        if (m_combo_subpixel_pos.GetCurSel() != CB_ERR)
        {
            m_subpixel_pos_level = m_combo_subpixel_pos.GetCurSel();
        }
        else
        {
            m_subpixel_pos_level = 0;
        }

        if (m_combo_layout_size_opt.GetCurSel() != CB_ERR)
        {
            m_layout_size_opt = m_combo_layout_size_opt.GetItemData(m_combo_layout_size_opt.GetCurSel());
        }
        else
        {
            m_layout_size_opt = DirectVobSubXyOptions::LAYOUT_SIZE_OPT_ORIGINAL_VIDEO_SIZE;
        }
        m_layout_size.cx = m_layout_size_x.GetPos32();
        m_layout_size.cy = m_layout_size_y.GetPos32();

        m_fVSAssRendering = !!m_vsassrendering.GetCheck();
        m_text_renderer_mode = GetTextRendererModeComboValue(m_text_renderer_mode_combo);
    }
    else
    {
        m_overlay_cache.SetRange32(0, 2048);
        m_overlay_no_blur_cache.SetRange32(0, 2048);
        m_scanline_cache.SetRange32(0, 16384);
        m_path_cache.SetRange32(0, 16384);

        m_overlay_cache.SetPos32(m_overlay_cache_max_item_num);
        m_overlay_no_blur_cache.SetPos32(m_overlay_no_blur_cache_max_item_num);
        m_scanline_cache.SetPos32(m_scan_line_data_cache_max_item_num);
        m_path_cache.SetPos32(m_path_cache_max_item_num);

        int temp = m_subpixel_pos_level;
        if(m_subpixel_pos_level < 0)
            temp = 0;
        else if ( m_subpixel_pos_level >= 5 )
            temp = 4;

        PopulateSubpixelPositionCombo(m_combo_subpixel_pos, temp);

        PopulateLayoutSizeCombo(m_combo_layout_size_opt, m_layout_size_opt);

        m_layout_size_x.SetRange32(1, 12800);
        m_layout_size_x.SetPos32(m_layout_size.cx);
        m_layout_size_y.SetRange32(1, 12800);
        m_layout_size_y.SetPos32(m_layout_size.cy);

        m_vsassrendering.SetCheck(m_fVSAssRendering);
        PopulateTextRendererModeCombo(m_text_renderer_mode_combo, m_text_renderer_mode);
    }
}

/* CDVSAboutPPage */

CDVSAboutPPage::CDVSAboutPPage( LPUNKNOWN lpunk, HRESULT* phr
    , TCHAR* pName/*=NAME("About Property Page")*/, int dialogId )
    : CDVSBasePPage(pName, lpunk
        , dialogId ? dialogId : IDD_DVSABOUTPAGE
        , dialogId ? dialogId : IDD_DVSABOUTPAGE)
{

}

bool CDVSAboutPPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HRESULT hr = NOERROR;
    switch(uMsg) {
    case WM_INITDIALOG:
        {
            CStringA version = XY_ABOUT_VERSION_STR;

            LPWSTR name = NULL;
            int chars = 0;
            hr = m_pDirectVobSubXy->XyGetString(DirectVobSubXyOptions::STRING_NAME, &name, &chars);
            CHECK_N_LOG(hr, "Failed to get option");
            CStringW str_name(name, chars);
            LocalFree(name);

            CStringA version_sha1 = XY_VSFILTER_VERSION_COMMIT_SHA1;
            if (!version_sha1.IsEmpty())
                version.Format("%ls %s (git %s)\nxy-VSFilter\nCopyright 2001-2012 Yu Zhuohuang, Gabest et. al.", str_name.GetString(), XY_ABOUT_VERSION_STR, version_sha1.Left(7));
            else
                version.Format("%ls %s\nxy-VSFilter\nCopyright 2001-2012 Yu Zhuohuang, Gabest et. al.", str_name.GetString(), XY_ABOUT_VERSION_STR);

            SetDlgItemTextA( m_Dlg, IDC_VERSION, version.GetString() );
            break;
        }
    }

    return false;
}
/* CDVSZoomPPage */

CDVSZoomPPage::CDVSZoomPPage(LPUNKNOWN pUnk, HRESULT* phr) :
    CDVSBasePPage(NAME("DirectVobSub Zoom Property Page"), pUnk, IDD_DVSZOOMPAGE, IDD_DVSZOOMPAGE)
{
    BindControl(IDC_SPIN1, m_posx);
    BindControl(IDC_SPIN2, m_posy);
    BindControl(IDC_SPIN7, m_scalex);
    BindControl(IDC_SPIN8, m_scaley);
}

bool CDVSZoomPPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case EN_CHANGE:
                {
                    if(LOWORD(wParam) == IDC_EDIT1 || LOWORD(wParam) == IDC_EDIT2
                        || LOWORD(wParam) == IDC_EDIT7 || LOWORD(wParam) == IDC_EDIT8)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());
                        UpdateControlData(true);
                        UpdateObjectData(true);
                        return(true);
                    }
                }

                break;
            }
        }
        break;
    }

    return(false);
}

void CDVSZoomPPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        m_rect.left = 1.0f * (short)m_posx.GetPos() / 100;
        m_rect.top = 1.0f * (short)m_posy.GetPos() / 100;
        m_rect.right = m_rect.left + 1.0f * (short)m_scalex.GetPos() / 100;
        m_rect.bottom = m_rect.top + 1.0f * (short)m_scaley.GetPos() / 100;
    }
    else
    {
        m_posx.SetRange(-100, 100);
        m_posx.SetPos((int)(m_rect.left*100));
        m_posy.SetRange(-100, 100);
        m_posy.SetPos((int)(m_rect.top*100));
        m_scalex.SetRange(-300, 300);
        m_scalex.SetPos((int)((m_rect.right-m_rect.left)*100));
        m_scaley.SetRange(-300, 300);
        m_scaley.SetPos((int)((m_rect.bottom-m_rect.top)*100));
    }
}

void CDVSZoomPPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        hr = m_pDirectVobSub->put_ZoomRect(&m_rect);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        hr = m_pDirectVobSub->get_ZoomRect(&m_rect);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

// TODO: Make CDVSColorPPage and CDVSPathsPPage use an interface on DirectVobSub instead of the registry to communicate

/* CDVSColorPPage */

CDVSColorPPage::CDVSColorPPage(LPUNKNOWN pUnk, HRESULT* phr
    , TCHAR *pName /*= NAME("DirectVobSub Property Page (color settings)")*/) 
    : CDVSBasePPage(pName, pUnk, IDD_DVSCOLORPAGE, IDD_DVSCOLORPAGE)
    , m_outputColorSpace(NULL)
    , m_inputColorSpace(NULL)
{
    BindControl(IDC_OUTPUT_FORMAT_LIST, m_outputFmtList);
    BindControl(IDC_INPUT_FORMAT_LIST, m_inputFmtList);
    BindControl(IDC_CHECK_FOLLOW_UPSTREAM, m_followUpstreamPreferredOrder);
    BindControl(IDC_COLORUP, m_btnColorUp);
    BindControl(IDC_COLORDOWN, m_btnColorDown);

    m_fDisableInstantUpdate = true;

    //donot know how to detect check event of CListCtrl's checkboxes
    //use this to false a update
    m_bDirty = true;
}

bool CDVSColorPPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
    case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case BN_CLICKED:
                {
                    switch(LOWORD(wParam))
                    {
                    case IDC_COLORUP:
                        {
                            int sel = m_outputFmtList.GetSelectionMark();
                            if(sel>0)
                            {
                                CString str = m_outputFmtList.GetItemText(sel,0);
                                int iPos = static_cast<int>(m_outputFmtList.GetItemData(sel));
                                BOOL checked = m_outputFmtList.GetCheck(sel);
                                m_outputFmtList.DeleteItem(sel);
                                sel--;
                                m_outputFmtList.InsertItem(sel, str);
                                m_outputFmtList.SetItemData(sel, iPos);
                                m_outputFmtList.SetItemState(sel, LVIS_SELECTED,LVIS_SELECTED);
                                m_outputFmtList.SetCheck(sel, checked);
                                m_outputFmtList.SetSelectionMark(sel);
                            }
                            if(sel>=0 && sel < m_outputFmtList.GetItemCount())
                            {
                                m_outputFmtList.SetFocus();
                            }
                            return(true);
                        }
                    case IDC_COLORDOWN:
                        {
                            int sel = m_outputFmtList.GetSelectionMark();
                            if(sel>=0 && sel < m_outputFmtList.GetItemCount()-1)
                            {
                                CString str = m_outputFmtList.GetItemText(sel,0);
                                int iPos = static_cast<int>(m_outputFmtList.GetItemData(sel));
                                BOOL checked = m_outputFmtList.GetCheck(sel);
                                m_outputFmtList.DeleteItem(sel);
                                sel++;
                                m_outputFmtList.InsertItem(sel, str);
                                m_outputFmtList.SetItemData(sel, iPos);
                                m_outputFmtList.SetItemState(sel, LVIS_SELECTED,LVIS_SELECTED);
                                m_outputFmtList.SetCheck(sel, checked);
                                m_outputFmtList.SetSelectionMark(sel);
                            }
                            if(sel>=0 && sel < m_outputFmtList.GetItemCount())
                            {
                                m_outputFmtList.SetFocus();
                            }
                            return(true);
                        }
                    case IDC_CHECK_FOLLOW_UPSTREAM:
                        {
                            AFX_MANAGE_STATE(AfxGetStaticModuleState());
                            m_btnColorUp.EnableWindow(!m_followUpstreamPreferredOrder.GetCheck());
                            m_btnColorDown.EnableWindow(!m_followUpstreamPreferredOrder.GetCheck());
                            return true;
                        }
                    }
                }
                break;
            }
        }
        break;
    }

    return(false);
}

void CDVSColorPPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        hr = m_pDirectVobSubXy->XySetBool(DirectVobSubXyOptions::BOOL_FOLLOW_UPSTREAM_PREFERRED_ORDER, m_fFollowUpstream);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetBin(DirectVobSubXyOptions::BIN_OUTPUT_COLOR_FORMAT, m_outputColorSpace, m_outputColorSpaceCount);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetBin(DirectVobSubXyOptions::BIN_INPUT_COLOR_FORMAT, m_inputColorSpace, m_inputColorSpaceCount);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        delete []m_outputColorSpace; m_outputColorSpace=NULL;
        delete []m_inputColorSpace;  m_inputColorSpace=NULL;
        hr = m_pDirectVobSubXy->XyGetBool(DirectVobSubXyOptions::BOOL_FOLLOW_UPSTREAM_PREFERRED_ORDER, &m_fFollowUpstream);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetBin(DirectVobSubXyOptions::BIN_OUTPUT_COLOR_FORMAT, reinterpret_cast<LPVOID*>(&m_outputColorSpace), &m_outputColorSpaceCount);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetBin(DirectVobSubXyOptions::BIN_INPUT_COLOR_FORMAT, reinterpret_cast<LPVOID*>(&m_inputColorSpace), &m_inputColorSpaceCount);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

void CDVSColorPPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        if(m_outputFmtList.GetItemCount() == m_outputColorSpaceCount)
        {
            for(int i = 0; i < m_outputColorSpaceCount; i++)
            {
                m_outputColorSpace[i].color_space = static_cast<ColorSpaceId>(m_outputFmtList.GetItemData(i));
                m_outputColorSpace[i].selected = (m_outputFmtList.GetCheck(i)==TRUE);
            }
        }
        else ASSERT(0);
        if(m_inputFmtList.GetItemCount() == m_inputColorSpaceCount)
        {
            for(int i = 0; i < m_inputColorSpaceCount; i++)
            {
                m_inputColorSpace[i].color_space = static_cast<ColorSpaceId>(m_inputFmtList.GetItemData(i));
                m_inputColorSpace[i].selected = (m_inputFmtList.GetCheck(i)==TRUE);
            }
        }
        else ASSERT(0);

        m_fFollowUpstream = !!m_followUpstreamPreferredOrder.GetCheck();
    }
    else
    {
        m_followUpstreamPreferredOrder.SetCheck(m_fFollowUpstream);
        m_btnColorUp.EnableWindow(!m_fFollowUpstream);
        m_btnColorDown.EnableWindow(!m_fFollowUpstream);

        m_outputFmtList.ShowScrollBar(SB_HORZ, FALSE);
        m_outputFmtList.DeleteAllItems();
        m_outputFmtList.DeleteColumn(0);
        m_outputFmtList.SetExtendedStyle( (m_outputFmtList.GetStyle()|LVS_EX_CHECKBOXES) & ~LVS_EX_GRIDLINES );
        m_outputFmtList.InsertColumn(0, ResStr(IDS_XY_UI_OUTPUT_COLUMN), LVCFMT_LEFT, 110);
        for(int i = 0; i < static_cast<int>(m_outputColorSpaceCount); i++)
        {
            m_outputFmtList.InsertItem(i, GetColorSpaceName(m_outputColorSpace[i].color_space,OUTPUT_COLOR_SPACE));
            m_outputFmtList.SetItemData(i, m_outputColorSpace[i].color_space);
            m_outputFmtList.SetCheck(i, static_cast<BOOL>(m_outputColorSpace[i].selected));
        }

        m_inputFmtList.ShowScrollBar(SB_HORZ, FALSE);
        m_inputFmtList.DeleteAllItems();
        m_inputFmtList.DeleteColumn(0);
        m_inputFmtList.SetExtendedStyle(m_inputFmtList.GetStyle()|LVS_EX_CHECKBOXES);
        m_inputFmtList.InsertColumn(0, ResStr(IDS_XY_UI_INPUT_COLUMN), LVCFMT_LEFT, 150);
        for(int i = 0; i < static_cast<int>(m_inputColorSpaceCount); i++)
        {
            m_inputFmtList.InsertItem(i, GetColorSpaceName(m_inputColorSpace[i].color_space,INPUT_COLOR_SPACE));
            m_inputFmtList.SetItemData(i, m_inputColorSpace[i].color_space);
            m_inputFmtList.SetCheck(i, static_cast<BOOL>(m_inputColorSpace[i].selected));
        }
    }
}

CDVSColorPPage::~CDVSColorPPage()
{
    delete []m_outputColorSpace;
    delete []m_inputColorSpace;
}

/* CDVSPathsPPage */

CDVSPathsPPage::CDVSPathsPPage( LPUNKNOWN lpunk, HRESULT* phr
    , TCHAR* pName /*= NAME("DirectVobSub Paths Property Page")*/, int dialogId )
    : CDVSBasePPage(pName, lpunk
        , dialogId ? dialogId : IDD_DVSPATHSPAGE
        , dialogId ? dialogId : IDD_DVSPATHSPAGE)
{
    BindControl(IDC_PATHLIST, m_pathlist);
    BindControl(IDC_PATHEDIT, m_path);
    BindControl(IDC_BROWSE, m_browse);
    BindControl(IDC_REMOVE, m_remove);
    BindControl(IDC_ADD, m_add);

    m_fDisableInstantUpdate = true;
}

bool CDVSPathsPPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case LBN_SELCHANGE:
                if((HWND)lParam == m_pathlist.m_hWnd)
                {
                    int i = m_pathlist.GetCurSel();
                    m_remove.EnableWindow(i >= 3 ? TRUE : FALSE);
                    if(i >= 0)
                    {
                        CString path;
                        m_pathlist.GetText(i, path);
                        m_path.SetWindowText(path);
                    }
                    return(true);
                }
                break;

            case LBN_SELCANCEL:
                if((HWND)lParam == m_pathlist.m_hWnd)
                {
                    m_remove.EnableWindow(FALSE);
                    return(true);
                }
                break;

            case BN_CLICKED:
                {
                    switch(LOWORD(wParam))
                    {
                    case IDC_BROWSE:
                        {
                            TCHAR pathbuff[MAX_PATH];

                            BROWSEINFO bi;
                            bi.hwndOwner = m_Dlg;
                            bi.pidlRoot = NULL;
                            bi.pszDisplayName = pathbuff;
                            bi.lpszTitle = _T("");
                            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_VALIDATE | BIF_USENEWUI;
                            bi.lpfn = NULL;
                            bi.lParam = 0;
                            bi.iImage = 0; 

                            LPITEMIDLIST iil;
                            if(iil = SHBrowseForFolder(&bi))
                            {
                                SHGetPathFromIDList(iil, pathbuff);
                                m_path.SetWindowText(pathbuff);
                            }

                            return(true);
                        }
                        break;

                    case IDC_REMOVE:
                        {
                            int i = m_pathlist.GetCurSel();
                            if(i >= 0)
                            {
                                m_pathlist.DeleteString(i);
                                i = min(i, m_pathlist.GetCount()-1);
                                if(i >= 0 && m_pathlist.GetCount() > 0)
                                {
                                    m_pathlist.SetCurSel(i);
                                    m_remove.EnableWindow(i >= 3 ? TRUE : FALSE);
                                }
                            }

                            return(true);
                        }
                        break;

                    case IDC_ADD:
                        {
                            CString path;
                            m_path.GetWindowText(path);
                            if(!path.IsEmpty() && m_pathlist.FindString(-1, path) < 0)
                                m_pathlist.AddString(path);

                            return(true);
                        }
                        break;
                    }
                }
                break;
            }
        }
        break;
    }

    return(false);
}

void CDVSPathsPPage::UpdateObjectData(bool fSave)
{
    if(fSave)
    {
        CString chk(_T("123456789")), path, tmp;
        int i = 0;
        do
        {
            tmp.Format(ResStr(IDS_RP_PATH), i++);
            path = theApp.GetProfileString(ResStr(IDS_R_DEFTEXTPATHES), tmp, chk);
            if(path != chk) theApp.WriteProfileString(ResStr(IDS_R_DEFTEXTPATHES), tmp, _T(""));
        }
        while(path != chk);

        for(i = 0; i < m_paths.GetSize(); i++)
        {
            tmp.Format(ResStr(IDS_RP_PATH), i);
            theApp.WriteProfileString(ResStr(IDS_R_DEFTEXTPATHES), tmp, m_paths[i]);
        }
    }
    else
    {
        CString chk(_T("123456789")), path, tmp;
        int i = 0;
        do
        {
            if(!path.IsEmpty()) m_paths.Add(path);
            tmp.Format(ResStr(IDS_RP_PATH), i++);
            path = theApp.GetProfileString(ResStr(IDS_R_DEFTEXTPATHES), tmp, chk);
        }
        while(path != chk);
    }
}

void CDVSPathsPPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        m_paths.RemoveAll();
        for(int i = 0; i < m_pathlist.GetCount(); i++) 
        {
            CString path;
            m_pathlist.GetText(i, path);
            m_paths.Add(path);
        }
    }
    else
    {
        m_pathlist.ResetContent();
        for(int i = 0; i < m_paths.GetSize(); i++) 
            m_pathlist.AddString(m_paths[i]);

        m_remove.EnableWindow(FALSE);
        m_add.EnableWindow(TRUE);
    }
}

//
// CXySubFilter pages
//
CXySubFilterTimingPPage::CXySubFilterTimingPPage(LPUNKNOWN pUnk, HRESULT* phr
    , TCHAR* pName)
    : CDVSTimingPPage(pUnk, phr, pName, IDD_XY_SUB_FILTER_TIMINGPAGE)
{
}

CXySubFilterPathsPPage::CXySubFilterPathsPPage(LPUNKNOWN pUnk, HRESULT* phr
    , TCHAR* pName)
    : CDVSPathsPPage(pUnk, phr, pName, IDD_XY_SUB_FILTER_PATHSPAGE)
{
}

CXySubFilterAboutPPage::CXySubFilterAboutPPage(LPUNKNOWN pUnk, HRESULT* phr
    , TCHAR* pName)
    : CDVSAboutPPage(pUnk, phr, pName, IDD_XY_SUB_FILTER_ABOUTPAGE)
{
}

//
// CXySubFilterMainPPage
//
CXySubFilterMainPPage::CXySubFilterMainPPage(LPUNKNOWN pUnk, HRESULT* phr) 
    : CDVSBasePPage(NAME("XySubFilter Property Page (main)"), pUnk, IDD_XY_SUB_FILTER_MAINPAGE, IDD_XY_SUB_FILTER_MAINPAGE)
    , m_nLangs(0)
    , m_ppLangs(NULL)
{
    BindControl(IDC_FILENAME, m_fnedit);
    BindControl(IDC_LANGCOMBO, m_langs);
    BindControl(IDC_OVERRIDEPLACEMENT, m_oplacement);
    BindControl(IDC_SPIN1, m_subposx);
    BindControl(IDC_SPIN2, m_subposy);
    BindControl(IDC_STYLES, m_styles);
    BindControl(IDC_CHECKBOX_FORCE_DEFAULT_STYLE, m_force_default_style);
    BindControl(IDC_ONLYSHOWFORCEDSUBS, m_forcedsubs);
    BindControl(IDC_CHECKBOX_HideTrayIcon, m_hide_tray_icon);

    BindControl(IDC_LOADCOMBO, m_load);
    BindControl(IDC_EXTLOAD, m_extload);
    BindControl(IDC_WEBLOAD, m_webload);
    BindControl(IDC_EMBLOAD, m_embload);
    BindControl(IDC_CSRI_PATH_EDIT, m_csriPathEdit);
    BindControl(IDC_CSRI_BROWSE, m_csriBrowseButton);
    BindControl(IDC_CSRI_RESET, m_csriResetButton);
    BindControl(IDC_COMBO_RENDER_BACKEND, m_backendCombo);
    BindControl(IDC_ACTUAL_RENDER_BACKEND, m_actualBackendLabel);
}

CXySubFilterMainPPage::~CXySubFilterMainPPage()
{
    FreeLangs();
}

void CXySubFilterMainPPage::FreeLangs()
{
    if(m_nLangs > 0 && m_ppLangs) 
    {
        for(int i = 0; i < m_nLangs; i++) CoTaskMemFree(m_ppLangs[i]);
        CoTaskMemFree(m_ppLangs);
        m_nLangs = 0;
        m_ppLangs = NULL;
    }
}

void CXySubFilterMainPPage::AllocLangs(int nLangs)
{
    m_ppLangs = (WCHAR**)CoTaskMemRealloc(m_ppLangs, sizeof(WCHAR*)*nLangs);
    m_nLangs = nLangs;
}

bool CXySubFilterMainPPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
    case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case BN_CLICKED:
                {
                    if(LOWORD(wParam) == IDC_OPEN)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());

                        CFileDialog fd(TRUE, NULL, NULL, 
                            OFN_EXPLORER|OFN_ENABLESIZING|OFN_HIDEREADONLY|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST,
                            _T(".idx .smi .sub .srt .psb .ssa .ass .usf .ssf|*.idx;*.smi;*.sub;*.srt;*.psb;*.ssa;*.ass;*.usf;*.ssf|"),
                            ResStr(IDS_XY_UI_ALL_FILES_FILTER),
                            CDialog::FromHandle(m_Dlg), 0);

                        if(fd.DoModal() == IDOK)
                        {
                            m_fnedit.SetWindowText(fd.GetPathName());
                        }

                        return(true);
                    }
                    else if(LOWORD(wParam) == IDC_STYLES)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());
                        EditStyle(m_hwnd, m_pDirectVobSubXy, &m_defStyle);
                        return(true);
                    }
                    else if(LOWORD(wParam) == IDC_CSRI_BROWSE)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());
                        CFileDialog fd(TRUE, _T("dll"), m_csriDllPath, OFN_EXPLORER | OFN_ENABLESIZING | OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
                            ResStr(IDS_XY_UI_DLL_FILES_FILTER), CDialog::FromHandle(m_Dlg), 0);
                        if(fd.DoModal() == IDOK)
                        {
							m_csriPathEdit.SetWindowText(fd.GetPathName());
                            m_csriResetButton.EnableWindow(!fd.GetPathName().IsEmpty());
                        }
                        return(true);
                    }
                    else if(LOWORD(wParam) == IDC_CSRI_RESET)
                    {
                        m_csriPathEdit.SetWindowText(CString());
                        m_csriResetButton.EnableWindow(false);
                        return(true);
                    }
                }
                break;
            case CBN_SELCHANGE:
                {
                    AFX_MANAGE_STATE(AfxGetStaticModuleState());

                    if(LOWORD(wParam) == IDC_LOADCOMBO)
                    {
                        m_extload.EnableWindow(m_load.GetCurSel() == 1);
                        m_webload.EnableWindow(m_load.GetCurSel() == 1);
                        m_embload.EnableWindow(m_load.GetCurSel() == 1);
                        return(true);
                    }
                }
                break;
            }
        }
        break;
    }
    return(false);
}

void CXySubFilterMainPPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        hr = m_pDirectVobSub->put_FileName(m_fn);
        CHECK_N_LOG(hr, "Failed to set option");
        if(hr == S_OK)
        {
            int nLangs = 0;
            hr = m_pDirectVobSub->get_LanguageCount(&nLangs);
            CHECK_N_LOG(hr, "Failed to get option");
            AllocLangs(nLangs);
            for(int i = 0; i < m_nLangs; i++) {
                hr = m_pDirectVobSub->get_LanguageName(i, &m_ppLangs[i]);
                CHECK_N_LOG(hr, "Failed to get option");
            }
            hr = m_pDirectVobSub->get_SelectedLanguage(&m_iSelectedLanguage);
            CHECK_N_LOG(hr, "Failed to get option");
        }

        hr = m_pDirectVobSub->put_SelectedLanguage(m_iSelectedLanguage);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_Placement(m_fOverridePlacement, m_PlacementXperc, m_PlacementYperc);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_VobSubSettings(true, m_fOnlyShowForcedVobSubs, false);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_TextSettings(&m_defStyle);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_AspectRatioSettings(&m_ePARCompensationType);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetBool(DirectVobSubXyOptions::BOOL_HIDE_TRAY_ICON, m_fHideTrayIcon);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_LoadSettings(m_LoadLevel, m_fExternalLoad, m_fWebLoad, m_fEmbeddedLoad);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetBool(DirectVobSubXyOptions::BOOL_FORCE_DEFAULT_STYLE, m_fForceDefaultStyle);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetString(DirectVobSubXyOptions::STRING_CSRI_LIB_PATH, m_csriDllPath.GetBuffer(), m_csriDllPath.GetLength());
        CHECK_N_LOG(hr, "Failed to set option");
        if (FAILED(hr) && !m_csriDllPath.IsEmpty()) {
            CStringW msg;
            msg.Format(ResStr(IDS_XY_UI_CSRI_LOAD_FAILED_FORMAT),
                m_csriDllPath.GetString());
            AfxMessageBox(msg, MB_ICONWARNING | MB_OK);
        }
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_SUBTITLE_RENDER_BACKEND, m_backend);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        hr = m_pDirectVobSub->get_FileName(m_fn);
        CHECK_N_LOG(hr, "Failed to get option");
        int nLangs = 0;
        hr = m_pDirectVobSub->get_LanguageCount(&nLangs); 
        CHECK_N_LOG(hr, "Failed to get option");
        AllocLangs(nLangs);
        for(int i = 0; i < m_nLangs; i++) {
            hr = m_pDirectVobSub->get_LanguageName(i, &m_ppLangs[i]);
            CHECK_N_LOG(hr, "Failed to get option");
        }
        hr = m_pDirectVobSub->get_SelectedLanguage(&m_iSelectedLanguage);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_Placement(&m_fOverridePlacement, &m_PlacementXperc, &m_PlacementYperc);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_VobSubSettings(NULL, &m_fOnlyShowForcedVobSubs, NULL);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_TextSettings(&m_defStyle);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_AspectRatioSettings(&m_ePARCompensationType);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetBool(DirectVobSubXyOptions::BOOL_HIDE_TRAY_ICON, &m_fHideTrayIcon);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_LoadSettings(&m_LoadLevel, &m_fExternalLoad, &m_fWebLoad, &m_fEmbeddedLoad);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetBool(DirectVobSubXyOptions::BOOL_FORCE_DEFAULT_STYLE, &m_fForceDefaultStyle);
        CHECK_N_LOG(hr, "Failed to get option");
        LPWSTR csriPath = nullptr;
        hr = m_pDirectVobSubXy->XyGetString(DirectVobSubXyOptions::STRING_CSRI_LIB_PATH, &csriPath, nullptr);
        if (SUCCEEDED(hr)) {
            m_csriDllPath = csriPath;
            LocalFree(csriPath);
        }
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_SUBTITLE_RENDER_BACKEND, &m_backend);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

void CXySubFilterMainPPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        CString fn;
        m_fnedit.GetWindowText(fn);
#ifdef UNICODE
        wcscpy_s(m_fn, MAX_PATH, fn);
        m_fn[MAX_PATH-1]=0;
#else
        mbstowcs(m_fn, fn, fn.GetLength()+1);
#endif
        m_iSelectedLanguage = m_langs.GetCurSel();
        m_fOverridePlacement = !!m_oplacement.GetCheck();
        m_PlacementXperc = m_subposx.GetPos();
        m_PlacementYperc = m_subposy.GetPos();
        m_fForceDefaultStyle = !!m_force_default_style.GetCheck();
        m_fOnlyShowForcedVobSubs = !!m_forcedsubs.GetCheck();
        m_fHideTrayIcon = !!m_hide_tray_icon.GetCheck();
        m_csriPathEdit.GetWindowText(m_csriDllPath);

        if(m_load.GetCurSel() >= 0) m_LoadLevel = m_load.GetItemData(m_load.GetCurSel());
        m_fExternalLoad = !!m_extload.GetCheck();
        m_fWebLoad = !!m_webload.GetCheck();
        m_fEmbeddedLoad = !!m_embload.GetCheck();
        if (m_backendCombo.GetCurSel() >= 0) m_backend = m_backendCombo.GetItemData(m_backendCombo.GetCurSel());
    }
    else
    {
        m_fnedit.SetWindowText(CString(m_fn));
        m_oplacement.SetCheck(m_fOverridePlacement);
        m_subposx.SetRange(-20, 120);
        m_subposx.SetPos(m_PlacementXperc);
        m_subposx.EnableWindow(m_fOverridePlacement);
        m_subposy.SetRange(-20, 120);
        m_subposy.SetPos(m_PlacementYperc);
        m_subposy.EnableWindow(m_fOverridePlacement);
        m_force_default_style.SetCheck(m_fForceDefaultStyle);
        m_forcedsubs.SetCheck(m_fOnlyShowForcedVobSubs);
        m_langs.ResetContent();
        m_langs.EnableWindow(m_nLangs > 0);
        for(int i = 0; i < m_nLangs; i++) m_langs.AddString(CString(m_ppLangs[i]));
        m_langs.SetCurSel(m_iSelectedLanguage);

        m_hide_tray_icon.SetCheck(m_fHideTrayIcon);

        m_load.ResetContent();
        m_load.AddString(ResStr(IDS_DONOTLOAD)); m_load.SetItemData(0, 2);
        m_load.AddString(ResStr(IDS_LOADWHENNEEDED)); m_load.SetItemData(1, 0);
        m_load.AddString(ResStr(IDS_ALWAYSLOAD)); m_load.SetItemData(2, 1);
        m_load.SetCurSel(!m_LoadLevel?1:m_LoadLevel==1?2:0);
        m_extload.SetCheck(m_fExternalLoad);
        m_webload.SetCheck(m_fWebLoad);
        m_embload.SetCheck(m_fEmbeddedLoad);
        m_extload.EnableWindow(m_load.GetCurSel() == 1);
        m_webload.EnableWindow(m_load.GetCurSel() == 1);
        m_embload.EnableWindow(m_load.GetCurSel() == 1);
        m_csriPathEdit.SetWindowText(m_csriDllPath);
        m_csriResetButton.EnableWindow(!m_csriDllPath.IsEmpty());

        const ComboEntry entries[] = {
            { IDS_XY_UI_BACKEND_LIBASS, SUBTITLE_RENDER_BACKEND_LIBASS },
            { IDS_XY_UI_BACKEND_VSFILTER, SUBTITLE_RENDER_BACKEND_VSFILTER },
            { IDS_XY_UI_BACKEND_CSRI, SUBTITLE_RENDER_BACKEND_CSRI },
        };
        PopulateCombo(m_backendCombo, entries, _countof(entries), m_backend);

        int actual_backend = SUBTITLE_RENDER_BACKEND_VSFILTER;
        if (SUCCEEDED(m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_ACTUAL_SUBTITLE_RENDER_BACKEND, &actual_backend))) {
            CString actual_backend_label;
            bool is_bitmap = false;
            m_pDirectVobSubXy->XyGetBool(DirectVobSubXyOptions::BOOL_IS_BITMAP, &is_bitmap);
            if (is_bitmap) {
                // Bitmap subtitles do not use the text renderer backend.
                actual_backend_label = ResStr(IDS_XY_UI_ACTUAL_BITMAP);
            }
            else {
                switch (NormalizeBackend(actual_backend)) {
                case SUBTITLE_RENDER_BACKEND_LIBASS:
                    actual_backend_label = ResStr(IDS_XY_UI_ACTUAL_LIBASS);
                    break;
                case SUBTITLE_RENDER_BACKEND_CSRI:
                    actual_backend_label = ResStr(IDS_XY_UI_ACTUAL_CSRI);
                    break;
                case SUBTITLE_RENDER_BACKEND_VSFILTER:
                default:
                    actual_backend_label = ResStr(IDS_XY_UI_ACTUAL_VSFILTER);
                    break;
                }
            }
            // Keep the backend column readable. HDR diagnostics live on the
            // More page where they have a full-width status row.
            m_actualBackendLabel.SetWindowText(actual_backend_label);
        }
    }
}


//
// CXySubFilterMorePPage
//
CXySubFilterMorePPage::CXySubFilterMorePPage(LPUNKNOWN pUnk, HRESULT* phr) 
    :CDVSBasePPage(NAME("XySubFilter Property Page (more)"), pUnk
    , IDD_XY_SUB_FILTER_MOREPAGE, IDD_XY_SUB_FILTER_MOREPAGE)
    , m_pgs_hdr_mode(DirectVobSubXyOptions::PGS_HDR_MODE_AUTO)
    , m_pgs_hdr_white_nits(int(HdrColor::DEFAULT_WHITE_NITS))
    , m_ui_language(DirectVobSubXyOptions::UI_LANGUAGE_AUTO)
    , m_text_hdr_mode(DirectVobSubXyOptions::PGS_HDR_MODE_AUTO)
{
    BindControl(IDC_SPINPathCache, m_path_cache);
    BindControl(IDC_SPINScanlineCache, m_scanline_cache);
    BindControl(IDC_SPINOverlayNoBlurCache, m_overlay_no_blur_cache);
    BindControl(IDC_SPINOverlayCache, m_overlay_cache);
    BindControl(IDC_COMBO_SUBPIXEL_POS, m_combo_subpixel_pos);

    BindControl(IDC_COMBO_LAYOUT_SIZE_OPT, m_combo_layout_size_opt);
    BindControl(IDC_SPIN_LAYOUT_SIZE_X, m_layout_size_x);
    BindControl(IDC_SPIN_LAYOUT_SIZE_Y, m_layout_size_y);

    BindControl(IDC_CHECKBOX_ALLOW_MOVING, m_allowmoving);
    BindControl(IDC_HIDE, m_hidesub);
    BindControl(IDC_AUTORELOAD, m_autoreload);
    BindControl(IDC_INSTANTUPDATE, m_instupd);


    BindControl(IDC_COMBO_COLOR_SPACE, m_combo_yuv_matrix);
    BindControl(IDC_COMBO_YUV_RANGE, m_combo_yuv_range);
    BindControl(IDC_COMBO_RGB_LEVEL, m_combo_rgb_level);
    BindControl(IDC_COMBO_PGS_HDR_MODE, m_combo_pgs_hdr_mode);
    BindControl(IDC_COMBO_UI_LANGUAGE, m_combo_ui_language);
    BindControl(IDC_COMBO_TEXT_HDR_MODE, m_combo_text_hdr_mode);
    BindControl(IDC_HDR_STATUS, m_hdrStatusLabel);
    BindControl(IDC_HDR_DIAG_OPEN, m_openHdrDiagnostic);
    BindControl(IDC_EDIT_PGS_HDR_WHITE_NITS, m_edit_pgs_hdr_white_nits);
    BindControl(IDC_SPIN_PGS_HDR_WHITE_NITS, m_spin_pgs_hdr_white_nits);

    BindControl(IDC_CHECKBOX_RENDER_TO_ORIGINAL_VIDEO_SIZE, m_checkbox_render_to_original_video_size);

    BindControl(IDC_EDIT_CACHE_SIZE, m_edit_cache_size);
    BindControl(IDC_COMBO_TEXT_RENDERER_MODE, m_text_renderer_mode_combo);
}

bool CXySubFilterMorePPage::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HRESULT hr = NOERROR;
    switch(uMsg)
    {
    case WM_COMMAND:
        {
            switch(HIWORD(wParam))
            {
            case BN_CLICKED:
                {
                    if(LOWORD(wParam) == IDC_CACHES_INFO_BTN)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());
                        DirectVobSubXyOptions::CachesInfo caches_info;
                        DirectVobSubXyOptions::XyFlyWeightInfo xy_fw_info;
                        hr = m_pDirectVobSubXy->XyGetBin2(DirectVobSubXyOptions::BIN2_CACHES_INFO, reinterpret_cast<LPVOID>(&caches_info), sizeof(caches_info));
                        CHECK_N_LOG(hr, "Failed to get option");
                        hr = m_pDirectVobSubXy->XyGetBin2(DirectVobSubXyOptions::BIN2_XY_FLY_WEIGHT_INFO, reinterpret_cast<LPVOID>(&xy_fw_info), sizeof(xy_fw_info));
                        CHECK_N_LOG(hr, "Failed to get option");
                        CString msg = FormatCachesMessage(caches_info, xy_fw_info);
                        MessageBox(
                            m_hwnd,
                            msg,
                            ResStr(IDS_XY_UI_CACHES_INFO_TITLE),
                            MB_OK | MB_ICONINFORMATION | MB_APPLMODAL
                            );
                        return(true);
                    }
                    else if(LOWORD(wParam) == IDC_HDR_DIAG_OPEN)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());
                        DirectVobSubImpl* dvs_impl =
                            dynamic_cast<DirectVobSubImpl*>(m_pDirectVobSub.p);
                        const CStringW path = dvs_impl
                            ? dvs_impl->GetHdrDiagnosticPath() : CStringW();
                        if (path.IsEmpty()
                            || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
                            MessageBox(
                                m_hwnd,
                                ResStr(IDS_XY_UI_HDR_DIAG_MISSING),
                                ResStr(IDS_XY_UI_HDR_DIAG_TITLE),
                                MB_OK | MB_ICONINFORMATION | MB_APPLMODAL);
                            return true;
                        }
                        const HINSTANCE result = ShellExecuteW(
                            m_hwnd, L"open", path, NULL, NULL, SW_SHOWNORMAL);
                        if (reinterpret_cast<INT_PTR>(result) <= 32) {
                            MessageBox(
                                m_hwnd,
                                ResStr(IDS_XY_UI_HDR_DIAG_FAILED),
                                ResStr(IDS_XY_UI_HDR_DIAG_TITLE),
                                MB_OK | MB_ICONERROR | MB_APPLMODAL);
                        }
                        return true;
                    }
                    else if(LOWORD(wParam) == IDC_INSTANTUPDATE)
                    {
                        AFX_MANAGE_STATE(AfxGetStaticModuleState());
                        theApp.WriteProfileInt(ResStr(IDS_R_GENERAL), ResStr(IDS_RG_INSTANTUPDATE), !!m_instupd.GetCheck());
                        return(true);
                    }
                }
                break;
            case EN_SETFOCUS:
                if(LOWORD(wParam) == IDC_EDIT_CACHE_SIZE)
                {
                    CString cache_size_str;
                    int cache_size;
                    m_edit_cache_size.GetWindowText(cache_size_str);
                    if(_stscanf_s(cache_size_str, _T("%d"), &cache_size) != 1)
                    {
                        cache_size_str.Format(_T("%d"), m_cache_size);
                        m_edit_cache_size.SetWindowText(cache_size_str);
                    }
                    return true;
                }
                break;
            case EN_CHANGE:
                if(LOWORD(wParam) == IDC_EDIT_CACHE_SIZE)
                {
                    CString cache_size_str;
                    m_edit_cache_size.GetWindowText(cache_size_str);
                    m_edit_cache_size.SetSel(cache_size_str.GetLength(),cache_size_str.GetLength()+1);
                    return true;
                }
                break;
            }
        }
        break;
    }
    return(false);
}

void CXySubFilterMorePPage::UpdateObjectData(bool fSave)
{
    HRESULT hr = NOERROR;
    if(fSave)
    {
        DirectVobSubImpl* dvs_impl =
            dynamic_cast<DirectVobSubImpl*>(m_pDirectVobSub.p);
        if (dvs_impl) {
            dvs_impl->BeginHdrOptionBatch();
        }
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_OVERLAY_CACHE_MAX_ITEM_NUM, m_overlay_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_OVERLAY_NO_BLUR_CACHE_MAX_ITEM_NUM, m_overlay_no_blur_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_SCAN_LINE_DATA_CACHE_MAX_ITEM_NUM, m_scan_line_data_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_PATH_DATA_CACHE_MAX_ITEM_NUM, m_path_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_SUBPIXEL_POS_LEVEL, m_subpixel_pos_level);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_LAYOUT_SIZE_OPT, m_layout_size_opt);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetSize(DirectVobSubXyOptions::SIZE_USER_SPECIFIED_LAYOUT_SIZE, m_layout_size);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_HideSubtitles(m_fHideSubtitles);
        CHECK_N_LOG(hr, "Failed to set option");
		hr = m_pDirectVobSubXy->XySetBool(DirectVobSubXyOptions::BOOL_ALLOW_MOVING, m_fAllowMoving);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSub->put_SubtitleReloader(m_fReloaderDisabled);
        CHECK_N_LOG(hr, "Failed to set option");


        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_YUV_RANGE, m_yuv_range);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_COLOR_SPACE, m_yuv_matrix);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_RGB_OUTPUT_TV_LEVEL, m_rgb_level);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_PGS_HDR_MODE, m_pgs_hdr_mode);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_PGS_HDR_WHITE_NITS, m_pgs_hdr_white_nits);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_UI_LANGUAGE, m_ui_language);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_TEXT_HDR_MODE, m_text_hdr_mode);
        CHECK_N_LOG(hr, "Failed to set option");

        if (dvs_impl) {
            dvs_impl->EndHdrOptionBatch();
        }

        hr = m_pDirectVobSubXy->XySetBool(DirectVobSubXyOptions::BOOL_RENDER_TO_ORIGINAL_VIDEO_SIZE, m_render_to_original_video_size);
        CHECK_N_LOG(hr, "Failed to set option");

        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_MAX_CACHE_SIZE_MB, m_cache_size);
        CHECK_N_LOG(hr, "Failed to set option");
        hr = m_pDirectVobSubXy->XySetInt(DirectVobSubXyOptions::INT_TEXT_RENDERER_MODE, m_text_renderer_mode);
        CHECK_N_LOG(hr, "Failed to set option");
    }
    else
    {
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_OVERLAY_CACHE_MAX_ITEM_NUM, &m_overlay_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_OVERLAY_NO_BLUR_CACHE_MAX_ITEM_NUM, &m_overlay_no_blur_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_SCAN_LINE_DATA_CACHE_MAX_ITEM_NUM, &m_scan_line_data_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_PATH_DATA_CACHE_MAX_ITEM_NUM, &m_path_cache_max_item_num);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_SUBPIXEL_POS_LEVEL, &m_subpixel_pos_level);
        CHECK_N_LOG(hr, "Failed to get option");

        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_LAYOUT_SIZE_OPT, &m_layout_size_opt);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetSize(DirectVobSubXyOptions::SIZE_USER_SPECIFIED_LAYOUT_SIZE, &m_layout_size);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_HideSubtitles(&m_fHideSubtitles);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetBool(DirectVobSubXyOptions::BOOL_ALLOW_MOVING, &m_fAllowMoving);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSub->get_SubtitleReloader(&m_fReloaderDisabled);
        CHECK_N_LOG(hr, "Failed to get option");


        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_YUV_RANGE, &m_yuv_range);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_COLOR_SPACE, &m_yuv_matrix);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_RGB_OUTPUT_TV_LEVEL, &m_rgb_level);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_PGS_HDR_MODE, &m_pgs_hdr_mode);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_PGS_HDR_WHITE_NITS, &m_pgs_hdr_white_nits);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_UI_LANGUAGE, &m_ui_language);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_TEXT_HDR_MODE, &m_text_hdr_mode);
        CHECK_N_LOG(hr, "Failed to get option");

        hr = m_pDirectVobSubXy->XyGetBool(DirectVobSubXyOptions::BOOL_RENDER_TO_ORIGINAL_VIDEO_SIZE, &m_render_to_original_video_size);
        CHECK_N_LOG(hr, "Failed to get option");

        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_MAX_CACHE_SIZE_MB, &m_cache_size);
        CHECK_N_LOG(hr, "Failed to get option");
        if (m_cache_size<0)
        {
            m_cache_size = -1;
        }

        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_AUTO_MAX_CACHE_SIZE_MB, &m_auto_cache_size);
        CHECK_N_LOG(hr, "Failed to get option");
        hr = m_pDirectVobSubXy->XyGetInt(DirectVobSubXyOptions::INT_TEXT_RENDERER_MODE, &m_text_renderer_mode);
        m_text_renderer_mode = NormalizeTextRendererMode(m_text_renderer_mode);
        CHECK_N_LOG(hr, "Failed to get option");
    }
}

void CXySubFilterMorePPage::UpdateControlData(bool fSave)
{
    if(fSave)
    {
        m_overlay_cache_max_item_num = m_overlay_cache.GetPos32();
        m_overlay_no_blur_cache_max_item_num = m_overlay_no_blur_cache.GetPos32();
        m_scan_line_data_cache_max_item_num = m_scanline_cache.GetPos32();
        m_path_cache_max_item_num = m_path_cache.GetPos32();

        if (m_combo_subpixel_pos.GetCurSel() != CB_ERR)
        {
            m_subpixel_pos_level = m_combo_subpixel_pos.GetCurSel();
        }
        else
        {
            m_subpixel_pos_level = 0;
        }

        if (m_combo_layout_size_opt.GetCurSel() != CB_ERR)
        {
            m_layout_size_opt = m_combo_layout_size_opt.GetItemData(m_combo_layout_size_opt.GetCurSel());
        }
        else
        {
            m_layout_size_opt = DirectVobSubXyOptions::LAYOUT_SIZE_OPT_ORIGINAL_VIDEO_SIZE;
        }
        m_layout_size.cx = m_layout_size_x.GetPos32();
        m_layout_size.cy = m_layout_size_y.GetPos32();

        m_fHideSubtitles = !!m_hidesub.GetCheck();
        m_fAllowMoving = !!m_allowmoving.GetCheck();
        m_fReloaderDisabled = !m_autoreload.GetCheck();


        if (m_combo_yuv_range.GetCurSel() != CB_ERR)
        {
            m_yuv_range = static_cast<int>(
                m_combo_yuv_range.GetItemData(m_combo_yuv_range.GetCurSel()));
        }
        else
        {
            m_yuv_range = CDirectVobSub::YuvRange_Auto;
        }
        if (m_combo_yuv_matrix.GetCurSel() != CB_ERR)
        {
            m_yuv_matrix = static_cast<int>(
                m_combo_yuv_matrix.GetItemData(m_combo_yuv_matrix.GetCurSel()));
        }
        else
        {
            m_yuv_matrix = CDirectVobSub::YuvMatrix_AUTO;
        }
        if (m_combo_rgb_level.GetCurSel() != CB_ERR)
        {
            m_rgb_level = static_cast<int>(
                m_combo_rgb_level.GetItemData(m_combo_rgb_level.GetCurSel()));
        }
        else
        {
            m_rgb_level = DirectVobSubXyOptions::RGB_OUTPUT_LEVEL_AUTO;
        }
        if (m_combo_pgs_hdr_mode.GetCurSel() != CB_ERR)
        {
            m_pgs_hdr_mode = static_cast<int>(
                m_combo_pgs_hdr_mode.GetItemData(m_combo_pgs_hdr_mode.GetCurSel()));
        }
        else
        {
            m_pgs_hdr_mode = DirectVobSubXyOptions::PGS_HDR_MODE_AUTO;
        }
        CString hdr_white_str;
        m_edit_pgs_hdr_white_nits.GetWindowText(hdr_white_str);
        int hdr_white = 0;
        if (_stscanf_s(hdr_white_str, _T("%d"), &hdr_white) == 1) {
            m_pgs_hdr_white_nits = hdr_white;
        }
        if (m_combo_ui_language.GetCurSel() != CB_ERR)
        {
            m_ui_language = static_cast<int>(
                m_combo_ui_language.GetItemData(m_combo_ui_language.GetCurSel()));
        }
        if (m_combo_text_hdr_mode.GetCurSel() != CB_ERR)
        {
            m_text_hdr_mode = static_cast<int>(
                m_combo_text_hdr_mode.GetItemData(m_combo_text_hdr_mode.GetCurSel()));
        }

        m_render_to_original_video_size = !!m_checkbox_render_to_original_video_size.GetCheck();

        CString cache_size_str;
        m_edit_cache_size.GetWindowText(cache_size_str);
        int cache_size;
        if(_stscanf_s(cache_size_str, _T("%d"), &cache_size) == 1) {
            m_cache_size = cache_size;
            if (m_cache_size<0)
            {
                m_cache_size = -1;
            }
        }
        m_text_renderer_mode = GetTextRendererModeComboValue(m_text_renderer_mode_combo);
    }
    else
    {
        m_overlay_cache.SetRange32(0, 2048);
        m_overlay_no_blur_cache.SetRange32(0, 2048);
        m_scanline_cache.SetRange32(0, 16384);
        m_path_cache.SetRange32(0, 16384);

        m_overlay_cache.SetPos32(m_overlay_cache_max_item_num);
        m_overlay_no_blur_cache.SetPos32(m_overlay_no_blur_cache_max_item_num);
        m_scanline_cache.SetPos32(m_scan_line_data_cache_max_item_num);
        m_path_cache.SetPos32(m_path_cache_max_item_num);

        int temp = m_subpixel_pos_level;
        if(m_subpixel_pos_level < 0)
            temp = 0;
        else if ( m_subpixel_pos_level >= 5 )
            temp = 4;

        PopulateSubpixelPositionCombo(m_combo_subpixel_pos, temp);

        PopulateLayoutSizeCombo(m_combo_layout_size_opt, m_layout_size_opt);

        m_layout_size_x.SetRange32(1, 12800);
        m_layout_size_x.SetPos32(m_layout_size.cx);
        m_layout_size_y.SetRange32(1, 12800);
        m_layout_size_y.SetPos32(m_layout_size.cy);

        m_hidesub.SetCheck(m_fHideSubtitles);
        m_allowmoving.SetCheck(m_fAllowMoving);
        m_autoreload.SetCheck(!m_fReloaderDisabled);
        m_instupd.SetCheck(!!theApp.GetProfileInt(ResStr(IDS_R_GENERAL), ResStr(IDS_RG_INSTANTUPDATE), 1));


        if( m_yuv_range != CDirectVobSub::YuvRange_Auto &&
            m_yuv_range != CDirectVobSub::YuvRange_PC &&
            m_yuv_range != CDirectVobSub::YuvRange_TV )
        {
            m_yuv_range = CDirectVobSub::YuvRange_Auto;
        }
        PopulateYuvRangeCombo(m_combo_yuv_range, m_yuv_range);

        if( m_yuv_matrix != CDirectVobSub::YuvMatrix_AUTO && 
            m_yuv_matrix != CDirectVobSub::BT_601 && 
            m_yuv_matrix != CDirectVobSub::BT_709 &&
            m_yuv_matrix != CDirectVobSub::BT_2020 &&
            m_yuv_matrix != CDirectVobSub::GUESS )
        {
            m_yuv_matrix = CDirectVobSub::YuvMatrix_AUTO;
        }
        PopulateColorSpaceCombo(m_combo_yuv_matrix, m_yuv_matrix);

        if (m_rgb_level != DirectVobSubXyOptions::RGB_OUTPUT_LEVEL_PC &&
            m_rgb_level != DirectVobSubXyOptions::RGB_OUTPUT_LEVEL_FORCE_TV)
        {
            m_rgb_level = DirectVobSubXyOptions::RGB_OUTPUT_LEVEL_AUTO;
        }
        PopulateRgbLevelCombo(m_combo_rgb_level, m_rgb_level);

        if (m_pgs_hdr_mode < DirectVobSubXyOptions::PGS_HDR_MODE_AUTO ||
            m_pgs_hdr_mode >= DirectVobSubXyOptions::PGS_HDR_MODE_COUNT)
        {
            m_pgs_hdr_mode = DirectVobSubXyOptions::PGS_HDR_MODE_AUTO;
        }
        // Relative-white was an early experiment for consumers that place SDR
        // white. madVR treats the bitmap as HDR and overexposes it, so keep the
        // value only for external ABI compatibility and stop exposing it.
        if (m_pgs_hdr_mode == DirectVobSubXyOptions::PGS_HDR_MODE_RELATIVE_WHITE) {
            m_pgs_hdr_mode = DirectVobSubXyOptions::PGS_HDR_MODE_AUTO;
        }
        PopulateHdrModeCombo(m_combo_pgs_hdr_mode, m_pgs_hdr_mode);

        if (m_ui_language < DirectVobSubXyOptions::UI_LANGUAGE_AUTO ||
            m_ui_language >= DirectVobSubXyOptions::UI_LANGUAGE_COUNT)
        {
            m_ui_language = DirectVobSubXyOptions::UI_LANGUAGE_AUTO;
        }
        PopulateUiLanguageCombo(m_combo_ui_language, m_ui_language);

        if (m_text_hdr_mode < DirectVobSubXyOptions::PGS_HDR_MODE_AUTO ||
            m_text_hdr_mode >= DirectVobSubXyOptions::PGS_HDR_MODE_COUNT)
        {
            m_text_hdr_mode = DirectVobSubXyOptions::PGS_HDR_MODE_AUTO;
        }
        if (m_text_hdr_mode == DirectVobSubXyOptions::PGS_HDR_MODE_RELATIVE_WHITE) {
            m_text_hdr_mode = DirectVobSubXyOptions::PGS_HDR_MODE_AUTO;
        }
        PopulateHdrModeCombo(m_combo_text_hdr_mode, m_text_hdr_mode);

        m_spin_pgs_hdr_white_nits.SetRange32(50, 1000);
        CString hdr_white_str;
        hdr_white_str.Format(_T("%d"), m_pgs_hdr_white_nits);
        SetEditTextIfChanged(m_edit_pgs_hdr_white_nits, hdr_white_str);

        m_checkbox_render_to_original_video_size.SetCheck(m_render_to_original_video_size);
        PopulateTextRendererModeCombo(m_text_renderer_mode_combo, m_text_renderer_mode);

        CString cache_size_str;
        m_edit_cache_size.GetWindowText(cache_size_str);
        if (m_cache_size<0 && cache_size_str.IsEmpty())
        {
            cache_size_str.Format(ResStr(IDS_XY_UI_AUTO_CACHE_FORMAT), m_auto_cache_size);
        }
        else
        {
            cache_size_str.Format(_T("%d"), m_cache_size);
        }
        m_edit_cache_size.SetWindowText(cache_size_str);

        DirectVobSubImpl* dvs_impl =
            dynamic_cast<DirectVobSubImpl*>(m_pDirectVobSub.p);
        if (dvs_impl) {
            m_hdrStatusLabel.SetWindowText(dvs_impl->GetHdrStatusText());
        }
    }
}
