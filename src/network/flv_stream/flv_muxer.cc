// Copyright 2026 Loong AI NVR Project

#include "network/flv_stream/flv_muxer.h"

#include <algorithm>
#include <cstring>

namespace loong::network {

namespace {
constexpr uint8_t kFlvTagVideo = 9;
constexpr uint8_t kAvcCodecId = 7;
constexpr uint8_t kKeyframeType = 1;
constexpr uint8_t kInterframeType = 2;
constexpr uint8_t kAvcSequenceHeader = 0;
constexpr uint8_t kAvcNalu = 1;
constexpr uint8_t kAvcEndOfSequence = 2;

constexpr uint8_t kNalTypeSps = 7;
constexpr uint8_t kNalTypePps = 8;
}  // namespace

void FlvMuxer::WriteUint24Be(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

void FlvMuxer::WriteUint32Be(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

void FlvMuxer::WriteUint16Be(std::vector<uint8_t>& buf, uint16_t val) {
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

std::vector<uint8_t> FlvMuxer::MakeFlvHeader() {
  std::vector<uint8_t> header;
  header.reserve(13);

  // FLV signature
  header.push_back('F');
  header.push_back('L');
  header.push_back('V');

  // Version
  header.push_back(0x01);

  // Flags: video only (bit 0 = video, bit 2 = audio)
  header.push_back(0x01);

  // Data offset (4 bytes, always 9 for FLV version 1)
  WriteUint32Be(header, 9);

  // PreviousTagSize0 = 0
  WriteUint32Be(header, 0);

  return header;
}

std::vector<FlvMuxer::NalUnit> FlvMuxer::ParseAnnexB(const uint8_t* data,
                                                       size_t size) {
  std::vector<NalUnit> units;
  if (size < 4) return units;

  size_t i = 0;
  size_t nal_start = 0;
  bool found_start = false;

  while (i < size) {
    // Look for start code: 0x000001 or 0x00000001
    bool is_start_code = false;
    size_t start_code_len = 0;

    if (i + 2 < size && data[i] == 0 && data[i + 1] == 0 &&
        data[i + 2] == 1) {
      is_start_code = true;
      start_code_len = 3;
    } else if (i + 3 < size && data[i] == 0 && data[i + 1] == 0 &&
               data[i + 2] == 0 && data[i + 3] == 1) {
      is_start_code = true;
      start_code_len = 4;
    }

    if (is_start_code) {
      if (found_start) {
        // End the previous NAL unit
        size_t nal_size = i - nal_start;
        if (nal_size > 0) {
          NalUnit unit;
          unit.data = data + nal_start;
          unit.size = nal_size;
          unit.type = data[nal_start] & 0x1F;
          units.push_back(unit);
        }
      }
      i += start_code_len;
      nal_start = i;
      found_start = true;
    } else {
      ++i;
    }
  }

  // Last NAL unit
  if (found_start && nal_start < size) {
    size_t nal_size = size - nal_start;
    NalUnit unit;
    unit.data = data + nal_start;
    unit.size = nal_size;
    unit.type = data[nal_start] & 0x1F;
    units.push_back(unit);
  }

  return units;
}

bool FlvMuxer::ExtractSpsPps(const uint8_t* data, size_t size,
                              std::vector<uint8_t>& sps,
                              std::vector<uint8_t>& pps) {
  auto units = ParseAnnexB(data, size);

  bool found_sps = false;
  bool found_pps = false;

  for (const auto& unit : units) {
    if (unit.type == kNalTypeSps && !found_sps) {
      sps.assign(unit.data, unit.data + unit.size);
      found_sps = true;
    } else if (unit.type == kNalTypePps && !found_pps) {
      pps.assign(unit.data, unit.data + unit.size);
      found_pps = true;
    }
    if (found_sps && found_pps) break;
  }

  return found_sps && found_pps;
}

std::vector<uint8_t> FlvMuxer::AnnexBToAvcc(const uint8_t* data,
                                              size_t size) {
  auto units = ParseAnnexB(data, size);
  std::vector<uint8_t> avcc;
  avcc.reserve(size);

  for (const auto& unit : units) {
    // Skip SPS and PPS — they go in the sequence header
    if (unit.type == kNalTypeSps || unit.type == kNalTypePps) {
      continue;
    }
    // Write 4-byte length prefix (big-endian) + NAL data
    auto nal_size = static_cast<uint32_t>(unit.size);
    WriteUint32Be(avcc, nal_size);
    avcc.insert(avcc.end(), unit.data, unit.data + unit.size);
  }

  return avcc;
}

std::vector<uint8_t> FlvMuxer::BuildFlvTag(uint8_t tag_type,
                                            const std::vector<uint8_t>& data,
                                            int64_t timestamp_ms) {
  auto data_size = static_cast<uint32_t>(data.size());
  auto ts = static_cast<uint32_t>(timestamp_ms & 0xFFFFFFFF);

  std::vector<uint8_t> tag;
  tag.reserve(11 + data.size() + 4);

  // Tag type
  tag.push_back(tag_type);

  // Data size (3 bytes)
  WriteUint24Be(tag, data_size);

  // Timestamp (3 bytes lower) + timestamp extended (1 byte upper)
  WriteUint24Be(tag, ts & 0x00FFFFFF);
  tag.push_back(static_cast<uint8_t>((ts >> 24) & 0xFF));

  // Stream ID (always 0)
  WriteUint24Be(tag, 0);

  // Tag data
  tag.insert(tag.end(), data.begin(), data.end());

  // PreviousTagSize (4 bytes) = 11 + data_size
  WriteUint32Be(tag, 11 + data_size);

  return tag;
}

std::vector<uint8_t> FlvMuxer::MakeSequenceHeaderTag(
    const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps) {
  if (sps.size() < 4 || pps.empty()) {
    return {};
  }

  // Build AVC video tag body
  std::vector<uint8_t> body;

  // Video tag header: keyframe(1) | AVC(7)
  body.push_back((kKeyframeType << 4) | kAvcCodecId);

  // AVC packet type: sequence header
  body.push_back(kAvcSequenceHeader);

  // Composition time offset: 0
  WriteUint24Be(body, 0);

  // AVCDecoderConfigurationRecord
  body.push_back(0x01);     // configurationVersion
  body.push_back(sps[1]);   // AVCProfileIndication
  body.push_back(sps[2]);   // profile_compatibility
  body.push_back(sps[3]);   // AVCLevelIndication
  body.push_back(0xFF);     // lengthSizeMinusOne = 3 (4 bytes)

  // Number of SPS: 1
  body.push_back(0xE1);

  // SPS length + data
  WriteUint16Be(body, static_cast<uint16_t>(sps.size()));
  body.insert(body.end(), sps.begin(), sps.end());

  // Number of PPS: 1
  body.push_back(0x01);

  // PPS length + data
  WriteUint16Be(body, static_cast<uint16_t>(pps.size()));
  body.insert(body.end(), pps.begin(), pps.end());

  return BuildFlvTag(kFlvTagVideo, body, 0);
}

std::vector<uint8_t> FlvMuxer::MakeVideoTag(const uint8_t* data, size_t size,
                                              int64_t timestamp_ms,
                                              bool is_keyframe) {
  // Convert Annex B to AVCC format (skip SPS/PPS)
  auto avcc_data = AnnexBToAvcc(data, size);
  if (avcc_data.empty()) {
    return {};
  }

  std::vector<uint8_t> body;
  body.reserve(5 + avcc_data.size());

  // Video tag header
  uint8_t frame_type = is_keyframe ? kKeyframeType : kInterframeType;
  body.push_back(static_cast<uint8_t>((frame_type << 4) | kAvcCodecId));

  // AVC packet type: NALU
  body.push_back(kAvcNalu);

  // Composition time offset: 0 (no B-frames)
  WriteUint24Be(body, 0);

  // AVCC data
  body.insert(body.end(), avcc_data.begin(), avcc_data.end());

  return BuildFlvTag(kFlvTagVideo, body, timestamp_ms);
}

std::vector<uint8_t> FlvMuxer::MakeEndOfSequenceTag(int64_t timestamp_ms) {
  std::vector<uint8_t> body;
  body.push_back((kKeyframeType << 4) | kAvcCodecId);
  body.push_back(kAvcEndOfSequence);
  WriteUint24Be(body, 0);

  return BuildFlvTag(kFlvTagVideo, body, timestamp_ms);
}

}  // namespace loong::network
