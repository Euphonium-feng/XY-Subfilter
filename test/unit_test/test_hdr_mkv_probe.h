#ifndef __TEST_HDR_MKV_PROBE_H__
#define __TEST_HDR_MKV_PROBE_H__

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "hdr_mkv_probe.h"

namespace {

typedef std::vector<unsigned char> Bytes;

void AppendId(Bytes* bytes, unsigned long long id)
{
    unsigned length = 1;
    while (length < 8 && (id >> (length * 8)) != 0) {
        ++length;
    }
    for (unsigned i = 0; i < length; ++i) {
        const unsigned shift = (length - i - 1) * 8;
        bytes->push_back(static_cast<unsigned char>((id >> shift) & 0xFF));
    }
}

void AppendSize(Bytes* bytes, unsigned long long size)
{
    unsigned length = 1;
    while (length < 8 && size >= ((1ULL << (length * 7)) - 1ULL)) {
        ++length;
    }
    for (unsigned i = 0; i < length; ++i) {
        const unsigned shift = (length - i - 1) * 8;
        unsigned char value =
            static_cast<unsigned char>((size >> shift) & 0xFF);
        if (i == 0) {
            value |= static_cast<unsigned char>(1u << (8 - length));
        }
        bytes->push_back(value);
    }
}

Bytes Element(unsigned long long id, const Bytes& payload)
{
    Bytes result;
    AppendId(&result, id);
    AppendSize(&result, payload.size());
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

Bytes UnsignedElement(unsigned long long id, unsigned long long value)
{
    Bytes payload;
    unsigned length = 1;
    while (length < 8 && (value >> (length * 8)) != 0) {
        ++length;
    }
    for (unsigned i = 0; i < length; ++i) {
        const unsigned shift = (length - i - 1) * 8;
        payload.push_back(static_cast<unsigned char>((value >> shift) & 0xFF));
    }
    return Element(id, payload);
}

Bytes StringElement(unsigned long long id, const char* value)
{
    const unsigned char* begin =
        reinterpret_cast<const unsigned char*>(value);
    return Element(id, Bytes(begin, begin + strlen(value)));
}

void Append(Bytes* target, const Bytes& value)
{
    target->insert(target->end(), value.begin(), value.end());
}

Bytes Colour(
    unsigned transfer,
    unsigned matrix,
    unsigned primaries,
    bool mastering,
    bool content_light)
{
    Bytes colour;
    Append(&colour, UnsignedElement(0x55BA, transfer));
    Append(&colour, UnsignedElement(0x55B1, matrix));
    Append(&colour, UnsignedElement(0x55BB, primaries));
    if (content_light) {
        Append(&colour, UnsignedElement(0x55BC, 1000));
        Append(&colour, UnsignedElement(0x55BD, 400));
    }
    if (mastering) {
        Bytes mastering_payload;
        const unsigned char luminance[] = {0x42, 0x5C, 0x00, 0x00};
        Append(
            &mastering_payload,
            Element(0x55D9, Bytes(luminance, luminance + sizeof(luminance))));
        Append(&colour, Element(0x55D0, mastering_payload));
    }
    return Element(0x55B0, colour);
}

Bytes VideoTrack(
    unsigned track_number,
    const char* codec,
    unsigned width,
    unsigned height,
    unsigned long long frame_duration_ns,
    unsigned transfer,
    unsigned matrix,
    unsigned primaries,
    bool mastering = false,
    bool content_light = false)
{
    Bytes video;
    Append(&video, UnsignedElement(0xB0, width));
    Append(&video, UnsignedElement(0xBA, height));
    Append(
        &video,
        Colour(
            transfer, matrix, primaries, mastering, content_light));

    Bytes track;
    Append(&track, UnsignedElement(0xD7, track_number));
    Append(&track, UnsignedElement(0x83, 1));
    Append(&track, StringElement(0x86, codec));
    Append(
        &track,
        UnsignedElement(0x23E383, frame_duration_ns));
    Append(&track, Element(0xE0, video));
    return Element(0xAE, track);
}

Bytes Matroska(const Bytes& tracks, bool unknown_segment_size = false)
{
    Bytes ebml = Element(0x1A45DFA3, Bytes());
    Bytes segment;
    Append(&segment, Element(0x1654AE6B, tracks));

    Bytes file = ebml;
    AppendId(&file, 0x18538067);
    if (unknown_segment_size) {
        const unsigned char unknown[] =
            {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        file.insert(file.end(), unknown, unknown + sizeof(unknown));
    }
    else {
        AppendSize(&file, segment.size());
    }
    Append(&file, segment);
    return file;
}

HdrDetection::HdrMediaSignature Signature(
    HdrDetection::HdrCodecFamily codec,
    unsigned width,
    unsigned height,
    unsigned long long frame_duration_ns)
{
    HdrDetection::HdrMediaSignature signature;
    signature.codec = codec;
    signature.width = width;
    signature.height = height;
    signature.frame_duration_ns = frame_duration_ns;
    return signature;
}

TEST(HdrMkvProbeTest, ParsesPqColourAndStaticHdrMetadata)
{
    const Bytes tracks = VideoTrack(
        1, "V_MPEGH/ISO/HEVC", 1920, 1080, 41708333,
        16, 9, 9, true, true);
    const Bytes file = Matroska(tracks, true);

    const HdrDetection::HdrMkvProbeResult result =
        HdrDetection::ProbeMatroskaHeader(
            file.data(), file.size(),
            Signature(HdrDetection::HDR_CODEC_HEVC, 1920, 1080, 41708333));

    ASSERT_EQ(HdrDetection::HDR_MKV_PROBE_OK, result.status);
    ASSERT_EQ(1u, result.video_tracks.size());
    EXPECT_EQ(0, result.selected_track_index);
    EXPECT_EQ(HdrColor::HDR_SIGNAL_HDR, result.video_tracks[0].evidence.signal);
    EXPECT_EQ(
        HdrColor::VIDEO_MATRIX_2020,
        result.video_tracks[0].evidence.matrix);
    EXPECT_TRUE(result.video_tracks[0].evidence.mastering_metadata);
    EXPECT_TRUE(result.video_tracks[0].evidence.content_light);
}

TEST(HdrMkvProbeTest, ParsesKnownSdrTransfer)
{
    const Bytes tracks = VideoTrack(
        1, "V_MPEG4/ISO/AVC", 1280, 720, 40000000,
        1, 1, 1);
    const Bytes file = Matroska(tracks);

    const HdrDetection::HdrMkvProbeResult result =
        HdrDetection::ProbeMatroskaHeader(
            file.data(), file.size(), HdrDetection::HdrMediaSignature());

    ASSERT_EQ(HdrDetection::HDR_MKV_PROBE_OK, result.status);
    ASSERT_EQ(1u, result.video_tracks.size());
    EXPECT_EQ(HdrColor::HDR_SIGNAL_SDR, result.video_tracks[0].evidence.signal);
}

TEST(HdrMkvProbeTest, ClassifiesHlgAsHdrWithoutSeparateMapping)
{
    const Bytes tracks = VideoTrack(
        1, "V_MPEGH/ISO/HEVC", 1920, 1080, 41708333,
        18, 9, 9);
    const Bytes file = Matroska(tracks);

    const HdrDetection::HdrMkvProbeResult result =
        HdrDetection::ProbeMatroskaHeader(
            file.data(), file.size(), HdrDetection::HdrMediaSignature());

    ASSERT_EQ(HdrDetection::HDR_MKV_PROBE_OK, result.status);
    ASSERT_EQ(1u, result.video_tracks.size());
    EXPECT_EQ(18, result.video_tracks[0].transfer_characteristics);
    EXPECT_EQ(HdrColor::HDR_SIGNAL_HDR, result.video_tracks[0].evidence.signal);
}

TEST(HdrMkvProbeTest, MissingColourLeavesSignalUnknown)
{
    Bytes video;
    Append(&video, UnsignedElement(0xB0, 1920));
    Append(&video, UnsignedElement(0xBA, 1080));

    Bytes track;
    Append(&track, UnsignedElement(0xD7, 1));
    Append(&track, UnsignedElement(0x83, 1));
    Append(&track, StringElement(0x86, "V_MPEGH/ISO/HEVC"));
    Append(&track, Element(0xE0, video));
    const Bytes file = Matroska(Element(0xAE, track));

    const HdrDetection::HdrMkvProbeResult result =
        HdrDetection::ProbeMatroskaHeader(
            file.data(), file.size(), HdrDetection::HdrMediaSignature());

    ASSERT_EQ(HdrDetection::HDR_MKV_PROBE_OK, result.status);
    ASSERT_EQ(1u, result.video_tracks.size());
    EXPECT_EQ(
        HdrColor::HDR_SIGNAL_UNKNOWN,
        result.video_tracks[0].evidence.signal);
}

TEST(HdrMkvProbeTest, SelectsOnlyUniqueActiveTrackMatch)
{
    Bytes tracks;
    Append(&tracks, VideoTrack(
        1, "V_MPEGH/ISO/HEVC", 1920, 1080, 41708333,
        16, 9, 9));
    Append(&tracks, VideoTrack(
        2, "V_MPEG4/ISO/AVC", 1280, 720, 40000000,
        1, 1, 1));
    const Bytes file = Matroska(tracks);

    const HdrDetection::HdrMkvProbeResult result =
        HdrDetection::ProbeMatroskaHeader(
            file.data(), file.size(),
            Signature(HdrDetection::HDR_CODEC_HEVC, 1920, 1080, 41708333));

    ASSERT_EQ(2u, result.video_tracks.size());
    EXPECT_EQ(0, result.selected_track_index);
    ASSERT_TRUE(HdrDetection::GetSelectedMkvTrack(result) != NULL);
    EXPECT_EQ(
        HdrColor::HDR_SIGNAL_HDR,
        HdrDetection::GetSelectedMkvTrack(result)->evidence.signal);
}

TEST(HdrMkvProbeTest, AmbiguousActiveTrackMatchIsRejected)
{
    Bytes tracks;
    Append(&tracks, VideoTrack(
        1, "V_MPEGH/ISO/HEVC", 1920, 1080, 41708333,
        16, 9, 9));
    Append(&tracks, VideoTrack(
        2, "V_MPEGH/ISO/HEVC", 1920, 1080, 41708333,
        1, 1, 1));
    const Bytes file = Matroska(tracks);

    const HdrDetection::HdrMkvProbeResult result =
        HdrDetection::ProbeMatroskaHeader(
            file.data(), file.size(),
            Signature(HdrDetection::HDR_CODEC_HEVC, 1920, 1080, 41708333));

    EXPECT_EQ(2u, result.video_tracks.size());
    EXPECT_EQ(-1, result.selected_track_index);
    EXPECT_TRUE(HdrDetection::GetSelectedMkvTrack(result) == NULL);
}

TEST(HdrMkvProbeTest, TruncatedInputFailsClosed)
{
    const Bytes tracks = VideoTrack(
        1, "V_MPEGH/ISO/HEVC", 1920, 1080, 41708333,
        16, 9, 9);
    Bytes file = Matroska(tracks, true);
    file.resize(file.size() - 3);

    const HdrDetection::HdrMkvProbeResult result =
        HdrDetection::ProbeMatroskaHeader(
            file.data(), file.size(), HdrDetection::HdrMediaSignature());

    EXPECT_NE(HdrDetection::HDR_MKV_PROBE_OK, result.status);
    EXPECT_TRUE(HdrDetection::GetSelectedMkvTrack(result) == NULL);
}

TEST(HdrMkvProbeTest, ElementLimitFailsClosed)
{
    Bytes colour;
    for (unsigned i = 0; i < 4200; ++i) {
        Append(&colour, UnsignedElement(0x55B9, 0));
    }

    Bytes video;
    Append(&video, Element(0x55B0, colour));
    Bytes track;
    Append(&track, UnsignedElement(0xD7, 1));
    Append(&track, UnsignedElement(0x83, 1));
    Append(&track, StringElement(0x86, "V_MPEGH/ISO/HEVC"));
    Append(&track, Element(0xE0, video));
    const Bytes file = Matroska(Element(0xAE, track));

    const HdrDetection::HdrMkvProbeResult result =
        HdrDetection::ProbeMatroskaHeader(
            file.data(), file.size(), HdrDetection::HdrMediaSignature());

    EXPECT_EQ(HdrDetection::HDR_MKV_PROBE_LIMIT_EXCEEDED, result.status);
    EXPECT_TRUE(HdrDetection::GetSelectedMkvTrack(result) == NULL);
}

}
#endif // __TEST_HDR_MKV_PROBE_H__
