/*
 * Bounded Matroska header inspection used only as an AUTO HDR fallback.
 * It never reads clusters or media payloads.
 */

#pragma once

#include <cstddef>
#include <vector>

#include "hdr_evidence.h"

namespace HdrDetection
{
    enum HdrMkvProbeStatus
    {
        HDR_MKV_PROBE_OK = 0,
        HDR_MKV_PROBE_UNSUPPORTED,
        HDR_MKV_PROBE_INVALID,
        HDR_MKV_PROBE_TRUNCATED,
        HDR_MKV_PROBE_LIMIT_EXCEEDED
    };

    struct HdrMkvTrack
    {
        unsigned long long track_number;
        HdrCodecFamily codec;
        unsigned width;
        unsigned height;
        unsigned long long frame_duration_ns;
        int transfer_characteristics;
        int matrix_coefficients;
        int primaries;
        HdrEvidence evidence;

        HdrMkvTrack();
    };

    struct HdrMkvProbeResult
    {
        HdrMkvProbeStatus status;
        unsigned long long elements_scanned;
        int selected_track_index;
        std::vector<HdrMkvTrack> video_tracks;

        HdrMkvProbeResult();
    };

    // Signature matching is deliberately conservative: ambiguous multi-track
    // files produce no selected track unless codec/size/frame duration isolate
    // exactly one candidate.
    HdrMkvProbeResult ProbeMatroskaHeader(
        const unsigned char* data,
        size_t size,
        const HdrMediaSignature& active_signature);

    const HdrMkvTrack* GetSelectedMkvTrack(const HdrMkvProbeResult& result);
}
