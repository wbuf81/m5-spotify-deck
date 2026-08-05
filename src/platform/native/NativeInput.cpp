// Emulator button input: keyboard stands in for the three front buttons.
//
//   Left  / A  -> Button A   (prev  / hold: volume down)
//   Space / S  -> Button B   (play  / hold: like)
//   Right / D  -> Button C   (next  / hold: volume up)

#if defined(EMULATOR)

#include <SDL.h>

#include <cstdio>
#include <cstdlib>

#include "../../input/Buttons.h"

void readRawButtons(bool pressed[3]) {
  const Uint8 *ks = SDL_GetKeyboardState(nullptr);

  // EMU_INPUT_DEBUG reports whether the keyboard state is reachable from this
  // thread, and echoes presses. Panel_sdl pumps events on its own thread, so
  // "the keymap is readable here" is worth being able to check directly.
  if (std::getenv("EMU_INPUT_DEBUG")) {
    static bool announced = false;
    if (!announced) {
      announced = true;
      std::fprintf(stderr, "[input] SDL keyboard state: %s\n",
                   ks ? "reachable" : "NULL");
    }
    if (ks && (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A] ||
               ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_S] ||
               ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D])) {
      std::fprintf(stderr, "[input] A=%d B=%d C=%d\n",
                   ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A],
                   ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_S],
                   ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]);
    }
  }

  if (!ks) {
    pressed[0] = pressed[1] = pressed[2] = false;
    return;
  }
  pressed[0] = ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A];
  pressed[1] = ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_S];
  pressed[2] = ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D];
}

#endif  // EMULATOR
