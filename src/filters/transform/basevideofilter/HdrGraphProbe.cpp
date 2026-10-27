#include "stdafx.h"
#include "HdrGraphProbe.h"

#include <set>
#include <vector>

#include "../../../DSUtil/DSUtil.h"
#include "../../../../include/moreuuids.h"

namespace
{
const unsigned kMaxGraphFilters = 64;
const unsigned kMaxGraphPins = 128;
const size_t kMaxMkvHeaderBytes = 2u * 1024u * 1024u;

const VIDEOINFOHEADER2* GetVideoInfo2(const CMediaType& media_type)
{
    if (!media_type.Format()) {
        return NULL;
    }

    const size_t length = media_type.FormatLength();
    if (media_type.formattype == FORMAT_VideoInfo2
        && length >= sizeof(VIDEOINFOHEADER2)) {
        return reinterpret_cast<const VIDEOINFOHEADER2*>(media_type.Format());
    }
    if (media_type.formattype == FORMAT_MPEG2_VIDEO
        && length >= FIELD_OFFSET(MPEG2VIDEOINFO, hdr) + sizeof(VIDEOINFOHEADER2)) {
        return &reinterpret_cast<const MPEG2VIDEOINFO*>(
            media_type.Format())->hdr;
    }
    if (media_type.formattype == FORMAT_DiracVideoInfo
        && length >= FIELD_OFFSET(DIRACINFOHEADER, hdr) + sizeof(VIDEOINFOHEADER2)) {
        return &reinterpret_cast<const DIRACINFOHEADER*>(
            media_type.Format())->hdr;
    }
    return NULL;
}

const VIDEOINFOHEADER* GetVideoInfo(const CMediaType& media_type)
{
    if (!media_type.Format()) {
        return NULL;
    }
    if (media_type.formattype == FORMAT_VideoInfo
        && media_type.FormatLength() >= sizeof(VIDEOINFOHEADER)) {
        return reinterpret_cast<const VIDEOINFOHEADER*>(media_type.Format());
    }
    if (media_type.formattype == FORMAT_MPEGVideo
        && media_type.FormatLength() >= sizeof(MPEG1VIDEOINFO)) {
        return &reinterpret_cast<const MPEG1VIDEOINFO*>(
            media_type.Format())->hdr;
    }
    return NULL;
}

HdrDetection::HdrCodecFamily ClassifyCodec(const CMediaType& media_type)
{
    if (media_type.subtype == MEDIASUBTYPE_HEVC
        || media_type.subtype == MEDIASUBTYPE_HVC1) {
        return HdrDetection::HDR_CODEC_HEVC;
    }
    if (media_type.subtype == MEDIASUBTYPE_H264
        || media_type.subtype == MEDIASUBTYPE_H264_bis
        || media_type.subtype == MEDIASUBTYPE_avc1) {
        return HdrDetection::HDR_CODEC_H264;
    }
    if (media_type.subtype == MEDIASUBTYPE_VP90) {
        return HdrDetection::HDR_CODEC_VP9;
    }
    if (media_type.subtype == MEDIASUBTYPE_MPEG2_VIDEO) {
        return HdrDetection::HDR_CODEC_MPEG2;
    }

    BITMAPINFOHEADER bih = {};
    if (ExtractBIH(&media_type, &bih)) {
        if (bih.biCompression == MAKEFOURCC('A', 'V', '0', '1')) {
            return HdrDetection::HDR_CODEC_AV1;
        }
        if (bih.biCompression == MAKEFOURCC('H', 'E', 'V', 'C')
            || bih.biCompression == MAKEFOURCC('H', '2', '6', '5')) {
            return HdrDetection::HDR_CODEC_HEVC;
        }
        if (bih.biCompression == MAKEFOURCC('H', '2', '6', '4')
            || bih.biCompression == MAKEFOURCC('A', 'V', 'C', '1')) {
            return HdrDetection::HDR_CODEC_H264;
        }
        if (bih.biCompression == MAKEFOURCC('V', 'P', '9', '0')) {
            return HdrDetection::HDR_CODEC_VP9;
        }
    }
    return HdrDetection::HDR_CODEC_UNKNOWN;
}

void UpdateActiveSignature(
    const CMediaType& media_type,
    int depth,
    int* active_depth,
    HdrDetection::HdrMediaSignature* signature)
{
    if (!active_depth || !signature || depth < *active_depth) {
        return;
    }

    HdrDetection::HdrMediaSignature candidate;
    candidate.codec = ClassifyCodec(media_type);

    BITMAPINFOHEADER bih = {};
    if (ExtractBIH(&media_type, &bih)) {
        candidate.width = bih.biWidth > 0 ? unsigned(bih.biWidth) : 0;
        candidate.height = bih.biHeight < 0
            ? unsigned(-bih.biHeight)
            : unsigned(bih.biHeight);
    }

    const VIDEOINFOHEADER2* video_info2 = GetVideoInfo2(media_type);
    if (video_info2) {
        if (video_info2->AvgTimePerFrame > 0) {
            candidate.frame_duration_ns =
                static_cast<unsigned long long>(
                    video_info2->AvgTimePerFrame) * 100ULL;
        }
    }
    else {
        const VIDEOINFOHEADER* video_info = GetVideoInfo(media_type);
        if (video_info && video_info->AvgTimePerFrame > 0) {
            candidate.frame_duration_ns =
                static_cast<unsigned long long>(
                    video_info->AvgTimePerFrame) * 100ULL;
        }
    }

    if (candidate.width || candidate.height
        || candidate.codec != HdrDetection::HDR_CODEC_UNKNOWN
        || candidate.frame_duration_ns) {
        *signature = candidate;
        *active_depth = depth;
    }
}

std::wstring ReadSourcePath(IBaseFilter* filter)
{
    CComQIPtr<IFileSourceFilter> source(filter);
    if (!source) {
        return std::wstring();
    }

    LPOLESTR source_path = NULL;
    AM_MEDIA_TYPE source_type;
    ZeroMemory(&source_type, sizeof(source_type));
    if (FAILED(source->GetCurFile(&source_path, &source_type))
        || !source_path) {
        if (source_type.pbFormat) {
            CoTaskMemFree(source_type.pbFormat);
        }
        if (source_type.pUnk) {
            source_type.pUnk->Release();
        }
        return std::wstring();
    }

    std::wstring result(source_path);
    CoTaskMemFree(source_path);
    if (source_type.pbFormat) {
        CoTaskMemFree(source_type.pbFormat);
    }
    if (source_type.pUnk) {
        source_type.pUnk->Release();
    }
    return result;
}

class GraphWalker
{
public:
    explicit GraphWalker(IBaseFilter* consumer)
        : m_consumer(consumer)
    {
        m_result.evidence = HdrDetection::HdrEvidenceSet();
        m_result.active_signature = HdrDetection::HdrMediaSignature();
    }

    HdrGraphProbeResult Run()
    {
        WalkFilter(m_consumer, 0, false);
        m_result.filters_scanned = unsigned(m_visited_filters.size());
        m_result.pins_scanned = unsigned(m_visited_pins.size());
        return m_result;
    }

private:
    void WalkFilter(IBaseFilter* filter, int depth, bool source_filter)
    {
        if (!filter || m_result.limit_reached
            || m_visited_filters.size() >= kMaxGraphFilters
            || m_visited_pins.size() >= kMaxGraphPins) {
            if (m_visited_filters.size() >= kMaxGraphFilters
                || m_visited_pins.size() >= kMaxGraphPins) {
                m_result.limit_reached = true;
            }
            return;
        }

        if (m_visited_filters.find(filter) != m_visited_filters.end()) {
            return;
        }
        m_visited_filters.insert(filter);
        m_kept_filters.push_back(filter);

        if (!source_filter && depth > 0) {
            CComQIPtr<IFileSourceFilter> source(filter);
            source_filter = source ? true : false;
        }
        if (!m_result.source_path_found && source_filter) {
            const std::wstring path = ReadSourcePath(filter);
            if (!path.empty()) {
                m_result.source_path = path;
                m_result.source_path_found = true;
            }
        }

        CComPtr<IEnumPins> pins;
        if (FAILED(filter->EnumPins(&pins)) || !pins) {
            return;
        }

        CComPtr<IPin> pin;
        while (pins->Next(1, &pin, NULL) == S_OK) {
            if (m_visited_pins.size() >= kMaxGraphPins) {
                m_result.limit_reached = true;
                break;
            }

            PIN_DIRECTION direction = PINDIR_OUTPUT;
            if (FAILED(pin->QueryDirection(&direction))
                || direction != PINDIR_INPUT
                || m_visited_pins.find(pin) != m_visited_pins.end()) {
                pin.Release();
                continue;
            }
            m_visited_pins.insert(pin);
            m_kept_pins.push_back(pin);

            CMediaType media_type;
            if (FAILED(pin->ConnectionMediaType(&media_type))
                || media_type.majortype != MEDIATYPE_Video) {
                pin.Release();
                continue;
            }

            ProbeMediaType(
                media_type, depth,
                source_filter
                    ? HdrDetection::HDR_EVIDENCE_STAGE_SOURCE
                    : depth == 0
                        ? HdrDetection::HDR_EVIDENCE_STAGE_CONSUMER
                        : HdrDetection::HDR_EVIDENCE_STAGE_UPSTREAM);

            CComPtr<IPin> upstream_pin;
            if (SUCCEEDED(pin->ConnectedTo(&upstream_pin)) && upstream_pin) {
                PIN_INFO pin_info = {};
                if (SUCCEEDED(upstream_pin->QueryPinInfo(&pin_info))
                    && pin_info.pFilter) {
                    CComPtr<IBaseFilter> upstream_filter;
                    upstream_filter.Attach(pin_info.pFilter);
                    WalkFilter(upstream_filter, depth + 1, false);
                }
            }
            pin.Release();
        }
    }

    void ProbeMediaType(
        const CMediaType& media_type,
        int depth,
        HdrDetection::HdrEvidenceStage stage)
    {
        HdrDetection::HdrEvidence evidence =
            ReadVideoColorMetadata(media_type);
        evidence.source = HdrDetection::HDR_EVIDENCE_SOURCE_GRAPH;
        evidence.stage = stage;
        evidence.depth = depth;
        m_result.evidence.Add(evidence);

        if (stage == HdrDetection::HDR_EVIDENCE_STAGE_SOURCE) {
            m_result.source_path_found =
                m_result.source_path_found || !m_result.source_path.empty();
        }
        UpdateActiveSignature(
            media_type, depth, &m_active_depth, &m_result.active_signature);
    }

    IBaseFilter* m_consumer;
    HdrGraphProbeResult m_result;
    std::set<IBaseFilter*> m_visited_filters;
    std::set<IPin*> m_visited_pins;
    std::vector<CComPtr<IBaseFilter> > m_kept_filters;
    std::vector<CComPtr<IPin> > m_kept_pins;
    int m_active_depth = -1;
};

bool IsRemoteOrDevicePath(const std::wstring& path)
{
    const wchar_t* prefixes[] = {
        L"http://", L"https://", L"ftp://", L"mms://", L"rtsp://"
    };
    for (size_t i = 0; i < _countof(prefixes); ++i) {
        const size_t length = wcslen(prefixes[i]);
        if (path.size() >= length
            && _wcsnicmp(path.c_str(), prefixes[i], length) == 0) {
            return true;
        }
    }
    return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
}

bool HasMkvExtension(const std::wstring& path)
{
    if (path.size() < 4) {
        return false;
    }
    return _wcsicmp(path.c_str() + path.size() - 4, L".mkv") == 0;
}

unsigned long long FileTimeToUint64(const FILETIME& file_time)
{
    ULARGE_INTEGER value;
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart;
}
}

HdrGraphProbeResult::HdrGraphProbeResult()
    : filters_scanned(0)
    , pins_scanned(0)
    , source_path_found(false)
    , limit_reached(false)
{
}

HdrMkvFileProbeResult::HdrMkvFileProbeResult()
    : file_status(HDR_MKV_FILE_OK)
    , file_size(0)
    , last_write_time(0)
    , bytes_read(0)
{
}

HdrDetection::HdrEvidence ReadVideoColorMetadata(const CMediaType& media_type)
{
    HdrDetection::HdrEvidence metadata;
    if (media_type.majortype != MEDIATYPE_Video) {
        return metadata;
    }

    const VIDEOINFOHEADER2* video_info = GetVideoInfo2(media_type);
    if (!video_info) {
        return metadata;
    }

    const DWORD flags = video_info->dwControlFlags;
    const bool color_info_present =
        (flags & AMCONTROL_USED) != 0
        && (flags & AMCONTROL_COLORINFO_PRESENT) != 0;
    const int transfer_matrix = int((flags & 0x00038000) >> 15);
    metadata.control_flags = flags;
    metadata.transfer_function = int((flags & 0xF8000000) >> 27);
    metadata.has_transfer = metadata.transfer_function >= 0;
    metadata.matrix = HdrColor::ClassifyVideoMatrix(
        transfer_matrix, color_info_present);
    metadata.has_matrix = metadata.matrix != HdrColor::VIDEO_MATRIX_UNKNOWN;
    metadata.signal = HdrColor::ClassifyTransferFunction(
        metadata.transfer_function, color_info_present);
    return metadata;
}

HdrGraphProbeResult ProbeHdrGraph(IBaseFilter* consumer_filter)
{
    return GraphWalker(consumer_filter).Run();
}

HdrMkvFileStatus GetLocalMkvFileIdentity(
    const std::wstring& path,
    unsigned long long* file_size,
    unsigned long long* last_write_time)
{
    if (!file_size || !last_write_time) {
        return HDR_MKV_FILE_READ_FAILED;
    }
    if (!HasMkvExtension(path)) {
        return HDR_MKV_FILE_NOT_MKV;
    }
    if (IsRemoteOrDevicePath(path)) {
        return HDR_MKV_FILE_NOT_LOCAL;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExW(
            path.c_str(), GetFileExInfoStandard, &attributes)
        || (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return HDR_MKV_FILE_OPEN_FAILED;
    }

    ULARGE_INTEGER size;
    size.HighPart = attributes.nFileSizeHigh;
    size.LowPart = attributes.nFileSizeLow;
    *file_size = size.QuadPart;
    *last_write_time = FileTimeToUint64(attributes.ftLastWriteTime);
    return HDR_MKV_FILE_OK;
}

HdrMkvFileProbeResult ProbeMatroskaFile(
    const std::wstring& path,
    const HdrDetection::HdrMediaSignature& active_signature)
{
    HdrMkvFileProbeResult result;
    if (!HasMkvExtension(path)) {
        result.file_status = HDR_MKV_FILE_NOT_MKV;
        return result;
    }
    if (IsRemoteOrDevicePath(path)) {
        result.file_status = HDR_MKV_FILE_NOT_LOCAL;
        return result;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        result.file_status = HDR_MKV_FILE_OPEN_FAILED;
        return result;
    }

    BY_HANDLE_FILE_INFORMATION info = {};
    LARGE_INTEGER size = {};
    if (!GetFileInformationByHandle(file, &info)
        || !GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
        CloseHandle(file);
        result.file_status = HDR_MKV_FILE_READ_FAILED;
        return result;
    }

    result.file_size = static_cast<unsigned long long>(size.QuadPart);
    result.last_write_time = FileTimeToUint64(info.ftLastWriteTime);
    const size_t to_read = size_t(std::min<unsigned long long>(
        result.file_size, kMaxMkvHeaderBytes));
    std::vector<unsigned char> buffer(to_read);

    size_t total = 0;
    while (total < buffer.size()) {
        DWORD read = 0;
        const size_t remaining = buffer.size() - total;
        const DWORD request = remaining > 0xFFFFFFFFu
            ? 0xFFFFFFFFu
            : DWORD(remaining);
        if (!ReadFile(
                file,
                buffer.data() + total,
                request,
                &read,
                NULL)
            || read == 0) {
            CloseHandle(file);
            result.file_status = HDR_MKV_FILE_READ_FAILED;
            return result;
        }
        total += read;
    }
    CloseHandle(file);

    result.bytes_read = total;
    result.header = HdrDetection::ProbeMatroskaHeader(
        buffer.empty() ? NULL : buffer.data(),
        total,
        active_signature);
    return result;
}
