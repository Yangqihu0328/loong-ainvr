// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_OVERLAY_FT_TEXT_RENDERER_FT_TEXT_RENDERER_H_
#define LOONG_OVERLAY_FT_TEXT_RENDERER_FT_TEXT_RENDERER_H_

#include <cstdint>
#include <memory>
#include <string>

namespace loong::overlay {

/// FreeType-based text renderer that supports UTF-8 / CJK characters.
/// Renders text directly onto BGR24 pixel buffers.
class FtTextRenderer {
 public:
  FtTextRenderer();
  ~FtTextRenderer();

  /// Load a TrueType/OpenType font from the given file path.
  /// @param font_path  Path to a .ttf/.otf/.ttc font file.
  /// @param pixel_size Default glyph pixel height.
  /// @return true if loaded successfully.
  bool LoadFont(const std::string& font_path, int pixel_size = 20);

  /// Check whether a font is loaded and ready to render.
  bool IsReady() const;

  /// Render UTF-8 text onto a BGR24 buffer.
  /// @param data   BGR24 pixel data.
  /// @param width  Frame width.
  /// @param height Frame height.
  /// @param stride Row stride in bytes.
  /// @param text   UTF-8 encoded string.
  /// @param x      Left edge of the text baseline.
  /// @param y      Top edge of the text bounding box.
  /// @param r,g,b  Text color (0–255).
  void RenderText(uint8_t* data, int width, int height, int stride,
                  const std::string& text, int x, int y,
                  uint8_t r = 255, uint8_t g = 255, uint8_t b = 255) const;

  /// Measure the bounding box of a UTF-8 string without rendering.
  /// @param text UTF-8 encoded string.
  /// @param[out] out_width  Width in pixels.
  /// @param[out] out_height Height in pixels.
  void MeasureText(const std::string& text,
                   int& out_width, int& out_height) const;

  /// Change pixel size (re-sets the current face size).
  void SetPixelSize(int pixel_size);

  FtTextRenderer(const FtTextRenderer&) = delete;
  FtTextRenderer& operator=(const FtTextRenderer&) = delete;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace loong::overlay

#endif  // LOONG_OVERLAY_FT_TEXT_RENDERER_FT_TEXT_RENDERER_H_
