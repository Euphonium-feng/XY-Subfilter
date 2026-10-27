#ifndef __TEST_HDR_EVIDENCE_H__
#define __TEST_HDR_EVIDENCE_H__

#include <gtest/gtest.h>

#include "hdr_evidence.h"

namespace {

HdrDetection::HdrEvidence MakeEvidence(
    HdrColor::HdrSignal signal,
    int transfer_function,
    HdrColor::VideoMatrix matrix,
    HdrDetection::HdrEvidenceSource source =
        HdrDetection::HDR_EVIDENCE_SOURCE_GRAPH,
    int depth = 0)
{
    HdrDetection::HdrEvidence evidence;
    evidence.signal = signal;
    evidence.transfer_function = transfer_function;
    evidence.matrix = matrix;
    evidence.has_transfer = transfer_function >= 0;
    evidence.has_matrix = matrix != HdrColor::VIDEO_MATRIX_UNKNOWN;
    evidence.source = source;
    evidence.stage = depth == 0
        ? HdrDetection::HDR_EVIDENCE_STAGE_CONSUMER
        : HdrDetection::HDR_EVIDENCE_STAGE_UPSTREAM;
    evidence.depth = depth;
    return evidence;
}

TEST(HdrEvidenceTest, MergeIsIndependentOfCollectionOrder)
{
    const HdrDetection::HdrEvidence unknown;
    const HdrDetection::HdrEvidence sdr =
        MakeEvidence(
            HdrColor::HDR_SIGNAL_SDR, 1,
            HdrColor::VIDEO_MATRIX_709);
    const HdrDetection::HdrEvidence hdr =
        MakeEvidence(
            HdrColor::HDR_SIGNAL_HDR, 8,
            HdrColor::VIDEO_MATRIX_2020);

    const HdrDetection::HdrEvidence observations[] = { unknown, sdr, hdr };
    const int permutations[][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
    };

    for (size_t i = 0;
         i < sizeof(permutations) / sizeof(permutations[0]);
         ++i) {
        HdrDetection::HdrEvidenceSet merged;
        for (size_t j = 0; j < 3; ++j) {
            merged.Add(observations[permutations[i][j]]);
        }
        EXPECT_EQ(HdrColor::HDR_SIGNAL_HDR, merged.signal);
        EXPECT_EQ(HdrColor::VIDEO_MATRIX_2020, merged.matrix);
    }
}

TEST(HdrEvidenceTest, ExplicitSdrWinsOverUnknownAndMatrix)
{
    HdrDetection::HdrEvidenceSet merged;
    merged.Add(MakeEvidence(
        HdrColor::HDR_SIGNAL_UNKNOWN, -1,
        HdrColor::VIDEO_MATRIX_2020));
    merged.Add(MakeEvidence(
        HdrColor::HDR_SIGNAL_SDR, 1,
        HdrColor::VIDEO_MATRIX_709));

    EXPECT_EQ(HdrColor::HDR_SIGNAL_SDR, merged.signal);
    EXPECT_EQ(HdrColor::VIDEO_MATRIX_2020, merged.matrix);
}

TEST(HdrEvidenceTest, ConcreteUnknownAndMatrixAreRetained)
{
    HdrDetection::HdrEvidenceSet merged;
    merged.Add(MakeEvidence(
        HdrColor::HDR_SIGNAL_UNKNOWN, 0,
        HdrColor::VIDEO_MATRIX_2020));

    EXPECT_EQ(HdrColor::HDR_SIGNAL_UNKNOWN, merged.signal);
    EXPECT_EQ(HdrColor::VIDEO_MATRIX_2020, merged.matrix);
    EXPECT_TRUE(merged.has_transfer);
}

TEST(HdrEvidenceTest, AutoPqUsesTheDocumentedPrecedence)
{
    HdrDetection::AutoHdrSignals signals;

    signals.side_data_says_hdr = true;
    signals.graph_signal = HdrColor::HDR_SIGNAL_SDR;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.graph_signal = HdrColor::HDR_SIGNAL_HDR;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.container_signal = HdrColor::HDR_SIGNAL_HDR;
    signals.graph_signal = HdrColor::HDR_SIGNAL_SDR;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.graph_signal = HdrColor::HDR_SIGNAL_SDR;
    signals.filename_says_hdr = true;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.graph_signal = HdrColor::HDR_SIGNAL_SDR;
    signals.graph_bt2020_matrix = true;
    EXPECT_FALSE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.graph_signal = HdrColor::HDR_SIGNAL_UNKNOWN;
    signals.graph_bt2020_matrix = true;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.container_bt2020_matrix = true;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    EXPECT_FALSE(HdrDetection::ResolveAutoPq(signals));
}

TEST(HdrEvidenceTest, FilenameAndSideDataOverrideConflictingSdrMetadata)
{
    HdrDetection::AutoHdrSignals signals;
    signals.graph_signal = HdrColor::HDR_SIGNAL_SDR;
    signals.container_signal = HdrColor::HDR_SIGNAL_SDR;
    signals.filename_says_hdr = true;
    signals.side_data_says_hdr = true;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));
}

TEST(HdrEvidenceTest, SideDataAbsenceDoesNotMeanSdr)
{
    HdrDetection::AutoHdrSignals signals;
    signals.graph_signal = HdrColor::HDR_SIGNAL_UNKNOWN;
    signals.container_signal = HdrColor::HDR_SIGNAL_UNKNOWN;
    signals.graph_bt2020_matrix = true;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));
}

}
#endif // __TEST_HDR_EVIDENCE_H__
