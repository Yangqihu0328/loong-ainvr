// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_HLS_STREAM_TS_MUXER_H_
#define LOONG_NETWORK_HLS_STREAM_TS_MUXER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace loong::network {

/// Lightweight MPEG-TS muxer for H.264 video-only HLS streaming.
///
/// Generates 188-byte TS packets containing:
///   - PAT (Program Association Table, PID 0x0000)
///   - PMT (Program Map Table, PID 0x1000)
///   - Video PES (Packetized Elementary Stream, PID 0x0100)
///
/// Each access unit (H.264 frame) is wrapped in PES packets then split
/// into 188-byte TS packets with proper continuity counters and PCR.
class TsMuxer {
 public:
  static constexpr int kTsPacketSize = 188;
  static constexpr uint16_t kPatPid = 0x0000;
  static constexpr uint16_t kPmtPid = 0x1000;
  static constexpr uint16_t kVideoPid = 0x0100;
  static constexpr uint16_t kPcrPid = kVideoPid;

  TsMuxer() = default;

  /// Write PAT + PMT packets (should be emitted at the start of each segment).
  std::vector<uint8_t> WritePsiTables();

  /// Write an H.264 access unit (Annex B format) as PES-in-TS packets.
  /// Returns a buffer containing one or more 188-byte TS packets.
  std::vector<uint8_t> WriteAccessUnit(const uint8_t* data, size_t size,
                                       int64_t pts_us, bool is_keyframe);

  /// Reset all continuity counters (call when starting a new segment).
  void Reset();

 private:
  /// Build a PAT section and wrap in a TS packet.
  void AppendPat(std::vector<uint8_t>& output);

  /// Build a PMT section and wrap in a TS packet.
  void AppendPmt(std::vector<uint8_t>& output);

  /// Build a PES header for an H.264 access unit.
  static std::vector<uint8_t> BuildPesHeader(int64_t pts_90khz);

  /// Write payload into one or more 188-byte TS packets.
  /// The first packet may include an adaptation field for PCR and
  /// random_access_indicator (keyframe).
  void PacketizePayload(std::vector<uint8_t>& output, uint16_t pid,
                        const uint8_t* payload, size_t payload_size,
                        uint8_t& cc, bool payload_start,
                        int64_t pcr_90khz, bool random_access);

  /// Compute CRC-32/MPEG-2 for PSI tables.
  static uint32_t Crc32Mpeg2(const uint8_t* data, size_t len);

  uint8_t pat_cc_ = 0;
  uint8_t pmt_cc_ = 0;
  uint8_t video_cc_ = 0;
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_HLS_STREAM_TS_MUXER_H_
