// Copyright 2026 Loong AI NVR Project

#include "overlay/ft_text_renderer/ft_text_renderer.h"

#include <algorithm>
#include <cstring>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "spdlog/spdlog.h"

namespace loong::overlay {

struct FtTextRenderer::Impl {
  FT_Library library = nullptr;
  FT_Face face = nullptr;
  int pixel_size = 20;
  bool ready = false;
};

FtTextRenderer::FtTextRenderer() : impl_(std::make_unique<Impl>()) {
  if (FT_Init_FreeType(&impl_->library) != 0) {
    spdlog::error("FtTextRenderer: failed to init FreeType");
    impl_->library = nullptr;
  }
}

FtTextRenderer::~FtTextRenderer() {
  if (impl_->face) FT_Done_Face(impl_->face);
  if (impl_->library) FT_Done_FreeType(impl_->library);
}

bool FtTextRenderer::LoadFont(const std::string& font_path, int pixel_size) {
  if (!impl_->library) return false;

  if (impl_->face) {
    FT_Done_Face(impl_->face);
    impl_->face = nullptr;
  }

  if (FT_New_Face(impl_->library, font_path.c_str(), 0, &impl_->face) != 0) {
    spdlog::error("FtTextRenderer: failed to load font '{}'", font_path);
    return false;
  }

  impl_->pixel_size = pixel_size;
  FT_Set_Pixel_Sizes(impl_->face, 0, static_cast<FT_UInt>(pixel_size));
  impl_->ready = true;
  spdlog::info("FtTextRenderer: loaded '{}' ({}px)", font_path, pixel_size);
  return true;
}

bool FtTextRenderer::IsReady() const { return impl_->ready; }

void FtTextRenderer::SetPixelSize(int pixel_size) {
  if (!impl_->face) return;
  impl_->pixel_size = pixel_size;
  FT_Set_Pixel_Sizes(impl_->face, 0, static_cast<FT_UInt>(pixel_size));
}

// Decode one UTF-8 code point; advance *pos past the consumed bytes.
static uint32_t DecodeUtf8(const std::string& s, size_t& pos) {
  auto c = static_cast<uint8_t>(s[pos]);
  uint32_t cp = 0;
  int extra = 0;

  if (c < 0x80) {
    cp = c;
  } else if ((c & 0xE0) == 0xC0) {
    cp = c & 0x1F;
    extra = 1;
  } else if ((c & 0xF0) == 0xE0) {
    cp = c & 0x0F;
    extra = 2;
  } else if ((c & 0xF8) == 0xF0) {
    cp = c & 0x07;
    extra = 3;
  } else {
    ++pos;
    return 0xFFFD;  // replacement char
  }

  ++pos;
  for (int i = 0; i < extra && pos < s.size(); ++i, ++pos) {
    cp = (cp << 6) | (static_cast<uint8_t>(s[pos]) & 0x3F);
  }
  return cp;
}

void FtTextRenderer::MeasureText(const std::string& text,
                                  int& out_width, int& out_height) const {
  out_width = 0;
  out_height = impl_->pixel_size;
  if (!impl_->ready) return;

  FT_Face face = impl_->face;
  size_t pos = 0;
  int pen_x = 0;

  while (pos < text.size()) {
    uint32_t cp = DecodeUtf8(text, pos);
    if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT) != 0) continue;
    pen_x += static_cast<int>(face->glyph->advance.x >> 6);
  }

  out_width = pen_x;
  out_height = impl_->pixel_size;
}

void FtTextRenderer::RenderText(uint8_t* data, int width, int height,
                                 int stride, const std::string& text,
                                 int x, int y,
                                 uint8_t r, uint8_t g, uint8_t b) const {
  if (!impl_->ready || !data) return;

  FT_Face face = impl_->face;
  int pen_x = x;
  int baseline_y = y + impl_->pixel_size;

  size_t pos = 0;
  while (pos < text.size()) {
    uint32_t cp = DecodeUtf8(text, pos);

    if (FT_Load_Char(face, cp, FT_LOAD_RENDER) != 0) continue;

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap& bmp = slot->bitmap;

    int glyph_x = pen_x + slot->bitmap_left;
    int glyph_y = baseline_y - slot->bitmap_top;

    for (unsigned int row = 0; row < bmp.rows; ++row) {
      int dy = glyph_y + static_cast<int>(row);
      if (dy < 0 || dy >= height) continue;

      for (unsigned int col = 0; col < bmp.width; ++col) {
        int dx = glyph_x + static_cast<int>(col);
        if (dx < 0 || dx >= width) continue;

        uint8_t alpha = bmp.buffer[row * static_cast<unsigned int>(bmp.pitch) + col];
        if (alpha == 0) continue;

        uint8_t* pixel = data + dy * stride + dx * 3;
        if (alpha == 255) {
          pixel[0] = b;
          pixel[1] = g;
          pixel[2] = r;
        } else {
          // Alpha blend
          uint16_t a = alpha;
          uint16_t ia = 255 - a;
          pixel[0] = static_cast<uint8_t>((a * b + ia * pixel[0]) / 255);
          pixel[1] = static_cast<uint8_t>((a * g + ia * pixel[1]) / 255);
          pixel[2] = static_cast<uint8_t>((a * r + ia * pixel[2]) / 255);
        }
      }
    }

    pen_x += static_cast<int>(slot->advance.x >> 6);
  }
}

}  // namespace loong::overlay
