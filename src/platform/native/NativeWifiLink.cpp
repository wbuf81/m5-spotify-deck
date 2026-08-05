// The host is already on a network; there is nothing to associate with.
#if defined(EMULATOR)

#include "../../net/WifiLink.h"

void WifiLink::begin(const char *, const char *) {
  status_ = LinkStatus::Online;
}

bool WifiLink::ensureConnected(uint32_t) {
  status_ = LinkStatus::Online;
  return true;
}

#endif  // EMULATOR
