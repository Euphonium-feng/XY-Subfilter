#include "bitmap_resampler.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <vector>

namespace
{
    const int kSplineRadius = 3;
    const int kLinearRadius = 1;
    const int kMaxTaps = 64;

    bool IsPositiveSize(const SIZE& size)
    {
        return size.cx > 0 && size.cy > 0;
    }

    int ClampInt(int value, int low, int high)
    {
        return value < low ? low : (value > high ? high : value);
    }

    double ClampDouble(double value, double low, double high)
    {
        return value < low ? low : (value > high ? high : value);
    }

    BYTE ClampByte(double value)
    {
        return static_cast<BYTE>(ClampDouble(value + 0.5, 0.0, 255.0));
    }

    // MSP_RGBA stores premultiplied colour over an inverted alpha where 0 is
    // fully opaque. The source-over invariant is color <= 255 - alpha; every
    // filtered channel is projected back onto that invariant.
    void StorePremultiplied(
        BYTE* pixel, double b, double g, double r, double a)
    {
        const BYTE alpha = ClampByte(a);
        const double limit = 255.0 - alpha;
        BYTE color[3] = {
            ClampByte(b), ClampByte(g), ClampByte(r)
        };
        for (int i = 0; i < 3; ++i) {
            color[i] = (BYTE)ClampDouble(color[i], 0.0, limit);
        }
        if (alpha == 0) {
            color[0] = color[1] = color[2] = 0;
        }
        pixel[0] = color[0];
        pixel[1] = color[1];
        pixel[2] = color[2];
        pixel[3] = alpha;
    }

    double Spline36Kernel(double x)
    {
        // 3-lobe cubic spline: f(0)=1, f(1..3)=0, C2-continuous, unit gain
        // at every integer phase.
        x = fabs(x);
        if (x < 1.0) {
            return 1.2 * x * x * x - 2.2 * x * x + 1.0;
        }
        if (x < 2.0) {
            return -0.6 * x * x * x + 3.2 * x * x - 5.4 * x + 2.8;
        }
        if (x < 3.0) {
            return 0.2 * x * x * x - 1.6 * x * x + 4.2 * x - 3.6;
        }
        return 0.0;
    }

    double LinearKernel(double x)
    {
        x = fabs(x);
        return x < 1.0 ? 1.0 - x : 0.0;
    }

    struct Tap
    {
        int index;
        double weight;
    };

    int BuildTaps(
        double center, double scale, bool spline, int source_size,
        Tap* taps, int capacity)
    {
        const double radius = spline ? kSplineRadius : kLinearRadius;
        const double filter_scale = scale < 1.0 ? scale : 1.0;
        const int first = (int)floor(center - radius / filter_scale);
        const int last = (int)ceil(center + radius / filter_scale);
        double total = 0.0;
        int count = 0;
        for (int i = first; i <= last; ++i) {
            if (count >= capacity) {
                return 0;
            }
            const double weight = spline
                ? Spline36Kernel((center - i) * filter_scale)
                : LinearKernel((center - i) * filter_scale);
            if (weight == 0.0) {
                continue;
            }
            taps[count].index = ClampInt(i, 0, source_size - 1);
            taps[count].weight = weight;
            total += weight;
            ++count;
        }
        if (total == 0.0) {
            taps[0].index = ClampInt(
                (int)floor(center + 0.5), 0, source_size - 1);
            taps[0].weight = 1.0;
            return 1;
        }
        for (int i = 0; i < count; ++i) {
            taps[i].weight /= total;
        }
        return count;
    }

    struct Rgba
    {
        double b, g, r, a;
    };

    bool ScaleSeparable(
        const BYTE* source, int source_pitch, const SIZE& design_size,
        BYTE* target, int target_pitch, const SIZE& target_size,
        const RECT& source_dirty, bool spline)
    {
        const BitmapResampler::ScaleGeometry geometry =
            BitmapResampler::ResolveScaleGeometry(design_size, target_size);

        if (!geometry.active) {
            RECT copy = source_dirty;
            copy.left = ClampInt(copy.left, 0, design_size.cx);
            copy.right = ClampInt(copy.right, 0, design_size.cx);
            copy.top = ClampInt(copy.top, 0, design_size.cy);
            copy.bottom = ClampInt(copy.bottom, 0, design_size.cy);
            if (copy.right <= copy.left || copy.bottom <= copy.top) {
                return true;
            }
            for (int y = copy.top; y < copy.bottom; ++y) {
                memcpy(
                    target + (size_t)y * target_pitch + (size_t)copy.left * 4,
                    source + (size_t)y * source_pitch + (size_t)copy.left * 4,
                    (size_t)(copy.right - copy.left) * 4);
            }
            return true;
        }

        RECT dirty = source_dirty;
        dirty.left = ClampInt(dirty.left, 0, design_size.cx);
        dirty.right = ClampInt(dirty.right, 0, design_size.cx);
        dirty.top = ClampInt(dirty.top, 0, design_size.cy);
        dirty.bottom = ClampInt(dirty.bottom, 0, design_size.cy);
        if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) {
            return true;
        }

        RECT mapped = {};
        if (!BitmapResampler::MapRectToTarget(dirty, geometry, &mapped)) {
            return true;
        }

        const double support = spline ? kSplineRadius : kLinearRadius;
        // Margin is needed in destination pixels: upscaling by s turns a
        // source support of r pixels into r*s destination pixels; downscaling
        // widens the source support but its destination footprint stays r.
        const int margin = (int)ceil(
            support * (geometry.scale > 1.0 ? geometry.scale : 1.0)) + 2;
        mapped.left = ClampInt(mapped.left - margin, 0, target_size.cx);
        mapped.right = ClampInt(mapped.right + margin, 0, target_size.cx);
        mapped.top = ClampInt(mapped.top - margin, 0, target_size.cy);
        mapped.bottom = ClampInt(mapped.bottom + margin, 0, target_size.cy);
        if (mapped.right <= mapped.left || mapped.bottom <= mapped.top) {
            return true;
        }

        // Two-pass separable filter. Horizontal taps depend only on the
        // destination column, so they are built once per call; the vertical
        // pass then combines horizontally-filtered rows into the destination.
        // Working memory is O(width), not a full float image.
        const int dst_width = mapped.right - mapped.left;
        std::vector<int> x_offsets((size_t)dst_width + 1, 0);
        std::vector<Tap> x_taps_flat;
        x_taps_flat.reserve((size_t)dst_width * 8);
        Tap taps_scratch[kMaxTaps];
        for (int x = mapped.left; x < mapped.right; ++x) {
            const double source_x =
                (x - geometry.offset.x + 0.5) / geometry.scale - 0.5;
            x_offsets[(size_t)(x - mapped.left)] =
                (int)x_taps_flat.size();
            const int count = BuildTaps(
                source_x, geometry.scale, spline, design_size.cx,
                taps_scratch, kMaxTaps);
            if (count == 0) {
                return false;
            }
            x_taps_flat.insert(
                x_taps_flat.end(), taps_scratch, taps_scratch + count);
        }
        x_offsets[(size_t)dst_width] = (int)x_taps_flat.size();

        std::vector<float> horizontal((size_t)dst_width * 4, 0.0f);
        std::vector<float> accumulated((size_t)dst_width * 4, 0.0f);

        for (int y = mapped.top; y < mapped.bottom; ++y) {
            const double source_y =
                (y - geometry.offset.y + 0.5) / geometry.scale - 0.5;
            Tap y_taps[kMaxTaps];
            const int y_count = BuildTaps(
                source_y, geometry.scale, spline, design_size.cy,
                y_taps, kMaxTaps);
            if (y_count == 0) {
                return false;
            }
            std::fill(accumulated.begin(), accumulated.end(), 0.0f);
            for (int ty = 0; ty < y_count; ++ty) {
                const BYTE* src_row =
                    source + (size_t)y_taps[ty].index * source_pitch;
                for (int x = 0; x < dst_width; ++x) {
                    const int start = x_offsets[(size_t)x];
                    const int end = x_offsets[(size_t)x + 1];
                    float b = 0.0f, g = 0.0f, r = 0.0f, a = 0.0f;
                    for (int t = start; t < end; ++t) {
                        const Tap& tap = x_taps_flat[(size_t)t];
                        const BYTE* px =
                            src_row + (size_t)tap.index * 4;
                        const float w = (float)tap.weight;
                        b += px[0] * w;
                        g += px[1] * w;
                        r += px[2] * w;
                        a += px[3] * w;
                    }
                    const size_t o = (size_t)x * 4;
                    horizontal[o + 0] = b;
                    horizontal[o + 1] = g;
                    horizontal[o + 2] = r;
                    horizontal[o + 3] = a;
                }
                const float wy = (float)y_taps[ty].weight;
                const int stride = dst_width * 4;
                for (int i = 0; i < stride; ++i) {
                    accumulated[(size_t)i] += horizontal[(size_t)i] * wy;
                }
            }

            BYTE* dst_row =
                target + (size_t)y * target_pitch + (size_t)mapped.left * 4;
            for (int x = 0; x < dst_width; ++x) {
                const size_t o = (size_t)x * 4;
                StorePremultiplied(
                    &dst_row[x * 4],
                    accumulated[o + 0], accumulated[o + 1],
                    accumulated[o + 2], accumulated[o + 3]);
            }
        }
        return true;
    }
}

namespace BitmapResampler
{
    ScaleGeometry::ScaleGeometry()
        : scale(1.0)
        , active(false)
    {
        design_size.cx = 0;
        design_size.cy = 0;
        target_size.cx = 0;
        target_size.cy = 0;
        scaled_size.cx = 0;
        scaled_size.cy = 0;
        offset.x = 0;
        offset.y = 0;
    }

    ScaleGeometry ResolveScaleGeometry(
        const SIZE& design_size, const SIZE& target_size)
    {
        ScaleGeometry result;
        result.design_size = design_size;
        result.target_size = target_size;
        if (!IsPositiveSize(design_size) || !IsPositiveSize(target_size)) {
            return result;
        }
        const double sx = (double)target_size.cx / design_size.cx;
        const double sy = (double)target_size.cy / design_size.cy;
        result.scale = sx < sy ? sx : sy;
        if (result.scale <= 0.0) {
            return result;
        }
        result.scaled_size.cx = ClampInt(
            (int)floor(design_size.cx * result.scale + 0.5),
            1, target_size.cx);
        result.scaled_size.cy = ClampInt(
            (int)floor(design_size.cy * result.scale + 0.5),
            1, target_size.cy);
        result.offset.x = (target_size.cx - result.scaled_size.cx) / 2;
        result.offset.y = (target_size.cy - result.scaled_size.cy) / 2;
        result.active = result.scaled_size.cx != design_size.cx
            || result.scaled_size.cy != design_size.cy;
        return result;
    }

    bool MapRectToTarget(
        const RECT& design_rect, const ScaleGeometry& geometry,
        RECT* target_rect)
    {
        if (!target_rect || geometry.design_size.cx <= 0
            || geometry.design_size.cy <= 0) {
            return false;
        }
        RECT mapped = {
            geometry.offset.x
                + (LONG)floor(design_rect.left * geometry.scale + 0.5),
            geometry.offset.y
                + (LONG)floor(design_rect.top * geometry.scale + 0.5),
            geometry.offset.x
                + (LONG)floor(design_rect.right * geometry.scale + 0.5),
            geometry.offset.y
                + (LONG)floor(design_rect.bottom * geometry.scale + 0.5)
        };
        mapped.left = ClampInt(mapped.left, 0, geometry.target_size.cx);
        mapped.right = ClampInt(mapped.right, 0, geometry.target_size.cx);
        mapped.top = ClampInt(mapped.top, 0, geometry.target_size.cy);
        mapped.bottom = ClampInt(mapped.bottom, 0, geometry.target_size.cy);
        if (mapped.right <= mapped.left || mapped.bottom <= mapped.top) {
            return false;
        }
        *target_rect = mapped;
        return true;
    }

    bool NeedsResample(const SIZE& design_size, const SIZE& target_size)
    {
        return design_size.cx != target_size.cx
            || design_size.cy != target_size.cy;
    }

    bool ScalePremultipliedRgba(
        const BYTE* source, int source_pitch, const SIZE& design_size,
        BYTE* target, int target_pitch, const SIZE& target_size,
        const RECT& source_dirty, Filter filter)
    {
        if (!source || !target || !IsPositiveSize(design_size)
            || !IsPositiveSize(target_size) || source_pitch <= 0
            || target_pitch <= 0
            || filter < FILTER_BILINEAR || filter >= FILTER_COUNT) {
            return false;
        }
        return ScaleSeparable(
            source, source_pitch, design_size, target, target_pitch,
            target_size, source_dirty, filter == FILTER_SPLINE36);
    }

    bool ScalePremultipliedRgbaScalar(
        const BYTE* source, int source_pitch, const SIZE& design_size,
        BYTE* target, int target_pitch, const SIZE& target_size,
        const RECT& source_dirty, Filter filter)
    {
        return ScalePremultipliedRgba(
            source, source_pitch, design_size, target, target_pitch,
            target_size, source_dirty, filter);
    }
}
