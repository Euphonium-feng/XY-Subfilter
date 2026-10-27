/*
 * Pure HDR evidence selection shared by the DirectShow graph scanner and the
 * AUTO mode resolver.
 */

#pragma once

#include "hdr_color.h"

namespace HdrDetection
{
    enum HdrEvidenceSource
    {
        HDR_EVIDENCE_SOURCE_NONE = 0,
        HDR_EVIDENCE_SOURCE_CONSUMER,
        HDR_EVIDENCE_SOURCE_GRAPH,
        HDR_EVIDENCE_SOURCE_CONTAINER,
        HDR_EVIDENCE_SOURCE_SIDE_DATA,
        HDR_EVIDENCE_SOURCE_FILENAME,
        HDR_EVIDENCE_SOURCE_CONSUMER_MATRIX
    };

    enum HdrEvidenceStage
    {
        HDR_EVIDENCE_STAGE_NONE = 0,
        HDR_EVIDENCE_STAGE_CONSUMER,
        HDR_EVIDENCE_STAGE_UPSTREAM,
        HDR_EVIDENCE_STAGE_SOURCE,
        HDR_EVIDENCE_STAGE_CONTAINER
    };

    enum HdrCodecFamily
    {
        HDR_CODEC_UNKNOWN = 0,
        HDR_CODEC_H264,
        HDR_CODEC_HEVC,
        HDR_CODEC_AV1,
        HDR_CODEC_VP9,
        HDR_CODEC_MPEG2,
        HDR_CODEC_OTHER
    };

    struct HdrMediaSignature
    {
        unsigned width;
        unsigned height;
        HdrCodecFamily codec;
        unsigned long long frame_duration_ns;

        HdrMediaSignature()
            : width(0)
            , height(0)
            , codec(HDR_CODEC_UNKNOWN)
            , frame_duration_ns(0)
        {
        }
    };

    struct HdrEvidence
    {
        HdrColor::HdrSignal signal;
        int transfer_function;
        unsigned long control_flags;
        HdrColor::VideoMatrix matrix;
        int primaries;
        bool has_transfer;
        bool has_matrix;
        bool has_primaries;
        bool mastering_metadata;
        bool content_light;
        HdrEvidenceSource source;
        HdrEvidenceStage stage;
        int depth;

        HdrEvidence()
            : signal(HdrColor::HDR_SIGNAL_UNKNOWN)
            , transfer_function(-1)
            , control_flags(0)
            , matrix(HdrColor::VIDEO_MATRIX_UNKNOWN)
            , primaries(-1)
            , has_transfer(false)
            , has_matrix(false)
            , has_primaries(false)
            , mastering_metadata(false)
            , content_light(false)
            , source(HDR_EVIDENCE_SOURCE_NONE)
            , stage(HDR_EVIDENCE_STAGE_NONE)
            , depth(0)
        {
        }
    };

    struct HdrEvidenceSet
    {
        HdrColor::HdrSignal signal;
        HdrColor::VideoMatrix matrix;
        HdrColor::HdrSignal selected_signal;
        HdrEvidenceSource selected_source;
        HdrEvidenceStage selected_stage;
        int selected_depth;
        int selected_transfer_function;
        unsigned long selected_control_flags;
        int selected_primaries;
        bool has_transfer;
        bool mastering_metadata;
        bool content_light;

        HdrEvidenceSet();

        // Merges one observation without depending on collection order.
        void Add(const HdrEvidence& evidence);

        // Convenience for direct-signal tests and the consumer matrix fallback.
        void AddSignal(
            HdrColor::HdrSignal signal_value,
            HdrColor::VideoMatrix matrix_value,
            HdrEvidenceSource source_value);
    };

    struct AutoHdrSignals
    {
        HdrColor::HdrSignal graph_signal;
        HdrColor::HdrSignal container_signal;
        bool graph_bt2020_matrix;
        bool container_bt2020_matrix;
        bool filename_says_hdr;
        bool side_data_says_hdr;

        AutoHdrSignals()
            : graph_signal(HdrColor::HDR_SIGNAL_UNKNOWN)
            , container_signal(HdrColor::HDR_SIGNAL_UNKNOWN)
            , graph_bt2020_matrix(false)
            , container_bt2020_matrix(false)
            , filename_says_hdr(false)
            , side_data_says_hdr(false)
        {
        }
    };

    // Resolves AUTO into the existing PQ-or-SDR decision. The precedence is:
    // side data, explicit HDR, filename HDR, explicit SDR, matrix fallback.
    bool ResolveAutoPq(const AutoHdrSignals& signals);
}
