#include "hdr_mkv_probe.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace HdrDetection
{
    namespace
    {
        const size_t kMaxEbmlDepth = 8;
        const unsigned long long kMaxEbmlElements = 4096;
        const unsigned long long kMaxMkvTracks = 64;

        const unsigned long long ID_EBML = 0x1A45DFA3;
        const unsigned long long ID_SEGMENT = 0x18538067;
        const unsigned long long ID_TRACKS = 0x1654AE6B;
        const unsigned long long ID_TRACK_ENTRY = 0xAE;
        const unsigned long long ID_TRACK_NUMBER = 0xD7;
        const unsigned long long ID_TRACK_TYPE = 0x83;
        const unsigned long long ID_CODEC_ID = 0x86;
        const unsigned long long ID_DEFAULT_DURATION = 0x23E383;
        const unsigned long long ID_VIDEO = 0xE0;
        const unsigned long long ID_PIXEL_WIDTH = 0xB0;
        const unsigned long long ID_PIXEL_HEIGHT = 0xBA;
        const unsigned long long ID_COLOUR = 0x55B0;
        const unsigned long long ID_MATRIX_COEFFICIENTS = 0x55B1;
        const unsigned long long ID_TRANSFER_CHARACTERISTICS = 0x55BA;
        const unsigned long long ID_PRIMARIES = 0x55BB;
        const unsigned long long ID_MAX_CLL = 0x55BC;
        const unsigned long long ID_MAX_FALL = 0x55BD;
        const unsigned long long ID_MASTERING_METADATA = 0x55D0;
        const unsigned long long ID_LUMINANCE_MAX = 0x55D9;
        const unsigned long long ID_LUMINANCE_MIN = 0x55DA;

        struct ElementHeader
        {
            unsigned long long id;
            unsigned long long size;
            size_t payload;
            bool unknown_size;
        };

        class Parser
        {
        public:
            Parser(
                const unsigned char* data,
                size_t size,
                const HdrMediaSignature& active_signature)
                : m_data(data)
                , m_size(size)
                , m_active_signature(active_signature)
                , m_elements(0)
                , m_status(HDR_MKV_PROBE_OK)
                , m_tracks_seen(0)
            {
            }

            HdrMkvProbeResult Run()
            {
                m_result = HdrMkvProbeResult();
                if (!m_data || m_size < 4) {
                    m_status = HDR_MKV_PROBE_TRUNCATED;
                    return Finish();
                }

                size_t position = 0;
                bool found_segment = false;
                while (position < m_size) {
                    ElementHeader header;
                    if (!ReadHeader(position, m_size, &header)) {
                        break;
                    }
                    if (header.id == ID_EBML) {
                        if (!SkipElement(header)) {
                            break;
                        }
                        position = header.payload + size_t(header.size);
                    }
                    else if (header.id == ID_SEGMENT) {
                        found_segment = true;
                        const size_t segment_end = header.unknown_size
                            ? m_size
                            : ClampEnd(header.payload, header.size);
                        ParseSegment(header.payload, segment_end, 1);
                        break;
                    }
                    else if (!SkipElement(header)) {
                        break;
                    }
                    else {
                        position = header.payload + size_t(header.size);
                    }
                }

                if (!found_segment) {
                    m_status = HDR_MKV_PROBE_UNSUPPORTED;
                }
                else {
                    SelectTrack();
                }
                return Finish();
            }

        private:
            bool ReadVint(
                size_t position,
                size_t end,
                bool keep_marker,
                unsigned long long* value,
                size_t* length,
                bool* unknown) const
            {
                if (position >= end || !value || !length || !unknown) {
                    return false;
                }

                const unsigned char first = m_data[position];
                if (first == 0) {
                    return false;
                }

                size_t count = 1;
                unsigned char mask = 0x80;
                while (count <= 8 && (first & mask) == 0) {
                    mask >>= 1;
                    ++count;
                }
                if (count > 8 || position + count > end) {
                    return false;
                }

                unsigned long long parsed = keep_marker
                    ? first
                    : (first & (mask - 1));
                for (size_t i = 1; i < count; ++i) {
                    parsed = (parsed << 8) | m_data[position + i];
                }

                *value = parsed;
                *length = count;
                *unknown = !keep_marker
                    && parsed == ((1ULL << (count * 7)) - 1ULL);
                return true;
            }

            bool ReadHeader(
                size_t position, size_t end, ElementHeader* header) const
            {
                if (!header) {
                    return false;
                }

                unsigned long long id = 0;
                size_t id_length = 0;
                bool id_unknown = false;
                if (!ReadVint(position, end, true, &id, &id_length, &id_unknown)
                    || id_unknown || id_length > 4) {
                    return false;
                }

                unsigned long long size = 0;
                size_t size_length = 0;
                bool size_unknown = false;
                if (!ReadVint(
                        position + id_length,
                        end,
                        false,
                        &size,
                        &size_length,
                        &size_unknown)) {
                    return false;
                }

                header->id = id;
                header->size = size;
                header->payload = position + id_length + size_length;
                header->unknown_size = size_unknown;
                return true;
            }

            size_t ClampEnd(size_t payload, unsigned long long size) const
            {
                if (size > m_size - payload) {
                    return m_size;
                }
                return payload + size_t(size);
            }

            bool SkipElement(const ElementHeader& header)
            {
                if (header.unknown_size) {
                    m_status = HDR_MKV_PROBE_INVALID;
                    return false;
                }
                if (header.size > m_size - header.payload) {
                    m_status = HDR_MKV_PROBE_TRUNCATED;
                    return false;
                }
                return true;
            }

            bool ReadUnsigned(
                const ElementHeader& header, unsigned long long* value) const
            {
                if (!value || header.unknown_size || header.size > 8
                    || header.size > m_size - header.payload) {
                    return false;
                }
                unsigned long long parsed = 0;
                for (size_t i = 0; i < size_t(header.size); ++i) {
                    parsed = (parsed << 8) | m_data[header.payload + i];
                }
                *value = parsed;
                return true;
            }

            bool ReadString(
                const ElementHeader& header, std::string* value) const
            {
                if (!value || header.unknown_size
                    || header.size > m_size - header.payload) {
                    return false;
                }
                value->assign(
                    reinterpret_cast<const char*>(m_data + header.payload),
                    size_t(header.size));
                return true;
            }

            bool ParseSegment(size_t begin, size_t end, size_t depth)
            {
                if (depth > kMaxEbmlDepth) {
                    m_status = HDR_MKV_PROBE_LIMIT_EXCEEDED;
                    return false;
                }

                size_t position = begin;
                while (position < end && m_status == HDR_MKV_PROBE_OK) {
                    ElementHeader header;
                    if (!ReadHeader(position, end, &header)) {
                        m_status = HDR_MKV_PROBE_INVALID;
                        return false;
                    }
                    if (!CountElement()) {
                        return false;
                    }
                    if (header.id == ID_TRACKS) {
                        if (header.unknown_size
                            || header.size > end - header.payload) {
                            m_status = header.unknown_size
                                ? HDR_MKV_PROBE_UNSUPPORTED
                                : HDR_MKV_PROBE_TRUNCATED;
                            return false;
                        }
                        ParseTracks(
                            header.payload, header.payload + size_t(header.size),
                            depth + 1);
                        position = header.payload + size_t(header.size);
                    }
                    else if (!SkipElement(header)) {
                        return false;
                    }
                    else {
                        position = header.payload + size_t(header.size);
                    }
                }
                return m_status == HDR_MKV_PROBE_OK;
            }

            bool CountElement()
            {
                ++m_elements;
                if (m_elements > kMaxEbmlElements) {
                    m_status = HDR_MKV_PROBE_LIMIT_EXCEEDED;
                    return false;
                }
                return true;
            }

            bool ParseTracks(size_t begin, size_t end, size_t depth)
            {
                size_t position = begin;
                while (position < end && m_status == HDR_MKV_PROBE_OK) {
                    ElementHeader header;
                    if (!ReadHeader(position, end, &header)) {
                        m_status = HDR_MKV_PROBE_INVALID;
                        return false;
                    }
                    if (!CountElement()) {
                        return false;
                    }
                    if (header.id == ID_TRACK_ENTRY) {
                        if (header.unknown_size
                            || header.size > end - header.payload) {
                            m_status = header.unknown_size
                                ? HDR_MKV_PROBE_UNSUPPORTED
                                : HDR_MKV_PROBE_TRUNCATED;
                            return false;
                        }
                        ParseTrackEntry(
                            header.payload, header.payload + size_t(header.size),
                            depth + 1);
                        position = header.payload + size_t(header.size);
                    }
                    else if (!SkipElement(header)) {
                        return false;
                    }
                    else {
                        position = header.payload + size_t(header.size);
                    }
                }
                return m_status == HDR_MKV_PROBE_OK;
            }

            bool ParseTrackEntry(size_t begin, size_t end, size_t depth)
            {
                HdrMkvTrack track;
                unsigned long long track_type = 0;
                bool has_track_type = false;
                std::string codec_id;

                size_t position = begin;
                while (position < end && m_status == HDR_MKV_PROBE_OK) {
                    ElementHeader header;
                    if (!ReadHeader(position, end, &header)) {
                        m_status = HDR_MKV_PROBE_INVALID;
                        return false;
                    }
                    if (!CountElement()) {
                        return false;
                    }

                    if (header.id == ID_TRACK_NUMBER) {
                        ReadUnsigned(header, &track.track_number);
                    }
                    else if (header.id == ID_TRACK_TYPE) {
                        has_track_type =
                            ReadUnsigned(header, &track_type);
                    }
                    else if (header.id == ID_CODEC_ID) {
                        ReadString(header, &codec_id);
                    }
                    else if (header.id == ID_DEFAULT_DURATION) {
                        ReadUnsigned(header, &track.frame_duration_ns);
                    }
                    else if (header.id == ID_VIDEO) {
                        if (header.unknown_size
                            || header.size > end - header.payload) {
                            m_status = header.unknown_size
                                ? HDR_MKV_PROBE_UNSUPPORTED
                                : HDR_MKV_PROBE_TRUNCATED;
                            return false;
                        }
                        ParseVideo(
                            header.payload, header.payload + size_t(header.size),
                            depth + 1, &track);
                        position = header.payload + size_t(header.size);
                        continue;
                    }

                    if (!SkipElement(header)) {
                        return false;
                    }
                    position = header.payload + size_t(header.size);
                }

                if (m_status != HDR_MKV_PROBE_OK) {
                    return false;
                }
                if (has_track_type && track_type == 1) {
                    track.codec = CodecFromId(codec_id);
                    FinalizeTrackEvidence(&track);
                    m_result.video_tracks.push_back(track);
                    ++m_tracks_seen;
                    if (m_tracks_seen > kMaxMkvTracks) {
                        m_status = HDR_MKV_PROBE_LIMIT_EXCEEDED;
                        return false;
                    }
                }
                return true;
            }

            bool ParseVideo(
                size_t begin, size_t end, size_t depth, HdrMkvTrack* track)
            {
                if (depth > kMaxEbmlDepth || !track) {
                    m_status = HDR_MKV_PROBE_LIMIT_EXCEEDED;
                    return false;
                }

                size_t position = begin;
                while (position < end && m_status == HDR_MKV_PROBE_OK) {
                    ElementHeader header;
                    if (!ReadHeader(position, end, &header)) {
                        m_status = HDR_MKV_PROBE_INVALID;
                        return false;
                    }
                    if (!CountElement()) {
                        return false;
                    }
                    if (header.id == ID_PIXEL_WIDTH) {
                        unsigned long long value = 0;
                        if (ReadUnsigned(header, &value)) {
                            track->width = unsigned(value);
                        }
                    }
                    else if (header.id == ID_PIXEL_HEIGHT) {
                        unsigned long long value = 0;
                        if (ReadUnsigned(header, &value)) {
                            track->height = unsigned(value);
                        }
                    }
                    else if (header.id == ID_COLOUR) {
                        if (header.unknown_size
                            || header.size > end - header.payload) {
                            m_status = header.unknown_size
                                ? HDR_MKV_PROBE_UNSUPPORTED
                                : HDR_MKV_PROBE_TRUNCATED;
                            return false;
                        }
                        ParseColour(
                            header.payload, header.payload + size_t(header.size),
                            depth + 1, track);
                        position = header.payload + size_t(header.size);
                        continue;
                    }

                    if (!SkipElement(header)) {
                        return false;
                    }
                    position = header.payload + size_t(header.size);
                }
                return m_status == HDR_MKV_PROBE_OK;
            }

            bool ParseColour(
                size_t begin, size_t end, size_t depth, HdrMkvTrack* track)
            {
                if (depth > kMaxEbmlDepth || !track) {
                    m_status = HDR_MKV_PROBE_LIMIT_EXCEEDED;
                    return false;
                }

                size_t position = begin;
                while (position < end && m_status == HDR_MKV_PROBE_OK) {
                    ElementHeader header;
                    if (!ReadHeader(position, end, &header)) {
                        m_status = HDR_MKV_PROBE_INVALID;
                        return false;
                    }
                    if (!CountElement()) {
                        return false;
                    }
                    if (header.id == ID_TRANSFER_CHARACTERISTICS) {
                        unsigned long long value = 0;
                        if (ReadUnsigned(header, &value)) {
                            track->transfer_characteristics = int(value);
                        }
                    }
                    else if (header.id == ID_MATRIX_COEFFICIENTS) {
                        unsigned long long value = 0;
                        if (ReadUnsigned(header, &value)) {
                            track->matrix_coefficients = int(value);
                        }
                    }
                    else if (header.id == ID_PRIMARIES) {
                        unsigned long long value = 0;
                        if (ReadUnsigned(header, &value)) {
                            track->primaries = int(value);
                        }
                    }
                    else if (header.id == ID_MAX_CLL
                        || header.id == ID_MAX_FALL) {
                        unsigned long long value = 0;
                        if (ReadUnsigned(header, &value) && value > 0) {
                            track->evidence.content_light = true;
                        }
                    }
                    else if (header.id == ID_MASTERING_METADATA) {
                        if (header.unknown_size
                            || header.size > end - header.payload) {
                            m_status = header.unknown_size
                                ? HDR_MKV_PROBE_UNSUPPORTED
                                : HDR_MKV_PROBE_TRUNCATED;
                            return false;
                        }
                        ParseMastering(
                            header.payload, header.payload + size_t(header.size),
                            depth + 1, track);
                        position = header.payload + size_t(header.size);
                        continue;
                    }

                    if (!SkipElement(header)) {
                        return false;
                    }
                    position = header.payload + size_t(header.size);
                }
                return m_status == HDR_MKV_PROBE_OK;
            }

            bool ParseMastering(
                size_t begin, size_t end, size_t depth, HdrMkvTrack* track)
            {
                if (depth > kMaxEbmlDepth || !track) {
                    m_status = HDR_MKV_PROBE_LIMIT_EXCEEDED;
                    return false;
                }

                size_t position = begin;
                while (position < end && m_status == HDR_MKV_PROBE_OK) {
                    ElementHeader header;
                    if (!ReadHeader(position, end, &header)) {
                        m_status = HDR_MKV_PROBE_INVALID;
                        return false;
                    }
                    if (!CountElement()) {
                        return false;
                    }
                    if (header.id == ID_LUMINANCE_MAX
                        || header.id == ID_LUMINANCE_MIN) {
                        unsigned long long value = 0;
                        if (ReadUnsigned(header, &value) && value > 0) {
                            track->evidence.mastering_metadata = true;
                        }
                    }
                    if (!SkipElement(header)) {
                        return false;
                    }
                    position = header.payload + size_t(header.size);
                }
                return m_status == HDR_MKV_PROBE_OK;
            }

            static HdrCodecFamily CodecFromId(const std::string& codec_id)
            {
                if (codec_id == "V_MPEGH/ISO/HEVC") {
                    return HDR_CODEC_HEVC;
                }
                if (codec_id == "V_MPEG4/ISO/AVC") {
                    return HDR_CODEC_H264;
                }
                if (codec_id == "V_AV1") {
                    return HDR_CODEC_AV1;
                }
                if (codec_id == "V_VP9") {
                    return HDR_CODEC_VP9;
                }
                if (codec_id == "V_MPEG2") {
                    return HDR_CODEC_MPEG2;
                }
                return HDR_CODEC_OTHER;
            }

            static HdrColor::HdrSignal ClassifyH273Transfer(int value)
            {
                switch (value) {
                case 16: // SMPTE ST 2084 (PQ)
                case 18: // ARIB STD-B67 (HLG)
                    return HdrColor::HDR_SIGNAL_HDR;
                case 1:  // BT.709
                case 4:  // gamma 2.2
                case 5:  // gamma 2.8
                case 6:  // SMPTE 170M
                case 7:  // SMPTE 240M
                case 8:  // linear
                case 11: // IEC 61966-2-4
                case 12: // BT.1361
                case 13: // IEC 61966-2-1
                case 14: // BT.2020 10-bit
                case 15: // BT.2020 12-bit
                case 17: // DCI
                    return HdrColor::HDR_SIGNAL_SDR;
                default:
                    return HdrColor::HDR_SIGNAL_UNKNOWN;
                }
            }

            static HdrColor::VideoMatrix ClassifyH273Matrix(int value)
            {
                switch (value) {
                case 9:  // BT.2020 non-constant luminance
                case 10: // BT.2020 constant luminance
                    return HdrColor::VIDEO_MATRIX_2020;
                case 1:
                case 3:
                    return HdrColor::VIDEO_MATRIX_709;
                case 5:
                case 6:
                    return HdrColor::VIDEO_MATRIX_601;
                default:
                    return HdrColor::VIDEO_MATRIX_UNKNOWN;
                }
            }

            static void FinalizeTrackEvidence(HdrMkvTrack* track)
            {
                if (!track) {
                    return;
                }

                track->evidence.source = HDR_EVIDENCE_SOURCE_CONTAINER;
                track->evidence.stage = HDR_EVIDENCE_STAGE_CONTAINER;
                track->evidence.depth = 1;
                track->evidence.matrix = ClassifyH273Matrix(
                    track->matrix_coefficients);
                track->evidence.has_matrix =
                    track->evidence.matrix != HdrColor::VIDEO_MATRIX_UNKNOWN;
                track->evidence.primaries = track->primaries;
                track->evidence.has_primaries = track->primaries >= 0;

                HdrColor::HdrSignal signal = ClassifyH273Transfer(
                    track->transfer_characteristics);
                track->evidence.transfer_function =
                    track->transfer_characteristics;
                track->evidence.has_transfer =
                    signal != HdrColor::HDR_SIGNAL_UNKNOWN;
                if (track->evidence.mastering_metadata
                    || track->evidence.content_light) {
                    signal = HdrColor::HDR_SIGNAL_HDR;
                }
                track->evidence.signal = signal;

                if (track->primaries == 9) {
                    // Primaries alone are not evidence of HDR. They are kept
                    // for diagnostics and future matrix fallback work.
                    track->evidence.has_primaries = true;
                }
            }

            static bool FrameDurationMatches(
                unsigned long long candidate, unsigned long long active)
            {
                if (!candidate || !active) {
                    return false;
                }
                const unsigned long long delta = candidate > active
                    ? candidate - active
                    : active - candidate;
                const unsigned long long tolerance = std::max<unsigned long long>(
                    1000000ULL, active / 100ULL);
                return delta <= tolerance;
            }

            static int MatchScore(
                const HdrMkvTrack& track,
                const HdrMediaSignature& active)
            {
                if (active.codec != HDR_CODEC_UNKNOWN
                    && track.codec != active.codec) {
                    return -1;
                }
                if (active.width && track.width != active.width) {
                    return -1;
                }
                if (active.height && track.height != active.height) {
                    return -1;
                }
                if (active.frame_duration_ns
                    && !FrameDurationMatches(
                        track.frame_duration_ns,
                        active.frame_duration_ns)) {
                    return -1;
                }

                int score = 0;
                if (active.codec != HDR_CODEC_UNKNOWN) {
                    score += 8;
                }
                if (active.width && active.height) {
                    score += 4;
                }
                if (active.frame_duration_ns) {
                    score += 2;
                }
                return score;
            }

            void SelectTrack()
            {
                m_result.selected_track_index = -1;
                if (m_result.video_tracks.empty()) {
                    return;
                }
                if (m_result.video_tracks.size() == 1) {
                    m_result.selected_track_index = 0;
                    return;
                }

                int best_score = -1;
                int best_count = 0;
                int best_index = -1;
                for (size_t i = 0; i < m_result.video_tracks.size(); ++i) {
                    const int score = MatchScore(
                        m_result.video_tracks[i], m_active_signature);
                    if (score < 0) {
                        continue;
                    }
                    if (score > best_score) {
                        best_score = score;
                        best_count = 1;
                        best_index = int(i);
                    }
                    else if (score == best_score) {
                        ++best_count;
                    }
                }

                if (best_score >= 0 && best_count == 1) {
                    m_result.selected_track_index = best_index;
                }
            }

            HdrMkvProbeResult Finish()
            {
                m_result.status = m_status;
                m_result.elements_scanned = m_elements;
                return m_result;
            }

            const unsigned char* m_data;
            size_t m_size;
            HdrMediaSignature m_active_signature;
            unsigned long long m_elements;
            HdrMkvProbeStatus m_status;
            unsigned long long m_tracks_seen;
            HdrMkvProbeResult m_result;
        };
    }

    HdrMkvTrack::HdrMkvTrack()
        : track_number(0)
        , codec(HDR_CODEC_UNKNOWN)
        , width(0)
        , height(0)
        , frame_duration_ns(0)
        , transfer_characteristics(-1)
        , matrix_coefficients(-1)
        , primaries(-1)
    {
    }

    HdrMkvProbeResult::HdrMkvProbeResult()
        : status(HDR_MKV_PROBE_OK)
        , elements_scanned(0)
        , selected_track_index(-1)
    {
    }

    HdrMkvProbeResult ProbeMatroskaHeader(
        const unsigned char* data,
        size_t size,
        const HdrMediaSignature& active_signature)
    {
        return Parser(data, size, active_signature).Run();
    }

    const HdrMkvTrack* GetSelectedMkvTrack(const HdrMkvProbeResult& result)
    {
        if (result.selected_track_index < 0
            || size_t(result.selected_track_index) >= result.video_tracks.size()) {
            return NULL;
        }
        return &result.video_tracks[size_t(result.selected_track_index)];
    }
}
