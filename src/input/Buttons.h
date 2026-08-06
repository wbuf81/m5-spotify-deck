#pragma once

// Platform-independent view of the three front buttons.
//
// readRawButtons() is the only part that differs between hardware and the
// emulator; everything above it is shared.

#include <cstdint>

#include "ButtonLogic.h"

enum class Btn : uint8_t { A = 0, B = 1, C = 2 };

// Fills pressed[3] from M5.BtnA/B/C on hardware, or the keyboard in the
// emulator. Implemented per platform.
void readRawButtons(bool pressed[3]);

class Buttons {
 public:
  void update(uint32_t now_ms) {
    bool pressed[3] = {false, false, false};
    readRawButtons(pressed);
    for (int i = 0; i < 3; ++i) {
      ev_[i] = logic_[i].update(pressed[i], now_ms);
    }
  }

  BtnEvent event(Btn b) const { return ev_[static_cast<int>(b)]; }
  bool isDown(Btn b) const { return logic_[static_cast<int>(b)].isDown(); }

  bool anyActivity() const {
    for (int i = 0; i < 3; ++i) {
      if (ev_[i] != BtnEvent::None || logic_[i].isDown()) return true;
    }
    return false;
  }

 private:
  ButtonLogic logic_[3];
  BtnEvent ev_[3] = {BtnEvent::None, BtnEvent::None, BtnEvent::None};
};
