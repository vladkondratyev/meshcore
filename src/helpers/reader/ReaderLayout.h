#pragma once

// E-book page layout, independent of Arduino so it also builds on a PC
// (tools/reader_preview). Text is Windows-1251, one byte per character:
//   '\n'  paragraph end          0xA0  no-break space
//   0xAD  soft hyphen (a break that draws '-')
// Positions are byte offsets into the book, so a bookmark survives a font change.

#include <stdint.h>
#include <string.h>
#include "ReaderTypes.h"

// Heavy routines stay out of line: inlined copies cost kilobytes of flash.
#if defined(__GNUC__)
  #define READER_NOINLINE __attribute__((noinline))
#else
  #define READER_NOINLINE
#endif

#ifndef READER_CACHE_SIZE
  #define READER_CACHE_SIZE 4096
#endif
#ifndef READER_MAX_LINES
  #define READER_MAX_LINES 12
#endif

class ReaderText {
public:
  virtual ~ReaderText() {}
  virtual uint32_t size() = 0;
  virtual int read(uint32_t pos, uint8_t* dst, int n) = 0;   // bytes read, 0 at the end
};

// Window over the book. Refills keep a quarter behind the requested byte, so
// short backward steps (previous page) usually stay inside the window.
class ReaderCache {
  ReaderText* _src = nullptr;
  uint8_t _buf[READER_CACHE_SIZE];
  uint32_t _start = 0;
  uint32_t _len = 0;
  uint32_t _size = 0;
public:
  void attach(ReaderText* src) {
    _src = src;
    _start = _len = 0;
    _size = src ? src->size() : 0;
  }
  void invalidate() { _start = _len = 0; _size = _src ? _src->size() : 0; }
  uint32_t size() const { return _size; }
  inline int get(uint32_t pos) {
    if (pos - _start < _len) return _buf[pos - _start];
    return refill(pos);
  }
  READER_NOINLINE int refill(uint32_t pos) {
    if (pos >= _size || !_src) return -1;
    _start = pos > READER_CACHE_SIZE / 4 ? pos - READER_CACHE_SIZE / 4 : 0;
    int n = _src->read(_start, _buf, READER_CACHE_SIZE);
    _len = n > 0 ? (uint32_t)n : 0;
    if (pos - _start >= _len) return -1;
    return _buf[pos - _start];
  }
};

// 1-bit canvas, MSB first, 1 = black ink.
struct ReaderCanvas {
  uint8_t* bits;
  int w, h, stride;
  void clear() { memset(bits, 0, (size_t)stride * h); }
  void set(int x, int y) {
    if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) bits[y * stride + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
  }
  READER_NOINLINE void fill(int x, int y, int fw, int fh) {
    for (int j = 0; j < fh; j++) for (int i = 0; i < fw; i++) set(x + i, y + j);
  }
  void hline(int x, int y, int len) { fill(x, y, len, 1); }
};

struct ReaderLine {
  uint32_t start, end;   // bytes drawn: [start, end)
  int16_t indent;        // first-line indent, px
  int16_t width;         // natural width including indent and hyphen, px
  bool hyphen;           // ends at a soft hyphen: draw '-'
  bool last;             // last line of a paragraph (never justified)
};

struct ReaderPage {
  uint32_t start, next;
  uint8_t count;
  ReaderLine lines[READER_MAX_LINES];
};

class ReaderLayout {
public:
  const ReaderFace* face = nullptr;
  int width = 0, height = 0;   // text box, px
  bool justify = false;

  static bool isSpace(int b) { return b == ' ' || b == '\t'; }

  static int glyphIndex(int b) {
    if (b == 0xA0 || b == '\t') return 0;   // draws as a space
    if (b < 0x20) return -1;
    return b - 0x20;
  }

  int adv(int b) const {
    if (b == 0xAD || b == '\r') return 0;
    int g = glyphIndex(b);
    return g < 0 ? 0 : face->adv[g];
  }
  int indentPx() const { return face->height * 3 / 4; }   // about one em

  // Lays out one line starting at `pos`. Returns the start of the next line.
  READER_NOINLINE uint32_t layoutLine(ReaderCache& c, uint32_t pos, ReaderLine& line) const {
    uint32_t size = c.size();
    bool para_start = pos == 0 || c.get(pos - 1) == '\n';
    // drop the spaces left at a wrap point (and leading spaces of a paragraph)
    while (pos < size && isSpace(c.get(pos))) pos++;
    line.start = pos;
    line.indent = 0;
    line.hyphen = false;
    line.last = false;
    if (pos >= size) { line.end = pos; line.width = 0; line.last = true; return pos; }
    if (para_start && c.get(pos) != '\n') line.indent = (int16_t)indentPx();

    int x = line.indent;
    const int hyph = adv('-');
    bool have_brk = false;
    uint32_t brk_end = 0, brk_next = 0;
    int brk_w = 0;
    bool brk_hyph = false;

    uint32_t i = pos;
    while (i < size) {
      int b = c.get(i);
      if (b < 0) break;
      if (b == '\n') {
        line.end = i; line.width = (int16_t)x; line.last = true;
        return i + 1;
      }
      if (isSpace(b)) {               // break before the space
        have_brk = true; brk_end = i; brk_w = x; brk_hyph = false;
        uint32_t j = i;
        while (j < size && isSpace(c.get(j))) j++;
        brk_next = j;
      } else if (b == 0xAD) {          // soft hyphen
        if (x + hyph <= width) {
          have_brk = true; brk_end = i; brk_w = x + hyph; brk_hyph = true; brk_next = i + 1;
        }
        i++;
        continue;
      }
      int a = adv(b);
      if (x + a > width && !isSpace(b)) {
        if (have_brk) {
          line.end = brk_end; line.width = (int16_t)brk_w; line.hyphen = brk_hyph;
          return brk_next;
        }
        if (i == pos) i++;             // a glyph wider than the line: take it anyway
        line.end = i; line.width = (int16_t)x;
        return i;
      }
      x += a;
      i++;
      if ((b == '-' || b == 0x96 || b == 0x97) && x <= width) {   // break after a dash
        have_brk = true; brk_end = i; brk_w = x; brk_hyph = false; brk_next = i;
      }
    }
    line.end = i; line.width = (int16_t)x; line.last = true;
    return i;
  }

  READER_NOINLINE void layoutPage(ReaderCache& c, uint32_t start, ReaderPage& page) const {
    uint32_t size = c.size();
    if (start > size) start = size;
    page.start = start;
    page.count = 0;
    uint32_t pos = start;
    int y = 0;
    while (pos < size && page.count < READER_MAX_LINES && y + face->height <= height) {
      ReaderLine& ln = page.lines[page.count];
      uint32_t next = layoutLine(c, pos, ln);
      if (next <= pos) next = pos + 1;
      // an empty paragraph at the top of a page would only waste a line
      if (page.count == 0 && ln.end == ln.start && ln.last) {
        pos = next;
        page.start = pos;
        continue;
      }
      page.count++;
      pos = next;
      y += face->line;
    }
    page.next = pos;
  }

  READER_NOINLINE uint32_t nextPageStart(ReaderCache& c, uint32_t start) const {
    ReaderPage p;
    layoutPage(c, start, p);
    return p.next;
  }

  int linesPerPage() const {
    if (!face || height < face->height) return 1;
    int n = (height - face->height) / face->line + 1;
    return n > READER_MAX_LINES ? READER_MAX_LINES : n;
  }

  // Start of the page that ends at `pos`. Line breaks depend only on where a
  // line starts, so the lines are laid out again from the start of a paragraph
  // (at most `back` bytes behind) and the page takes the last lines before pos.
  READER_NOINLINE uint32_t prevPageStart(ReaderCache& c, uint32_t pos, uint32_t back = 4000) const {
    if (pos == 0) return 0;
    uint32_t s = prevByLines(c, pos, back);
    if (nextPageStart(c, s) == pos) return s;
    // blank lines right before pos may have been skipped at the top of this page
    uint32_t p2 = pos;
    while (p2 >= 2 && c.get(p2 - 1) == '\n' && c.get(p2 - 2) == '\n') p2--;
    if (p2 != pos && p2 > 0) {
      uint32_t s2 = prevByLines(c, p2, back);
      if (nextPageStart(c, s2) == pos) return s2;
    }
    return s;
  }

private:
  READER_NOINLINE uint32_t prevByLines(ReaderCache& c, uint32_t pos, uint32_t back) const {
    uint32_t from = pos > back ? pos - back : 0;
    uint32_t anchor = from;
    if (from > 0) {
      uint32_t j = from;
      while (j < pos && c.get(j - 1) != '\n') j++;
      if (j < pos) anchor = j;
    }
    const int want = linesPerPage();
    uint32_t ring[READER_MAX_LINES];
    int n = 0;
    uint32_t q = anchor;
    while (q < pos) {
      ReaderLine ln;
      uint32_t next = layoutLine(c, q, ln);
      if (next <= q) next = q + 1;
      // an empty paragraph would be dropped at the top of the page; don't count it
      if (!(ln.end == ln.start && ln.last && n == 0)) ring[n++ % want] = q;
      q = next;
    }
    if (n == 0) return anchor;
    uint32_t start = n < want ? ring[0] : ring[n % want];
    // a blank line skipped at the top lets the page reach past pos; drop a line
    for (int guard = 0; guard < want && start < pos && nextPageStart(c, start) > pos; guard++) {
      ReaderLine ln;
      start = layoutLine(c, start, ln);
    }
    return start < pos ? start : anchor;
  }

public:

  READER_NOINLINE void drawGlyph(ReaderCanvas& cv, int b, int x, int top) const {
    int g = glyphIndex(b);
    if (g <= 0) return;   // control or space
    int w = face->w[g];
    if (w == 0) return;
    int rowb = (w + 7) >> 3;
    const uint8_t* bits = face->bits + face->off[g];
    int x0 = x + face->xo[g];
    for (int r = 0; r < face->height; r++) {
      const uint8_t* row = bits + r * rowb;
      for (int col = 0; col < w; col++) {
        if (row[col >> 3] & (0x80 >> (col & 7))) cv.set(x0 + col, top + r);
      }
    }
  }

  READER_NOINLINE void drawPage(ReaderCache& c, const ReaderPage& page, ReaderCanvas& cv, int ox, int oy) const {
    int y = oy;
    for (int li = 0; li < page.count; li++) {
      const ReaderLine& ln = page.lines[li];
      // justification: spread the slack over the gaps between words
      int gaps = 0;
      if (justify && !ln.last) {
        for (uint32_t i = ln.start; i < ln.end; i++) if (isSpace(c.get(i))) gaps++;
      }
      int slack = width - ln.width;
      if (gaps == 0 || slack <= 0 || slack > gaps * adv(' ') * 2) gaps = 0;
      int x = ox + ln.indent, gap_i = 0;
      for (uint32_t i = ln.start; i < ln.end; i++) {
        int b = c.get(i);
        if (b < 0) break;
        drawGlyph(cv, b, x, y);
        x += adv(b);
        if (gaps && isSpace(b)) {
          gap_i++;
          x += slack * gap_i / gaps - slack * (gap_i - 1) / gaps;
        }
      }
      if (ln.hyphen) drawGlyph(cv, '-', x, y);
      y += face->line;
    }
  }

  // Page geometry shared by the device and the PC preview.
  static void textBox(int W, int H, int& x, int& y, int& w, int& h) {
    x = 4; y = 1; w = W - 8; h = H - 1 - 6;
  }

  // Bottom strip: dotted track, solid progress, envelope when messages wait.
  READER_NOINLINE static void drawProgress(ReaderCanvas& cv, uint32_t pos, uint32_t size, bool unread) {
    int y = cv.h - 3;
    int x0 = 4, x1 = cv.w - (unread ? 16 : 4);
    for (int x = x0; x < x1; x += 3) cv.set(x, y + 1);
    int len = size ? (int)((int64_t)(x1 - x0) * pos / size) : 0;
    if (len > 0) cv.fill(x0, y, len, 2);
    if (unread) {                           // 9x6 envelope
      int ex = cv.w - 12, ey = cv.h - 7;
      cv.hline(ex, ey, 9); cv.hline(ex, ey + 5, 9);
      cv.fill(ex, ey, 1, 6); cv.fill(ex + 8, ey, 1, 6);
      for (int k = 0; k < 4; k++) { cv.set(ex + 1 + k, ey + 1 + k); cv.set(ex + 7 - k, ey + 1 + k); }
    }
  }

  // Width of a 1251 string, for menus.
  READER_NOINLINE int textWidth(const char* s) const {
    int x = 0;
    for (; *s; s++) x += adv((uint8_t)*s);
    return x;
  }
  READER_NOINLINE int drawText(ReaderCanvas& cv, const char* s, int x, int top) const {
    for (; *s; s++) {
      int b = (uint8_t)*s;
      drawGlyph(cv, b, x, top);
      x += adv(b);
    }
    return x;
  }
};
