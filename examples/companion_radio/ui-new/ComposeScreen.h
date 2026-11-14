#pragma once

// Write page: pick a channel or a favourite contact, then send a stock phrase
// or a text typed on a one-screen keyboard. Included from UITask.cpp only.
//
// Single button everywhere on this page:
//   click  - next item (keyboard: next key in the row)
//   double - previous item (keyboard: row below)
//   triple - back (keyboard: row above)
//   hold   - choose / type the key (COMPOSE_HOLD_MILLIS here, shorter than elsewhere)
//   quad   - leave to the home pages

#include "UITask.h"
#include "../MyMesh.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>

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
  enum Mode : uint8_t { PICK, PHRASE, KEYS, RESULT };

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

  // stock phrases; "ping" stays in English in both languages.
  // The first PHRASES_P1 share the screen with the keyboard button, the rest
  // fill a second screen (4 rows of 2) that the focus moves to after the last one.
  #define PHRASES_P1  6
  const char* const* phrases(int& n) const {
    static const char* const en[] = {
      "ok", "ping", "yes", "no", "hello", "goodbye",
      "on my way", "arrived", "waiting", "where?", "thanks", "soon", "call back", "SOS",
    };
    static const char* const rus[] = {
      "ок", "ping", "да", "нет", "привет", "пока",
      "в пути", "на месте", "жду", "где ты?", "спасибо", "скоро", "перезвоню", "SOS",
    };
    n = sizeof(en) / sizeof(en[0]);
    return ru() ? rus : en;
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

  void send() {
    if (_len == 0 || _target < 0) return;
    _text[_len] = 0;
    if (isChannel(_target)) {
      _sent = the_mesh.uiSendChannel(_t[_target].ch, _text) ? 3 : 0;
    } else {
      _sent = the_mesh.uiSendDirect(_t[_target].key, _text);
    }
    if (_sent) rememberRecent(_t[_target]);
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
    char name[48];
    if (_target >= 0) targetName(_target, name, sizeof(name));
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

  // screen 1: the keyboard entry on top (item 0), phrases below in two columns;
  // screen 2: the remaining phrases, 4 rows of 2. The title shows 1/2 or 2/2.
  void drawPhrases(DisplayDriver& display) {
    int np;
    const char* const* p = phrases(np);
    bool second = _sel > PHRASES_P1;
    char name[48];
    targetName(_target, name, sizeof(name));
    drawTitle(display, name, second ? "2/2" : "1/2");
    int half = display.width() / 2;
    if (!second) {
      drawCell(display, 2, 15, display.width() - 4, ru() ? "клавиатура" : "keyboard", _sel == 0, true);
      for (int i = 0; i < PHRASES_P1 && i < np; i++) {
        drawCell(display, (i % 2) * half + 2, 15 + (i / 2 + 1) * 12, half - 4, p[i], _sel == i + 1, true);
      }
    } else {
      for (int i = PHRASES_P1; i < np; i++) {
        int k = i - PHRASES_P1;
        drawCell(display, (k % 2) * half + 2, 15 + (k / 2) * 12, half - 4, p[i], _sel == i + 1, true);
      }
    }
  }

  void drawKeys(DisplayDriver& display) {
    // text line: the tail of the text with a cursor, or the recipient while empty
    char count[8];
    snprintf(count, sizeof(count), "%d", utf8Count(_text));
    if (_len == 0) {
      char name[48], line[56];
      targetName(_target, name, sizeof(name));
      snprintf(line, sizeof(line), "to %s", name);
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
  ComposeScreen(UITask* task) : _task(task) { _text[0] = 0; }

  void enter() {
    loadTargets();
    _mode = PICK;
    _sel = _top = 0;
    _target = -1;
    _len = 0;
    _text[0] = 0;
  }

  // reply to an incoming message: the sender (or its channel) is the recipient
  // even when it is not a favourite; the phrases come up first
  bool enterReply(const Target& t) {
    if (!exists(t)) return false;
    enter();
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
      send();   // same text, same recipient
      return true;
    }
    if (c == KEY_QUAD || _mode == RESULT) {
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
    if (_mode == PHRASE) {
      int np;
      const char* const* p = phrases(np);
      if (next || c == KEY_DOWN) step(_sel, np + 1, 1);
      else if (prev || c == KEY_UP) step(_sel, np + 1, -1);
      else if (back) {
        _mode = PICK;
        _sel = _target;
        _target = -1;
      } else if (ok) {
        if (_sel == 0) {
          _mode = KEYS;
          _row = 1;   // letters
          _col = 0;
        } else {
          StrHelper::strncpy(_text, p[_sel - 1], sizeof(_text));
          _len = strlen(_text);
          send();
        }
      }
      return true;
    }

    // keyboard: click moves along the row, double goes a row down, triple a row up
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      if (_row == KB_ACTION_ROW) {
        int a = (actionAt(_col) + 1) % ACT_COUNT;
        _col = actionCol(a);
      } else {
        step(_col, cols(), 1);
      }
    } else if (c == KEY_LEFT) {
      if (_row == KB_ACTION_ROW) {
        int a = (actionAt(_col) + ACT_COUNT - 1) % ACT_COUNT;
        _col = actionCol(a);
      } else {
        step(_col, cols(), -1);
      }
    } else if (c == KEY_PREV || c == KEY_DOWN) {
      step(_row, KB_ROWS, 1);
    } else if (c == KEY_SELECT || c == KEY_UP) {
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
