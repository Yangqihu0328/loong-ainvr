// Copyright 2026 Loong AI NVR Project

#include "network/hls_stream/ts_muxer.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace loong::network {

namespace {

constexpr uint8_t kSyncByte = 0x47;
constexpr uint8_t kStuffingByte = 0xFF;
constexpr uint8_t kStreamIdVideo = 0xE0;

constexpr uint16_t kProgramNumber = 1;
constexpr uint16_t kTransportStreamId = 1;

// H.264 stream type in PMT
constexpr uint8_t kStreamTypeH264 = 0x1B;

// Maximum payload in a TS packet (188 - 4 byte header)
constexpr int kMaxPayload = 184;

void WriteUint16Be(uint8_t* buf, uint16_t val) {
  buf[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
  buf[1] = static_cast<uint8_t>(val & 0xFF);
}

}  // namespace

void TsMuxer::Reset() {
  pat_cc_ = 0;
  pmt_cc_ = 0;
  video_cc_ = 0;
}

std::vector<uint8_t> TsMuxer::WritePsiTables() {
  std::vector<uint8_t> output;
  output.reserve(kTsPacketSize * 2);
  AppendPat(output);
  AppendPmt(output);
  return output;
}

// ========== PAT ==========

void TsMuxer::AppendPat(std::vector<uint8_t>& output) {
  // PAT section payload (before CRC):
  //   table_id(1) + section_syntax(2) + ts_id(2) + version(1) +
  //   section_num(1) + last_section(1) + program_num(2) + pmt_pid(2) = 12
  // + CRC(4) = 16 bytes section
  // + pointer_field(1) = 17 bytes payload

  std::array<uint8_t, 17> section{};
  section[0] = 0x00;  // pointer_field

  section[1] = 0x00;  // table_id = PAT
  // section_syntax_indicator=1, 0, reserved=11, section_length=13 (0x00D)
  section[2] = 0xB0;
  section[3] = 0x0D;  // section_length = 13 bytes after this field

  WriteUint16Be(&section[4], kTransportStreamId);

  // reserved=11, version=0, current_next=1
  section[6] = 0xC1;

  section[7] = 0x00;  // section_number
  section[8] = 0x00;  // last_section_number

  WriteUint16Be(&section[9], kProgramNumber);

  // reserved=111 + PMT PID
  section[11] = static_cast<uint8_t>(0xE0 | ((kPmtPid >> 8) & 0x1F));
  section[12] = static_cast<uint8_t>(kPmtPid & 0xFF);

  // CRC-32 over bytes [1..12]
  uint32_t crc = Crc32Mpeg2(&section[1], 12);
  section[13] = static_cast<uint8_t>((crc >> 24) & 0xFF);
  section[14] = static_cast<uint8_t>((crc >> 16) & 0xFF);
  section[15] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  section[16] = static_cast<uint8_t>(crc & 0xFF);

  // Build TS packet
  size_t pos = output.size();
  output.resize(pos + kTsPacketSize);
  auto* pkt = &output[pos];

  pkt[0] = kSyncByte;
  // payload_unit_start=1, PID=0x0000
  pkt[1] = 0x40;
  pkt[2] = 0x00;
  // adaptation_field_control=01 (payload only), continuity counter
  pkt[3] = static_cast<uint8_t>(0x10 | (pat_cc_ & 0x0F));
  pat_cc_ = (pat_cc_ + 1) & 0x0F;

  std::memcpy(&pkt[4], section.data(), section.size());
  // Stuff remaining bytes
  std::memset(&pkt[4 + section.size()], kStuffingByte,
              static_cast<size_t>(kTsPacketSize) - 4 - section.size());
}

// ========== PMT ==========

void TsMuxer::AppendPmt(std::vector<uint8_t>& output) {
  // PMT section (single video stream, no descriptors):
  //   table_id(1) + section_syntax(2) + program_num(2) + version(1) +
  //   section_num(1) + last_section(1) + reserved+PCR_PID(2) +
  //   reserved+program_info_len(2) + stream_type(1) + reserved+es_pid(2) +
  //   reserved+es_info_len(2) = 17
  // + CRC(4) = 21 bytes section
  // + pointer_field(1) = 22 bytes payload

  std::array<uint8_t, 22> section{};
  section[0] = 0x00;  // pointer_field

  section[1] = 0x02;  // table_id = PMT
  // section_syntax_indicator=1, 0, reserved=11, section_length=18 (0x012)
  section[2] = 0xB0;
  section[3] = 0x12;  // section_length

  WriteUint16Be(&section[4], kProgramNumber);

  section[6] = 0xC1;  // reserved + version=0 + current_next=1
  section[7] = 0x00;  // section_number
  section[8] = 0x00;  // last_section_number

  // reserved=111 + PCR_PID
  section[9] = static_cast<uint8_t>(0xE0 | ((kPcrPid >> 8) & 0x1F));
  section[10] = static_cast<uint8_t>(kPcrPid & 0xFF);

  // reserved=1111 + program_info_length=0
  section[11] = 0xF0;
  section[12] = 0x00;

  // Stream entry: H.264 video
  section[13] = kStreamTypeH264;
  section[14] = static_cast<uint8_t>(0xE0 | ((kVideoPid >> 8) & 0x1F));
  section[15] = static_cast<uint8_t>(kVideoPid & 0xFF);
  // ES_info_length = 0
  section[16] = 0xF0;
  section[17] = 0x00;

  // CRC-32 over bytes [1..17]
  uint32_t crc = Crc32Mpeg2(&section[1], 17);
  section[18] = static_cast<uint8_t>((crc >> 24) & 0xFF);
  section[19] = static_cast<uint8_t>((crc >> 16) & 0xFF);
  section[20] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  section[21] = static_cast<uint8_t>(crc & 0xFF);

  // Build TS packet
  size_t pos = output.size();
  output.resize(pos + kTsPacketSize);
  auto* pkt = &output[pos];

  pkt[0] = kSyncByte;
  pkt[1] = static_cast<uint8_t>(0x40 | ((kPmtPid >> 8) & 0x1F));
  pkt[2] = static_cast<uint8_t>(kPmtPid & 0xFF);
  pkt[3] = static_cast<uint8_t>(0x10 | (pmt_cc_ & 0x0F));
  pmt_cc_ = (pmt_cc_ + 1) & 0x0F;

  std::memcpy(&pkt[4], section.data(), section.size());
  std::memset(&pkt[4 + section.size()], kStuffingByte,
              static_cast<size_t>(kTsPacketSize) - 4 - section.size());
}

// ========== PES Header ==========

std::vector<uint8_t> TsMuxer::BuildPesHeader(int64_t pts_90khz) {
  // PES header with PTS only (no DTS for simple streaming):
  //   start_code(3) + stream_id(1) + pes_packet_length(2) +
  //   flags(2) + pes_header_data_length(1) + PTS(5) = 14 bytes

  std::vector<uint8_t> header(14);

  // PES start code: 0x000001
  header[0] = 0x00;
  header[1] = 0x00;
  header[2] = 0x01;
  header[3] = kStreamIdVideo;

  // PES packet length = 0 (unbounded for video)
  header[4] = 0x00;
  header[5] = 0x00;

  // '10' + PES_scrambling=00 + PES_priority=0 + data_alignment=1 +
  // copyright=0 + original=0
  header[6] = 0x84;

  // PTS_DTS_flags=10 (PTS only) + other flags=0
  header[7] = 0x80;

  // PES_header_data_length = 5 (PTS field)
  header[8] = 0x05;

  // PTS encoding (5 bytes):
  // '0010' + PTS[32..30] + marker_bit
  auto pts = static_cast<uint64_t>(pts_90khz);
  header[9] = static_cast<uint8_t>(
      0x21 | ((pts >> 29) & 0x0E));
  header[10] = static_cast<uint8_t>((pts >> 22) & 0xFF);
  header[11] = static_cast<uint8_t>(
      0x01 | ((pts >> 14) & 0xFE));
  header[12] = static_cast<uint8_t>((pts >> 7) & 0xFF);
  header[13] = static_cast<uint8_t>(
      0x01 | ((pts << 1) & 0xFE));

  return header;
}

// ========== TS Packetization ==========

void TsMuxer::PacketizePayload(std::vector<uint8_t>& output, uint16_t pid,
                               const uint8_t* payload, size_t payload_size,
                               uint8_t& cc, bool payload_start,
                               int64_t pcr_90khz, bool random_access) {
  size_t offset = 0;
  bool first_packet = true;

  while (offset < payload_size) {
    size_t pos = output.size();
    output.resize(pos + kTsPacketSize);
    auto* pkt = &output[pos];

    pkt[0] = kSyncByte;

    bool pusi = first_packet && payload_start;
    pkt[1] = static_cast<uint8_t>(
        (pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
    pkt[2] = static_cast<uint8_t>(pid & 0xFF);

    // Determine if we need an adaptation field
    bool need_adaptation = (first_packet && (pcr_90khz >= 0 || random_access));
    size_t remaining = payload_size - offset;

    if (need_adaptation) {
      // Adaptation field with PCR and/or random_access_indicator
      // adaptation_field_length(1) + flags(1) + PCR(6) = 8 bytes
      int adapt_len = 7;  // 1 flags + 6 PCR
      if (pcr_90khz < 0) {
        adapt_len = 1;  // flags only
      }

      int available = kMaxPayload - 1 - adapt_len;  // -1 for adapt_len byte
      if (available < 0) available = 0;
      auto avail = static_cast<size_t>(available);

      size_t copy_len = std::min(remaining, avail);
      int padding = static_cast<int>(avail) - static_cast<int>(copy_len);

      pkt[3] = static_cast<uint8_t>(0x30 | (cc & 0x0F));  // adapt + payload
      cc = (cc + 1) & 0x0F;

      int total_adapt = adapt_len + padding;
      pkt[4] = static_cast<uint8_t>(total_adapt);

      uint8_t flags = 0;
      if (pcr_90khz >= 0) flags |= 0x10;  // PCR flag
      if (random_access) flags |= 0x40;    // random_access_indicator
      pkt[5] = flags;

      int adapt_offset = 6;
      if (pcr_90khz >= 0) {
        auto pcr_base = static_cast<uint64_t>(pcr_90khz);
        uint16_t pcr_ext = 0;
        pkt[adapt_offset] = static_cast<uint8_t>((pcr_base >> 25) & 0xFF);
        pkt[adapt_offset + 1] = static_cast<uint8_t>((pcr_base >> 17) & 0xFF);
        pkt[adapt_offset + 2] = static_cast<uint8_t>((pcr_base >> 9) & 0xFF);
        pkt[adapt_offset + 3] = static_cast<uint8_t>((pcr_base >> 1) & 0xFF);
        pkt[adapt_offset + 4] = static_cast<uint8_t>(
            ((pcr_base & 1) << 7) | 0x7E | ((pcr_ext >> 8) & 0x01));
        pkt[adapt_offset + 5] = static_cast<uint8_t>(pcr_ext & 0xFF);
        adapt_offset += 6;
      }

      // Padding in adaptation field
      if (padding > 0) {
        std::memset(&pkt[adapt_offset], kStuffingByte,
                    static_cast<size_t>(padding));
      }

      int payload_offset = 4 + 1 + total_adapt;
      if (copy_len > 0) {
        std::memcpy(&pkt[payload_offset], payload + offset, copy_len);
      }
      offset += copy_len;

    } else {
      // No adaptation field (or stuffing-only adaptation for short payloads)
      size_t copy_len = std::min(remaining, static_cast<size_t>(kMaxPayload));

      if (copy_len < static_cast<size_t>(kMaxPayload)) {
        // Need stuffing adaptation field to fill the packet
        int stuff = kMaxPayload - static_cast<int>(copy_len);

        pkt[3] = static_cast<uint8_t>(0x30 | (cc & 0x0F));  // adapt + payload
        cc = (cc + 1) & 0x0F;

        if (stuff == 1) {
          pkt[4] = 0x00;  // adaptation_field_length = 0
          std::memcpy(&pkt[5], payload + offset, copy_len);
        } else {
          pkt[4] = static_cast<uint8_t>(stuff - 1);
          pkt[5] = 0x00;  // flags = 0
          if (stuff > 2) {
            std::memset(&pkt[6], kStuffingByte,
                        static_cast<size_t>(stuff - 2));
          }
          std::memcpy(&pkt[4 + stuff], payload + offset, copy_len);
        }
      } else {
        pkt[3] = static_cast<uint8_t>(0x10 | (cc & 0x0F));  // payload only
        cc = (cc + 1) & 0x0F;
        std::memcpy(&pkt[4], payload + offset, copy_len);
      }
      offset += copy_len;
    }

    first_packet = false;
  }
}

std::vector<uint8_t> TsMuxer::WriteAccessUnit(const uint8_t* data,
                                               size_t size,
                                               int64_t pts_us,
                                               bool is_keyframe) {
  if (data == nullptr || size == 0) return {};

  // Convert microseconds to 90kHz clock
  int64_t pts_90khz = pts_us * 90 / 1000;

  // Build PES header + data
  auto pes_header = BuildPesHeader(pts_90khz);

  std::vector<uint8_t> pes_packet;
  pes_packet.reserve(pes_header.size() + size);
  pes_packet.insert(pes_packet.end(), pes_header.begin(), pes_header.end());
  pes_packet.insert(pes_packet.end(), data, data + size);

  std::vector<uint8_t> output;
  // Rough estimate: each TS packet holds ~184 bytes of payload
  output.reserve((pes_packet.size() / 180 + 2) * kTsPacketSize);

  int64_t pcr = is_keyframe ? pts_90khz : -1;
  PacketizePayload(output, kVideoPid, pes_packet.data(), pes_packet.size(),
                   video_cc_, true, pcr, is_keyframe);

  return output;
}

// ========== CRC-32/MPEG-2 ==========

uint32_t TsMuxer::Crc32Mpeg2(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]) << 24;
    for (int j = 0; j < 8; ++j) {
      if ((crc & 0x80000000) != 0) {
        crc = (crc << 1) ^ 0x04C11DB7;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

}  // namespace loong::network
