// Emulator button input: keyboard stands in for the three front buttons.
//
//   Left  / A  -> Button A   (prev  / hold: volume down)
//   Space / S  -> Button B   (play  / hold: like)
//   Right / D  -> Button C   (next  / hold: volume up)

#if defined(EMULATOR)

#include <SDL.h>

#include "../../input/Buttons.h"

void readRawButtons(bool pressed[3]) {
  const Uint8 *ks = SDL_GetKeyboardState(nullptr);
  if (!ks) {
    pressed[0] = pressed[1] = pressed[2] = false;
    return;
  }
  pressed[0] = ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A];
  pressed[1] = ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_S];
  pressed[2] = ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D];
}

#endif  // EMULATOR
