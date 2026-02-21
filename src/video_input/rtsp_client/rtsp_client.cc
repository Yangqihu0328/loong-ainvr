// Copyright 2026 Loong AI NVR Project

#include "video_input/rtsp_client/rtsp_client.h"

#include <algorithm>
#include <cstring>

#include "spdlog/spdlog.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

namespace loong {
namespace video_input {

RtspClient::RtspClient() = default;

RtspClient::~RtspClient() {
  Close();
}

bool RtspClient::Open(const RtspClientConfig& config, int channel_id) {
  // Close any previous session first.
  Close();

  config_ = config;
  channel_id_ = channel_id;
  packets_received_ = 0;
  reconnect_attempts_ = 0;

  if (config_.url.empty()) {
    spdlog::error("RtspClient ch{}: URL is empty", channel_id_);
    SetState(RtspConnectionState::kError, "URL is empty");
    return false;
  }

  SetState(RtspConnectionState::kConnecting);

  if (!Connect()) {
    SetState(RtspConnectionState::kError, "Initial connection failed");
    return false;
  }

  SetState(RtspConnectionState::kConnected,
           "Connected to " + config_.url);

  // Start the pull loop thread.
  running_ = true;
  pull_thread_ = std::thread(&RtspClient::PullLoop, this);

  spdlog::info("RtspClient ch{}: opened {}", channel_id_, config_.url);
  return true;
}

void RtspClient::Close() {
  running_ = false;
  if (pull_thread_.joinable()) {
    pull_thread_.join();
  }
  Disconnect();
  SetState(RtspConnectionState::kDisconnected);
}

bool RtspClient::IsConnected() const {
  return state_.load() == RtspConnectionState::kConnected;
}

RtspConnectionState RtspClient::GetState() const {
  return state_.load();
}

RtspStreamInfo RtspClient::GetStreamInfo() const {
  return stream_info_;
}

std::string RtspClient::GetUrl() const {
  return config_.url;
}

int RtspClient::GetChannelId() const {
  return channel_id_;
}

int64_t RtspClient::PacketsReceived() const {
  return packets_received_.load();
}

int RtspClient::ReconnectAttempts() const {
  return reconnect_attempts_.load();
}

void RtspClient::SetPacketCallback(PacketCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  packet_callback_ = std::move(callback);
}

void RtspClient::SetStateCallback(StateCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  state_callback_ = std::move(callback);
}

// ---------- Private methods ----------

bool RtspClient::Connect() {
  AVDictionary* opts = nullptr;

  // Set transport mode.
  const char* transport =
      (config_.transport == RtspTransport::kTcp) ? "tcp" : "udp";
  av_dict_set(&opts, "rtsp_transport", transport, 0);

  // Set connection timeout (in microseconds).
  std::string timeout_us =
      std::to_string(static_cast<int64_t>(config_.connect_timeout_ms) * 1000);
  av_dict_set(&opts, "stimeout", timeout_us.c_str(), 0);

  // Allocate format context and open input.
  fmt_ctx_ = avformat_alloc_context();
  int ret = avformat_open_input(&fmt_ctx_, config_.url.c_str(), nullptr, &opts);
  av_dict_free(&opts);

  if (ret != 0) {
    char err_buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(ret, err_buf, sizeof(err_buf));
    spdlog::error("RtspClient ch{}: avformat_open_input failed — {}",
                  channel_id_, err_buf);
    fmt_ctx_ = nullptr;
    return false;
  }

  // Retrieve stream information.
  if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
    spdlog::error("RtspClient ch{}: failed to find stream info",
                  channel_id_);
    avformat_close_input(&fmt_ctx_);
    return false;
  }

  // Locate the first video stream.
  video_stream_index_ = -1;
  for (unsigned i = 0; i < fmt_ctx_->nb_streams; ++i) {
    if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_index_ = static_cast<int>(i);
      break;
    }
  }

  if (video_stream_index_ < 0) {
    spdlog::error("RtspClient ch{}: no video stream found", channel_id_);
    avformat_close_input(&fmt_ctx_);
    return false;
  }

  // Extract stream metadata.
  auto* codecpar = fmt_ctx_->streams[video_stream_index_]->codecpar;

  if (codecpar->codec_id == AV_CODEC_ID_H264) {
    stream_info_.codec = CodecType::kH264;
  } else if (codecpar->codec_id == AV_CODEC_ID_HEVC) {
    stream_info_.codec = CodecType::kH265;
  } else {
    stream_info_.codec = CodecType::kUnknown;
  }

  stream_info_.width = codecpar->width;
  stream_info_.height = codecpar->height;

  // Attempt to read framerate from the stream.
  auto& r = fmt_ctx_->streams[video_stream_index_]->avg_frame_rate;
  if (r.den > 0) {
    stream_info_.framerate = static_cast<int>(r.num / r.den);
  }

  spdlog::info(
      "RtspClient ch{}: connected — {} {}x{} @{}fps",
      channel_id_,
      (stream_info_.codec == CodecType::kH264) ? "H.264" :
      (stream_info_.codec == CodecType::kH265) ? "H.265" : "Unknown",
      stream_info_.width, stream_info_.height,
      stream_info_.framerate);

  // For container formats (MP4/MKV/etc.) the packets are in AVCC/HVCC format
  // (length-prefixed NAL units). The decoder expects Annex B (start-code
  // prefixed). Install an mp4toannexb bitstream filter to convert on the fly.
  // RTSP already delivers Annex B so this is only needed for file inputs.
  bool needs_bsf = false;
  is_file_source_ = false;
  if (fmt_ctx_->iformat && fmt_ctx_->iformat->name) {
    std::string fmt_name(fmt_ctx_->iformat->name);
    // iformat->name can be a comma-separated list, e.g. "mov,mp4,m4a,3gp..."
    if (fmt_name.find("mov") != std::string::npos ||
        fmt_name.find("mp4") != std::string::npos ||
        fmt_name.find("matroska") != std::string::npos ||
        fmt_name.find("flv") != std::string::npos ||
        fmt_name.find("avi") != std::string::npos) {
      needs_bsf = true;
      is_file_source_ = true;
    }
  }

  if (needs_bsf && (codecpar->codec_id == AV_CODEC_ID_H264 ||
                     codecpar->codec_id == AV_CODEC_ID_HEVC)) {
    const char* bsf_name = (codecpar->codec_id == AV_CODEC_ID_H264)
                               ? "h264_mp4toannexb"
                               : "hevc_mp4toannexb";
    const AVBitStreamFilter* filter = av_bsf_get_by_name(bsf_name);
    if (filter) {
      int bsf_ret = av_bsf_alloc(filter, &bsf_ctx_);
      if (bsf_ret >= 0) {
        avcodec_parameters_copy(bsf_ctx_->par_in, codecpar);
        bsf_ctx_->time_base_in =
            fmt_ctx_->streams[video_stream_index_]->time_base;
        bsf_ret = av_bsf_init(bsf_ctx_);
      }
      if (bsf_ret < 0) {
        spdlog::warn("RtspClient ch{}: failed to init {} filter",
                      channel_id_, bsf_name);
        av_bsf_free(&bsf_ctx_);
        bsf_ctx_ = nullptr;
      } else {
        spdlog::info("RtspClient ch{}: {} filter enabled for container input",
                      channel_id_, bsf_name);
      }
    }
  }

  return true;
}

void RtspClient::Disconnect() {
  if (bsf_ctx_) {
    av_bsf_free(&bsf_ctx_);
    bsf_ctx_ = nullptr;
  }
  if (fmt_ctx_) {
    avformat_close_input(&fmt_ctx_);
    fmt_ctx_ = nullptr;
  }
  video_stream_index_ = -1;
}

void RtspClient::PullLoop() {
  AVPacket* pkt = av_packet_alloc();
  if (!pkt) {
    spdlog::error("RtspClient ch{}: failed to allocate AVPacket",
                  channel_id_);
    SetState(RtspConnectionState::kError, "AVPacket allocation failed");
    return;
  }

  // Real-time pacing for file sources: map PTS to wall-clock so we don't
  // flood the pipeline faster than the original frame rate.
  auto file_wall_origin = std::chrono::steady_clock::now();
  int64_t file_pts_origin = AV_NOPTS_VALUE;
  AVRational file_time_base = {1, 1};
  if (is_file_source_ && video_stream_index_ >= 0) {
    file_time_base = fmt_ctx_->streams[video_stream_index_]->time_base;
  }

  while (running_) {
    int ret = av_read_frame(fmt_ctx_, pkt);

    if (ret < 0) {
      if (ret == AVERROR_EOF && is_file_source_) {
        spdlog::info("RtspClient ch{}: file EOF, looping", channel_id_);

        if (bsf_ctx_) {
          av_bsf_flush(bsf_ctx_);
        }

        if (av_seek_frame(fmt_ctx_, video_stream_index_, 0,
                          AVSEEK_FLAG_BACKWARD) >= 0) {
          file_pts_origin = AV_NOPTS_VALUE;
          file_wall_origin = std::chrono::steady_clock::now();
          continue;
        }
        spdlog::warn("RtspClient ch{}: seek failed, stopping", channel_id_);
        SetState(RtspConnectionState::kError, "File seek failed");
        break;
      }

      if (ret == AVERROR_EOF) {
        spdlog::info("RtspClient ch{}: stream ended (EOF)", channel_id_);
      } else {
        char err_buf[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(ret, err_buf, sizeof(err_buf));
        spdlog::warn("RtspClient ch{}: read error — {}", channel_id_,
                      err_buf);
      }

      if (running_ && config_.auto_reconnect) {
        if (TryReconnect()) {
          continue;
        }
      }

      SetState(RtspConnectionState::kError, "Stream read failed");
      break;
    }

    if (pkt->stream_index != video_stream_index_) {
      av_packet_unref(pkt);
      continue;
    }

    int64_t orig_pts = pkt->pts;

    if (bsf_ctx_) {
      if (av_bsf_send_packet(bsf_ctx_, pkt) < 0) {
        av_packet_unref(pkt);
        continue;
      }
      while (av_bsf_receive_packet(bsf_ctx_, pkt) == 0) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (packet_callback_) {
          bool is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
          packet_callback_(
              channel_id_,
              pkt->data, static_cast<size_t>(pkt->size),
              pkt->pts, pkt->dts,
              is_key, stream_info_.codec);
        }
        ++packets_received_;
        av_packet_unref(pkt);
      }
    } else {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      if (packet_callback_) {
        bool is_key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        packet_callback_(
            channel_id_,
            pkt->data, static_cast<size_t>(pkt->size),
            pkt->pts, pkt->dts,
            is_key, stream_info_.codec);
      }
      ++packets_received_;
      av_packet_unref(pkt);
    }

    // Pace file playback to approximately real-time using PTS.
    if (is_file_source_ && orig_pts != AV_NOPTS_VALUE) {
      if (file_pts_origin == AV_NOPTS_VALUE) {
        file_pts_origin = orig_pts;
        file_wall_origin = std::chrono::steady_clock::now();
      } else {
        double elapsed_sec =
            static_cast<double>(orig_pts - file_pts_origin) *
            av_q2d(file_time_base);
        if (elapsed_sec > 0) {
          auto target = file_wall_origin +
              std::chrono::microseconds(
                  static_cast<int64_t>(elapsed_sec * 1'000'000));
          auto now = std::chrono::steady_clock::now();
          if (target > now) {
            std::this_thread::sleep_until(target);
          }
        }
      }
    }
  }

  av_packet_free(&pkt);
}

bool RtspClient::TryReconnect() {
  SetState(RtspConnectionState::kReconnecting);

  int delay_ms = config_.reconnect_delay_ms;

  while (running_) {
    ++reconnect_attempts_;

    // Check max attempts (0 = unlimited).
    if (config_.max_reconnect_attempts > 0 &&
        reconnect_attempts_.load() > config_.max_reconnect_attempts) {
      spdlog::error(
          "RtspClient ch{}: max reconnection attempts ({}) exceeded",
          channel_id_, config_.max_reconnect_attempts);
      return false;
    }

    spdlog::info("RtspClient ch{}: reconnecting (attempt {}, delay {}ms)",
                 channel_id_, reconnect_attempts_.load(), delay_ms);

    // Wait before attempting reconnection (interruptible).
    for (int waited = 0; waited < delay_ms && running_; waited += 100) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!running_) return false;

    // Tear down old connection and try a fresh one.
    Disconnect();

    if (Connect()) {
      spdlog::info("RtspClient ch{}: reconnected successfully", channel_id_);
      SetState(RtspConnectionState::kConnected, "Reconnected");
      return true;
    }

    // Exponential backoff (capped at max delay).
    delay_ms = std::min(delay_ms * 2, config_.reconnect_max_delay_ms);
  }

  return false;
}

void RtspClient::SetState(RtspConnectionState new_state,
                           const std::string& message) {
  RtspConnectionState old_state = state_.exchange(new_state);
  if (old_state == new_state) return;

  spdlog::debug("RtspClient ch{}: {} -> {} {}",
                channel_id_,
                RtspConnectionStateToString(old_state),
                RtspConnectionStateToString(new_state),
                message.empty() ? "" : ("— " + message));

  std::lock_guard<std::mutex> lock(callback_mutex_);
  if (state_callback_) {
    state_callback_(channel_id_, new_state, message);
  }
}

}  // namespace video_input
}  // namespace loong
