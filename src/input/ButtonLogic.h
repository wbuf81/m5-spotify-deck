#pragma once

// Tap / long-press / hold-repeat, as a pure state machine.
//
// Deliberately takes raw pressed-flags and a timestamp rather than reading
// hardware, so it is exercised by host tests with synthetic timing instead of by
// pressing buttons and squinting.

#include <cstdint>

enum class BtnEvent : uint8_t {
  None,
  Tap,
  LongStart,
  LongRepeat,
  LongEnd,
};

constexpr uint32_t LONG_PRESS_MS = 500;
constexpr uint32_t HOLD_REPEAT_MS = 150;

class ButtonLogic {
 public:
  // Feed the current physical state; returns at most one event per call.
  BtnEvent update(bool pressed, uint32_t now_ms) {
    if (pressed && !was_pressed_) {
      was_pressed_ = true;
      down_at_ = now_ms;
      long_fired_ = false;
      return BtnEvent::None;
    }

    if (pressed && was_pressed_) {
      if (!long_fired_ && (now_ms - down_at_) >= LONG_PRESS_MS) {
        long_fired_ = true;
        last_repeat_ = now_ms;
        return BtnEvent::LongStart;
      }
      if (long_fired_ && (now_ms - last_repeat_) >= HOLD_REPEAT_MS) {
        last_repeat_ = now_ms;
        return BtnEvent::LongRepeat;
      }
      return BtnEvent::None;
    }

    if (!pressed && was_pressed_) {
      was_pressed_ = false;
      return long_fired_ ? BtnEvent::LongEnd : BtnEvent::Tap;
    }

    return BtnEvent::None;
  }

  bool isDown() const { return was_pressed_; }

 private:
  bool was_pressed_ = false;
  bool long_fired_ = false;
  uint32_t down_at_ = 0;
  uint32_t last_repeat_ = 0;
};
