#pragma once

#include <string>

#include "../../BaseClasses/streams.h"
#include "../../../subpic/hdr_evidence.h"
#include "../../../subpic/hdr_mkv_probe.h"

struct HdrGraphProbeResult
{
    HdrDetection::HdrEvidenceSet evidence;
    HdrDetection::HdrMediaSignature active_signature;
    std::wstring source_path;
    unsigned filters_scanned;
    unsigned pins_scanned;
    bool source_path_found;
    bool limit_reached;

    HdrGraphProbeResult();
};

enum HdrMkvFileStatus
{
    HDR_MKV_FILE_OK = 0,
    HDR_MKV_FILE_NOT_MKV,
    HDR_MKV_FILE_NOT_LOCAL,
    HDR_MKV_FILE_OPEN_FAILED,
    HDR_MKV_FILE_READ_FAILED
};

struct HdrMkvFileProbeResult
{
    HdrMkvFileStatus file_status;
    HdrDetection::HdrMkvProbeResult header;
    unsigned long long file_size;
    unsigned long long last_write_time;
    size_t bytes_read;

    HdrMkvFileProbeResult();
};

HdrDetection::HdrEvidence ReadVideoColorMetadata(const CMediaType& media_type);

HdrGraphProbeResult ProbeHdrGraph(IBaseFilter* consumer_filter);

HdrMkvFileStatus GetLocalMkvFileIdentity(
    const std::wstring& path,
    unsigned long long* file_size,
    unsigned long long* last_write_time);

HdrMkvFileProbeResult ProbeMatroskaFile(
    const std::wstring& path,
    const HdrDetection::HdrMediaSignature& active_signature);
