#include "TextWrap.h"

#include <M5Unified.h>

#include <cstdio>
#include <cstring>

int wrapText(const char *text, int max_w, char out[][WRAP_MAX_LINE],
             int max_lines) {
  int nlines = 0;
  const char *p = text;

  while (*p && nlines < max_lines) {
    char buf[WRAP_MAX_LINE] = {};
    int i = 0;
    int last_space = -1;

    while (p[i] && i < WRAP_MAX_LINE - 1) {
      buf[i] = p[i];
      buf[i + 1] = '\0';
      if (M5.Display.textWidth(buf) > max_w) break;
      if (p[i] == ' ') last_space = i;
      ++i;
    }

    int cut;
    if (!p[i] || i >= WRAP_MAX_LINE - 1) {
      cut = i;
    } else if (last_space > 0) {
      cut = last_space;
    } else {
      cut = i > 0 ? i : 1;  // single unbreakable word
    }

    std::memcpy(out[nlines], p, static_cast<size_t>(cut));
    out[nlines][cut] = '\0';
    ++nlines;

    p += cut;
    while (*p == ' ') ++p;
  }

  // Text left over: mark the final line as truncated.
  if (*p && nlines > 0) {
    char *line = out[nlines - 1];
    int len = static_cast<int>(std::strlen(line));
    while (len > 0) {
      char probe[WRAP_MAX_LINE];
      std::snprintf(probe, sizeof(probe), "%.*s...", len, line);
      if (M5.Display.textWidth(probe) <= max_w) {
        std::snprintf(line, WRAP_MAX_LINE, "%s", probe);
        break;
      }
      --len;
    }
  }

  return nlines;
}
