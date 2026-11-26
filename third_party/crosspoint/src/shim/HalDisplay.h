// HeySticky shim: CrossPoint's HalDisplay as a plain 1-bit framebuffer. The renderer draws into it
// (800x480 panel-native, MSB first, 0 = ink); HeySticky copies it to Seeed_GFX itself (reader_engine.cpp).
// No grayscale: the renderer falls back to black-and-white dithering.
#pragma once
#include <Arduino.h>
#include <GrayscaleCapabilities.h>
#include <BoardConfig.h>

class HalDisplay {
 public:
  using Controller = BoardConfig::DisplayController;
  using GrayscaleMode = freeink::GrayscaleMode;
  using GrayscaleCapabilities = freeink::GrayscaleCapabilities;
  using GrayscaleBase = freeink::GrayscaleBase;
  using GrayscaleEncoding = freeink::GrayscaleEncoding;
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };

  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  void begin(bool = false) {
    if (!fb) fb = static_cast<uint8_t*>(ps_malloc(BUFFER_SIZE));
    if (!fb) fb = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
    clearScreen();
  }
  Controller getController() const { return Controller{}; }
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode = GrayscaleMode::Overlay) const { return {}; }
  const char* controllerName() const { return nullptr; }
  const char* panelName() const { return nullptr; }
  void setReaderRefresh(bool) {}
  void setOverlayFullTone(bool) {}
  bool displayRetainedGrayscaleOverlay(uint16_t, uint16_t, uint16_t, uint16_t, bool) { return false; }

  void clearScreen(uint8_t color = 0xFF) const { if (fb) memset(fb, color, BUFFER_SIZE); }
  void drawImage(const uint8_t* img, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool = false) const {
    if (!fb || (x & 7)) return;  // callers pass byte-aligned x
    const uint16_t wb = (w + 7) / 8;
    for (uint16_t r = 0; r < h && y + r < DISPLAY_HEIGHT; r++)
      memcpy(fb + (y + r) * DISPLAY_WIDTH_BYTES + x / 8, img + r * wb, min<int>(wb, DISPLAY_WIDTH_BYTES - x / 8));
  }
  void displayBuffer(RefreshMode = FAST_REFRESH, bool = false) {}  // HeySticky refreshes the panel
  void displayBufferAsync(RefreshMode m = FAST_REFRESH) { displayBuffer(m); }
  void waitRefreshComplete() {}
  bool supportsAsyncRefresh() const { return false; }
  void setInverted(bool) {}
  bool isInverted() const { return false; }
  void deepSleep() {}
  void controllerIdle() {}
  uint8_t* getFrameBuffer() const { return fb; }
  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut) {
    if (lent || !fb) return nullptr;
    lent = true;
    if (sizeOut) *sizeOut = BUFFER_SIZE;
    return fb;
  }
  void returnFrameBufferStorage() { lent = false; clearScreen(); }
  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
  void displayGrayscaleBase(RefreshMode = HALF_REFRESH, bool = false) {}
  bool displayGrayscaleBase(GrayscaleMode, RefreshMode = HALF_REFRESH, bool = false) { return false; }
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t*) {}
  void displayGrayBuffer(bool = false) {}
  void grayscaleRevert() {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return BUFFER_SIZE; }

 private:
  uint8_t* fb = nullptr;
  bool lent = false;
};
