#ifndef __TEST_HDR_COLOR_H__
#define __TEST_HDR_COLOR_H__

#include <gtest/gtest.h>
#include "hdr_color.h"

namespace {

// BT.2408 graphics white, quantized as an absolute PQ signal.
TEST(HdrColorTest, PqCodeForReferenceWhite)
{
    // 203 nits is the reference white for HDR graphics.
    EXPECT_EQ(148, HdrColor::PqCodeFromNits(203.0, 8));
    EXPECT_EQ(594, HdrColor::PqCodeFromNits(203.0, 10));
    // The SDR reference white sits lower in code value.
    EXPECT_EQ(130, HdrColor::PqCodeFromNits(100.0, 8));
    EXPECT_EQ(520, HdrColor::PqCodeFromNits(100.0, 10));
}

TEST(HdrColorTest, PqCodeEndpoints)
{
    EXPECT_EQ(0, HdrColor::PqCodeFromNits(0.0, 8));
    EXPECT_EQ(0, HdrColor::PqCodeFromNits(0.0, 10));
    EXPECT_EQ(255, HdrColor::PqCodeFromNits(10000.0, 8));
    EXPECT_EQ(1023, HdrColor::PqCodeFromNits(10000.0, 10));
    // Out of range input is clamped, never wrapped.
    EXPECT_EQ(255, HdrColor::PqCodeFromNits(20000.0, 8));
    EXPECT_EQ(0, HdrColor::PqCodeFromNits(-100.0, 8));
}

TEST(HdrColorTest, PqRoundTripWithinOnePercent)
{
    for (double nits = 0.1; nits <= 10000.0; nits *= 1.37) {
        const double signal = HdrColor::PqOetfFromNits(nits);
        const double back = HdrColor::PqNitsFromSignal(signal);
        if (nits < 1.0) {
            // Below one nit the EOTF noise floor dominates the ratio.
            EXPECT_LT(back, 1.0) << "nits=" << nits;
            continue;
        }
        EXPECT_NEAR(nits, back, nits * 0.01) << "nits=" << nits;
    }
}

TEST(HdrColorTest, PqCodeRoundTripWithinOnePercent)
{
    for (int code = 1; code <= 1023; ++code) {
        const double nits = HdrColor::PqNitsFromCode(code, 10);
        const int back = HdrColor::PqCodeFromNits(nits, 10);
        EXPECT_EQ(code, back) << "code=" << code;
    }
}

TEST(HdrColorTest, PqIsMonotonic)
{
    int previous = -1;
    for (int nits = 0; nits <= 10000; nits += 7) {
        const int code = HdrColor::PqCodeFromNits(static_cast<double>(nits), 10);
        EXPECT_GE(code, previous) << "nits=" << nits;
        previous = code;
    }
}

TEST(HdrColorTest, SdrGammaRoundTrip)
{
    for (int code = 0; code <= 255; ++code) {
        const double linear = HdrColor::SdrCodeToLinear(code);
        const int back = HdrColor::LinearToSdrCode(linear);
        EXPECT_EQ(code, back) << "code=" << code;
    }
}

TEST(HdrColorTest, OffModeIsByteIdentical)
{
    const HdrColor::Config off = HdrColor::NormalizeConfig(HdrColor::MAP_OFF, 203.0);
    EXPECT_FALSE(HdrColor::IsPqMapping(off));

    for (int value = 0; value <= 255; ++value) {
        const DWORD argb = 0xA5000000 | (DWORD(value) << 16) | (DWORD(value) << 8) | DWORD(value);
        EXPECT_EQ(argb, HdrColor::MapArgb(argb, off)) << "value=" << value;
        EXPECT_EQ(value, HdrColor::MapComponent(static_cast<BYTE>(value), off));
    }
}

TEST(HdrColorTest, PqMappingPreservesAlpha)
{
    const HdrColor::Config pq = HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 203.0);
    for (int alpha = 0; alpha <= 255; alpha += 17) {
        const DWORD mapped = HdrColor::MapArgb(0xFF0000u | (DWORD(alpha) << 24), pq);
        EXPECT_EQ(alpha, int((mapped >> 24) & 0xFF)) << "alpha=" << alpha;
    }
}

TEST(HdrColorTest, PqMappingDarkensWhite)
{
    const HdrColor::Config pq = HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 203.0);
    // White must land on the reference white code, far below 255.
    EXPECT_EQ(148, HdrColor::MapComponent(255, pq));
    // Black stays black.
    EXPECT_EQ(0, HdrColor::MapComponent(0, pq));
    // The high half of the range is compressed; that is where overexposure
    // used to show up. Low codes may rise slightly because PQ resolves dark
    // levels more finely than an SDR gamma curve does.
    for (int value = 128; value <= 255; ++value) {
        EXPECT_LE(HdrColor::MapComponent(static_cast<BYTE>(value), pq), value)
            << "value=" << value;
    }
}

TEST(HdrColorTest, PqMappingIsMonotonic)
{
    const HdrColor::Config pq = HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 203.0);
    int previous = -1;
    for (int value = 0; value <= 255; ++value) {
        const int code = HdrColor::MapComponent(static_cast<BYTE>(value), pq);
        EXPECT_GE(code, previous) << "value=" << value;
        previous = code;
    }
}

TEST(HdrColorTest, WhiteNitsChangesOutput)
{
    const HdrColor::Config dim = HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 100.0);
    const HdrColor::Config bright = HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 400.0);
    EXPECT_LT(HdrColor::MapComponent(255, dim), HdrColor::MapComponent(255, bright));
    EXPECT_EQ(130, HdrColor::MapComponent(255, dim));
    EXPECT_EQ(166, HdrColor::MapComponent(255, bright));
}

TEST(HdrColorTest, NormalizeConfigClampsAndRejectsUnknownModes)
{
    EXPECT_EQ(HdrColor::MAP_OFF, HdrColor::NormalizeConfig(12345, 203.0).mode);
    EXPECT_EQ(HdrColor::MAP_OFF, HdrColor::NormalizeConfig(-1, 203.0).mode);
    EXPECT_EQ(HdrColor::MAP_PQ, HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 203.0).mode);

    EXPECT_DOUBLE_EQ(HdrColor::MIN_WHITE_NITS,
        HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 1.0).white_nits);
    EXPECT_DOUBLE_EQ(HdrColor::MAX_WHITE_NITS,
        HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 1e9).white_nits);
    // A default-constructed config uses the BT.2408 white point.
    EXPECT_DOUBLE_EQ(HdrColor::DEFAULT_WHITE_NITS, HdrColor::Config().white_nits);
}

TEST(HdrColorTest, IsSameOutputComparesModeAndWhitePoint)
{
    const HdrColor::Config a(HdrColor::MAP_PQ, 203.0);
    const HdrColor::Config b(HdrColor::MAP_PQ, 203.2);
    const HdrColor::Config c(HdrColor::MAP_PQ, 400.0);
    const HdrColor::Config off1(HdrColor::MAP_OFF, 100.0);
    const HdrColor::Config off2(HdrColor::MAP_OFF, 400.0);

    EXPECT_TRUE(HdrColor::IsSameOutput(a, b));
    EXPECT_FALSE(HdrColor::IsSameOutput(a, c));
    // White point is irrelevant when mapping is off.
    EXPECT_TRUE(HdrColor::IsSameOutput(off1, off2));
    EXPECT_FALSE(HdrColor::IsSameOutput(a, off1));
}

TEST(HdrColorTest, IsSameRevisionIncludesConfigurationGeneration)
{
    const HdrColor::Config first(HdrColor::MAP_PQ, 203.0, 1);
    const HdrColor::Config same(HdrColor::MAP_PQ, 203.0, 1);
    const HdrColor::Config next(HdrColor::MAP_PQ, 203.0, 2);

    EXPECT_TRUE(HdrColor::IsSameOutput(first, next));
    EXPECT_TRUE(HdrColor::IsSameRevision(first, same));
    EXPECT_FALSE(HdrColor::IsSameRevision(first, next));

    const HdrColor::Config off(HdrColor::MAP_OFF, 203.0, 2);
    EXPECT_FALSE(HdrColor::IsSameRevision(first, off));
}

TEST(HdrColorTest, ClassifiesConsumerTransferFunction)
{
    EXPECT_EQ(HdrColor::HDR_SIGNAL_HDR,
        HdrColor::ClassifyTransferFunction(8, true)); // SMPTE ST 2084
    EXPECT_EQ(HdrColor::HDR_SIGNAL_HDR,
        HdrColor::ClassifyTransferFunction(10, true)); // HLG
    EXPECT_EQ(HdrColor::HDR_SIGNAL_SDR,
        HdrColor::ClassifyTransferFunction(1, true)); // BT.709
    EXPECT_EQ(HdrColor::HDR_SIGNAL_SDR,
        HdrColor::ClassifyTransferFunction(7, true)); // BT.2020 SDR
    // PQ/HLG codes are unambiguous even if a decoder omits the colour-info
    // flag; SDR curves still require the flag before overriding AUTO.
    EXPECT_EQ(HdrColor::HDR_SIGNAL_HDR,
        HdrColor::ClassifyTransferFunction(8, false));
    EXPECT_EQ(HdrColor::HDR_SIGNAL_HDR,
        HdrColor::ClassifyTransferFunction(10, false));
    EXPECT_EQ(HdrColor::HDR_SIGNAL_UNKNOWN,
        HdrColor::ClassifyTransferFunction(1, false));
}

TEST(HdrColorTest, ClassifiesConsumerVideoMatrix)
{
    EXPECT_EQ(HdrColor::VIDEO_MATRIX_601,
        HdrColor::ClassifyVideoMatrix(2, true));
    EXPECT_EQ(HdrColor::VIDEO_MATRIX_709,
        HdrColor::ClassifyVideoMatrix(1, true));
    EXPECT_EQ(HdrColor::VIDEO_MATRIX_709,
        HdrColor::ClassifyVideoMatrix(3, true));
    EXPECT_EQ(HdrColor::VIDEO_MATRIX_2020,
        HdrColor::ClassifyVideoMatrix(4, true));
    EXPECT_EQ(HdrColor::VIDEO_MATRIX_UNKNOWN,
        HdrColor::ClassifyVideoMatrix(4, false));
}

TEST(HdrColorTest, ExplicitSdrHintOverridesBt2020MatrixFallback)
{
    EXPECT_TRUE(HdrColor::ShouldUsePqForAuto(HdrColor::HDR_SIGNAL_HDR, false));
    EXPECT_FALSE(HdrColor::ShouldUsePqForAuto(HdrColor::HDR_SIGNAL_SDR, true));
    EXPECT_TRUE(HdrColor::ShouldUsePqForAuto(HdrColor::HDR_SIGNAL_UNKNOWN, true));
    EXPECT_FALSE(HdrColor::ShouldUsePqForAuto(HdrColor::HDR_SIGNAL_UNKNOWN, false));
}

TEST(HdrColorTest, SixteenBitWordUsesTenBitPqSample)
{
    const HdrColor::Config pq = HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 203.0);
    // 10-bit code 594 shifted into the high bits of the word.
    EXPECT_EQ(WORD(594 << 6), HdrColor::MapComponentTo16BitWord(255, pq));
    EXPECT_EQ(WORD(0), HdrColor::MapComponentTo16BitWord(0, pq));

    // Mapping off keeps the historical 8-bit-in-high-byte convention.
    const HdrColor::Config off = HdrColor::NormalizeConfig(HdrColor::MAP_OFF, 203.0);
    EXPECT_EQ(WORD(255 << 8), HdrColor::MapComponentTo16BitWord(255, off));
    EXPECT_EQ(WORD(0x00 << 8), HdrColor::MapComponentTo16BitWord(0, off));
}


// Text subtitles go through the same MapArgb path as the PGS palette, so the
// guarantees the renderer relies on are asserted here as well.
TEST(HdrColorTest, TextColorOffModeIsIdentity)
{
    const HdrColor::Config off = HdrColor::NormalizeConfig(HdrColor::MAP_OFF, 203.0);
    // Alpha encodes opacity for text and must survive untouched.
    for (int a = 0; a <= 255; a += 15) {
        const DWORD argb = (DWORD(a) << 24) | 0x00FFEEDD;
        EXPECT_EQ(argb, HdrColor::MapArgb(argb, off)) << "alpha=" << a;
    }
}

TEST(HdrColorTest, TextColorPqKeepsAlphaAndLowersWhite)
{
    const HdrColor::Config pq = HdrColor::NormalizeConfig(HdrColor::MAP_PQ, 203.0);

    // Full white lands on the BT.2408 reference white code.
    const DWORD mapped_white = HdrColor::MapArgb(0xFFFFFFFF, pq);
    EXPECT_EQ(0xFFu, mapped_white >> 24);
    EXPECT_EQ(148, int((mapped_white >> 16) & 0xFF));

    // Mid grey becomes 106: 128 -> 0.19 linear -> 38.8 nits -> PQ code 106.
    const DWORD mapped_grey = HdrColor::MapArgb(0xFF808080, pq);
    EXPECT_EQ(0xFFu, mapped_grey >> 24);         // alpha untouched
    EXPECT_EQ(106, int((mapped_grey >> 16) & 0xFF));
    EXPECT_EQ(106, int((mapped_grey >> 8) & 0xFF));
    EXPECT_EQ(106, int(mapped_grey & 0xFF));
}

TEST(HdrColorTest, TextColorRelativeWhiteKeepsCodeSpace)
{
    const HdrColor::Config rel =
        HdrColor::NormalizeConfig(HdrColor::MAP_RELATIVE_WHITE, 203.0);
    // Full white stays at full code value: the scaling is in linear light and
    // is undone by the gamma encode.
    EXPECT_EQ(255, HdrColor::MapComponent(255, rel));
    EXPECT_EQ(0, HdrColor::MapComponent(0, rel));
    // Mid grey brightens because the target white is above SDR white.
    EXPECT_GE(HdrColor::MapComponent(128, rel), 128);
}

}
#endif // __TEST_HDR_COLOR_H__
