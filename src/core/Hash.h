#pragma once

// FNV-1a, the project's one string hash.
//
// Everything that wants a stable per-track choice — which view, which scene,
// Daisy's activity, the NES world number — hashes the track id through this.
// It lived as four separate copies before moving here; if it ever needs to
// change, it changes once, and every consumer keeps agreeing on the answer.

#include <cstdint>

inline uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  while (s && *s) {
    h ^= static_cast<uint8_t>(*s++);
    h *= 16777619u;
  }
  return h;
}
