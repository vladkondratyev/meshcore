#pragma once

#include <stdint.h>

// One bitmap face of the e-book reader. Glyph i draws Windows-1251 byte 0x20 + i.
// A glyph bitmap is `height` rows of ceil(w / 8) bytes, MSB first, 1 = ink.
// It is drawn at pen_x + xo; the pen then moves by adv.
struct ReaderFace {
  const char* name;
  uint8_t height;    // rows per glyph, top of line to bottom of descenders
  uint8_t ascent;    // baseline row
  uint8_t line;      // line pitch in pixels
  const uint8_t* adv;
  const int8_t* xo;
  const uint8_t* w;
  const uint16_t* off;
  const uint8_t* bits;
};
