/*
 * Premultiplied RGBA bitmap scaling for subtitle design canvases.
 * Pure geometry and pixel kernels; no DirectShow or provider state.
 */

#pragma once

#include <WTypes.h>

namespace BitmapResampler
{
    enum Filter
    {
        FILTER_BILINEAR = 0,
        FILTER_SPLINE36,
        FILTER_COUNT
    };

    // Maps a design canvas onto the target canvas with one isotropic scale
    // factor and centres the result. Both rects are zero-based sizes in
    // pixels; the padding is split with the remainder on the right/bottom.
    struct ScaleGeometry
    {
        SIZE design_size;
        SIZE target_size;
        SIZE scaled_size;
        POINT offset;
        double scale;
        bool active;

        ScaleGeometry();
    };

    ScaleGeometry ResolveScaleGeometry(
        const SIZE& design_size, const SIZE& target_size);

    // Maps a design-space rect to target space using the same geometry as the
    // pixel blit. The result is clipped to the target canvas. Returns false
    // when the mapped rect is empty.
    bool MapRectToTarget(
        const RECT& design_rect, const ScaleGeometry& geometry,
        RECT* target_rect);

    // True when the two sizes differ, i.e. a resample is required even if
    // their aspect ratios are identical.
    bool NeedsResample(
        const SIZE& design_size, const SIZE& target_size);

    // Scales a premultiplied RGBA source into a premultiplied RGBA target.
    // Pixels are B,G,R,A bytes. Only destination pixels touched by
    // source_dirty are written; source_dirty is in design-canvas coordinates.
    // The caller must keep both buffers alive for the duration of the call.
    bool ScalePremultipliedRgba(
        const BYTE* source, int source_pitch,
        const SIZE& design_size,
        BYTE* target, int target_pitch,
        const SIZE& target_size,
        const RECT& source_dirty,
        Filter filter);

    // Scalar reference used by tests and as the fallback when SSE2 is not
    // selected. Exposed so the accelerated path can be compared bit for bit
    // with a tolerant bound.
    bool ScalePremultipliedRgbaScalar(
        const BYTE* source, int source_pitch,
        const SIZE& design_size,
        BYTE* target, int target_pitch,
        const SIZE& target_size,
        const RECT& source_dirty,
        Filter filter);
}
