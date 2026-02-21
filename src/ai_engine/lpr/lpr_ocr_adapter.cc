// Copyright 2026 Loong AI NVR Project

#include "ai_engine/lpr/lpr_ocr_adapter.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace loong::ai_engine {

LprOcrAdapter::LprOcrAdapter(int input_width, int input_height)
    : input_width_(input_width),
      input_height_(input_height),
      charset_(DefaultChineseCharset()) {}

bool LprOcrAdapter::PreProcess(const uint8_t* image_data, int width, int height,
                               std::vector<float>& input_data,
                               TensorShape& input_shape) {
  if (!image_data || width <= 0 || height <= 0) return false;

  auto iw = static_cast<size_t>(input_width_);
  auto ih = static_cast<size_t>(input_height_);
  input_data.resize(iw * ih);

  // Simple bilinear-ish resize to grayscale + normalize to [-1, 1]
  auto sw = static_cast<size_t>(width);
  for (size_t y = 0; y < ih; ++y) {
    for (size_t x = 0; x < iw; ++x) {
      size_t src_x = x * static_cast<size_t>(width) / iw;
      size_t src_y = y * static_cast<size_t>(height) / ih;
      if (src_x >= sw) src_x = sw - 1;
      if (src_y >= static_cast<size_t>(height))
        src_y = static_cast<size_t>(height) - 1;

      // BGR → grayscale: 0.114*B + 0.587*G + 0.299*R
      size_t src_idx = (src_y * sw + src_x) * 3;
      float gray = 0.114F * static_cast<float>(image_data[src_idx]) +
                   0.587F * static_cast<float>(image_data[src_idx + 1]) +
                   0.299F * static_cast<float>(image_data[src_idx + 2]);

      input_data[y * iw + x] = (gray / 127.5F) - 1.0F;
    }
  }

  input_shape = {1, 1, static_cast<int64_t>(input_height_),
                 static_cast<int64_t>(input_width_)};
  return true;
}

bool LprOcrAdapter::PostProcess(const std::vector<float>& output_data,
                                const TensorShape& output_shape,
                                int original_width, int original_height,
                                float confidence_threshold,
                                float /*nms_threshold*/,
                                std::vector<Detection>& detections) {
  if (output_shape.size() < 2) return false;

  // CRNN output: [T, num_classes] or [1, T, num_classes]
  int time_steps = 0;
  int num_classes = 0;
  if (output_shape.size() == 2) {
    time_steps = static_cast<int>(output_shape[0]);
    num_classes = static_cast<int>(output_shape[1]);
  } else if (output_shape.size() == 3) {
    time_steps = static_cast<int>(output_shape[1]);
    num_classes = static_cast<int>(output_shape[2]);
  } else {
    return false;
  }

  float avg_conf = 0.0F;
  std::string plate_text = CtcGreedyDecode(output_data, time_steps, num_classes,
                                           charset_, &avg_conf);

  if (plate_text.empty() || avg_conf < confidence_threshold) {
    return true;
  }

  Detection det;
  det.x1 = 0;
  det.y1 = 0;
  det.x2 = static_cast<float>(original_width);
  det.y2 = static_cast<float>(original_height);
  det.confidence = avg_conf;
  det.class_id = 0;
  det.class_name = plate_text;

  detections.push_back(std::move(det));
  return true;
}

std::string LprOcrAdapter::CtcGreedyDecode(
    const std::vector<float>& logits, int time_steps, int num_classes,
    const std::vector<std::string>& charset, float* avg_confidence) {
  if (time_steps <= 0 || num_classes <= 0) return {};
  if (logits.size() < static_cast<size_t>(time_steps * num_classes)) return {};

  constexpr int kBlankIdx = 0;
  std::string result;
  int prev_idx = kBlankIdx;
  float total_conf = 0.0F;
  int char_count = 0;

  for (int t = 0; t < time_steps; ++t) {
    size_t offset = static_cast<size_t>(t) * static_cast<size_t>(num_classes);
    const float* row = logits.data() + offset;

    // Softmax + argmax
    float max_val = *std::max_element(row, row + num_classes);
    float sum_exp = 0.0F;
    int best_idx = 0;
    float best_val = -1e9F;

    for (int c = 0; c < num_classes; ++c) {
      float exp_val = std::exp(row[c] - max_val);
      sum_exp += exp_val;
      if (row[c] > best_val) {
        best_val = row[c];
        best_idx = c;
      }
    }

    float prob = std::exp(best_val - max_val) / sum_exp;

    // CTC rules: skip blank, skip repeat of same character
    if (best_idx != kBlankIdx && best_idx != prev_idx) {
      auto char_idx = static_cast<size_t>(best_idx);
      if (char_idx < charset.size()) {
        result += charset[char_idx];
      }
      total_conf += prob;
      char_count++;
    }

    prev_idx = best_idx;
  }

  if (avg_confidence) {
    *avg_confidence =
        (char_count > 0) ? (total_conf / static_cast<float>(char_count)) : 0.0F;
  }

  return result;
}

void LprOcrAdapter::SetCharset(std::vector<std::string> charset) {
  charset_ = std::move(charset);
}

std::vector<std::string> LprOcrAdapter::DefaultChineseCharset() {
  return {
      "",  // index 0 = CTC blank
      "京", "津", "沪", "渝", "冀", "豫", "云", "辽", "黑", "湘", "皖", "鲁",
      "新", "苏", "浙", "赣", "鄂", "桂", "甘", "晋", "蒙", "陕", "吉", "闽",
      "贵", "粤", "川", "青", "藏", "琼", "宁", "0",  "1",  "2",  "3",  "4",
      "5",  "6",  "7",  "8",  "9",  "A",  "B",  "C",  "D",  "E",  "F",  "G",
      "H",  "J",  "K",  "L",  "M",  "N",  "P",  "Q",  "R",  "S",  "T",  "U",
      "V",  "W",  "X",  "Y",  "Z",  "港", "澳", "学", "警", "挂",
  };
}

}  // namespace loong::ai_engine
