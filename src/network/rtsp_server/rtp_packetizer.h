// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_RTSP_SERVER_RTP_PACKETIZER_H_
#define LOONG_NETWORK_RTSP_SERVER_RTP_PACKETIZER_H_

#include "core/common/types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace loong::network {

/// Callback for each generated RTP packet.
using RtpPacketCallback = std::function<void(const uint8_t* data, size_t size)>;

/// Packetizes H.264/H.265 Annex B NAL units into RTP packets (RFC 6184/7798).
///
/// Supports:
///   - Single NAL unit packets (NAL size <= MTU)
///   - FU-A fragmentation (NAL size > MTU)
///   - RTP over TCP interleaved framing (4-byte header)
class RtpPacketizer {
 public:
  explicit RtpPacketizer(CodecType codec);

  /// Set the SSRC for RTP headers.
  void SetSsrc(uint32_t ssrc) { ssrc_ = ssrc; }

  /// Set the RTP interleaved channel for TCP transport.
  void SetInterleavedChannel(uint8_t channel) { channel_ = channel; }

  /// Packetize an Annex B access unit and emit RTP packets via callback.
  /// Each callback invocation contains a complete RTP-over-TCP interleaved
  /// frame (4-byte header + RTP packet).
  void Packetize(const uint8_t* data, size_t size, int64_t pts,
                 RtpPacketCallback callback);

 private:
  static constexpr int kMaxRtpPayload = 1400;
  static constexpr uint8_t kRtpVersion = 2;

  void PacketizeH264Nal(const uint8_t* nal, size_t nal_size, uint32_t timestamp,
                        bool marker, RtpPacketCallback& callback);

  void EmitPacket(const uint8_t* payload, size_t payload_size,
                  uint32_t timestamp, bool marker, RtpPacketCallback& callback);

  /// Parse Annex B start codes and extract NAL units.
  static std::vector<std::pair<const uint8_t*, size_t>> FindNalUnits(
      const uint8_t* data, size_t size);

  CodecType codec_;
  uint32_t ssrc_ = 0x12345678;
  uint16_t seq_ = 0;
  uint8_t channel_ = 0;  // RTP interleaved channel (0 = RTP, 1 = RTCP)
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_RTSP_SERVER_RTP_PACKETIZER_H_
