#include "bitmap_resampler.h"

#if defined(_M_X64) || defined(__SSE2__) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define BITMAP_RESAMPLER_HAVE_SSE2 1
#include <emmintrin.h>
#else
#define BITMAP_RESAMPLER_HAVE_SSE2 0
#endif
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

    // Before FlipAlphaValue, MSP_RGBA stores an inverted alpha: 0 is fully
    // opaque and 255 is fully transparent. FillSolidRect writes premultiplied
    // colour into this form, so the stored layout satisfies
    // RGB <= 255 - alpha. Project the filtered result back onto that
    // invariant; fully transparent pixels (alpha 255) carry zero RGB.
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
        if (alpha == 255) {
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
        // at every integer phase. This is the canonical spline36 kernel used
        // by mpv/FFmpeg, not a Mitchell-Netravali B/C cubic.
        x = fabs(x);
        if (x < 1.0) {
            return (7.0 * x * x * x - 11.0 * x * x + 4.0) / 4.0;
        }
        if (x < 2.0) {
            return (-3.0 * x * x * x + 16.0 * x * x - 27.0 * x + 14.0)
                / 2.0;
        }
        if (x < 3.0) {
            return (x * x * x - 8.0 * x * x + 21.0 * x - 18.0) / 5.0;
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
        const int span = last - first + 1;
        if (span > capacity) {
            // Extreme downscale: the kernel support is wider than the tap
            // budget. Sample the kernel over evenly spaced bins instead of
            // failing, so the subtitle remains visible.
            const double step = (double)span / capacity;
            double total = 0.0;
            for (int k = 0; k < capacity; ++k) {
                const double sample = first + (k + 0.5) * step;
                const double weight = spline
                    ? Spline36Kernel((center - sample) * filter_scale)
                    : LinearKernel((center - sample) * filter_scale);
                taps[k].index = ClampInt(
                    (int)floor(sample + 0.5), 0, source_size - 1);
                taps[k].weight = weight;
                total += weight;
            }
            if (total == 0.0) {
                for (int k = 0; k < capacity; ++k) {
                    taps[k].weight = 1.0 / capacity;
                }
            } else {
                for (int k = 0; k < capacity; ++k) {
                    taps[k].weight /= total;
                }
            }
            return capacity;
        }
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

    RECT ClipRectToTarget(const RECT& rect, const SIZE& target_size)
    {
        RECT clipped = {
            ClampInt(rect.left, 0, target_size.cx),
            ClampInt(rect.top, 0, target_size.cy),
            ClampInt(rect.right, 0, target_size.cx),
            ClampInt(rect.bottom, 0, target_size.cy)
        };
        return clipped;
    }

    bool IsEmptyRect(const RECT& rect)
    {
        return rect.right <= rect.left || rect.bottom <= rect.top;
    }

    int FilterMargin(double scale, bool spline)
    {
        // Destination-space support of one source pixel. Upscaling by s
        // stretches the support to r*s destination pixels; downscaling keeps
        // the destination footprint at r. The extra pixel absorbs the
        // half-pixel phase produced by the centre mapping.
        const double support = spline ? kSplineRadius : kLinearRadius;
        return (int)ceil(support * (scale > 1.0 ? scale : 1.0)) + 2;
    }

#if BITMAP_RESAMPLER_HAVE_SSE2
    void FilterRowSse2(
        const BYTE* source_row, const std::vector<int>& x_offsets,
        const std::vector<Tap>& x_taps, int dst_width, float* output)
    {
        const __m128i zero = _mm_setzero_si128();
        for (int x = 0; x < dst_width; ++x) {
            const int start = x_offsets[(size_t)x];
            const int end = x_offsets[(size_t)x + 1];
            __m128 sum = _mm_setzero_ps();
            for (int t = start; t < end; ++t) {
                const Tap& tap = x_taps[(size_t)t];
                const DWORD packed = *reinterpret_cast<const DWORD*>(
                    source_row + (size_t)tap.index * 4);
                __m128i bytes = _mm_cvtsi32_si128(static_cast<int>(packed));
                __m128i words = _mm_unpacklo_epi8(bytes, zero);
                __m128i dwords = _mm_unpacklo_epi16(words, zero);
                const __m128 values = _mm_cvtepi32_ps(dwords);
                const __m128 weight = _mm_set1_ps((float)tap.weight);
                sum = _mm_add_ps(sum, _mm_mul_ps(values, weight));
            }
            _mm_storeu_ps(output + (size_t)x * 4, sum);
        }
    }

    void AccumulateRowSse2(
        float* accumulated, const float* row, int floats, float weight)
    {
        const __m128 w = _mm_set1_ps(weight);
        for (int i = 0; i < floats; i += 4) {
            const __m128 dst = _mm_loadu_ps(accumulated + i);
            const __m128 src = _mm_loadu_ps(row + i);
            _mm_storeu_ps(
                accumulated + i, _mm_add_ps(dst, _mm_mul_ps(src, w)));
        }
    }
#endif

    void FilterRowScalar(
        const BYTE* source_row, const std::vector<int>& x_offsets,
        const std::vector<Tap>& x_taps, int dst_width, float* output)
    {
        for (int x = 0; x < dst_width; ++x) {
            const int start = x_offsets[(size_t)x];
            const int end = x_offsets[(size_t)x + 1];
            float b = 0.0f, g = 0.0f, r = 0.0f, a = 0.0f;
            for (int t = start; t < end; ++t) {
                const Tap& tap = x_taps[(size_t)t];
                const BYTE* px = source_row + (size_t)tap.index * 4;
                const float w = (float)tap.weight;
                b += px[0] * w;
                g += px[1] * w;
                r += px[2] * w;
                a += px[3] * w;
            }
            const size_t o = (size_t)x * 4;
            output[o + 0] = b;
            output[o + 1] = g;
            output[o + 2] = r;
            output[o + 3] = a;
        }
    }

    void AccumulateRowScalar(
        float* accumulated, const float* row, int floats, float weight)
    {
        for (int i = 0; i < floats; ++i) {
            accumulated[i] += row[i] * weight;
        }
    }

    // Two-pass separable filter. Horizontal taps depend only on the
    // destination column, so they are built once for the whole call.
    // Horizontal rows are cached only within the union of sources touched by
    // this dirty rect; each is filtered once. Working memory is bounded by the
    // dirty band, not by the source height or the full canvas.
    bool ScaleSeparable(
        const BYTE* source, int source_pitch, const SIZE& design_size,
        BYTE* target, int target_pitch, const SIZE& target_size,
        const RECT& source_dirty, bool spline, bool use_sse2)
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

        RECT mapped = {};
        if (!BitmapResampler::MapDirtyRectToTarget(
                source_dirty, geometry,
                spline
                    ? BitmapResampler::FILTER_SPLINE36
                    : BitmapResampler::FILTER_BILINEAR,
                &mapped)) {
            return true;
        }

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

        std::vector<float> accumulated((size_t)dst_width * 4, 0.0f);

        const int row_stride = dst_width * 4;
        // A destination row uses at most kMaxTaps source rows. Keep those
        // horizontally filtered rows in a ring indexed by source row; the
        // window advances monotonically as y advances. This keeps the working
        // set at two float rows plus the tap budget, independent of the source
        // height or the full canvas.
        const int ring_size = kMaxTaps + 2;
        std::vector<int> ring_rows((size_t)ring_size, -1);
        std::vector<float> ring_cache((size_t)ring_size * row_stride, 0.0f);

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
                const int source_row = y_taps[ty].index;
                const int slot = source_row % ring_size;
                if (ring_rows[(size_t)slot] != source_row) {
                    float* output = &ring_cache[(size_t)slot * row_stride];
                    const BYTE* src_row =
                        source + (size_t)source_row * source_pitch;
#if BITMAP_RESAMPLER_HAVE_SSE2
                    if (use_sse2) {
                        FilterRowSse2(
                            src_row, x_offsets, x_taps_flat, dst_width,
                            output);
                    } else
#endif
                    {
                        FilterRowScalar(
                            src_row, x_offsets, x_taps_flat, dst_width,
                            output);
                    }
                    ring_rows[(size_t)slot] = source_row;
                }
                const float wy = (float)y_taps[ty].weight;
                const float* row = &ring_cache[(size_t)slot * row_stride];
#if BITMAP_RESAMPLER_HAVE_SSE2
                if (use_sse2) {
                    AccumulateRowSse2(
                        accumulated.data(), row, row_stride, wy);
                } else
#endif
                {
                    AccumulateRowScalar(
                        accumulated.data(), row, row_stride, wy);
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
        // Padding alone also requires the copy path: a 1440x1080 design
        // centred in 1920x1080 has scale 1 and a non-zero x offset.
        result.active = result.scaled_size.cx != design_size.cx
            || result.scaled_size.cy != design_size.cy
            || result.offset.x != 0
            || result.offset.y != 0;
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

    bool MapDirtyRectToTarget(
        const RECT& source_dirty, const ScaleGeometry& geometry,
        Filter filter, RECT* target_dirty)
    {
        if (!target_dirty || !IsPositiveSize(geometry.design_size)
            || !IsPositiveSize(geometry.target_size)
            || filter < FILTER_BILINEAR || filter >= FILTER_COUNT) {
            return false;
        }

        RECT dirty = ClipRectToTarget(
            source_dirty, geometry.design_size);
        if (IsEmptyRect(dirty)) {
            return false;
        }

        if (!geometry.active) {
            *target_dirty = dirty;
            return true;
        }

        RECT mapped = {};
        if (!MapRectToTarget(dirty, geometry, &mapped)) {
            return false;
        }

        const int margin =
            FilterMargin(geometry.scale, filter == FILTER_SPLINE36);
        RECT expanded = {
            mapped.left - margin,
            mapped.top - margin,
            mapped.right + margin,
            mapped.bottom + margin
        };

        // Letterbox padding is always transparent. The filter clamps source
        // samples at the design edge, so writing the support halo outside the
        // scaled rect would smear content into the padding.
        const RECT scaled = {
            geometry.offset.x,
            geometry.offset.y,
            geometry.offset.x + geometry.scaled_size.cx,
            geometry.offset.y + geometry.scaled_size.cy
        };
        RECT written = {
            expanded.left > scaled.left ? expanded.left : scaled.left,
            expanded.top > scaled.top ? expanded.top : scaled.top,
            expanded.right < scaled.right ? expanded.right : scaled.right,
            expanded.bottom < scaled.bottom ? expanded.bottom : scaled.bottom
        };
        written = ClipRectToTarget(written, geometry.target_size);
        if (IsEmptyRect(written)) {
            return false;
        }

        *target_dirty = written;
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
            target_size, source_dirty, filter == FILTER_SPLINE36, true);
    }

    bool ScalePremultipliedRgbaScalar(
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
            target_size, source_dirty, filter == FILTER_SPLINE36, false);
    }

    bool HasSse2Kernel()
    {
        return BITMAP_RESAMPLER_HAVE_SSE2 != 0;
    }
}
