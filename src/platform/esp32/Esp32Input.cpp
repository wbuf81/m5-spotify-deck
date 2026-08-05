// Device button input: the three tactile buttons under the screen.
//
// M5.update() is called once per loop in main; these read the state it latched.

#if !defined(EMULATOR)

#include <M5Unified.h>

#include "../../input/Buttons.h"

void readRawButtons(bool pressed[3]) {
  pressed[0] = M5.BtnA.isPressed();
  pressed[1] = M5.BtnB.isPressed();
  pressed[2] = M5.BtnC.isPressed();
}

#endif  // !EMULATOR
