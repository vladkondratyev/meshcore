// PC preview and self-test of the e-book page layout.
//
//   c++ -std=c++17 -O2 -I src/helpers/reader tools/reader_preview/reader_preview.cpp -o reader_preview
//   ./reader_preview book.txt out_dir [face 0..3] [pages]
//
// Converts a UTF-8 text to Windows-1251 as the upload page does, renders the
// first pages to out_dir/page_NN.pbm (250x122, the MeshPocket panel), then
// walks the whole book forward and back and reports pages that do not match.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "ReaderFont.h"
#include "ReaderLayout.h"
#include "Cp1251.h"

class MemText : public ReaderText {
public:
  std::vector<uint8_t> data;
  uint32_t size() override { return (uint32_t)data.size(); }
  int read(uint32_t pos, uint8_t* dst, int n) override {
    if (pos >= data.size()) return 0;
    int m = (int)std::min<size_t>(n, data.size() - pos);
    memcpy(dst, data.data() + pos, m);
    return m;
  }
};

static std::vector<uint8_t> utf8To1251(const std::string& s) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < s.size();) {
    uint8_t c = s[i];
    uint32_t cp; int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += n;
    if (cp == '\r') continue;
    uint8_t b = unicodeToCp1251(cp);
    if (b == 0 && cp != 0) b = '?';
    out.push_back(b);
  }
  return out;
}

static void savePbm(const std::string& path, const ReaderCanvas& cv) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P4\n%d %d\n", cv.w, cv.h);
  fwrite(cv.bits, 1, (size_t)cv.stride * cv.h, f);
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s book.txt out_dir [face] [pages]\n", argv[0]);
    return 2;
  }
  FILE* f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  std::string raw;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
  fclose(f);
  std::string out_dir = argv[2];
  int face = argc > 3 ? atoi(argv[3]) : 1;
  int pages = argc > 4 ? atoi(argv[4]) : 6;
  if (face < 0 || face >= READER_FACE_COUNT) face = 1;

  MemText text;
  text.data = utf8To1251(raw);
  ReaderCache cache;
  cache.attach(&text);

  const int W = 250, H = 122;
  std::vector<uint8_t> bits((W + 7) / 8 * H);
  ReaderCanvas cv{bits.data(), W, H, (W + 7) / 8};
  ReaderLayout lay;
  lay.face = READER_FACES[face];
  int bx, by;
  ReaderLayout::textBox(W, H, bx, by, lay.width, lay.height);

  // forward walk over the whole book
  std::vector<uint32_t> starts;
  uint32_t pos = 0;
  ReaderPage page;
  while (pos < text.size()) {
    lay.layoutPage(cache, pos, page);
    starts.push_back(page.start);
    if (page.next <= pos) { fprintf(stderr, "stuck at %u\n", pos); return 1; }
    pos = page.next;
  }
  printf("face %s: %zu bytes, %zu pages, %d lines/page\n", lay.face->name, text.data.size(), starts.size(), lay.linesPerPage());

  // back walk: the page before each page must be full and lead exactly to it.
  // A blank line at a page border allows two valid splits, so the page may
  // differ from the forward walk (the device keeps a history for that case).
  int bad = 0, shifted = 0;
  for (size_t i = 1; i < starts.size(); i++) {
    uint32_t p = lay.prevPageStart(cache, starts[i]);
    ReaderPage pp, cur, after;
    lay.layoutPage(cache, p, pp);
    lay.layoutPage(cache, starts[i], cur);
    lay.layoutPage(cache, pp.next, after);
    bool leads = after.start == cur.start;
    bool full = pp.count == lay.linesPerPage() || pp.start == 0;
    if (!leads || !full || p >= starts[i]) {
      if (bad < 5) printf("  bad back step at page %zu: got %u (count %d), forward %u\n", i, p, pp.count, starts[i - 1]);
      bad++;
    } else if (p != starts[i - 1]) {
      shifted++;
    }
  }
  printf("back walk: %d bad, %d shifted by a blank line\n", bad, shifted);

  for (int i = 0; i < pages && i < (int)starts.size(); i++) {
    cv.clear();
    lay.layoutPage(cache, starts[i], page);
    lay.drawPage(cache, page, cv, bx, by);
    ReaderLayout::drawProgress(cv, page.next, text.size(), i == 1);
    char name[64];
    snprintf(name, sizeof name, "/page_%02d.pbm", i);
    savePbm(out_dir + name, cv);
  }
  return bad ? 3 : 0;
}
