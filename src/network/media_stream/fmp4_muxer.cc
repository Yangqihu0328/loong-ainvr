// Copyright 2026 Loong AI NVR Project

#include "network/media_stream/fmp4_muxer.h"

#include <algorithm>
#include <cstring>

namespace loong::network {

// ============================================================
// Byte Writing Helpers
// ============================================================

void Fmp4Muxer::WriteU8(std::vector<uint8_t>& buf, uint8_t val) {
  buf.push_back(val);
}

void Fmp4Muxer::WriteU16Be(std::vector<uint8_t>& buf, uint16_t val) {
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

void Fmp4Muxer::WriteU24Be(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

void Fmp4Muxer::WriteU32Be(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

void Fmp4Muxer::WriteU64Be(std::vector<uint8_t>& buf, uint64_t val) {
  WriteU32Be(buf, static_cast<uint32_t>((val >> 32) & 0xFFFFFFFF));
  WriteU32Be(buf, static_cast<uint32_t>(val & 0xFFFFFFFF));
}

// ============================================================
// MP4 Box Helpers
// ============================================================

std::vector<uint8_t> Fmp4Muxer::MakeBox(const char type[4],
                                         const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> box;
  auto total_size = static_cast<uint32_t>(8 + payload.size());
  box.reserve(total_size);
  WriteU32Be(box, total_size);
  box.insert(box.end(), type, type + 4);
  box.insert(box.end(), payload.begin(), payload.end());
  return box;
}

std::vector<uint8_t> Fmp4Muxer::MakeFullBox(
    const char type[4], uint8_t version, uint32_t flags,
    const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> inner;
  inner.reserve(4 + payload.size());
  WriteU8(inner, version);
  WriteU24Be(inner, flags);
  inner.insert(inner.end(), payload.begin(), payload.end());
  return MakeBox(type, inner);
}

void Fmp4Muxer::AppendBox(std::vector<uint8_t>& out, const char type[4],
                           const std::vector<uint8_t>& payload) {
  auto box = MakeBox(type, payload);
  out.insert(out.end(), box.begin(), box.end());
}

// ============================================================
// NAL Unit Parsing
// ============================================================

uint8_t Fmp4Muxer::H264NalType(uint8_t header) {
  return header & 0x1F;
}

uint8_t Fmp4Muxer::H265NalType(uint8_t header) {
  return (header >> 1) & 0x3F;
}

std::vector<Fmp4Muxer::NalUnit> Fmp4Muxer::ParseAnnexB(const uint8_t* data,
                                                         size_t size) {
  std::vector<NalUnit> units;
  if (size < 4) return units;

  size_t i = 0;
  size_t nal_start = 0;
  bool found_start = false;

  while (i < size) {
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
      if (found_start && nal_start < i) {
        NalUnit unit;
        unit.data = data + nal_start;
        unit.size = i - nal_start;
        unit.type = data[nal_start] & 0xFF;
        units.push_back(unit);
      }
      i += start_code_len;
      nal_start = i;
      found_start = true;
    } else {
      ++i;
    }
  }

  if (found_start && nal_start < size) {
    NalUnit unit;
    unit.data = data + nal_start;
    unit.size = size - nal_start;
    unit.type = data[nal_start] & 0xFF;
    units.push_back(unit);
  }

  return units;
}

bool Fmp4Muxer::ExtractH264Params(const uint8_t* data, size_t size,
                                   std::vector<uint8_t>& sps,
                                   std::vector<uint8_t>& pps) {
  auto units = ParseAnnexB(data, size);
  bool found_sps = false;
  bool found_pps = false;

  for (const auto& unit : units) {
    uint8_t nal_type = H264NalType(unit.type);
    if (nal_type == 7 && !found_sps) {  // SPS
      sps.assign(unit.data, unit.data + unit.size);
      found_sps = true;
    } else if (nal_type == 8 && !found_pps) {  // PPS
      pps.assign(unit.data, unit.data + unit.size);
      found_pps = true;
    }
    if (found_sps && found_pps) break;
  }

  return found_sps && found_pps;
}

bool Fmp4Muxer::ExtractH265Params(const uint8_t* data, size_t size,
                                   std::vector<uint8_t>& vps,
                                   std::vector<uint8_t>& sps,
                                   std::vector<uint8_t>& pps) {
  auto units = ParseAnnexB(data, size);
  bool found_vps = false;
  bool found_sps = false;
  bool found_pps = false;

  for (const auto& unit : units) {
    uint8_t nal_type = H265NalType(unit.type);
    if (nal_type == 32 && !found_vps) {  // VPS
      vps.assign(unit.data, unit.data + unit.size);
      found_vps = true;
    } else if (nal_type == 33 && !found_sps) {  // SPS
      sps.assign(unit.data, unit.data + unit.size);
      found_sps = true;
    } else if (nal_type == 34 && !found_pps) {  // PPS
      pps.assign(unit.data, unit.data + unit.size);
      found_pps = true;
    }
    if (found_vps && found_sps && found_pps) break;
  }

  return found_vps && found_sps && found_pps;
}

std::vector<uint8_t> Fmp4Muxer::AnnexBToMp4(const uint8_t* data, size_t size,
                                              CodecType codec) {
  auto units = ParseAnnexB(data, size);
  std::vector<uint8_t> mp4_data;
  mp4_data.reserve(size);

  for (const auto& unit : units) {
    if (codec == CodecType::kH264) {
      uint8_t nal_type = H264NalType(unit.type);
      if (nal_type == 7 || nal_type == 8) continue;  // Skip SPS/PPS
    } else if (codec == CodecType::kH265) {
      uint8_t nal_type = H265NalType(unit.type);
      if (nal_type == 32 || nal_type == 33 || nal_type == 34) continue;
    }
    auto nal_size = static_cast<uint32_t>(unit.size);
    WriteU32Be(mp4_data, nal_size);
    mp4_data.insert(mp4_data.end(), unit.data, unit.data + unit.size);
  }

  return mp4_data;
}

// ============================================================
// ftyp Box
// ============================================================

std::vector<uint8_t> Fmp4Muxer::MakeFtyp() {
  std::vector<uint8_t> payload;
  // Major brand: isom
  payload.insert(payload.end(), {'i', 's', 'o', 'm'});
  // Minor version: 0x200
  WriteU32Be(payload, 0x200);
  // Compatible brands
  payload.insert(payload.end(), {'i', 's', 'o', 'm'});
  payload.insert(payload.end(), {'i', 's', 'o', '5'});
  payload.insert(payload.end(), {'i', 's', 'o', '6'});
  payload.insert(payload.end(), {'m', 'p', '4', '1'});
  return MakeBox("ftyp", payload);
}

// ============================================================
// moov Sub-boxes
// ============================================================

std::vector<uint8_t> Fmp4Muxer::MakeMvhd() {
  std::vector<uint8_t> payload;
  payload.reserve(96);
  WriteU32Be(payload, 0);          // creation_time
  WriteU32Be(payload, 0);          // modification_time
  WriteU32Be(payload, kTimescale); // timescale
  WriteU32Be(payload, 0);          // duration (unknown for live)

  WriteU32Be(payload, 0x00010000); // rate = 1.0 (fixed-point 16.16)
  WriteU16Be(payload, 0x0100);     // volume = 1.0 (fixed-point 8.8)
  // reserved (10 bytes)
  for (int i = 0; i < 10; ++i) WriteU8(payload, 0);

  // Identity matrix (9 × int32, 36 bytes)
  // [0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000]
  WriteU32Be(payload, 0x00010000); WriteU32Be(payload, 0);
  WriteU32Be(payload, 0);          WriteU32Be(payload, 0);
  WriteU32Be(payload, 0x00010000); WriteU32Be(payload, 0);
  WriteU32Be(payload, 0);          WriteU32Be(payload, 0);
  WriteU32Be(payload, 0x40000000);

  // pre_defined (6 × uint32)
  for (int i = 0; i < 6; ++i) WriteU32Be(payload, 0);

  WriteU32Be(payload, 2);  // next_track_ID

  return MakeFullBox("mvhd", 0, 0, payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeTkhd(uint16_t width, uint16_t height) {
  std::vector<uint8_t> payload;
  payload.reserve(80);
  WriteU32Be(payload, 0);  // creation_time
  WriteU32Be(payload, 0);  // modification_time
  WriteU32Be(payload, 1);  // track_ID
  WriteU32Be(payload, 0);  // reserved
  WriteU32Be(payload, 0);  // duration (unknown for live)

  // reserved (8 bytes)
  WriteU32Be(payload, 0); WriteU32Be(payload, 0);

  WriteU16Be(payload, 0);  // layer
  WriteU16Be(payload, 0);  // alternate_group
  WriteU16Be(payload, 0);  // volume (0 for video)
  WriteU16Be(payload, 0);  // reserved

  // Identity matrix
  WriteU32Be(payload, 0x00010000); WriteU32Be(payload, 0);
  WriteU32Be(payload, 0);          WriteU32Be(payload, 0);
  WriteU32Be(payload, 0x00010000); WriteU32Be(payload, 0);
  WriteU32Be(payload, 0);          WriteU32Be(payload, 0);
  WriteU32Be(payload, 0x40000000);

  // width and height in fixed-point 16.16
  WriteU32Be(payload, static_cast<uint32_t>(width) << 16);
  WriteU32Be(payload, static_cast<uint32_t>(height) << 16);

  // flags=3 means track_enabled | track_in_movie
  return MakeFullBox("tkhd", 0, 3, payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeMdhd() {
  std::vector<uint8_t> payload;
  payload.reserve(20);
  WriteU32Be(payload, 0);          // creation_time
  WriteU32Be(payload, 0);          // modification_time
  WriteU32Be(payload, kTimescale); // timescale
  WriteU32Be(payload, 0);          // duration

  WriteU16Be(payload, 0x55C4);     // language: 'und' (undetermined)
  WriteU16Be(payload, 0);          // pre_defined

  return MakeFullBox("mdhd", 0, 0, payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeHdlr() {
  std::vector<uint8_t> payload;
  payload.reserve(25);
  WriteU32Be(payload, 0);  // pre_defined
  // handler_type: 'vide'
  payload.insert(payload.end(), {'v', 'i', 'd', 'e'});
  // reserved (3 × uint32)
  WriteU32Be(payload, 0); WriteU32Be(payload, 0); WriteU32Be(payload, 0);
  // name (null-terminated string)
  const char* name = "VideoHandler";
  payload.insert(payload.end(), name, name + strlen(name) + 1);

  return MakeFullBox("hdlr", 0, 0, payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeVmhd() {
  std::vector<uint8_t> payload;
  payload.reserve(8);
  WriteU16Be(payload, 0);  // graphicsmode
  WriteU16Be(payload, 0);  // opcolor[0]
  WriteU16Be(payload, 0);  // opcolor[1]
  WriteU16Be(payload, 0);  // opcolor[2]
  // flags=1 for vmhd
  return MakeFullBox("vmhd", 0, 1, payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeDinf() {
  // dref with a single self-contained entry
  std::vector<uint8_t> dref_payload;
  WriteU32Be(dref_payload, 1);  // entry_count

  // url entry (self-contained, flags=1)
  std::vector<uint8_t> url_payload;  // empty = self-contained
  auto url_box = MakeFullBox("url ", 0, 1, url_payload);
  dref_payload.insert(dref_payload.end(), url_box.begin(), url_box.end());

  auto dref = MakeFullBox("dref", 0, 0, dref_payload);
  return MakeBox("dinf", dref);
}

std::vector<uint8_t> Fmp4Muxer::MakeStbl(const std::vector<uint8_t>& stsd) {
  std::vector<uint8_t> payload;
  payload.insert(payload.end(), stsd.begin(), stsd.end());

  // Empty required boxes for fragmented MP4
  std::vector<uint8_t> empty_entries;

  // stts (decoding time-to-sample)
  WriteU32Be(empty_entries, 0);  // entry_count = 0
  auto stts = MakeFullBox("stts", 0, 0, empty_entries);
  payload.insert(payload.end(), stts.begin(), stts.end());

  // stsc (sample-to-chunk)
  auto stsc = MakeFullBox("stsc", 0, 0, empty_entries);
  payload.insert(payload.end(), stsc.begin(), stsc.end());

  // stsz (sample size)
  std::vector<uint8_t> stsz_payload;
  WriteU32Be(stsz_payload, 0);  // sample_size = 0 (variable)
  WriteU32Be(stsz_payload, 0);  // sample_count = 0
  auto stsz = MakeFullBox("stsz", 0, 0, stsz_payload);
  payload.insert(payload.end(), stsz.begin(), stsz.end());

  // stco (chunk offset)
  auto stco = MakeFullBox("stco", 0, 0, empty_entries);
  payload.insert(payload.end(), stco.begin(), stco.end());

  return MakeBox("stbl", payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeMinf(const std::vector<uint8_t>& stsd) {
  std::vector<uint8_t> payload;

  auto vmhd = MakeVmhd();
  payload.insert(payload.end(), vmhd.begin(), vmhd.end());

  auto dinf = MakeDinf();
  payload.insert(payload.end(), dinf.begin(), dinf.end());

  auto stbl = MakeStbl(stsd);
  payload.insert(payload.end(), stbl.begin(), stbl.end());

  return MakeBox("minf", payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeMdia(const std::vector<uint8_t>& stsd) {
  std::vector<uint8_t> payload;

  auto mdhd = MakeMdhd();
  payload.insert(payload.end(), mdhd.begin(), mdhd.end());

  auto hdlr = MakeHdlr();
  payload.insert(payload.end(), hdlr.begin(), hdlr.end());

  auto minf = MakeMinf(stsd);
  payload.insert(payload.end(), minf.begin(), minf.end());

  return MakeBox("mdia", payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeTrak(uint16_t width, uint16_t height,
                                          const std::vector<uint8_t>& stsd) {
  std::vector<uint8_t> payload;

  auto tkhd = MakeTkhd(width, height);
  payload.insert(payload.end(), tkhd.begin(), tkhd.end());

  auto mdia = MakeMdia(stsd);
  payload.insert(payload.end(), mdia.begin(), mdia.end());

  return MakeBox("trak", payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeMvex() {
  // trex (track extends) — default sample settings for fragments
  std::vector<uint8_t> trex_payload;
  WriteU32Be(trex_payload, 1);  // track_ID
  WriteU32Be(trex_payload, 1);  // default_sample_description_index
  WriteU32Be(trex_payload, 0);  // default_sample_duration
  WriteU32Be(trex_payload, 0);  // default_sample_size
  WriteU32Be(trex_payload, 0);  // default_sample_flags

  auto trex = MakeFullBox("trex", 0, 0, trex_payload);
  return MakeBox("mvex", trex);
}

std::vector<uint8_t> Fmp4Muxer::MakeMoov(uint16_t width, uint16_t height,
                                          const std::vector<uint8_t>& stsd) {
  std::vector<uint8_t> payload;

  auto mvhd = MakeMvhd();
  payload.insert(payload.end(), mvhd.begin(), mvhd.end());

  auto trak = MakeTrak(width, height, stsd);
  payload.insert(payload.end(), trak.begin(), trak.end());

  auto mvex = MakeMvex();
  payload.insert(payload.end(), mvex.begin(), mvex.end());

  return MakeBox("moov", payload);
}

// ============================================================
// AVC (H.264) Configuration
// ============================================================

std::vector<uint8_t> Fmp4Muxer::MakeAvcC(const std::vector<uint8_t>& sps,
                                          const std::vector<uint8_t>& pps) {
  std::vector<uint8_t> payload;
  // AVCDecoderConfigurationRecord
  WriteU8(payload, 1);          // configurationVersion
  WriteU8(payload, sps[1]);     // AVCProfileIndication
  WriteU8(payload, sps[2]);     // profile_compatibility
  WriteU8(payload, sps[3]);     // AVCLevelIndication
  WriteU8(payload, 0xFF);       // lengthSizeMinusOne = 3 (4 bytes NALU length)

  // numOfSequenceParameterSets
  WriteU8(payload, 0xE1);  // 0xE0 | 1
  WriteU16Be(payload, static_cast<uint16_t>(sps.size()));
  payload.insert(payload.end(), sps.begin(), sps.end());

  // numOfPictureParameterSets
  WriteU8(payload, 1);
  WriteU16Be(payload, static_cast<uint16_t>(pps.size()));
  payload.insert(payload.end(), pps.begin(), pps.end());

  return MakeBox("avcC", payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeAvc1Stsd(
    const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps,
    uint16_t width, uint16_t height) {
  // avc1 sample entry
  std::vector<uint8_t> avc1_payload;
  avc1_payload.reserve(128 + sps.size() + pps.size());

  // reserved (6 bytes)
  for (int i = 0; i < 6; ++i) WriteU8(avc1_payload, 0);
  // data_reference_index
  WriteU16Be(avc1_payload, 1);
  // pre_defined + reserved (16 bytes)
  for (int i = 0; i < 16; ++i) WriteU8(avc1_payload, 0);
  // width, height
  WriteU16Be(avc1_payload, width);
  WriteU16Be(avc1_payload, height);
  // horizresolution (72.0 dpi)
  WriteU32Be(avc1_payload, 0x00480000);
  // vertresolution (72.0 dpi)
  WriteU32Be(avc1_payload, 0x00480000);
  // reserved
  WriteU32Be(avc1_payload, 0);
  // frame_count
  WriteU16Be(avc1_payload, 1);
  // compressorname (32 bytes, padded with zeros)
  for (int i = 0; i < 32; ++i) WriteU8(avc1_payload, 0);
  // depth
  WriteU16Be(avc1_payload, 0x0018);
  // pre_defined
  WriteU16Be(avc1_payload, 0xFFFF);

  // avcC box
  auto avcc = MakeAvcC(sps, pps);
  avc1_payload.insert(avc1_payload.end(), avcc.begin(), avcc.end());

  auto avc1 = MakeBox("avc1", avc1_payload);

  // stsd full box wrapping
  std::vector<uint8_t> stsd_payload;
  WriteU32Be(stsd_payload, 1);  // entry_count
  stsd_payload.insert(stsd_payload.end(), avc1.begin(), avc1.end());

  return MakeFullBox("stsd", 0, 0, stsd_payload);
}

// ============================================================
// HEVC (H.265) Configuration
// ============================================================

std::vector<uint8_t> Fmp4Muxer::MakeHvcC(const std::vector<uint8_t>& vps,
                                          const std::vector<uint8_t>& sps,
                                          const std::vector<uint8_t>& pps) {
  std::vector<uint8_t> payload;
  // HEVCDecoderConfigurationRecord (ISO 14496-15 Section 8.3.3.1)

  WriteU8(payload, 1);  // configurationVersion

  // Extract general_profile_space, general_tier_flag, general_profile_idc
  // from SPS (after NAL header: 2 bytes for H.265)
  uint8_t general_profile_space = 0;
  uint8_t general_tier_flag = 0;
  uint8_t general_profile_idc = 0;
  uint32_t general_profile_compatibility_flags = 0;
  uint64_t general_constraint_indicator_flags = 0;
  uint8_t general_level_idc = 0;

  // Parse profile_tier_level from SPS if enough data
  // SPS NAL: [nal_header(2)] [sps_video_parameter_set_id(4bits)]
  //          [sps_max_sub_layers_minus1(3bits)] [sps_temporal_id_nesting_flag(1bit)]
  //          [profile_tier_level(...)]
  if (sps.size() > 15) {
    // Byte 2 of SPS contains vps_id, max_sub_layers, temporal_id_nesting
    // Profile tier level starts at bit offset 16 (byte index 2)
    // general_profile_space(2) | general_tier_flag(1) | general_profile_idc(5)
    uint8_t ptl_byte = sps[2];
    general_profile_space = (ptl_byte >> 6) & 0x03;
    general_tier_flag = (ptl_byte >> 5) & 0x01;
    general_profile_idc = ptl_byte & 0x1F;

    // general_profile_compatibility_flags (32 bits)
    general_profile_compatibility_flags =
        (static_cast<uint32_t>(sps[3]) << 24) |
        (static_cast<uint32_t>(sps[4]) << 16) |
        (static_cast<uint32_t>(sps[5]) << 8) |
        static_cast<uint32_t>(sps[6]);

    // general_constraint_indicator_flags (48 bits)
    general_constraint_indicator_flags =
        (static_cast<uint64_t>(sps[7]) << 40) |
        (static_cast<uint64_t>(sps[8]) << 32) |
        (static_cast<uint64_t>(sps[9]) << 24) |
        (static_cast<uint64_t>(sps[10]) << 16) |
        (static_cast<uint64_t>(sps[11]) << 8) |
        static_cast<uint64_t>(sps[12]);

    // general_level_idc
    general_level_idc = sps[13];
  }

  // general_profile_space(2) | general_tier_flag(1) | general_profile_idc(5)
  WriteU8(payload, static_cast<uint8_t>((general_profile_space << 6) |
                                        (general_tier_flag << 5) |
                                        general_profile_idc));
  WriteU32Be(payload, general_profile_compatibility_flags);

  // general_constraint_indicator_flags (6 bytes)
  for (int i = 5; i >= 0; --i) {
    WriteU8(payload,
            static_cast<uint8_t>((general_constraint_indicator_flags >>
                                  (i * 8)) & 0xFF));
  }

  WriteU8(payload, general_level_idc);

  // min_spatial_segmentation_idc (with reserved bits)
  WriteU16Be(payload, 0xF000);  // reserved(4) + min_spatial_segmentation_idc=0
  // parallelismType
  WriteU8(payload, 0xFC);  // reserved(6) + parallelismType=0
  // chromaFormat
  WriteU8(payload, 0xFC);  // reserved(6) + chroma_format_idc=0
  // bitDepthLumaMinus8
  WriteU8(payload, 0xF8);  // reserved(5) + bit_depth_luma_minus8=0
  // bitDepthChromaMinus8
  WriteU8(payload, 0xF8);  // reserved(5) + bit_depth_chroma_minus8=0
  // avgFrameRate
  WriteU16Be(payload, 0);
  // constantFrameRate(2) | numTemporalLayers(3) | temporalIdNested(1) |
  // lengthSizeMinusOne(2)
  WriteU8(payload, 0x0F);  // lengthSizeMinusOne=3 (4 bytes NALU length)
  // numOfArrays
  WriteU8(payload, 3);  // VPS + SPS + PPS

  // VPS array
  WriteU8(payload, 0x20);  // array_completeness(0) | nal_unit_type(32=VPS)
  WriteU16Be(payload, 1);  // numNalus
  WriteU16Be(payload, static_cast<uint16_t>(vps.size()));
  payload.insert(payload.end(), vps.begin(), vps.end());

  // SPS array
  WriteU8(payload, 0x21);  // nal_unit_type(33=SPS)
  WriteU16Be(payload, 1);
  WriteU16Be(payload, static_cast<uint16_t>(sps.size()));
  payload.insert(payload.end(), sps.begin(), sps.end());

  // PPS array
  WriteU8(payload, 0x22);  // nal_unit_type(34=PPS)
  WriteU16Be(payload, 1);
  WriteU16Be(payload, static_cast<uint16_t>(pps.size()));
  payload.insert(payload.end(), pps.begin(), pps.end());

  return MakeBox("hvcC", payload);
}

std::vector<uint8_t> Fmp4Muxer::MakeHev1Stsd(
    const std::vector<uint8_t>& vps, const std::vector<uint8_t>& sps,
    const std::vector<uint8_t>& pps, uint16_t width, uint16_t height) {
  // hev1 sample entry (same structure as avc1 but different codec box)
  std::vector<uint8_t> hev1_payload;
  hev1_payload.reserve(128 + vps.size() + sps.size() + pps.size());

  // reserved (6 bytes)
  for (int i = 0; i < 6; ++i) WriteU8(hev1_payload, 0);
  // data_reference_index
  WriteU16Be(hev1_payload, 1);
  // pre_defined + reserved (16 bytes)
  for (int i = 0; i < 16; ++i) WriteU8(hev1_payload, 0);
  // width, height
  WriteU16Be(hev1_payload, width);
  WriteU16Be(hev1_payload, height);
  // horizresolution (72.0 dpi)
  WriteU32Be(hev1_payload, 0x00480000);
  // vertresolution (72.0 dpi)
  WriteU32Be(hev1_payload, 0x00480000);
  // reserved
  WriteU32Be(hev1_payload, 0);
  // frame_count
  WriteU16Be(hev1_payload, 1);
  // compressorname (32 bytes)
  for (int i = 0; i < 32; ++i) WriteU8(hev1_payload, 0);
  // depth
  WriteU16Be(hev1_payload, 0x0018);
  // pre_defined
  WriteU16Be(hev1_payload, 0xFFFF);

  // hvcC box
  auto hvcc = MakeHvcC(vps, sps, pps);
  hev1_payload.insert(hev1_payload.end(), hvcc.begin(), hvcc.end());

  auto hev1 = MakeBox("hev1", hev1_payload);

  // stsd full box wrapping
  std::vector<uint8_t> stsd_payload;
  WriteU32Be(stsd_payload, 1);  // entry_count
  stsd_payload.insert(stsd_payload.end(), hev1.begin(), hev1.end());

  return MakeFullBox("stsd", 0, 0, stsd_payload);
}

// ============================================================
// Init Segment Builders
// ============================================================

std::vector<uint8_t> Fmp4Muxer::MakeH264InitSegment(
    const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps,
    uint16_t width, uint16_t height) {
  if (sps.size() < 4 || pps.empty()) return {};

  auto ftyp = MakeFtyp();
  auto stsd = MakeAvc1Stsd(sps, pps, width, height);
  auto moov = MakeMoov(width, height, stsd);

  std::vector<uint8_t> init_segment;
  init_segment.reserve(ftyp.size() + moov.size());
  init_segment.insert(init_segment.end(), ftyp.begin(), ftyp.end());
  init_segment.insert(init_segment.end(), moov.begin(), moov.end());
  return init_segment;
}

std::vector<uint8_t> Fmp4Muxer::MakeH265InitSegment(
    const std::vector<uint8_t>& vps, const std::vector<uint8_t>& sps,
    const std::vector<uint8_t>& pps, uint16_t width, uint16_t height) {
  if (vps.empty() || sps.empty() || pps.empty()) return {};

  auto ftyp = MakeFtyp();
  auto stsd = MakeHev1Stsd(vps, sps, pps, width, height);
  auto moov = MakeMoov(width, height, stsd);

  std::vector<uint8_t> init_segment;
  init_segment.reserve(ftyp.size() + moov.size());
  init_segment.insert(init_segment.end(), ftyp.begin(), ftyp.end());
  init_segment.insert(init_segment.end(), moov.begin(), moov.end());
  return init_segment;
}

// ============================================================
// Media Segment (moof + mdat)
// ============================================================

std::vector<uint8_t> Fmp4Muxer::MakeMoof(uint32_t sequence_number,
                                          uint64_t decode_time,
                                          uint32_t duration,
                                          uint32_t sample_size,
                                          bool is_keyframe) {
  // mfhd (movie fragment header)
  std::vector<uint8_t> mfhd_payload;
  WriteU32Be(mfhd_payload, sequence_number);
  auto mfhd = MakeFullBox("mfhd", 0, 0, mfhd_payload);

  // tfhd (track fragment header)
  // flags: 0x020000 = default-base-is-moof
  std::vector<uint8_t> tfhd_payload;
  WriteU32Be(tfhd_payload, 1);  // track_ID
  auto tfhd = MakeFullBox("tfhd", 0, 0x020000, tfhd_payload);

  // tfdt (track fragment decode time) — version 1 for 64-bit baseMediaDecodeTime
  std::vector<uint8_t> tfdt_payload;
  WriteU64Be(tfdt_payload, decode_time);
  auto tfdt = MakeFullBox("tfdt", 1, 0, tfdt_payload);

  // trun (track run)
  // flags: 0x000001 = data-offset-present
  //        0x000100 = sample-duration-present
  //        0x000200 = sample-size-present
  //        0x000400 = sample-flags-present
  uint32_t trun_flags = 0x000001 | 0x000100 | 0x000200 | 0x000400;
  std::vector<uint8_t> trun_payload;
  WriteU32Be(trun_payload, 1);  // sample_count = 1

  // data_offset: will be computed after moof size is known.
  // Placeholder — we'll fixup after assembling.
  WriteU32Be(trun_payload, 0);  // data_offset placeholder

  // sample_duration
  WriteU32Be(trun_payload, duration);
  // sample_size (length-prefixed frame data size)
  WriteU32Be(trun_payload, sample_size);
  // sample_flags
  // For keyframes: 0x02000000 (sample_depends_on=2 means "does not depend")
  // For non-keyframes: 0x01010000 (sample_is_non_sync | sample_depends_on=1)
  uint32_t sample_flags =
      is_keyframe ? 0x02000000U : 0x01010000U;
  WriteU32Be(trun_payload, sample_flags);

  auto trun = MakeFullBox("trun", 0, trun_flags, trun_payload);

  // traf (track fragment)
  std::vector<uint8_t> traf_payload;
  traf_payload.insert(traf_payload.end(), tfhd.begin(), tfhd.end());
  traf_payload.insert(traf_payload.end(), tfdt.begin(), tfdt.end());
  traf_payload.insert(traf_payload.end(), trun.begin(), trun.end());
  auto traf = MakeBox("traf", traf_payload);

  // moof
  std::vector<uint8_t> moof_payload;
  moof_payload.insert(moof_payload.end(), mfhd.begin(), mfhd.end());
  moof_payload.insert(moof_payload.end(), traf.begin(), traf.end());
  auto moof = MakeBox("moof", moof_payload);

  // Fix up data_offset in trun.
  // data_offset = moof_size + mdat_header_size(8)
  auto moof_size = static_cast<uint32_t>(moof.size());
  uint32_t data_offset = moof_size + 8;

  // Navigate to the data_offset field within the trun in the serialized moof.
  // trun is inside traf, which is inside moof.
  // moof header(8) + mfhd + traf header(8) + tfhd + tfdt + trun header(8+4)
  //   + trun version/flags(4) + sample_count(4) + data_offset(4)
  // Instead of computing exact offset, search for the placeholder.
  // The data_offset is at a known position in trun_payload.
  // We need to find it in the serialized moof.
  //
  // More robust: compute absolute offset.
  // moof_header(8) + mfhd_size + traf_header(8) + tfhd_size + tfdt_size
  //   + trun_header(8) + version_flags(4) + sample_count(4) = data_offset field
  size_t offset_in_moof = 8 + mfhd.size() + 8 + tfhd.size() + tfdt.size() +
                          8 + 4 + 4;  // 8=trun box header, 4=ver+flags, 4=sample_count
  if (offset_in_moof + 4 <= moof.size()) {
    moof[offset_in_moof] = static_cast<uint8_t>((data_offset >> 24) & 0xFF);
    moof[offset_in_moof + 1] =
        static_cast<uint8_t>((data_offset >> 16) & 0xFF);
    moof[offset_in_moof + 2] =
        static_cast<uint8_t>((data_offset >> 8) & 0xFF);
    moof[offset_in_moof + 3] = static_cast<uint8_t>(data_offset & 0xFF);
  }

  return moof;
}

std::vector<uint8_t> Fmp4Muxer::MakeMediaSegment(
    const uint8_t* data, size_t size, uint64_t decode_time,
    uint32_t duration, uint32_t sequence_number, bool is_keyframe,
    CodecType codec) {
  // Convert Annex B to length-prefixed MP4 format
  auto mp4_data = AnnexBToMp4(data, size, codec);
  if (mp4_data.empty()) return {};

  auto sample_size = static_cast<uint32_t>(mp4_data.size());

  // Build moof
  auto moof = MakeMoof(sequence_number, decode_time, duration,
                       sample_size, is_keyframe);

  // Build mdat
  auto mdat_size = static_cast<uint32_t>(8 + mp4_data.size());
  std::vector<uint8_t> mdat;
  mdat.reserve(mdat_size);
  WriteU32Be(mdat, mdat_size);
  mdat.insert(mdat.end(), {'m', 'd', 'a', 't'});
  mdat.insert(mdat.end(), mp4_data.begin(), mp4_data.end());

  // Concatenate moof + mdat
  std::vector<uint8_t> segment;
  segment.reserve(moof.size() + mdat.size());
  segment.insert(segment.end(), moof.begin(), moof.end());
  segment.insert(segment.end(), mdat.begin(), mdat.end());
  return segment;
}

}  // namespace loong::network
