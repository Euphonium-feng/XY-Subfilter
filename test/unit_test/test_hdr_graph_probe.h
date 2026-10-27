#ifndef __TEST_HDR_GRAPH_PROBE_H__
#define __TEST_HDR_GRAPH_PROBE_H__

#include <gtest/gtest.h>

#include "../../src/filters/BaseClasses/streams.h"
#include "../../src/filters/transform/basevideofilter/HdrGraphProbe.h"
#include "../../src/DSUtil/DSUtil.h"
#include "../../include/moreuuids.h"

namespace {

const GUID kProbeP010 = {
    0x30313050, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}
};

CMediaType MakeProbeVideoType(
    const GUID& subtype,
    const GUID& format_type,
    DWORD control_flags)
{
    CMediaType media_type;
    media_type.SetType(&MEDIATYPE_Video);
    media_type.SetSubtype(&subtype);
    media_type.SetFormatType(&format_type);

    if (format_type == FORMAT_MPEG2_VIDEO) {
        const size_t length =
            FIELD_OFFSET(MPEG2VIDEOINFO, dwSequenceHeader);
        MPEG2VIDEOINFO* info = reinterpret_cast<MPEG2VIDEOINFO*>(
            media_type.AllocFormatBuffer(length));
        ZeroMemory(info, length);
        info->hdr.dwControlFlags = control_flags;
        info->hdr.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->hdr.bmiHeader.biWidth = 1920;
        info->hdr.bmiHeader.biHeight = 1080;
        info->hdr.AvgTimePerFrame = 416667;
    }
    else if (format_type == FORMAT_DiracVideoInfo) {
        const size_t length =
            FIELD_OFFSET(DIRACINFOHEADER, dwSequenceHeader);
        DIRACINFOHEADER* info = reinterpret_cast<DIRACINFOHEADER*>(
            media_type.AllocFormatBuffer(length));
        ZeroMemory(info, length);
        info->hdr.dwControlFlags = control_flags;
        info->hdr.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->hdr.bmiHeader.biWidth = 1920;
        info->hdr.bmiHeader.biHeight = 1080;
        info->hdr.AvgTimePerFrame = 416667;
        info->cbSequenceHeader = 0;
    }
    else {
        VIDEOINFOHEADER2* info =
            reinterpret_cast<VIDEOINFOHEADER2*>(
                media_type.AllocFormatBuffer(sizeof(VIDEOINFOHEADER2)));
        ZeroMemory(info, sizeof(*info));
        info->dwControlFlags = control_flags;
        info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->bmiHeader.biWidth = 1920;
        info->bmiHeader.biHeight = 1080;
        info->AvgTimePerFrame = 416667;
    }
    return media_type;
}

class ProbeSourcePin : public CBaseOutputPin
{
public:
    ProbeSourcePin(
        CBaseFilter* filter,
        CCritSec* lock,
        HRESULT* result,
        const CMediaType& media_type)
        : CBaseOutputPin(
            NAME("ProbeSourcePin"), filter, lock, result, L"Output")
        , m_media_type(media_type)
    {
    }

    HRESULT CheckMediaType(const CMediaType* media_type)
    {
        return media_type && media_type->majortype == MEDIATYPE_Video
            ? S_OK : S_FALSE;
    }

    HRESULT GetMediaType(int position, CMediaType* media_type)
    {
        if (position < 0) {
            return E_INVALIDARG;
        }
        if (position > 0) {
            return VFW_S_NO_MORE_ITEMS;
        }
        *media_type = m_media_type;
        return S_OK;
    }

    HRESULT DecideBufferSize(
        IMemAllocator*, ALLOCATOR_PROPERTIES*)
    {
        return E_NOTIMPL;
    }

private:
    CMediaType m_media_type;
};

class ProbeSourceFilter : public CBaseFilter
{
public:
    ProbeSourceFilter(const CMediaType& media_type, HRESULT* result)
        : CBaseFilter(
            NAME("ProbeSourceFilter"), NULL, &m_lock, CLSID_NULL)
        , m_pin(this, &m_lock, result, media_type)
    {
    }

    int GetPinCount() { return 1; }
    CBasePin* GetPin(int position)
    {
        return position == 0 ? &m_pin : NULL;
    }

private:
    CCritSec m_lock;
    ProbeSourcePin m_pin;
};

class ProbeInputPin : public CBaseInputPin
{
public:
    ProbeInputPin(
        CBaseFilter* filter,
        CCritSec* lock,
        HRESULT* result,
        LPCWSTR name)
        : CBaseInputPin(NAME("ProbeInputPin"), filter, lock, result, name)
    {
    }

    HRESULT CheckMediaType(const CMediaType* media_type)
    {
        return media_type && media_type->majortype == MEDIATYPE_Video
            ? S_OK : S_FALSE;
    }

    STDMETHODIMP Receive(IMediaSample*)
    {
        return S_OK;
    }
};

class ProbeMiddleFilter : public CBaseFilter
{
public:
    ProbeMiddleFilter(
        const CMediaType& input_type,
        const CMediaType& output_type,
        HRESULT* result)
        : CBaseFilter(
            NAME("ProbeMiddleFilter"), NULL, &m_lock, CLSID_NULL)
        , m_input(this, &m_lock, result, L"Input")
        , m_output(this, &m_lock, result, output_type)
    {
        (void)input_type;
    }

    int GetPinCount() { return 2; }
    CBasePin* GetPin(int position)
    {
        if (position == 0) return &m_input;
        if (position == 1) return &m_output;
        return NULL;
    }

    ProbeInputPin* Input() { return &m_input; }
    ProbeSourcePin* Output() { return &m_output; }

private:
    CCritSec m_lock;
    ProbeInputPin m_input;
    ProbeSourcePin m_output;
};

class ProbeConsumerFilter : public CBaseFilter
{
public:
    ProbeConsumerFilter(HRESULT* result)
        : CBaseFilter(
            NAME("ProbeConsumerFilter"), NULL, &m_lock, CLSID_NULL)
        , m_input(this, &m_lock, result, L"Video")
    {
    }

    int GetPinCount() { return 1; }
    CBasePin* GetPin(int position)
    {
        return position == 0 ? &m_input : NULL;
    }

    ProbeInputPin* Input() { return &m_input; }

private:
    CCritSec m_lock;
    ProbeInputPin m_input;
};

TEST(HdrGraphProbeTest, ReadsVideoInfo2DirectAndNestedFormats)
{
    const DWORD flags =
        AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT | (8u << 27) | (4u << 15);
    const CMediaType direct = MakeProbeVideoType(
        kProbeP010, FORMAT_VideoInfo2, flags);
    const CMediaType mpeg2 = MakeProbeVideoType(
        MEDIASUBTYPE_MPEG2_VIDEO, FORMAT_MPEG2_VIDEO, flags);
    const CMediaType dirac = MakeProbeVideoType(
        MEDIASUBTYPE_DiracVideo, FORMAT_DiracVideoInfo, flags);

    EXPECT_EQ(
        HdrColor::HDR_SIGNAL_HDR,
        ReadVideoColorMetadata(direct).signal);
    EXPECT_EQ(
        HdrColor::HDR_SIGNAL_HDR,
        ReadVideoColorMetadata(mpeg2).signal);
    EXPECT_EQ(
        HdrColor::HDR_SIGNAL_HDR,
        ReadVideoColorMetadata(dirac).signal);
    EXPECT_EQ(
        HdrColor::VIDEO_MATRIX_2020,
        ReadVideoColorMetadata(direct).matrix);
}

TEST(HdrGraphProbeTest, TruncatedVideoInfoIsRejected)
{
    CMediaType media_type;
    media_type.SetType(&MEDIATYPE_Video);
    media_type.SetSubtype(&kProbeP010);
    media_type.SetFormatType(&FORMAT_VideoInfo2);
    media_type.AllocFormatBuffer(sizeof(BITMAPINFOHEADER));

    const HdrDetection::HdrEvidence evidence =
        ReadVideoColorMetadata(media_type);
    EXPECT_EQ(HdrColor::HDR_SIGNAL_UNKNOWN, evidence.signal);
}

TEST(HdrGraphProbeTest, FindsUpstreamPqBehindRgbConsumerInput)
{
    const DWORD pq_flags =
        AMCONTROL_USED | AMCONTROL_COLORINFO_PRESENT | (8u << 27) | (4u << 15);
    const CMediaType compressed = MakeProbeVideoType(
        MEDIASUBTYPE_HEVC, FORMAT_VideoInfo2, pq_flags);
    const CMediaType rgb = MakeProbeVideoType(
        MEDIASUBTYPE_ARGB32, FORMAT_VideoInfo2, 0);

    HRESULT result = S_OK;
    ProbeSourceFilter* source = new ProbeSourceFilter(compressed, &result);
    ASSERT_NE(nullptr, source);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> source_lifetime = source;

    ProbeMiddleFilter* middle =
        new ProbeMiddleFilter(compressed, rgb, &result);
    ASSERT_NE(nullptr, middle);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> middle_lifetime = middle;

    ProbeConsumerFilter* consumer = new ProbeConsumerFilter(&result);
    ASSERT_NE(nullptr, consumer);
    ASSERT_EQ(S_OK, result);
    CComPtr<IBaseFilter> consumer_lifetime = consumer;

    ASSERT_EQ(
        S_OK,
        middle->Input()->ReceiveConnection(
            source->GetPin(0), &compressed));
    ASSERT_EQ(
        S_OK,
        consumer->Input()->ReceiveConnection(
            middle->Output(), &rgb));

    const HdrGraphProbeResult probe = ProbeHdrGraph(consumer);
    EXPECT_EQ(HdrColor::HDR_SIGNAL_HDR, probe.evidence.signal);
    EXPECT_EQ(HdrDetection::HDR_CODEC_HEVC, probe.active_signature.codec);
    EXPECT_EQ(1920u, probe.active_signature.width);
    EXPECT_EQ(1080u, probe.active_signature.height);
    EXPECT_FALSE(probe.limit_reached);

    consumer->Input()->Disconnect();
    middle->Output()->Disconnect();
    middle->Input()->Disconnect();
    source->GetPin(0)->Disconnect();
}

}
#endif // __TEST_HDR_GRAPH_PROBE_H__
