// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_MEDIA_STREAM_FMP4_MUXER_H_
#define LOONG_NETWORK_MEDIA_STREAM_FMP4_MUXER_H_

#include "core/common/types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace loong::network {

/// Fragmented MP4 (fMP4) muxer for WebSocket-based video streaming.
///
/// Generates ISO BMFF init segments and media segments from raw
/// H.264 (Annex B) or H.265 (Annex B) encoded video frames.
///
/// The output can be fed directly into the browser MediaSource Extensions
/// (MSE) API via WebSocket binary frames:
///   1. Send init segment (ftyp + moov) once on connection.
///   2. Send media segments (moof + mdat) per encoded frame.
class Fmp4Muxer {
 public:
  /// Generate the fMP4 init segment for H.264.
  /// Contains ftyp + moov boxes with AVCDecoderConfigurationRecord.
  /// @param sps  H.264 SPS NAL unit data (without start code).
  /// @param pps  H.264 PPS NAL unit data (without start code).
  /// @param width  Video width.
  /// @param height Video height.
  /// @return Init segment bytes, or empty on error.
  static std::vector<uint8_t> MakeH264InitSegment(
      const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps,
      uint16_t width, uint16_t height);

  /// Generate the fMP4 init segment for H.265.
  /// Contains ftyp + moov boxes with HEVCDecoderConfigurationRecord.
  /// @param vps  H.265 VPS NAL unit data (without start code).
  /// @param sps  H.265 SPS NAL unit data (without start code).
  /// @param pps  H.265 PPS NAL unit data (without start code).
  /// @param width  Video width.
  /// @param height Video height.
  /// @return Init segment bytes, or empty on error.
  static std::vector<uint8_t> MakeH265InitSegment(
      const std::vector<uint8_t>& vps, const std::vector<uint8_t>& sps,
      const std::vector<uint8_t>& pps, uint16_t width, uint16_t height);

  /// Generate a media segment (moof + mdat) from a single encoded frame.
  /// @param data  Annex B encoded frame data.
  /// @param size  Frame data size.
  /// @param decode_time  Decode timestamp (in timescale units, 90kHz).
  /// @param duration  Frame duration (in timescale units, 90kHz).
  /// @param sequence_number  Fragment sequence number (monotonically
  /// increasing).
  /// @param is_keyframe  Whether this is a keyframe (IDR/CRA/BLA).
  /// @param codec  Codec type (H.264 or H.265).
  /// @return Media segment bytes, or empty on error.
  static std::vector<uint8_t> MakeMediaSegment(
      const uint8_t* data, size_t size, uint64_t decode_time, uint32_t duration,
      uint32_t sequence_number, bool is_keyframe, CodecType codec);

  /// Extract SPS and PPS from an H.264 Annex B bitstream.
  /// @return true if both SPS and PPS are found.
  static bool ExtractH264Params(const uint8_t* data, size_t size,
                                std::vector<uint8_t>& sps,
                                std::vector<uint8_t>& pps);

  /// Extract VPS, SPS, and PPS from an H.265 Annex B bitstream.
  /// @return true if all three are found.
  static bool ExtractH265Params(const uint8_t* data, size_t size,
                                std::vector<uint8_t>& vps,
                                std::vector<uint8_t>& sps,
                                std::vector<uint8_t>& pps);

  /// Timescale used for timestamps (90kHz, standard for MPEG transport).
  static constexpr uint32_t kTimescale = 90000;

 private:
  /// A parsed NAL unit.
  struct NalUnit {
    const uint8_t* data;
    size_t size;
    uint8_t type;
  };

  // NAL unit parsing
  static std::vector<NalUnit> ParseAnnexB(const uint8_t* data, size_t size);
  static uint8_t H264NalType(uint8_t header);
  static uint8_t H265NalType(uint8_t header);

  // Convert Annex B to length-prefixed (MP4/AVCC/HVCC) format.
  // Skips parameter set NALUs (SPS/PPS/VPS).
  static std::vector<uint8_t> AnnexBToMp4(const uint8_t* data, size_t size,
                                          CodecType codec);

  // MP4 box building helpers
  static std::vector<uint8_t> MakeBox(const char type[4],
                                      const std::vector<uint8_t>& payload);
  static std::vector<uint8_t> MakeFullBox(const char type[4], uint8_t version,
                                          uint32_t flags,
                                          const std::vector<uint8_t>& payload);
  static void AppendBox(std::vector<uint8_t>& out, const char type[4],
                        const std::vector<uint8_t>& payload);

  // Init segment box builders
  static std::vector<uint8_t> MakeFtyp();
  static std::vector<uint8_t> MakeMvhd();
  static std::vector<uint8_t> MakeTkhd(uint16_t width, uint16_t height);
  static std::vector<uint8_t> MakeMdhd();
  static std::vector<uint8_t> MakeHdlr();
  static std::vector<uint8_t> MakeVmhd();
  static std::vector<uint8_t> MakeDinf();
  static std::vector<uint8_t> MakeStbl(const std::vector<uint8_t>& stsd);
  static std::vector<uint8_t> MakeMinf(const std::vector<uint8_t>& stsd);
  static std::vector<uint8_t> MakeMdia(const std::vector<uint8_t>& stsd);
  static std::vector<uint8_t> MakeTrak(uint16_t width, uint16_t height,
                                       const std::vector<uint8_t>& stsd);
  static std::vector<uint8_t> MakeMvex();
  static std::vector<uint8_t> MakeMoov(uint16_t width, uint16_t height,
                                       const std::vector<uint8_t>& stsd);

  // Sample description builders
  static std::vector<uint8_t> MakeAvcC(const std::vector<uint8_t>& sps,
                                       const std::vector<uint8_t>& pps);
  static std::vector<uint8_t> MakeHvcC(const std::vector<uint8_t>& vps,
                                       const std::vector<uint8_t>& sps,
                                       const std::vector<uint8_t>& pps);
  static std::vector<uint8_t> MakeAvc1Stsd(const std::vector<uint8_t>& sps,
                                           const std::vector<uint8_t>& pps,
                                           uint16_t width, uint16_t height);
  static std::vector<uint8_t> MakeHev1Stsd(const std::vector<uint8_t>& vps,
                                           const std::vector<uint8_t>& sps,
                                           const std::vector<uint8_t>& pps,
                                           uint16_t width, uint16_t height);

  // Media segment box builders
  static std::vector<uint8_t> MakeMoof(uint32_t sequence_number,
                                       uint64_t decode_time, uint32_t duration,
                                       uint32_t sample_size, bool is_keyframe);

  // Byte writing helpers
  static void WriteU8(std::vector<uint8_t>& buf, uint8_t val);
  static void WriteU16Be(std::vector<uint8_t>& buf, uint16_t val);
  static void WriteU24Be(std::vector<uint8_t>& buf, uint32_t val);
  static void WriteU32Be(std::vector<uint8_t>& buf, uint32_t val);
  static void WriteU64Be(std::vector<uint8_t>& buf, uint64_t val);
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_MEDIA_STREAM_FMP4_MUXER_H_
