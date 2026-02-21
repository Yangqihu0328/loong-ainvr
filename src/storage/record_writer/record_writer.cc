// Copyright 2026 Loong AI NVR Project

#include "storage/record_writer/record_writer.h"

#include "spdlog/spdlog.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <utility>

namespace loong::storage {

RecordWriter::RecordWriter(int channel_id, RecordConfig config)
    : channel_id_(channel_id), config_(std::move(config)) {}

RecordWriter::~RecordWriter() { Close(); }

bool RecordWriter::WriteFrame(const uint8_t* data, size_t size, int64_t pts,
                              int64_t dts, bool is_keyframe) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Open segment on first keyframe
  if (!is_open_) {
    if (!is_keyframe) return true;  // Wait for keyframe to start
    if (!OpenSegment()) return false;
    segment_start_pts_ = pts;
  }

  // Check if segment duration exceeded — rotate on next keyframe
  if (is_keyframe && segment_start_pts_ >= 0) {
    int64_t duration_us = pts - segment_start_pts_;
    int64_t duration_sec = duration_us / 1000000;
    if (duration_sec >= config_.segment_duration_sec) {
      CloseSegment();
      if (!OpenSegment()) return false;
      segment_start_pts_ = pts;
    }
  }

  // Write packet
  AVPacket* pkt = av_packet_alloc();
  pkt->data = const_cast<uint8_t*>(data);
  pkt->size = static_cast<int>(size);

  // Convert PTS/DTS to stream time_base
  AVRational src_tb = {1, 1000000};  // microseconds
  pkt->pts = av_rescale_q(pts, src_tb, video_stream_->time_base);
  pkt->dts = av_rescale_q(dts, src_tb, video_stream_->time_base);
  pkt->stream_index = video_stream_->index;

  if (is_keyframe) {
    pkt->flags |= AV_PKT_FLAG_KEY;
  }

  int ret = av_interleaved_write_frame(fmt_ctx_, pkt);
  av_packet_free(&pkt);

  if (ret < 0) {
    spdlog::warn("RecordWriter ch{}: write_frame error {}", channel_id_, ret);
    return false;
  }

  total_bytes_written_ += static_cast<int64_t>(size);
  ++frame_count_;
  return true;
}

void RecordWriter::StartNewSegment() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (is_open_) {
    CloseSegment();
  }
  OpenSegment();
}

void RecordWriter::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  CloseSegment();
}

std::string RecordWriter::CurrentSegmentPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_segment_path_;
}

bool RecordWriter::OpenSegment() {
  current_segment_path_ = GenerateSegmentPath();

  // Ensure directory exists
  std::filesystem::path dir(current_segment_path_);
  std::filesystem::create_directories(dir.parent_path());

  int ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr, "mp4",
                                           current_segment_path_.c_str());
  if (ret < 0 || !fmt_ctx_) {
    spdlog::error("RecordWriter ch{}: failed to create output context",
                  channel_id_);
    return false;
  }

  // Add video stream
  AVCodecID codec_id =
      (config_.codec == CodecType::kH264) ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;
  const AVCodec* codec = avcodec_find_encoder(codec_id);
  video_stream_ = avformat_new_stream(fmt_ctx_, codec);
  if (!video_stream_) {
    spdlog::error("RecordWriter ch{}: failed to add stream", channel_id_);
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    return false;
  }

  video_stream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
  video_stream_->codecpar->codec_id = codec_id;
  video_stream_->codecpar->width = config_.width;
  video_stream_->codecpar->height = config_.height;
  video_stream_->codecpar->bit_rate =
      static_cast<int64_t>(config_.bitrate_kbps) * 1000;
  video_stream_->time_base = {1, config_.framerate};

  // Open output file
  if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&fmt_ctx_->pb, current_segment_path_.c_str(),
                    AVIO_FLAG_WRITE);
    if (ret < 0) {
      spdlog::error("RecordWriter ch{}: failed to open '{}'", channel_id_,
                    current_segment_path_);
      avformat_free_context(fmt_ctx_);
      fmt_ctx_ = nullptr;
      return false;
    }
  }

  // Set fragmented MP4 for better fault tolerance
  AVDictionary* opts = nullptr;
  av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov", 0);

  ret = avformat_write_header(fmt_ctx_, &opts);
  av_dict_free(&opts);

  if (ret < 0) {
    spdlog::error("RecordWriter ch{}: write_header failed", channel_id_);
    avio_closep(&fmt_ctx_->pb);
    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    return false;
  }

  is_open_ = true;
  spdlog::info("RecordWriter ch{}: opened segment '{}'", channel_id_,
               current_segment_path_);
  return true;
}

void RecordWriter::CloseSegment() {
  if (!is_open_ || !fmt_ctx_) return;

  av_write_trailer(fmt_ctx_);

  if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
    avio_closep(&fmt_ctx_->pb);
  }

  avformat_free_context(fmt_ctx_);
  fmt_ctx_ = nullptr;
  video_stream_ = nullptr;
  is_open_ = false;

  spdlog::info("RecordWriter ch{}: closed segment '{}' ({} frames)",
               channel_id_, current_segment_path_, frame_count_);
}

std::string RecordWriter::GenerateSegmentPath() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&time_t, &tm);

  std::ostringstream ss;
  ss << config_.base_path << "/ch" << std::setfill('0') << std::setw(2)
     << channel_id_ << "/seg_" << std::setfill('0') << std::setw(4)
     << (tm.tm_year + 1900) << std::setfill('0') << std::setw(2)
     << (tm.tm_mon + 1) << std::setfill('0') << std::setw(2) << tm.tm_mday
     << "_" << std::setfill('0') << std::setw(2) << tm.tm_hour
     << std::setfill('0') << std::setw(2) << tm.tm_min << std::setfill('0')
     << std::setw(2) << tm.tm_sec << ".mp4";
  return ss.str();
}

}  // namespace loong::storage
