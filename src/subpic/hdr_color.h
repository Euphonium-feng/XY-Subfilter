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

#pragma once

#include <WTypes.h>

namespace HdrColor
{
    // Rec. ITU-R BT.2408 graphics white for HDR productions.
    const double DEFAULT_WHITE_NITS = 203.0;

    // SDR reference white, i.e. what code 255 means without HDR mapping.
    const double SDR_WHITE_NITS = 100.0;

    // BT.1886 display gamma used to relate SDR code values to linear light.
    const double SDR_GAMMA = 2.4;

    const double PQ_PEAK_NITS = 10000.0;

    // Nominal limit for the mastering display the white point is expressed in.
    const double MIN_WHITE_NITS = 50.0;
    const double MAX_WHITE_NITS = 1000.0;

    enum MapMode
    {
        MAP_OFF = 0,        // pass through: unchanged SDR behaviour
        MAP_PQ,             // encode the chosen white point as a PQ code value
        MAP_RELATIVE_WHITE, // keep SDR code space, let the consumer place white
        MAP_COUNT
    };

    struct Config
    {
        int mode;
        double white_nits;

        Config() : mode(MAP_OFF), white_nits(DEFAULT_WHITE_NITS) {}
        Config(int m, double nits) : mode(m), white_nits(nits) {}
    };

    // Clamps untrusted option values into a usable configuration.
    Config NormalizeConfig(int mode, double white_nits);

    // True when the configuration alters pixel output for the PQ mode.
    bool IsPqMapping(const Config& config);

    // True when two configurations produce identical output.
    bool IsSameOutput(const Config& a, const Config& b);

    // SMPTE ST 2084 (PQ) constants as defined by the standard.
    double PqOetfFromLinear(double linear);
    double PqEotfToLinear(double pq);

    // Absolute luminance in nits <-> normalized PQ signal in [0,1].
    double PqOetfFromNits(double nits);
    double PqNitsFromSignal(double pq);

    // Quantizes absolute luminance into a code of the given bit depth.
    int PqCodeFromNits(double nits, int bit_depth);

    // Recovers absolute luminance from a quantized PQ code.
    double PqNitsFromCode(int code, int bit_depth);

    // BT.1886 relation between an 8-bit SDR code and linear light in [0,1].
    double SdrCodeToLinear(int code);
    int LinearToSdrCode(double linear);

    // Maps a single 8-bit component for an 8-bit RGB target.
    BYTE MapComponent(BYTE value, const Config& config);

    // Maps a single 8-bit component into the high bits of a 16-bit container
    // word, matching how MSP_P010/MSP_P016 store 10-bit samples.
    WORD MapComponentTo16BitWord(BYTE value, const Config& config);

    // Maps a packed ARGB triple, preserving the alpha byte untouched.
    DWORD MapArgb(DWORD argb, const Config& config);
}
