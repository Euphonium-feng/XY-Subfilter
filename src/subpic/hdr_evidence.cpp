#include "hdr_evidence.h"

namespace HdrDetection
{
    namespace
    {
        int SignalRank(HdrColor::HdrSignal signal)
        {
            switch (signal) {
            case HdrColor::HDR_SIGNAL_HDR:
                return 3;
            case HdrColor::HDR_SIGNAL_SDR:
                return 2;
            default:
                return 1;
            }
        }

        int SourceRank(HdrEvidenceSource source)
        {
            switch (source) {
            case HDR_EVIDENCE_SOURCE_SIDE_DATA:
                return 6;
            case HDR_EVIDENCE_SOURCE_CONTAINER:
                return 5;
            case HDR_EVIDENCE_SOURCE_CONSUMER:
                return 4;
            case HDR_EVIDENCE_SOURCE_GRAPH:
                return 3;
            case HDR_EVIDENCE_SOURCE_FILENAME:
                return 2;
            case HDR_EVIDENCE_SOURCE_CONSUMER_MATRIX:
                return 1;
            default:
                return 0;
            }
        }

        bool PreferSelection(
            const HdrEvidence& candidate, const HdrEvidenceSet& current)
        {
            const int candidate_signal = SignalRank(candidate.signal);
            const int current_signal = SignalRank(current.selected_signal);
            if (candidate_signal != current_signal) {
                return candidate_signal > current_signal;
            }

            const int candidate_source = SourceRank(candidate.source);
            const int current_source = SourceRank(current.selected_source);
            if (candidate_source != current_source) {
                return candidate_source > current_source;
            }
            return candidate.depth < current.selected_depth;
        }
    }

    HdrEvidenceSet::HdrEvidenceSet()
        : signal(HdrColor::HDR_SIGNAL_UNKNOWN)
        , matrix(HdrColor::VIDEO_MATRIX_UNKNOWN)
        , selected_signal(HdrColor::HDR_SIGNAL_UNKNOWN)
        , selected_source(HDR_EVIDENCE_SOURCE_NONE)
        , selected_stage(HDR_EVIDENCE_STAGE_NONE)
        , selected_depth(0x7fffffff)
        , selected_transfer_function(-1)
        , selected_control_flags(0)
        , selected_primaries(-1)
        , has_transfer(false)
        , mastering_metadata(false)
        , content_light(false)
    {
    }

    void HdrEvidenceSet::Add(const HdrEvidence& evidence)
    {
        const int candidate_rank = SignalRank(evidence.signal);
        const int current_rank = SignalRank(signal);
        if (candidate_rank > current_rank) {
            signal = evidence.signal;
        }

        if (evidence.matrix == HdrColor::VIDEO_MATRIX_2020) {
            matrix = HdrColor::VIDEO_MATRIX_2020;
        }
        else if (matrix == HdrColor::VIDEO_MATRIX_UNKNOWN
            && evidence.matrix != HdrColor::VIDEO_MATRIX_UNKNOWN) {
            matrix = evidence.matrix;
        }

        has_transfer = has_transfer || evidence.has_transfer;
        mastering_metadata = mastering_metadata || evidence.mastering_metadata;
        content_light = content_light || evidence.content_light;

        if (selected_source == HDR_EVIDENCE_SOURCE_NONE
            || PreferSelection(evidence, *this)) {
            selected_source = evidence.source;
            selected_signal = evidence.signal;
            selected_stage = evidence.stage;
            selected_depth = evidence.depth;
            selected_transfer_function = evidence.transfer_function;
            selected_control_flags = evidence.control_flags;
            selected_primaries = evidence.primaries;
        }
    }

    void HdrEvidenceSet::AddSignal(
        HdrColor::HdrSignal signal_value,
        HdrColor::VideoMatrix matrix_value,
        HdrEvidenceSource source_value)
    {
        HdrEvidence evidence;
        evidence.signal = signal_value;
        evidence.matrix = matrix_value;
        evidence.has_matrix = matrix_value != HdrColor::VIDEO_MATRIX_UNKNOWN;
        evidence.source = source_value;
        evidence.stage = source_value == HDR_EVIDENCE_SOURCE_CONSUMER
            || source_value == HDR_EVIDENCE_SOURCE_CONSUMER_MATRIX
            ? HDR_EVIDENCE_STAGE_CONSUMER
            : HDR_EVIDENCE_STAGE_CONTAINER;
        evidence.depth = source_value == HDR_EVIDENCE_SOURCE_CONTAINER ? 1 : 0;
        Add(evidence);
    }

    bool ResolveAutoPq(const AutoHdrSignals& signals)
    {
        if (signals.side_data_says_hdr) {
            return true;
        }
        if (signals.graph_signal == HdrColor::HDR_SIGNAL_HDR
            || signals.container_signal == HdrColor::HDR_SIGNAL_HDR
            || signals.filename_says_hdr) {
            return true;
        }
        if (signals.graph_signal == HdrColor::HDR_SIGNAL_SDR
            || signals.container_signal == HdrColor::HDR_SIGNAL_SDR) {
            return false;
        }
        return signals.graph_bt2020_matrix || signals.container_bt2020_matrix;
    }
}
