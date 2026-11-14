#pragma once

// Write page: pick a channel or a favourite contact, then send a stock phrase
// or a text typed on a one-screen keyboard. Included from UITask.cpp only.
//
// Single button. Lists (recipients, phrases):
//   click  - next item        double - previous item
//   triple - back             quad / five - leave to the home pages
//   hold   - choose (COMPOSE_HOLD_MILLIS here, shorter than elsewhere)
// Keyboard:
//   click  - right    double - left    triple - down    quad - up
//   five   - leave without sending     hold - type the key

#include "UITask.h"
#include "../MyMesh.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>
#include "MorseTables.h"

#ifndef COMPOSE_MAX_TEXT
  #define COMPOSE_MAX_TEXT  120
#endif
#ifndef COMPOSE_MAX_FAVS
  #define COMPOSE_MAX_FAVS  40
#endif
#ifndef COMPOSE_HOLD_MILLIS
  #define COMPOSE_HOLD_MILLIS  500
#endif

class ComposeScreen : public UIScreen {
  enum Mode : uint8_t { PICK, PHRASE, KEYS, MORSE, RESULT };

  // keyboard: 4 rows of keys, then one row of wide keys.
  // English: 10 keys a row. Russian (`set lang ru`): 12 keys a row, all 33 letters.
  #define KB_ROWS   5
  #define KB_ACTION_ROW  4
  #define KB_MAX_COLS  12
  bool ru() const { return _task->langRu(); }
  int cols() const { return ru() ? 12 : 10; }
  // UTF-8 string of one row; every key is one letter
  const char* keyRow(int r) const {
    static const char* const en[KB_ACTION_ROW] = {
      "1234567890",
      "qwertyuiop",
      "asdfghjkl?",
      "zxcvbnm,.!",
    };
    static const char* const rus[KB_ACTION_ROW] = {
      "1234567890.,",
      "йцукенгшщзхъ",
      "фывапролджэ!",
      "ячсмитьбюё?:",
    };
    return ru() ? rus[r] : en[r];
  }
  // copies key c of row r (one UTF-8 letter) into buf
  void keyAt(int r, int c, char* buf) const {
    const char* p = keyRow(r);
    for (int i = 0; i < c && *p; i++) {
      p++;
      while (((uint8_t)*p & 0xC0) == 0x80) p++;
    }
    int n = 0;
    if (*p) {
      buf[n++] = *p++;
      while (((uint8_t)*p & 0xC0) == 0x80 && n < 4) buf[n++] = *p++;
    }
    buf[n] = 0;
  }
  enum Action : uint8_t { ACT_SPACE, ACT_DEL, ACT_SEND, ACT_COUNT };
  // first column each wide key covers, and its label
  int actionCol(int a) const {
    static const int c10[ACT_COUNT + 1] = { 0, 4, 7, 10 };
    static const int c12[ACT_COUNT + 1] = { 0, 5, 8, 12 };
    return ru() ? c12[a] : c10[a];
  }
  static const char* actionLabel(int a) { static const char* const l[ACT_COUNT] = { "space", "del", "SEND" }; return l[a]; }
  int actionAt(int col) const {
    for (int a = ACT_COUNT - 1; a >= 0; a--) if (col >= actionCol(a)) return a;
    return 0;
  }

  // stock phrases (UiPhrases.h) or the ones set from the console. Screen 1
  // has the keyboard button on top and 3 rows of phrases below it, further
  // screens 4 rows. A phrase that fits half the width shares its row, a longer
  // one takes the whole row. The focus moves on to the next screen after the
  // last phrase of one; the layout is worked out when drawing.
  static int numPhrases() { return UI_PHRASE_COUNT; }
  static const char* phraseAt(int i) { return the_mesh.uiPhrase(i); }

  struct Slot { uint8_t page, row, col, wide; };   // col 0 left, 1 right
  Slot _slot[UI_PHRASE_COUNT];
  int _pages = 1;

  void layoutPhrases(DisplayDriver& display) {
    int half = display.width() / 2;
    int page = 0, row = 1, col = 0;   // row 0 of screen 1 is the keyboard button
    char shown[UI_PHRASE_LEN + 4];
    for (int i = 0; i < numPhrases(); i++) {
      display.translateUTF8ToBlocks(shown, phraseAt(i), sizeof(shown));
      bool wide = display.getTextWidth(shown) > half - 6;
      if (wide && col == 1) { row++; col = 0; }   // a wide one starts a fresh row
      if (row > 3) { page++; row = 0; col = 0; }
      _slot[i].page = page;
      _slot[i].row = row;
      _slot[i].col = col;
      _slot[i].wide = wide;
      if (wide || col == 1) { row++; col = 0; }
      else col = 1;
    }
    _pages = page + 1;
  }

  static int utf8Count(const char* s) {
    int n = 0;
    for (; *s; s++) if (((uint8_t)*s & 0xC0) != 0x80) n++;
    return n;
  }

public:
  typedef ComposeTarget Target;

private:
  UITask* _task;
  Mode _mode = PICK;

  // targets: the last COMPOSE_RECENT recipients first, then device channels,
  // then favourite chat/room contacts
  #define COMPOSE_RECENT  3
  #define COMPOSE_MAX_TARGETS  (MAX_GROUP_CHANNELS + COMPOSE_MAX_FAVS + 1)
  Target _t[COMPOSE_MAX_TARGETS];
  int _num_t = 0;
  int _num_recent = 0;          // how many of the first targets are recent ones
  Target _recent[COMPOSE_RECENT];
  int _recent_n = 0;
  int _sel = 0, _top = 0;
  int _target = -1;

  char _text[COMPOSE_MAX_TEXT + 1];
  int _len = 0;
  int _row = 1, _col = 0;   // keyboard cursor; on the action row _col is the column it came from

  char _re[SENT_RE_LEN];    // replying to this message; "" when writing anew
  int _sent = 0;            // 0 failed, 1 direct, 2 flood, 3 channel
  unsigned long _result_at = 0;

  struct Collect : public ContactVisitor {
    ComposeScreen* s;
    int n = 0;
    void onContactVisit(const ContactInfo& c) override {
      if (n >= COMPOSE_MAX_FAVS) return;
      if ((c.flags & 0x01) == 0) return;   // bit 0 = favourite
      if (c.type != ADV_TYPE_CHAT && c.type != ADV_TYPE_ROOM) return;
      Target t;
      t.is_ch = 0;
      t.ch = 0;
      memcpy(t.key, c.id.pub_key, 6);
      if (s->addTarget(t)) n++;
    }
  };

  int numTargets() const { return _num_t; }
  bool isChannel(int i) const { return _t[i].is_ch != 0; }

  static bool exists(const Target& t) {
    if (t.is_ch) {
      ChannelDetails ch;
      return the_mesh.getChannel(t.ch, ch) && ch.name[0];
    }
    return the_mesh.lookupContactByPubKey(t.key, 6) != NULL;
  }

  int findTarget(const Target& t) const {
    for (int i = 0; i < _num_t; i++) if (_t[i].same(t)) return i;
    return -1;
  }

  bool addTarget(const Target& t) {
    if (_num_t >= COMPOSE_MAX_TARGETS || findTarget(t) >= 0) return false;
    _t[_num_t++] = t;
    return true;
  }

  void targetName(int i, char* buf, size_t n) {
    if (i >= 0 && i < _num_t) {
      if (isChannel(i)) {
        ChannelDetails ch;
        if (the_mesh.getChannel(_t[i].ch, ch)) {
          snprintf(buf, n, "#%s", ch.name[0] == '#' ? ch.name + 1 : ch.name);
          return;
        }
      } else {
        ContactInfo* c = the_mesh.lookupContactByPubKey(_t[i].key, 6);
        if (c) {
          snprintf(buf, n, "@%s", c->name);
          return;
        }
      }
    }
    snprintf(buf, n, "?");
  }

  void loadTargets() {
    _num_t = 0;
    for (int i = 0; i < _recent_n; i++) {   // the last recipients, newest first
      if (exists(_recent[i])) addTarget(_recent[i]);
    }
    _num_recent = _num_t;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(i, ch) || !ch.name[0]) continue;
      Target t;
      t.is_ch = 1;
      t.ch = (uint8_t)i;
      memset(t.key, 0, sizeof(t.key));
      addTarget(t);
    }
    Collect v;
    v.s = this;
    the_mesh.scanRecentContacts(0, &v);   // latest heard first
  }

  void rememberRecent(const Target& t) {
    int at = _recent_n < COMPOSE_RECENT ? _recent_n : COMPOSE_RECENT - 1;
    for (int i = 0; i < _recent_n; i++) if (_recent[i].same(t)) { at = i; break; }
    for (int i = at; i > 0; i--) _recent[i] = _recent[i - 1];
    _recent[0] = t;
    if (at >= _recent_n) _recent_n = at + 1;
  }

  static void step(int& v, int n, int d) {
    if (n > 0) v = (v + d + n) % n;
  }

  bool replying() const { return _re[0] != 0; }

  // recipient as the title shows it: "RE @Alexey" while answering a message
  void titleName(char* buf, size_t n) {
    char name[48];
    targetName(_target, name, sizeof(name));
    if (replying()) snprintf(buf, n, "RE %s", name);
    else StrHelper::strncpy(buf, name, n);
  }

  // every send goes to the Sent list; sending again updates the same entry
  void logSent(bool again) {
    SentLog& log = _task->sentLog();
    SentEntry* e = (again && log.count() > 0) ? log.get(0) : log.add();
    e->at = millis();
    e->to = _t[_target];
    e->kind = (uint8_t)_sent;
    e->delivered = false;
    e->ack = 0;
    e->ack_until = 0;
    if (_sent == 1 || _sent == 2) {
      e->ack = the_mesh.uiLastAck();
      e->ack_until = the_mesh.uiLastAckUntil();
    }
    StrHelper::strncpy(e->re, _re, sizeof(e->re));
    StrHelper::strncpy(e->text, _text, sizeof(e->text));
  }

  void send(bool again = false) {
    if (_len == 0 || _target < 0) return;
    _text[_len] = 0;
    if (isChannel(_target)) {
      _sent = the_mesh.uiSendChannel(_t[_target].ch, _text) ? 3 : 0;
    } else {
      _sent = the_mesh.uiSendDirect(_t[_target].key, _text);
    }
    if (_sent) rememberRecent(_t[_target]);
    logSent(again);
    _task->notify(_sent ? UIEventType::ack : UIEventType::none);
    _mode = RESULT;
    _result_at = millis();
  }

  void drawTitle(DisplayDriver& display, const char* left, const char* right) {
    char shown[48];
    display.translateUTF8ToBlocks(shown, left, sizeof(shown));
    int rw = right ? display.getTextWidth(right) : 0;
    display.setTextSize(1);
    if (right) {
      display.setColor(UIColor::corp_blue);
      display.setCursor(display.width() - rw - 1, 0);
      display.print(right);
    }
    display.setColor(UIColor::secondary_txt);
    display.drawTextEllipsized(0, 0, display.width() - rw - 4, shown);
    display.setColor(UIColor::corp_blue);
    display.drawRect(0, 11, display.width(), 1);
  }

  void drawTargetTitle(DisplayDriver& display) {
    char name[56];
    if (_target >= 0) titleName(name, sizeof(name));
    else strcpy(name, "Write to:");
    drawTitle(display, name, NULL);
  }

  // text in a box, inverted when selected
  // text in a box, inverted when selected; a framed cell looks like a button
  void drawCell(DisplayDriver& display, int x, int y, int w, const char* text, bool sel, bool framed = false) {
    int top = framed ? y - 2 : y - 1;
    int h = framed ? 11 : 10;
    display.setColor(UIColor::primary_txt);
    if (sel) {
      display.fillRect(x, top, w, h);
      display.setColor(UIColor::window_bkg);
    } else if (framed) {
      display.drawRect(x, top, w, h);
    }
    int tw = display.getTextWidth(text);
    if (tw > w - 2) display.drawTextEllipsized(x + 1, y, w - 2, text);
    else {
      display.setCursor(x + (w - tw) / 2, y);
      display.print(text);
    }
    display.setColor(UIColor::primary_txt);
  }

  void drawPick(DisplayDriver& display) {
    drawTargetTitle(display);
    if (numTargets() == 0) {
      display.setColor(UIColor::primary_txt);
      display.drawTextCentered(display.width() / 2, 22, "No channels");
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 34, "or favourites");
      return;
    }
    const int rows = 4;
    if (_sel < _top) _top = _sel;
    if (_sel >= _top + rows) _top = _sel - rows + 1;
    for (int r = 0; r < rows && _top + r < numTargets(); r++) {
      int i = _top + r;
      char item[48], shown[48];
      targetName(i, item, sizeof(item));
      display.translateUTF8ToBlocks(shown, item, sizeof(shown));
      bool sel = i == _sel;
      int y = 14 + r * 12;
      if (sel) {
        display.setColor(UIColor::primary_txt);
        display.fillRect(0, y - 1, display.width(), 11);
        display.setColor(UIColor::window_bkg);
      } else {
        display.setColor(UIColor::primary_txt);
      }
      display.drawTextEllipsized(2, y, display.width() - 4, shown);
      display.setColor(UIColor::primary_txt);
      if (i == _num_recent - 1 && i + 1 < numTargets()) {   // dotted line after the recent ones
        display.setColor(UIColor::secondary_txt);
        for (int x = 2; x < display.width() - 2; x += 4) display.fillRect(x, y + 10, 2, 1);
        display.setColor(UIColor::primary_txt);
      }
    }
  }

  // PHRASE items: 0 the keyboard, 1 Morse (when it is on), then the phrases
  bool morseOn() const { return _task->morseEnabled(); }
  int firstPhrase() const { return morseOn() ? 2 : 1; }
  bool isMorseItem(int sel) const { return morseOn() && sel == 1; }

  // screen 1: the keyboard button on top (item 0) with Morse next to it,
  // phrases below; further screens: the next phrases. The title shows 1/3, 2/3...
  void drawPhrases(DisplayDriver& display) {
    layoutPhrases(display);
    int fp = firstPhrase();
    int page = _sel < fp ? 0 : _slot[_sel - fp].page;
    char name[56], pos[8];
    titleName(name, sizeof(name));
    snprintf(pos, sizeof(pos), "%d/%d", page + 1, _pages);
    drawTitle(display, name, pos);
    int w = display.width(), half = w / 2;
    char shown[UI_PHRASE_LEN + 4];
    if (page == 0) {
      if (morseOn()) {
        drawCell(display, 2, 15, half - 4, ru() ? "клавиши" : "keys", _sel == 0, true);
        drawCell(display, half + 2, 15, half - 4, ru() ? "морзе" : "morse", _sel == 1, true);
      } else {
        drawCell(display, 2, 15, w - 4, ru() ? "клавиатура" : "keyboard", _sel == 0, true);
      }
    }
    for (int i = 0; i < numPhrases(); i++) {
      const Slot& sl = _slot[i];
      if (sl.page != page) continue;
      display.translateUTF8ToBlocks(shown, phraseAt(i), sizeof(shown));
      int y = 15 + sl.row * 12;
      if (sl.wide) drawCell(display, 2, y, w - 4, shown, _sel == i + fp, true);
      else drawCell(display, sl.col * half + 2, y, half - 4, shown, _sel == i + fp, true);
    }
  }

  void drawKeys(DisplayDriver& display) {
    drawTextLine(display);
    drawKeyboard(display);
  }

  // text line: the tail of the text with a cursor, or the recipient while empty
  void drawTextLine(DisplayDriver& display) {
    char count[8];
    snprintf(count, sizeof(count), "%d", utf8Count(_text));
    if (_len == 0) {
      char name[48], line[64];
      targetName(_target, name, sizeof(name));
      snprintf(line, sizeof(line), "%s %s", replying() ? "RE" : "to", name);
      drawTitle(display, line, count);
    } else {
      int room = display.width() - display.getTextWidth(count) - 6;
      int from = 0;
      char tail[COMPOSE_MAX_TEXT + 2];
      for (;;) {
        snprintf(tail, sizeof(tail), "%s_", &_text[from]);
        if (display.getTextWidth(tail) <= room || from >= _len) break;
        from++;
        while (from < _len && ((uint8_t)_text[from] & 0xC0) == 0x80) from++;   // whole letters only
      }
      display.setTextSize(1);
      display.setColor(UIColor::corp_blue);
      display.setCursor(display.width() - display.getTextWidth(count) - 1, 0);
      display.print(count);
      display.setColor(UIColor::primary_txt);
      display.setCursor(0, 0);
      display.print(tail);
      display.setColor(UIColor::corp_blue);
      display.drawRect(0, 11, display.width(), 1);
    }
  }

  void drawKeyboard(DisplayDriver& display) {
    int w = display.width();
    int kc = cols();
    for (int r = 0; r < KB_ROWS; r++) {
      int y = 13 + r * 10;
      if (r == KB_ACTION_ROW) {
        int cur = actionAt(_col);
        for (int a = 0; a < ACT_COUNT; a++) {
          int x0 = actionCol(a) * w / kc, x1 = actionCol(a + 1) * w / kc;
          drawCell(display, x0, y, x1 - x0 - 1, actionLabel(a), _row == r && cur == a);
        }
        continue;
      }
      for (int c = 0; c < kc; c++) {
        char cell[5];
        keyAt(r, c, cell);
        int x0 = c * w / kc, x1 = (c + 1) * w / kc;
        drawCell(display, x0, y, x1 - x0, cell, _row == r && _col == c);
      }
    }
  }

  // ------------------------------------------------------------ Morse
  // The button is read directly: a press shorter than the dash time is a dot,
  // longer is a dash. A pause of 2 dash times ends the letter, 5 dash times
  // puts a space. The alphabet follows the language (ru / en).
  bool _m_down = false;
  bool _m_hold_exit = false;     // the press that left Morse is still down
  unsigned long _m_edge = 0;     // when the button last went down or up
  char _m_code[MORSE_MAX_LEN + 2];
  int _m_n = 0;
  bool _m_gap_done = true;       // the word pause already put its space
  char _m_note[48];              // a short note under the code: what happened
  bool _m_note_warn = false;
  unsigned long _m_note_until = 0;
  unsigned long _m_since = 0;    // when Morse was opened, for the rotating hints

  unsigned long dashMs() const { return _task->morseDashMs(); }
  unsigned long letterGapMs() const { return dashMs() * 2; }
  unsigned long wordGapMs() const { return dashMs() * 5; }
  unsigned long exitHoldMs() const { unsigned long t = dashMs() * 5; return t < 2000 ? 2000 : t; }

  const MorseSym* morseTable(int& n) const {
    if (ru()) { n = sizeof(MORSE_RU) / sizeof(MORSE_RU[0]); return MORSE_RU; }
    n = sizeof(MORSE_EN) / sizeof(MORSE_EN[0]);
    return MORSE_EN;
  }

  const char* morseLookup(const char* code) const {
    int n;
    const MorseSym* t = morseTable(n);
    for (int i = 0; i < n; i++) if (strcmp(t[i].code, code) == 0) return t[i].out;
    return NULL;
  }

  void morseNote(const char* text, bool warn) {
    StrHelper::strncpy(_m_note, text, sizeof(_m_note));
    _m_note_warn = warn;
    _m_note_until = millis() + (warn ? 3000 : 1200);
  }

  void appendText(const char* s) {
    int l = strlen(s);
    if (_len + l > COMPOSE_MAX_TEXT) { morseNote("too long", true); return; }
    memcpy(&_text[_len], s, l + 1);
    _len += l;
  }

  void eraseLetter() {
    while (_len > 0 && ((uint8_t)_text[_len - 1] & 0xC0) == 0x80) _len--;
    if (_len > 0) _len--;
    _text[_len] = 0;
  }

  void addSpace() {
    if (_len > 0 && _len < COMPOSE_MAX_TEXT && _text[_len - 1] != ' ') { _text[_len++] = ' '; _text[_len] = 0; }
  }

  bool allDots(const char* code) const {
    for (; *code; code++) if (*code != '.') return false;
    return true;
  }

  // the pattern is complete: a letter, a service signal or nothing known
  void morseCommit() {
    if (_m_n == 0) return;
    _m_code[_m_n] = 0;
    char note[48];
    if (strcmp(_m_code, MORSE_SEND) == 0) {
      _m_n = 0;
      while (_len > 0 && _text[_len - 1] == ' ') _text[--_len] = 0;
      if (_len > 0) send();
      else morseNote(ru() ? "пусто: нечего слать" : "empty: nothing to send", true);
      return;
    }
    if (strcmp(_m_code, MORSE_SPACE) == 0) {
      addSpace();
      morseNote(ru() ? "пробел" : "space", false);
    } else if (_m_n >= MORSE_DEL_MIN && allDots(_m_code)) {
      eraseLetter();
      morseNote(ru() ? "стёрто" : "erased", false);
    } else {
      const char* out = _m_n <= MORSE_MAX_LEN ? morseLookup(_m_code) : NULL;
      if (out) {
        appendText(out);
        snprintf(note, sizeof(note), "%s = %s", _m_code, out);
        morseNote(note, false);
      } else {
        // not a letter: nothing is typed; say so and show what was received
        snprintf(note, sizeof(note), "%s ?", _m_code);
        morseNote(note, true);
        _task->notify(UIEventType::none);
      }
    }
    _m_n = 0;
    _m_gap_done = false;
  }

  void enterMorse() {
    _mode = MORSE;
    _m_n = 0;
    _m_down = _task->isButtonPressed();
    _m_hold_exit = _m_down;   // the hold that opened Morse must be let go first
    _m_edge = millis();
    _m_gap_done = true;
    _m_note[0] = 0;
    _m_note_until = 0;
    _m_since = millis();
  }

  void leaveMorse() {
    _m_n = 0;
    _mode = PHRASE;
    _sel = 1;
    _task->showAlert(ru() ? "Морзе: выход" : "Morse: exit", 800);
  }

  // dot/dash marks in a row; the one being pressed grows from a dot to a dash
  int drawPattern(DisplayDriver& display, int x, int y, const char* code, int n, int live) {
    display.setColor(UIColor::primary_txt);
    for (int i = 0; i < n; i++) {
      int w = code[i] == '-' ? 12 : 4;
      display.fillRect(x, y, w, 4);
      x += w + 4;
    }
    if (live >= 0) {   // 0 = still a dot, 1 = already a dash
      int w = live ? 12 : 4;
      display.drawRect(x, y, w, 4);
      x += w + 4;
    }
    return x;
  }

  void drawMorse(DisplayDriver& display) {
    drawTextLine(display);
    int w = display.width();
    unsigned long now = millis();
    bool pressing = _m_down && !_m_hold_exit;
    int live = pressing ? ((now - _m_edge) >= dashMs() ? 1 : 0) : -1;

    // row 1: what is being keyed and what it means so far
    int x = drawPattern(display, 2, 17, _m_code, _m_n, live);
    char cur[MORSE_MAX_LEN + 2];
    memcpy(cur, _m_code, _m_n);
    int cn = _m_n;
    if (live >= 0 && cn < MORSE_MAX_LEN) cur[cn++] = live ? '-' : '.';
    cur[cn] = 0;
    char line[48];
    if (cn > 0) {
      const char* now_is = NULL;
      if (strcmp(cur, MORSE_SEND) == 0) now_is = ru() ? "отпр." : "send";
      else if (strcmp(cur, MORSE_SPACE) == 0) now_is = ru() ? "пробел" : "space";
      else if (cn >= MORSE_DEL_MIN && allDots(cur)) now_is = ru() ? "стереть" : "erase";
      else now_is = morseLookup(cur);
      snprintf(line, sizeof(line), "= %s", now_is ? now_is : "?");
      display.setColor(now_is ? UIColor::primary_txt : UIColor::warning_txt);
      int tw = display.getTextWidth(line);
      if (x < w - tw - 2) {
        display.setCursor(w - tw - 1, 15);
        display.print(line);
      }
    } else {
      display.setColor(UIColor::secondary_txt);
      snprintf(line, sizeof(line), ru() ? "точка < %lu мс < тире" : "dot < %lums < dash", dashMs());
      display.drawTextCentered(w / 2, 15, line);
    }

    // row 2: what one more dot or dash would give
    display.setColor(UIColor::secondary_txt);
    if (cn > 0 && cn < MORSE_MAX_LEN) {
      char nd[MORSE_MAX_LEN + 2], nh[MORSE_MAX_LEN + 2];
      memcpy(nd, cur, cn); nd[cn] = '.'; nd[cn + 1] = 0;
      memcpy(nh, cur, cn); nh[cn] = '-'; nh[cn + 1] = 0;
      const char* a = morseLookup(nd);
      const char* b = morseLookup(nh);
      if (strcmp(nh, MORSE_SEND) == 0) b = ru() ? "отпр." : "send";
      if (strcmp(nd, MORSE_SEND) == 0) a = ru() ? "отпр." : "send";
      snprintf(line, sizeof(line), "+. %s   +- %s", a ? a : "-", b ? b : "-");
      char shown[48];
      display.translateUTF8ToBlocks(shown, line, sizeof(shown));
      display.drawTextCentered(w / 2, 27, shown);
    } else if (cn == 0) {
      unsigned long secs = (exitHoldMs() + 999) / 1000;
      snprintf(line, sizeof(line), ru() ? "держать %lu с: выход" : "hold %lus: exit", secs);
      display.drawTextCentered(w / 2, 27, line);
    }

    // row 3: what just happened, or a crib of the alphabet
    if (_m_note[0] && (long)(now - _m_note_until) < 0) {
      display.setColor(_m_note_warn ? UIColor::warning_txt : UIColor::primary_txt);
      char shown[64];
      if (_m_note_warn && _m_note[0] && strchr(_m_note, '?')) {
        snprintf(line, sizeof(line), "%s %s", _m_note, ru() ? "нет знака" : "no sign");
        display.translateUTF8ToBlocks(shown, line, sizeof(shown));
      } else {
        display.translateUTF8ToBlocks(shown, _m_note, sizeof(shown));
      }
      display.drawTextCentered(w / 2, 39, shown);
    } else {
      int n;
      const MorseSym* t = morseTable(n);
      int groups = (n + 2) / 3;
      int g = ((now - _m_since) / 2500) % groups;
      line[0] = 0;
      for (int i = g * 3; i < g * 3 + 3 && i < n; i++) {
        char part[16];
        snprintf(part, sizeof(part), "%s%s%s", line[0] ? " " : "", t[i].out, t[i].code);
        strncat(line, part, sizeof(line) - strlen(line) - 1);
      }
      char shown[48];
      display.translateUTF8ToBlocks(shown, line, sizeof(shown));
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w / 2, 39, shown);
    }

    // row 4: the service signals, in turn
    static const char* const ru_tips[] = { ".-.-. отправить", "-...- пробел", "........ стереть", "пауза: пробел" };
    static const char* const en_tips[] = { ".-.-. send", "-...- space", "........ erase", "long pause: space" };
    const char* const* tips = ru() ? ru_tips : en_tips;
    char shown[40];
    display.translateUTF8ToBlocks(shown, tips[((now - _m_since) / 3000) % 4], sizeof(shown));
    display.setColor(UIColor::corp_blue);
    display.drawTextCentered(w / 2, 52, shown);
  }

  bool canRetry() const {
    return _sent == 0 || ((_sent == 1 || _sent == 2) && the_mesh.uiAckState() == MyMesh::UI_ACK_LOST);
  }

  void drawResult(DisplayDriver& display) {
    drawTargetTitle(display);
    const char* line = "Send failed";
    if (_sent == 3) {
      line = "Sent to channel";
    } else if (_sent) {
      switch (the_mesh.uiAckState()) {
        case MyMesh::UI_ACK_OK:   line = "Delivered"; break;
        case MyMesh::UI_ACK_LOST: line = "No reply"; break;
        default: line = _sent == 2 ? "Sent (flood)..." : "Sent (direct)...";
      }
    }
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    display.drawTextCentered(display.width() / 2, 26, line);
    display.setColor(UIColor::secondary_txt);
    if (canRetry() && _len > 0) {
      display.drawTextCentered(display.width() / 2, 42, "hold: send again");
    }
    display.drawTextCentered(display.width() / 2, 53, "press: back");
  }

  void typeKey() {
    if (_row == KB_ACTION_ROW) {
      switch (actionAt(_col)) {
        case ACT_SPACE:
          if (_len > 0 && _len < COMPOSE_MAX_TEXT && _text[_len - 1] != ' ') { _text[_len++] = ' '; _text[_len] = 0; }
          break;
        case ACT_DEL:
          while (_len > 0 && ((uint8_t)_text[_len - 1] & 0xC0) == 0x80) _len--;   // continuation bytes
          if (_len > 0) _len--;
          _text[_len] = 0;
          break;
        default:
          while (_len > 0 && _text[_len - 1] == ' ') _text[--_len] = 0;
          if (_len > 0) send();
          else _task->showAlert("Empty", 800);
      }
      return;
    }
    char key[5];
    keyAt(_row, _col, key);
    int kl = strlen(key);
    if (_len + kl > COMPOSE_MAX_TEXT) {
      _task->showAlert("Too long", 800);
      return;
    }
    memcpy(&_text[_len], key, kl + 1);
    _len += kl;
  }

public:
  ComposeScreen(UITask* task) : _task(task) { _text[0] = 0; _re[0] = 0; _m_code[0] = 0; _m_note[0] = 0; }

  bool morseActive() const { return _mode == MORSE; }

  // called on every loop while Morse is open, with the raw button state
  void morseButton(bool down) {
    unsigned long now = millis();
    if (_m_hold_exit) {             // wait for the hold that opened or closed Morse to end
      if (!down) { _m_hold_exit = false; _m_down = false; _m_edge = now; }
      return;
    }
    if (down != _m_down) {
      if (!down) {                  // released: the length of the press decides
        unsigned long held = now - _m_edge;
        if (held >= 20) {           // shorter is contact bounce
          if (_m_n < MORSE_MAX_LEN + 1) _m_code[_m_n++] = held >= dashMs() ? '-' : '.';
        }
      }
      _m_down = down;
      _m_edge = now;
      _task->keepDisplayAwake();
      _task->requestRefresh();
      return;
    }
    if (down) {
      if (now - _m_edge >= exitHoldMs()) {   // a very long hold leaves Morse, the text stays
        _m_hold_exit = true;
        leaveMorse();
      }
      return;
    }
    unsigned long gap = now - _m_edge;
    if (_m_n > 0 && gap >= letterGapMs()) {
      morseCommit();
      _task->requestRefresh();
    } else if (_m_n == 0 && !_m_gap_done && gap >= wordGapMs()) {
      addSpace();
      _m_gap_done = true;
      _task->requestRefresh();
    }
  }

  void enter() {
    loadTargets();
    _mode = PICK;
    _sel = _top = 0;
    _target = -1;
    _len = 0;
    _text[0] = 0;
    _re[0] = 0;
  }

  // write to a given recipient, even one that is not a favourite; the phrases
  // come up first. re = the incoming message being answered, or NULL.
  bool enterTo(const Target& t, const char* re) {
    if (!exists(t)) return false;
    enter();
    if (re && re[0]) {
      StrHelper::strncpy(_re, re, sizeof(_re));
      if (strlen(re) >= sizeof(_re)) {   // cut short: drop a broken last letter
        int n = strlen(_re);
        int lead = n;
        while (lead > 0 && ((uint8_t)_re[lead - 1] & 0xC0) == 0x80) lead--;
        if (lead > 0 && ((uint8_t)_re[lead - 1] & 0x80)) {   // a multi-byte letter starts at lead-1
          uint8_t b = (uint8_t)_re[lead - 1];
          int need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
          if (n - (lead - 1) < need) _re[lead - 1] = 0;
        }
      }
    }
    int i = findTarget(t);
    if (i < 0) {
      if (_num_t >= COMPOSE_MAX_TARGETS) _num_t--;
      i = _num_t;
      _t[_num_t++] = t;
    }
    _target = _sel = i;
    _mode = PHRASE;
    _sel = 0;
    return true;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    switch (_mode) {
      case PICK:   drawPick(display); return 5000;
      case PHRASE: drawPhrases(display); return 5000;
      case KEYS:   drawKeys(display); return 5000;
      case MORSE:
        drawMorse(display);
        if (millis() - _m_edge < 60000) _task->keepDisplayAwake();   // idle a minute: the screen may sleep
        return 40;
      default: break;
    }
    drawResult(display);
    _task->keepDisplayAwake();
    bool waiting = (_sent == 1 || _sent == 2) && the_mesh.uiAckState() == MyMesh::UI_ACK_WAIT;
    if (waiting) _result_at = millis();   // the countdown starts once the outcome is known
    long keep = canRetry() ? 15000 : 4000;   // time to decide on sending again
    if (!waiting && (long)(millis() - _result_at) > keep) {
      _task->gotoHomeScreen();
      return 100;
    }
    return 500;
  }

  bool handleInput(char c) override {
    bool next = c == KEY_NEXT || c == KEY_RIGHT;
    bool prev = c == KEY_PREV || c == KEY_LEFT;
    bool ok = c == KEY_ENTER;
    bool back = c == KEY_SELECT || c == KEY_CANCEL;

    if (_mode == RESULT && ok && canRetry() && _len > 0) {
      send(true);   // same text, same recipient
      return true;
    }
    if (c == KEY_PENTA || (c == KEY_QUAD && _mode != KEYS) || _mode == RESULT) {
      _task->gotoHomeScreen();
      return true;
    }
    if (_mode == PICK) {
      if (next || c == KEY_DOWN) step(_sel, numTargets(), 1);
      else if (prev || c == KEY_UP) step(_sel, numTargets(), -1);
      else if (back) _task->gotoHomeScreen();
      else if (ok && numTargets() > 0) {
        _target = _sel;
        _mode = PHRASE;
        _sel = 0;
      }
      return true;
    }
    if (_mode == MORSE) return true;   // Morse reads the button itself
    if (_mode == PHRASE) {
      int items = numPhrases() + firstPhrase();
      if (_sel >= items) _sel = 0;
      if (next || c == KEY_DOWN) step(_sel, items, 1);
      else if (prev || c == KEY_UP) step(_sel, items, -1);
      else if (back) {
        _mode = PICK;
        _sel = _target;
        _target = -1;
        _re[0] = 0;   // another recipient is not a reply
      } else if (ok) {
        if (_sel == 0) {
          _mode = KEYS;
          _row = 1;   // letters
          _col = 0;
        } else if (isMorseItem(_sel)) {
          enterMorse();
        } else {
          StrHelper::strncpy(_text, phraseAt(_sel - firstPhrase()), sizeof(_text));
          _len = strlen(_text);
          send();
        }
      }
      return true;
    }

    // keyboard: 1 click right, 2 left, 3 down, 4 up (5 leaves, see above)
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      if (_row == KB_ACTION_ROW) {
        int a = (actionAt(_col) + 1) % ACT_COUNT;
        _col = actionCol(a);
      } else {
        step(_col, cols(), 1);
      }
    } else if (c == KEY_PREV || c == KEY_LEFT) {
      if (_row == KB_ACTION_ROW) {
        int a = (actionAt(_col) + ACT_COUNT - 1) % ACT_COUNT;
        _col = actionCol(a);
      } else {
        step(_col, cols(), -1);
      }
    } else if (c == KEY_SELECT || c == KEY_DOWN) {
      step(_row, KB_ROWS, 1);
    } else if (c == KEY_QUAD || c == KEY_UP) {
      step(_row, KB_ROWS, -1);
    } else if (c == KEY_CANCEL) {
      _mode = PHRASE;
      _sel = 0;
    } else if (ok) {
      typeKey();
    }
    return true;
  }
};
