/*
 * Pure HDR evidence selection shared by the DirectShow graph scanner and the
 * AUTO mode resolver.
 */

#pragma once

#include "hdr_color.h"

namespace HdrDetection
{
    struct HdrEvidence
    {
        HdrColor::HdrSignal signal;
        int transfer_function;
        unsigned long control_flags;
        HdrColor::VideoMatrix matrix;

        HdrEvidence()
            : signal(HdrColor::HDR_SIGNAL_UNKNOWN)
            , transfer_function(-1)
            , control_flags(0)
            , matrix(HdrColor::VIDEO_MATRIX_UNKNOWN)
        {
        }
    };

    struct AutoHdrSignals
    {
        HdrColor::HdrSignal signal;
        bool bt2020_matrix;
        bool filename_says_hdr;
        bool side_data_says_hdr;

        AutoHdrSignals()
            : signal(HdrColor::HDR_SIGNAL_UNKNOWN)
            , bt2020_matrix(false)
            , filename_says_hdr(false)
            , side_data_says_hdr(false)
        {
        }
    };

    // True when candidate should replace current while collecting evidence
    // from pins or graph filters.
    bool PreferHdrEvidence(
        const HdrEvidence& candidate, const HdrEvidence& current);

    // Resolves AUTO into the existing PQ-or-SDR decision. The precedence is:
    // side data, explicit HDR, filename HDR, explicit SDR, matrix fallback.
    bool ResolveAutoPq(const AutoHdrSignals& signals);
}
