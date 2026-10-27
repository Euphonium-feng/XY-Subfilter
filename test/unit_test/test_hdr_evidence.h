#ifndef __TEST_HDR_EVIDENCE_H__
#define __TEST_HDR_EVIDENCE_H__

#include <gtest/gtest.h>
#include "hdr_evidence.h"

namespace {

HdrDetection::HdrEvidence MakeEvidence(
    HdrColor::HdrSignal signal,
    int transfer_function,
    HdrColor::VideoMatrix matrix)
{
    HdrDetection::HdrEvidence evidence;
    evidence.signal = signal;
    evidence.transfer_function = transfer_function;
    evidence.matrix = matrix;
    return evidence;
}

TEST(HdrEvidenceTest, HdrWinsRegardlessOfCollectionOrder)
{
    const HdrDetection::HdrEvidence unknown;
    const HdrDetection::HdrEvidence sdr =
        MakeEvidence(HdrColor::HDR_SIGNAL_SDR, 1, HdrColor::VIDEO_MATRIX_709);
    const HdrDetection::HdrEvidence hdr =
        MakeEvidence(HdrColor::HDR_SIGNAL_HDR, 8, HdrColor::VIDEO_MATRIX_2020);

    EXPECT_TRUE(HdrDetection::PreferHdrEvidence(hdr, unknown));
    EXPECT_TRUE(HdrDetection::PreferHdrEvidence(hdr, sdr));
    EXPECT_FALSE(HdrDetection::PreferHdrEvidence(sdr, hdr));
    EXPECT_FALSE(HdrDetection::PreferHdrEvidence(unknown, hdr));
}

TEST(HdrEvidenceTest, ExplicitSdrWinsOverUnknownEvidence)
{
    const HdrDetection::HdrEvidence unknown;
    const HdrDetection::HdrEvidence sdr =
        MakeEvidence(HdrColor::HDR_SIGNAL_SDR, 1, HdrColor::VIDEO_MATRIX_709);

    EXPECT_TRUE(HdrDetection::PreferHdrEvidence(sdr, unknown));
    EXPECT_FALSE(HdrDetection::PreferHdrEvidence(unknown, sdr));
    EXPECT_FALSE(HdrDetection::PreferHdrEvidence(sdr, sdr));
}

TEST(HdrEvidenceTest, ConcreteUnknownEvidenceReplacesEmptyUnknown)
{
    const HdrDetection::HdrEvidence empty;
    const HdrDetection::HdrEvidence concrete = MakeEvidence(
        HdrColor::HDR_SIGNAL_UNKNOWN, 0, HdrColor::VIDEO_MATRIX_UNKNOWN);

    EXPECT_TRUE(HdrDetection::PreferHdrEvidence(concrete, empty));
    EXPECT_FALSE(HdrDetection::PreferHdrEvidence(empty, concrete));
}

TEST(HdrEvidenceTest, AutoPqUsesTheDocumentedPrecedence)
{
    HdrDetection::AutoHdrSignals signals;

    signals.side_data_says_hdr = true;
    signals.signal = HdrColor::HDR_SIGNAL_SDR;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.signal = HdrColor::HDR_SIGNAL_HDR;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.signal = HdrColor::HDR_SIGNAL_SDR;
    signals.filename_says_hdr = true;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.signal = HdrColor::HDR_SIGNAL_SDR;
    signals.bt2020_matrix = true;
    EXPECT_FALSE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    signals.signal = HdrColor::HDR_SIGNAL_UNKNOWN;
    signals.bt2020_matrix = true;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));

    signals = HdrDetection::AutoHdrSignals();
    EXPECT_FALSE(HdrDetection::ResolveAutoPq(signals));
}

TEST(HdrEvidenceTest, FilenameAndSideDataOverrideConflictingMetadata)
{
    HdrDetection::AutoHdrSignals signals;
    signals.signal = HdrColor::HDR_SIGNAL_SDR;
    signals.filename_says_hdr = true;
    signals.side_data_says_hdr = true;
    signals.bt2020_matrix = false;
    EXPECT_TRUE(HdrDetection::ResolveAutoPq(signals));
}

}
#endif // __TEST_HDR_EVIDENCE_H__
