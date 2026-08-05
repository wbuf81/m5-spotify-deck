#pragma once

// Word wrapping measured against the font currently selected on the display.

#include <cstddef>

constexpr int WRAP_MAX_LINE = 48;

// Fills out[] with up to max_lines wrapped lines and returns how many were
// used. Hard-breaks any single word too long to fit, and marks overflow with
// "..." so a long title truncates visibly rather than spilling out of its
// column. Set the desired font on the display before calling.
int wrapText(const char *text, int max_w, char out[][WRAP_MAX_LINE],
             int max_lines);
