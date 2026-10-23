/*
 * HDR color science for bitmap subtitle compositing.
 *
 * Bitmap subtitles (PGS/DVB) only carry 8-bit SDR palette colors. When the
 * video is HDR, those code values are consumed as if they were already in the
 * video's transfer function, which pushes subtitle white far above the
 * intended reference luminance. This module provides the conversions needed to
 * write palette colors with the correct code values instead.
 *
 * Reference white for HDR graphics follows BT.2408: 203 nits.
 */

#include "hdr_color.h"

#include <math.h>

namespace
{
// SMPTE ST 2084 constants.
const double PQ_M1 = 2610.0 / 16384.0;
const double PQ_M2 = 2523.0 / 4096.0 * 128.0;
const double PQ_C1 = 3424.0 / 4096.0;
const double PQ_C2 = 2413.0 / 4096.0 * 32.0;
const double PQ_C3 = 2392.0 / 4096.0 * 32.0;

inline double Clamp(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline int ClampToCode(double v, int max_code)
{
    if (!(v > 0.0)) {   // also catches NaN
        return 0;
    }
    const double rounded = floor(v + 0.5);
    if (rounded >= max_code) {
        return max_code;
    }
    return static_cast<int>(rounded);
}

// 8-bit code -> linear light in [0,1] using the BT.1886 display gamma.
inline double CodeToLinear(int code)
{
    return pow(Clamp(code / 255.0, 0.0, 1.0), HdrColor::SDR_GAMMA);
}

inline int LinearToCode(double linear)
{
    return ClampToCode(pow(Clamp(linear, 0.0, 1.0), 1.0 / HdrColor::SDR_GAMMA) * 255.0, 255);
}
}

namespace HdrColor
{
Config NormalizeConfig(int mode, double white_nits, unsigned long long generation)
{
    Config config;
    if (mode != MAP_PQ && mode != MAP_RELATIVE_WHITE) {
        mode = MAP_OFF;
    }
    config.mode = mode;
    config.white_nits = Clamp(white_nits, MIN_WHITE_NITS, MAX_WHITE_NITS);
    config.generation = generation;
    return config;
}

HdrSignal ClassifyTransferFunction(int transfer_function, bool color_info_present)
{
    if (!color_info_present) {
        return HDR_SIGNAL_UNKNOWN;
    }

    // DXVA2 transfer-function values: SMPTE 2084 (PQ) and HLG are HDR.
    // BT.709, SMPTE 170M/240M, linear, logarithmic SDR, and BT.2020 SDR
    // all represent non-HDR transfer curves.
    switch (transfer_function) {
    case 1:  // BT.709
    case 2:  // SMPTE 170M
    case 3:  // SMPTE 240M
    case 4:  // Linear
    case 5:  // Log 100
    case 6:  // Log 316
    case 7:  // BT.2020 SDR
        return HDR_SIGNAL_SDR;
    case 8:  // SMPTE ST 2084 (PQ)
    case 10: // HLG
        return HDR_SIGNAL_HDR;
    default:
        return HDR_SIGNAL_UNKNOWN;
    }
}

bool ShouldUsePqForAuto(HdrSignal signal, bool bt2020_matrix)
{
    if (signal == HDR_SIGNAL_HDR) {
        return true;
    }
    if (signal == HDR_SIGNAL_SDR) {
        return false;
    }
    return bt2020_matrix;
}

bool IsPqMapping(const Config& config)
{
    return config.mode == MAP_PQ;
}

bool IsSameOutput(const Config& a, const Config& b)
{
    if (a.mode != b.mode) {
        return false;
    }
    if (a.mode == MAP_OFF) {
        return true;
    }
    // Only the white point matters beyond the mode itself.
    return fabs(a.white_nits - b.white_nits) < 0.5;
}

bool IsSameRevision(const Config& a, const Config& b)
{
    return a.generation == b.generation && IsSameOutput(a, b);
}

double PqOetfFromLinear(double linear)
{
    const double v = Clamp(linear, 0.0, 1.0);
    const double p = pow(v, PQ_M1);
    return pow((PQ_C1 + PQ_C2 * p) / (1.0 + PQ_C3 * p), PQ_M2);
}

double PqEotfToLinear(double pq)
{
    const double n = Clamp(pq, 0.0, 1.0);
    const double p = pow(n, 1.0 / PQ_M2);
    const double num = p - PQ_C1;
    if (num <= 0.0) {
        return 0.0;
    }
    return Clamp(pow(num / (PQ_C2 - PQ_C3 * p), 1.0 / PQ_M1), 0.0, 1.0);
}

double PqOetfFromNits(double nits)
{
    return PqOetfFromLinear(Clamp(nits, 0.0, PQ_PEAK_NITS) / PQ_PEAK_NITS);
}

double PqNitsFromSignal(double pq)
{
    return PqEotfToLinear(pq) * PQ_PEAK_NITS;
}

int PqCodeFromNits(double nits, int bit_depth)
{
    const int max_code = bit_depth >= 10 ? 1023 : 255;
    return ClampToCode(PqOetfFromNits(nits) * max_code, max_code);
}

double PqNitsFromCode(int code, int bit_depth)
{
    const int max_code = bit_depth >= 10 ? 1023 : 255;
    return PqNitsFromSignal(Clamp(code / double(max_code), 0.0, 1.0));
}

double SdrCodeToLinear(int code)
{
    return CodeToLinear(code);
}

int LinearToSdrCode(double linear)
{
    return LinearToCode(linear);
}

BYTE MapComponent(BYTE value, const Config& config)
{
    switch (config.mode)
    {
    case MAP_PQ:
    {
        // Code -> linear light, scaled so that 255 lands on the white point,
        // then quantized as an absolute PQ signal.
        const double nits = CodeToLinear(value) * config.white_nits;
        return static_cast<BYTE>(PqCodeFromNits(nits, 8));
    }
    case MAP_RELATIVE_WHITE:
    {
        // Keep the SDR gamma space but pre-compensate the linear luminance for
        // a higher reference white. Used when the consumer places SDR white at
        // display white and would otherwise show subtitles too dim next to
        // HDR content.
        const double linear = CodeToLinear(value) * (config.white_nits / SDR_WHITE_NITS);
        return static_cast<BYTE>(LinearToCode(linear));
    }
    case MAP_OFF:
    default:
        return value;
    }
}

WORD MapComponentTo16BitWord(BYTE value, const Config& config)
{
    if (config.mode == MAP_PQ) {
        const double nits = CodeToLinear(value) * config.white_nits;
        // P010/P016 keep the 10-bit sample in the high bits of a 16-bit word.
        return static_cast<WORD>(PqCodeFromNits(nits, 10) << 6);
    }
    // Preserve the existing convention of placing the 8-bit code in the high byte.
    return static_cast<WORD>(value) << 8;
}

DWORD MapArgb(DWORD argb, const Config& config)
{
    if (config.mode == MAP_OFF) {
        return argb;
    }
    const BYTE a = static_cast<BYTE>(argb >> 24);
    const BYTE r = MapComponent(static_cast<BYTE>(argb >> 16), config);
    const BYTE g = MapComponent(static_cast<BYTE>(argb >> 8), config);
    const BYTE b = MapComponent(static_cast<BYTE>(argb), config);
    return (DWORD(a) << 24) | (DWORD(r) << 16) | (DWORD(g) << 8) | b;
}
}
