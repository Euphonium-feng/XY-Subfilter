#include "hdr_evidence.h"

namespace HdrDetection
{
    bool PreferHdrEvidence(
        const HdrEvidence& candidate, const HdrEvidence& current)
    {
        if (candidate.signal == HdrColor::HDR_SIGNAL_HDR) {
            return true;
        }
        if (current.signal == HdrColor::HDR_SIGNAL_HDR) {
            return false;
        }
        if (candidate.signal == HdrColor::HDR_SIGNAL_SDR) {
            return current.signal != HdrColor::HDR_SIGNAL_SDR;
        }
        return candidate.signal == HdrColor::HDR_SIGNAL_UNKNOWN
            && current.signal == HdrColor::HDR_SIGNAL_UNKNOWN
            && candidate.transfer_function >= 0;
    }

    bool ResolveAutoPq(const AutoHdrSignals& signals)
    {
        if (signals.side_data_says_hdr) {
            return true;
        }
        return HdrColor::ShouldUsePqForAuto(
            signals.signal,
            signals.bt2020_matrix,
            signals.filename_says_hdr);
    }
}
