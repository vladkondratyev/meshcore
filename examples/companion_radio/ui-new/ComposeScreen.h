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

  // keyboard: 4 rows of 10 keys, then one row of wide keys
  #define KB_COLS   10
  #define KB_ROWS   5
  #define KB_ACTION_ROW  4
  static const char* keyRow(int r) {
    static const char* const rows[KB_ACTION_ROW] = {
      "1234567890",
      "qwertyuiop",
      "asdfghjkl?",
      "zxcvbnm,.!",
    };
    return rows[r];
  }
  enum Action : uint8_t { ACT_SPACE, ACT_DEL, ACT_SEND, ACT_COUNT };
  // first column each wide key covers, and its label
  static int actionCol(int a) { static const int c[ACT_COUNT + 1] = { 0, 4, 7, KB_COLS }; return c[a]; }
  static const char* actionLabel(int a) { static const char* const l[ACT_COUNT] = { "space", "del", "SEND" }; return l[a]; }
  static int actionAt(int col) {
    for (int a = ACT_COUNT - 1; a >= 0; a--) if (col >= actionCol(a)) return a;
    return 0;
  }

  static const char* const* phrases(int& n) {
    static const char* const p[] = { "ok", "ping", "yes", "no", "hello", "goodbye" };
    n = sizeof(p) / sizeof(p[0]);
    return p;
  }

  UITask* _task;
  Mode _mode = PICK;

  // targets: device channels first, then favourite chat/room contacts
  uint8_t _ch[MAX_GROUP_CHANNELS];
  int _num_ch = 0;
  uint8_t _fav[COMPOSE_MAX_FAVS][6];   // pub key prefixes
  int _num_fav = 0;
  int _sel = 0, _top = 0;
  int _target = -1;

  char _text[COMPOSE_MAX_TEXT + 1];
  int _len = 0;
  int _row = 1, _col = 0;   // keyboard cursor; on the action row _col is the column it came from

  int _sent = 0;            // 0 failed, 1 direct, 2 flood, 3 channel
  unsigned long _result_at = 0;

  struct Collect : public ContactVisitor {
    ComposeScreen* s;
    void onContactVisit(const ContactInfo& c) override {
      if (s->_num_fav >= COMPOSE_MAX_FAVS) return;
      if ((c.flags & 0x01) == 0) return;   // bit 0 = favourite
      if (c.type != ADV_TYPE_CHAT && c.type != ADV_TYPE_ROOM) return;
      memcpy(s->_fav[s->_num_fav++], c.id.pub_key, 6);
    }
  };

  int numTargets() const { return _num_ch + _num_fav; }
  bool isChannel(int i) const { return i < _num_ch; }

  void targetName(int i, char* buf, size_t n) {
    if (isChannel(i)) {
      ChannelDetails ch;
      if (the_mesh.getChannel(_ch[i], ch)) {
        snprintf(buf, n, "#%s", ch.name[0] == '#' ? ch.name + 1 : ch.name);
        return;
      }
    } else {
      ContactInfo* c = the_mesh.lookupContactByPubKey(_fav[i - _num_ch], 6);
      if (c) {
        snprintf(buf, n, "@%s", c->name);
        return;
      }
    }
    snprintf(buf, n, "?");
  }

  void loadTargets() {
    _num_ch = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0]) _ch[_num_ch++] = (uint8_t)i;
    }
    _num_fav = 0;
    Collect v;
    v.s = this;
    the_mesh.scanRecentContacts(0, &v);   // latest heard first
  }

  static void step(int& v, int n, int d) {
    if (n > 0) v = (v + d + n) % n;
  }

  void send() {
    if (_len == 0 || _target < 0) return;
    _text[_len] = 0;
    if (isChannel(_target)) {
      _sent = the_mesh.uiSendChannel(_ch[_target], _text) ? 3 : 0;
    } else {
      _sent = the_mesh.uiSendDirect(_fav[_target - _num_ch], _text);
    }
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
    }
  }

  // the keyboard entry on top (item 0), phrases below in two columns
  void drawPhrases(DisplayDriver& display) {
    drawTargetTitle(display);
    int np;
    const char* const* p = phrases(np);
    int half = display.width() / 2;
    drawCell(display, 2, 15, display.width() - 4, "keyboard", _sel == 0, true);
    for (int i = 0; i < np; i++) {
      drawCell(display, (i % 2) * half + 2, 15 + (i / 2 + 1) * 12, half - 4, p[i], _sel == i + 1, true);
    }
  }

  void drawKeys(DisplayDriver& display) {
    // text line: the tail of the text with a cursor, or the recipient while empty
    char count[8];
    snprintf(count, sizeof(count), "%d", _len);
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
    for (int r = 0; r < KB_ROWS; r++) {
      int y = 13 + r * 10;
      if (r == KB_ACTION_ROW) {
        int cur = actionAt(_col);
        for (int a = 0; a < ACT_COUNT; a++) {
          int x0 = actionCol(a) * w / KB_COLS, x1 = actionCol(a + 1) * w / KB_COLS;
          drawCell(display, x0, y, x1 - x0 - 1, actionLabel(a), _row == r && cur == a);
        }
        continue;
      }
      const char* keys = keyRow(r);
      for (int c = 0; c < KB_COLS; c++) {
        char cell[2] = { keys[c], 0 };
        int x0 = c * w / KB_COLS, x1 = (c + 1) * w / KB_COLS;
        drawCell(display, x0, y, x1 - x0, cell, _row == r && _col == c);
      }
    }
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
    display.drawTextCentered(display.width() / 2, 53, "press: back");
  }

  void typeKey() {
    if (_row == KB_ACTION_ROW) {
      switch (actionAt(_col)) {
        case ACT_SPACE:
          if (_len > 0 && _len < COMPOSE_MAX_TEXT && _text[_len - 1] != ' ') { _text[_len++] = ' '; _text[_len] = 0; }
          break;
        case ACT_DEL:
          if (_len > 0) _text[--_len] = 0;
          break;
        default:
          while (_len > 0 && _text[_len - 1] == ' ') _text[--_len] = 0;
          if (_len > 0) send();
          else _task->showAlert("Empty", 800);
      }
      return;
    }
    if (_len >= COMPOSE_MAX_TEXT) {
      _task->showAlert("Too long", 800);
      return;
    }
    _text[_len++] = keyRow(_row)[_col];
    _text[_len] = 0;
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
    if (!waiting && (long)(millis() - _result_at) > 4000) {
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
        step(_col, KB_COLS, 1);
      }
    } else if (c == KEY_LEFT) {
      if (_row == KB_ACTION_ROW) {
        int a = (actionAt(_col) + ACT_COUNT - 1) % ACT_COUNT;
        _col = actionCol(a);
      } else {
        step(_col, KB_COLS, -1);
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
