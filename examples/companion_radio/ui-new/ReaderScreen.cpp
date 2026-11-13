#ifdef BOOK_READER

#include "ReaderScreen.h"
#include "UITask.h"
#include <helpers/reader/ReaderFont.h>
#include <helpers/reader/Cp1251.h>

#ifndef READER_FULL_REFRESH_EVERY
  #define READER_FULL_REFRESH_EVERY 10   // page turns between full e-ink refreshes
#endif
#define READER_SAVE_DELAY_MS  20000       // bookmark goes to flash after this pause
#define READER_MAX_W 256
#define READER_MAX_H 128

static uint8_t canvas_bits[(READER_MAX_W / 8) * READER_MAX_H];

enum { MI_CONTINUE, MI_FONT, MI_ALIGN, MI_LIST, MI_START, MI_EXIT, MI_COUNT };

static void frameRect(ReaderCanvas& cv, int x, int y, int w, int h) {
  cv.hline(x, y, w);
  cv.hline(x, y + h - 1, w);
  cv.fill(x, y, 1, h);
  cv.fill(x + w - 1, y, 1, h);
}

ReaderScreen::ReaderScreen(UITask* task, BookStore* store) : _task(task), _store(store) {
  _lay.face = READER_FACES[_face];
}

void ReaderScreen::enter() {
  _full_next = true;
  _gen = _store->generation();
  int16_t cur = _store->current();
  if (cur >= 0 && openBook((uint8_t)cur)) {
    _mode = READ;
  } else {
    showList();
  }
}

bool ReaderScreen::openBook(uint8_t id) {
  const BookEntry* e = _store->find(id);
  if (!e) return false;
  if (_book >= 0 && _book != id) saveBookmark();
  _book = id;
  _text.open(_store, id);
  _cache.attach(&_text);
  _face = e->face < READER_FACE_COUNT ? e->face : 1;
  _lay.face = READER_FACES[_face];
  _lay.justify = (e->flags & 1) != 0;
  _pos = e->pos < e->size ? e->pos : 0;
  _hist_n = 0;
  _turns = 0;
  invalidatePage();
  _store->setCurrent(id);
  _full_next = true;
  return true;
}

void ReaderScreen::closeBook() {
  _book = -1;
  _cache.attach(nullptr);
  _hist_n = 0;
  invalidatePage();
}

void ReaderScreen::saveBookmark() {
  if (_book < 0) return;
  uint32_t pos = _page_valid ? _page.start : _pos;
  _store->setBookmark((uint8_t)_book, pos, _face, _lay.justify ? 1 : 0);
  _store->flush();
  _dirty = false;
}

void ReaderScreen::showList() {
  _mode = _store->count() > 0 ? LIST : EMPTY;
  _list_sel = 0;
  for (uint8_t i = 0; i < _store->count(); i++) {
    if (_store->entry(i)->id == _book) _list_sel = i;
  }
  _list_top = 0;
  _full_next = true;
}

void ReaderScreen::exitReader() {
  if (_dirty) saveBookmark();
  _mode = _book >= 0 ? READ : LIST;   // the Books page brings the reader back here
  _task->syncDisplayRefresh();
  _task->gotoHomeScreen();
}

void ReaderScreen::ensurePage() {
  if (_page_valid || _book < 0) return;
  _lay.layoutPage(_cache, _pos, _page);
  _pos = _page.start;
  _page_valid = true;
}

void ReaderScreen::markTurned() {
  invalidatePage();
  _dirty = true;
  _dirty_at = millis();
  if (++_turns % READER_FULL_REFRESH_EVERY == 0) _full_next = true;
}

void ReaderScreen::nextPage() {
  ensurePage();
  if (_page.next >= _cache.size()) return;   // last page
  if (_hist_n == sizeof(_hist) / sizeof(_hist[0])) {
    memmove(_hist, _hist + 1, sizeof(_hist) - sizeof(_hist[0]));
    _hist_n--;
  }
  _hist[_hist_n++] = _page.start;
  _pos = _page.next;
  markTurned();
}

void ReaderScreen::prevPage() {
  ensurePage();
  if (_page.start == 0) return;
  _pos = _hist_n > 0 ? _hist[--_hist_n] : _lay.prevPageStart(_cache, _page.start);
  markTurned();
}

void ReaderScreen::cycleFont() {
  ensurePage();
  _face = (uint8_t)((_face + 1) % READER_FACE_COUNT);
  _lay.face = READER_FACES[_face];
  _pos = _page.start;      // the same text stays at the top
  _hist_n = 0;             // page starts differ for another size
  invalidatePage();
  _dirty = true;
  _dirty_at = millis();
  _full_next = true;
}

void ReaderScreen::poll() {
  if (_store->generation() != _gen) {      // a book was added or deleted over Bluetooth
    _gen = _store->generation();
    if (_book >= 0 && !_store->find((uint8_t)_book)) {
      closeBook();
      showList();
    } else if (_mode == LIST || _mode == EMPTY) {
      showList();
    }
    _task->requestRefresh();
  }
  if (_dirty && (millis() - _dirty_at) > READER_SAVE_DELAY_MS) saveBookmark();
}

int ReaderScreen::drawLabel(ReaderCanvas& cv, const ReaderFace* face, const char* utf8, int x, int y) {
  char buf[80];
  utf8ToCp1251(buf, sizeof(buf), utf8);
  ReaderLayout l;
  l.face = face;
  return l.drawText(cv, buf, x, y);
}

// Draws a 1251 string cut with an ellipsis to max_w pixels.
void ReaderScreen::drawFitted(ReaderCanvas& cv, const char* s, int x, int y, int max_w) {
  ReaderLayout l;
  l.face = READER_FACES[0];
  char buf[BOOK_TITLE_MAX + 2];
  int w = 0, n = 0;
  const int ell = l.adv(0x85);
  for (; s[n] && n < BOOK_TITLE_MAX; n++) {
    int a = l.adv((uint8_t)s[n]);
    if (w + a > max_w) {
      while (n > 0 && w + ell > max_w) w -= l.adv((uint8_t)s[--n]);
      memcpy(buf, s, n);
      buf[n++] = (char)0x85;
      buf[n] = 0;
      l.drawText(cv, buf, x, y);
      return;
    }
    w += a;
  }
  memcpy(buf, s, n);
  buf[n] = 0;
  l.drawText(cv, buf, x, y);
}

void ReaderScreen::drawRead(ReaderCanvas& cv) {
  ensurePage();
  int bx, by, bw, bh;
  ReaderLayout::textBox(cv.w, cv.h, bx, by, bw, bh);
  if (_page_valid) {
    _lay.drawPage(_cache, _page, cv, bx, by);
    ReaderLayout::drawProgress(cv, _page.next, _cache.size(), _task->getMsgCount() > 0);
  }
}

void ReaderScreen::drawMenu(ReaderCanvas& cv) {
  const ReaderFace* f = READER_FACES[0];
  const BookEntry* e = _book >= 0 ? _store->find((uint8_t)_book) : nullptr;
  char tmp[48];
  ensurePage();
  uint32_t size = _cache.size();
  int pct = size ? (int)((uint64_t)(_page_valid ? _page.next : _pos) * 100 / size) : 0;
  snprintf(tmp, sizeof(tmp), "%d%%", pct);
  ReaderLayout l;
  l.face = f;
  int pw = l.textWidth(tmp);
  l.drawText(cv, tmp, cv.w - 3 - pw, 0);
  if (e) drawFitted(cv, e->title, 3, 0, cv.w - 12 - pw);
  cv.hline(0, f->line, cv.w);

  for (int i = 0; i < MI_COUNT; i++) {
    int y = f->line + 2 + i * f->line;
    const char* label = "";
    switch (i) {
      case MI_CONTINUE: label = "Продолжить"; break;
      case MI_FONT:
        snprintf(tmp, sizeof(tmp), "Шрифт: %s", READER_FACES[_face]->name);
        label = tmp;
        break;
      case MI_ALIGN: label = _lay.justify ? "Выравнивание: по ширине" : "Выравнивание: влево"; break;
      case MI_LIST: label = "Список книг"; break;
      case MI_START: label = "В начало книги"; break;
      case MI_EXIT: label = "Выход в меню"; break;
    }
    drawLabel(cv, f, label, 8, y);
    if (i == _menu_sel) frameRect(cv, 2, y - 1, cv.w - 4, f->line + 1);
  }
}

void ReaderScreen::drawList(ReaderCanvas& cv) {
  const ReaderFace* f = READER_FACES[0];
  char tmp[48];
  snprintf(tmp, sizeof(tmp), "Книги: %u   свободно %lu КБ", (unsigned)_store->count(),
           (unsigned long)(_store->freeBytes() / 1024));
  drawLabel(cv, f, tmp, 3, 0);
  cv.hline(0, f->line, cv.w);

  int rows = (cv.h - f->line - 2) / f->line;
  int total = _store->count() + 1;              // + "Exit"
  if (_list_sel >= total) _list_sel = (uint8_t)(total - 1);
  if (_list_sel < _list_top) _list_top = _list_sel;
  if (_list_sel >= _list_top + rows) _list_top = (uint8_t)(_list_sel - rows + 1);

  ReaderLayout l;
  l.face = f;
  for (int r = 0; r < rows && _list_top + r < total; r++) {
    int i = _list_top + r;
    int y = f->line + 2 + r * f->line;
    if (i < _store->count()) {
      const BookEntry* e = _store->entry(i);
      snprintf(tmp, sizeof(tmp), "%d%%", e->size ? (int)((uint64_t)e->pos * 100 / e->size) : 0);
      int pw = l.textWidth(tmp);
      l.drawText(cv, tmp, cv.w - 6 - pw, y);
      drawFitted(cv, e->title, 8, y, cv.w - 22 - pw);
    } else {
      drawLabel(cv, f, "Выход в меню", 8, y);
    }
    if (i == _list_sel) frameRect(cv, 2, y - 1, cv.w - 4, f->line + 1);
  }
}

void ReaderScreen::drawConfirm(ReaderCanvas& cv) {
  const ReaderFace* f = READER_FACES[1];
  const BookEntry* e = _store->entry(_list_sel);
  drawLabel(cv, f, "Удалить книгу?", 6, 8);
  if (e) {
    ReaderLayout l;
    l.face = READER_FACES[0];
    drawFitted(cv, e->title, 6, 8 + f->line + 4, cv.w - 12);
  }
  drawLabel(cv, READER_FACES[0], "Долгое нажатие — удалить", 6, cv.h - 2 * READER_FACES[0]->line - 4);
  drawLabel(cv, READER_FACES[0], "Нажатие — оставить", 6, cv.h - READER_FACES[0]->line - 4);
}

void ReaderScreen::drawEmpty(ReaderCanvas& cv) {
  const ReaderFace* f = READER_FACES[1];
  const ReaderFace* s = READER_FACES[0];
  drawLabel(cv, f, "Книг пока нет", 6, 4);
  int y = 4 + f->line + 6;
  drawLabel(cv, s, "Загрузите их по Bluetooth со", 6, y); y += s->line;
  drawLabel(cv, s, "страницы tools/book_uploader", 6, y); y += s->line;
  drawLabel(cv, s, "(Chrome, PIN как у телефона).", 6, y); y += s->line;
  drawLabel(cv, s, "Нажатие — выход", 6, cv.h - s->line - 4);
}

int ReaderScreen::render(DisplayDriver& display) {
  int W = display.frameWidth(), H = display.frameHeight();
  if (W > READER_MAX_W) W = READER_MAX_W;
  if (H > READER_MAX_H) H = READER_MAX_H;
  ReaderCanvas cv{canvas_bits, W, H, (W + 7) / 8};
  cv.clear();
  int bx, by, bw, bh;
  ReaderLayout::textBox(W, H, bx, by, bw, bh);
  if (bw != _lay.width || bh != _lay.height) {   // first frame: pages need the text box
    _lay.width = bw;
    _lay.height = bh;
    _hist_n = 0;
    invalidatePage();
  }

  display.setRefreshMode(1);   // page turns: short waveform on the changed pixels
  if (_full_next) {
    display.markFull();
    _full_next = false;
  }
  switch (_mode) {
    case READ:    drawRead(cv); break;
    case MENU:    drawMenu(cv); break;
    case LIST:    drawList(cv); break;
    case CONFIRM: drawConfirm(cv); break;
    case EMPTY:   drawEmpty(cv); break;
  }
  display.setColor(UIColor::primary_txt);
  display.blit1(0, 0, W, H, canvas_bits);
  return 60000;
}

bool ReaderScreen::menuInput(char c) {
  if (c == KEY_NEXT || c == KEY_DOWN || c == KEY_RIGHT) {
    _menu_sel = (uint8_t)((_menu_sel + 1) % MI_COUNT);
    return true;
  }
  if (c == KEY_PREV || c == KEY_UP || c == KEY_LEFT) {
    _menu_sel = (uint8_t)((_menu_sel + MI_COUNT - 1) % MI_COUNT);
    return true;
  }
  if (c == KEY_ENTER || c == KEY_SELECT) {
    switch (_menu_sel) {
      case MI_CONTINUE: _mode = READ; _full_next = true; break;
      case MI_FONT: cycleFont(); break;
      case MI_ALIGN: _lay.justify = !_lay.justify; _dirty = true; _dirty_at = millis(); break;
      case MI_LIST: saveBookmark(); showList(); break;
      case MI_START:
        _pos = 0; _hist_n = 0; invalidatePage(); _dirty = true; _dirty_at = millis();
        _mode = READ; _full_next = true;
        break;
      case MI_EXIT: exitReader(); break;
    }
    return true;
  }
  if (c == KEY_QUAD || c == KEY_CANCEL) {
    _mode = READ;
    _full_next = true;
    return true;
  }
  return false;
}

bool ReaderScreen::listInput(char c) {
  int total = _store->count() + 1;
  if (c == KEY_NEXT || c == KEY_DOWN || c == KEY_RIGHT) {
    _list_sel = (uint8_t)((_list_sel + 1) % total);
    return true;
  }
  if (c == KEY_PREV || c == KEY_UP || c == KEY_LEFT) {
    _list_sel = (uint8_t)((_list_sel + total - 1) % total);
    return true;
  }
  if (c == KEY_ENTER) {
    if (_list_sel >= _store->count()) {
      exitReader();
    } else if (openBook(_store->entry(_list_sel)->id)) {
      _mode = READ;
    }
    return true;
  }
  if (c == KEY_SELECT && _list_sel < _store->count()) {
    _mode = CONFIRM;
    _full_next = true;
    return true;
  }
  if (c == KEY_QUAD || c == KEY_CANCEL) {
    exitReader();
    return true;
  }
  return false;
}

bool ReaderScreen::handleInput(char c) {
  switch (_mode) {
    case READ:
      if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) { nextPage(); return true; }
      if (c == KEY_PREV || c == KEY_LEFT || c == KEY_UP) { prevPage(); return true; }
      if (c == KEY_SELECT) { cycleFont(); return true; }
      if (c == KEY_ENTER) { _mode = MENU; _menu_sel = 0; _full_next = true; return true; }
      if (c == KEY_QUAD || c == KEY_CANCEL) { exitReader(); return true; }
      return false;
    case MENU:
      return menuInput(c);
    case LIST:
      return listInput(c);
    case CONFIRM:
      if (c == KEY_ENTER) {
        const BookEntry* e = _store->entry(_list_sel);
        if (e) {
          uint8_t id = e->id;
          if (id == _book) closeBook();
          _store->remove(id);
          _gen = _store->generation();
        }
      }
      showList();
      return true;
    case EMPTY:
      exitReader();
      return true;
  }
  return false;
}

#endif
