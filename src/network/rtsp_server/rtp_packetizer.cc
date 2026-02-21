// Copyright 2026 Loong AI NVR Project

#include "network/rtsp_server/rtp_packetizer.h"

#include <cstring>

namespace loong::network {

RtpPacketizer::RtpPacketizer(CodecType codec) : codec_(codec) {}

void RtpPacketizer::Packetize(const uint8_t* data, size_t size, int64_t pts,
                              RtpPacketCallback callback) {
  auto nals = FindNalUnits(data, size);
  if (nals.empty()) return;

  // Convert microsecond PTS to 90 kHz RTP timestamp.
  auto timestamp = static_cast<uint32_t>((pts * 90000) / 1000000);

  for (size_t i = 0; i < nals.size(); ++i) {
    bool last_nal = (i == nals.size() - 1);
    PacketizeH264Nal(nals[i].first, nals[i].second, timestamp,
                     last_nal, callback);
  }
}

void RtpPacketizer::PacketizeH264Nal(const uint8_t* nal, size_t nal_size,
                                     uint32_t timestamp, bool marker,
                                     RtpPacketCallback& callback) {
  if (nal_size == 0) return;

  if (nal_size <= kMaxRtpPayload) {
    // Single NAL unit packet — fits in one RTP packet.
    EmitPacket(nal, nal_size, timestamp, marker, callback);
  } else {
    // FU-A fragmentation.
    uint8_t nal_header = nal[0];
    uint8_t nal_type = nal_header & 0x1F;
    uint8_t nri = nal_header & 0x60;

    const uint8_t* frag_ptr = nal + 1;
    size_t remaining = nal_size - 1;
    bool first = true;

    while (remaining > 0) {
      size_t chunk = std::min(remaining,
                              static_cast<size_t>(kMaxRtpPayload - 2));
      bool last = (chunk == remaining);

      uint8_t fu_indicator = nri | 28;  // FU-A type = 28
      uint8_t fu_header = nal_type;
      if (first) fu_header |= 0x80;   // Start bit
      if (last) fu_header |= 0x40;    // End bit

      std::vector<uint8_t> payload;
      payload.reserve(2 + chunk);
      payload.push_back(fu_indicator);
      payload.push_back(fu_header);
      payload.insert(payload.end(), frag_ptr, frag_ptr + chunk);

      EmitPacket(payload.data(), payload.size(), timestamp,
                 last && marker, callback);

      frag_ptr += chunk;
      remaining -= chunk;
      first = false;
    }
  }
}

void RtpPacketizer::EmitPacket(const uint8_t* payload, size_t payload_size,
                               uint32_t timestamp, bool marker,
                               RtpPacketCallback& callback) {
  // Build RTP header (12 bytes).
  uint8_t rtp_header[12];
  rtp_header[0] = static_cast<uint8_t>((kRtpVersion << 6));  // V=2
  rtp_header[1] = 96;  // PT=96 (dynamic)
  if (marker) rtp_header[1] |= 0x80;
  rtp_header[2] = static_cast<uint8_t>((seq_ >> 8) & 0xFF);
  rtp_header[3] = static_cast<uint8_t>(seq_ & 0xFF);
  rtp_header[4] = static_cast<uint8_t>((timestamp >> 24) & 0xFF);
  rtp_header[5] = static_cast<uint8_t>((timestamp >> 16) & 0xFF);
  rtp_header[6] = static_cast<uint8_t>((timestamp >> 8) & 0xFF);
  rtp_header[7] = static_cast<uint8_t>(timestamp & 0xFF);
  rtp_header[8] = static_cast<uint8_t>((ssrc_ >> 24) & 0xFF);
  rtp_header[9] = static_cast<uint8_t>((ssrc_ >> 16) & 0xFF);
  rtp_header[10] = static_cast<uint8_t>((ssrc_ >> 8) & 0xFF);
  rtp_header[11] = static_cast<uint8_t>(ssrc_ & 0xFF);
  ++seq_;

  size_t rtp_size = 12 + payload_size;

  // Build RTP-over-TCP interleaved frame: '$' + channel + 2-byte length.
  std::vector<uint8_t> frame;
  frame.reserve(4 + rtp_size);
  frame.push_back('$');
  frame.push_back(channel_);
  frame.push_back(static_cast<uint8_t>((rtp_size >> 8) & 0xFF));
  frame.push_back(static_cast<uint8_t>(rtp_size & 0xFF));
  frame.insert(frame.end(), rtp_header, rtp_header + 12);
  frame.insert(frame.end(), payload, payload + payload_size);

  callback(frame.data(), frame.size());
}

std::vector<std::pair<const uint8_t*, size_t>> RtpPacketizer::FindNalUnits(
    const uint8_t* data, size_t size) {
  std::vector<std::pair<const uint8_t*, size_t>> nals;
  size_t i = 0;

  // Find first start code.
  while (i + 3 < size) {
    if (data[i] == 0 && data[i + 1] == 0) {
      if (data[i + 2] == 1) {
        i += 3;
        break;
      }
      if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
        i += 4;
        break;
      }
    }
    ++i;
  }
  if (i >= size) return nals;

  size_t nal_start = i;

  while (i + 3 <= size) {
    if (data[i] == 0 && data[i + 1] == 0) {
      bool found = false;
      size_t sc_len = 0;
      if (data[i + 2] == 1) {
        found = true;
        sc_len = 3;
      } else if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
        found = true;
        sc_len = 4;
      }
      if (found) {
        nals.emplace_back(data + nal_start, i - nal_start);
        nal_start = i + sc_len;
        i = nal_start;
        continue;
      }
    }
    ++i;
  }

  // Last NAL unit.
  if (nal_start < size) {
    nals.emplace_back(data + nal_start, size - nal_start);
  }

  return nals;
}

}  // namespace loong::network
