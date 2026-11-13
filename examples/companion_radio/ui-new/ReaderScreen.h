#pragma once

#ifdef BOOK_READER

#include <helpers/ui/UIScreen.h>
#include <helpers/reader/ReaderLayout.h>
#include "../BookStore.h"

class UITask;

// Full-screen e-book reader drawn in panel pixels with its own fonts.
// Single button: click - next page, double - previous, triple - font size,
// long - menu, quad - back to the home pages.
class ReaderScreen : public UIScreen {
  enum Mode : uint8_t { READ, MENU, LIST, CONFIRM, EMPTY };

  UITask* _task;
  BookStore* _store;
  Mode _mode = LIST;
  BookText _text;
  ReaderCache _cache;
  ReaderLayout _lay;
  ReaderPage _page;
  bool _page_valid = false;
  int16_t _book = -1;
  uint32_t _pos = 0;
  uint8_t _face = 1;
  uint32_t _hist[24];   // page starts behind the current page, newest last
  uint8_t _hist_n = 0;
  uint16_t _gen = 0;
  uint8_t _menu_sel = 0;
  uint8_t _list_sel = 0;
  uint8_t _list_top = 0;
  uint16_t _turns = 0;
  bool _full_next = true;
  bool _dirty = false;
  unsigned long _dirty_at = 0;

  bool openBook(uint8_t id);
  void closeBook();
  void saveBookmark();
  void invalidatePage() { _page_valid = false; }
  void ensurePage();
  void nextPage();
  void prevPage();
  void cycleFont();
  void markTurned();
  void showList();
  void exitReader();

  void drawRead(ReaderCanvas& cv);
  void drawMenu(ReaderCanvas& cv);
  void drawList(ReaderCanvas& cv);
  void drawConfirm(ReaderCanvas& cv);
  void drawEmpty(ReaderCanvas& cv);
  int drawLabel(ReaderCanvas& cv, const ReaderFace* face, const char* utf8, int x, int y);
  void drawFitted(ReaderCanvas& cv, const char* cp1251, int x, int y, int max_w);

  bool menuInput(char c);
  bool listInput(char c);

public:
  ReaderScreen(UITask* task, BookStore* store);
  void enter();               // from the Books page
  bool isReading() const { return _mode == READ; }
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;
  void poll() override;
};

#endif
