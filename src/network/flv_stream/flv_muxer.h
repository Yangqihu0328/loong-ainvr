// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_FLV_STREAM_FLV_MUXER_H_
#define LOONG_NETWORK_FLV_STREAM_FLV_MUXER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace loong::network {

/// FLV format muxer for H.264 video streaming.
/// Generates FLV headers, sequence header tags, and video data tags
/// from raw H.264 NAL unit streams (Annex B format).
class FlvMuxer {
 public:
  /// Generate the 13-byte FLV file header (9-byte header + 4-byte prev tag 0).
  static std::vector<uint8_t> MakeFlvHeader();

  /// Extract SPS and PPS NAL units from an Annex B bitstream.
  /// Returns true if both SPS and PPS are found.
  static bool ExtractSpsPps(const uint8_t* data, size_t size,
                            std::vector<uint8_t>& sps,
                            std::vector<uint8_t>& pps);

  /// Build an FLV video tag containing the AVC sequence header
  /// (AVCDecoderConfigurationRecord) from SPS and PPS data.
  static std::vector<uint8_t> MakeSequenceHeaderTag(
      const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps);

  /// Build an FLV video tag from H.264 Annex B data.
  /// The data is converted to AVCC format (4-byte length prefix).
  static std::vector<uint8_t> MakeVideoTag(const uint8_t* data, size_t size,
                                           int64_t timestamp_ms,
                                           bool is_keyframe);

  /// Build an AVC end-of-sequence FLV video tag.
  static std::vector<uint8_t> MakeEndOfSequenceTag(int64_t timestamp_ms);

 private:
  /// Write a 24-bit big-endian integer to a buffer.
  static void WriteUint24Be(std::vector<uint8_t>& buf, uint32_t val);

  /// Write a 32-bit big-endian integer to a buffer.
  static void WriteUint32Be(std::vector<uint8_t>& buf, uint32_t val);

  /// Write a 16-bit big-endian integer to a buffer.
  static void WriteUint16Be(std::vector<uint8_t>& buf, uint16_t val);

  /// Convert H.264 Annex B format (start code delimited) to AVCC format
  /// (4-byte length prefix). Skips SPS/PPS NAL units.
  static std::vector<uint8_t> AnnexBToAvcc(const uint8_t* data, size_t size);

  /// Find all NAL unit boundaries in an Annex B bitstream.
  struct NalUnit {
    const uint8_t* data;
    size_t size;
    uint8_t type;
  };
  static std::vector<NalUnit> ParseAnnexB(const uint8_t* data, size_t size);

  /// Build a complete FLV tag with header and data.
  static std::vector<uint8_t> BuildFlvTag(uint8_t tag_type,
                                          const std::vector<uint8_t>& data,
                                          int64_t timestamp_ms);
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_FLV_STREAM_FLV_MUXER_H_
