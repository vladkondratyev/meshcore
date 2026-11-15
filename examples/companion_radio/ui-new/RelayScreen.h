#pragma once

// Relays page: the smart switches set up with `relay add` (or found here),
// each with its state. Included from UITask.cpp only.
//
//   click  - next line        double - previous line
//   hold   - switch the relay on / off; on "Find relays" look for Bluetooth
//            relay boards nearby; on a found board add it as a new relay
//   triple / quad - back to the home pages

#include "UITask.h"
#include "../MyMesh.h"

class RelayScreen : public UIScreen {
  UITask* _task;
  int _sel = 0, _top = 0;
  uint32_t _seen_seq = 0;   // the last job outcome already shown

  bool ru() const { return _task->langRu(); }
  RelayManager& rm() { return the_mesh.relays; }

  // lines: the relays set up, then "Find relays" (Bluetooth builds), then what was found
  struct Line { uint8_t kind; int8_t idx; };   // kind 0 relay, 1 find, 2 found board
  Line _lines[RELAY_MAX + 1 + RELAY_FOUND_MAX];
  int _n = 0;

  void build() {
    _n = 0;
    for (int i = 0; i < RELAY_MAX; i++) if (rm().get(i)) { _lines[_n].kind = 0; _lines[_n].idx = i; _n++; }
#ifdef RELAY_BLE
    _lines[_n].kind = 1; _lines[_n].idx = -1; _n++;
    for (int k = 0; k < rm().foundCount(); k++) {
      // a board that is already a relay is not offered again
      const RelayFound* f = rm().found(k);
      bool known = false;
      for (int i = 0; i < RELAY_MAX && !known; i++) {
        RelayDef* r = rm().get(i);
        if (r && r->kind == RELAY_LCTECH && memcmp(r->mac, f->mac, 6) == 0) known = true;
      }
      if (!known) { _lines[_n].kind = 2; _lines[_n].idx = k; _n++; }
    }
#endif
    if (_sel >= _n) _sel = _n > 0 ? _n - 1 : 0;
  }

  void lineText(const Line& l, char* left, size_t ln, char* right, size_t rn) {
    right[0] = 0;
    if (l.kind == 0) {
      RelayDef* r = rm().get(l.idx);
      snprintf(left, ln, "%d %s", l.idx + 1, r ? r->name : "?");
      if (rm().busyIndex() == l.idx) snprintf(right, rn, "...");
      else if (r) snprintf(right, rn, "%s", r->state == RELAY_ON ? (ru() ? "ВКЛ" : "ON")
                                         : r->state == RELAY_OFF ? (ru() ? "ВЫКЛ" : "OFF") : "?");
    } else if (l.kind == 1) {
      snprintf(left, ln, "%s", rm().scanning() ? (ru() ? "Поиск..." : "Scanning...") : (ru() ? "Найти реле" : "Find relays"));
    } else {
      const RelayFound* f = rm().found(l.idx);
      snprintf(left, ln, "+ %s", f ? f->name : "?");
      if (f) snprintf(right, rn, "%d", f->rssi);
    }
  }

public:
  RelayScreen(UITask* task) : _task(task) {}

  void enter() { _sel = _top = 0; _seen_seq = rm().resultSeq(); }

  int render(DisplayDriver& display) override {
    if (rm().resultSeq() != _seen_seq) {   // a switch finished: say how it went
      _seen_seq = rm().resultSeq();
      if (rm().lastOk()) _task->notify(UIEventType::ack);
      else if (rm().lastError()[0]) _task->showAlert(rm().lastError(), 1500);
    }
    build();
    display.setTextSize(1);
    char title[24];
    snprintf(title, sizeof(title), ru() ? "Реле: %d" : "Relays: %d", rm().count());
    char shown[48];
    display.translateUTF8ToBlocks(shown, title, sizeof(shown));
    display.setColor(UIColor::secondary_txt);
    display.drawTextEllipsized(0, 0, display.width(), shown);
    display.setColor(UIColor::corp_blue);
    display.drawRect(0, 11, display.width(), 1);

    if (_n == 0) {
      display.setColor(UIColor::primary_txt);
      display.drawTextCentered(display.width() / 2, 22, ru() ? "Нет реле" : "No relays");
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 36, ru() ? "relay add в консоли" : "relay add in console");
      return 2000;
    }
    const int rows = 4;
    if (_sel < _top) _top = _sel;
    if (_sel >= _top + rows) _top = _sel - rows + 1;
    for (int r = 0; r < rows && _top + r < _n; r++) {
      int i = _top + r;
      char left[40], right[12], ls[48], rs[16];
      lineText(_lines[i], left, sizeof(left), right, sizeof(right));
      display.translateUTF8ToBlocks(ls, left, sizeof(ls));
      display.translateUTF8ToBlocks(rs, right, sizeof(rs));
      int y = 14 + r * 12;
      bool sel = i == _sel;
      display.setColor(UIColor::primary_txt);
      if (sel) {
        display.fillRect(0, y - 1, display.width(), 11);
        display.setColor(UIColor::window_bkg);
      } else if (_lines[i].kind != 0) {
        display.setColor(UIColor::secondary_txt);
      }
      int rw = display.getTextWidth(rs);
      display.drawTextEllipsized(2, y, display.width() - rw - 8, ls);
      if (rs[0]) {
        display.setCursor(display.width() - rw - 2, y);
        display.print(rs);
      }
      display.setColor(UIColor::primary_txt);
    }
    return rm().busy() ? 300 : 2000;
  }

  bool handleInput(char c) override {
    build();
    if (c == KEY_SELECT || c == KEY_QUAD || c == KEY_PENTA || c == KEY_CANCEL) {
      _task->gotoHomeScreen();
      return true;
    }
    if (_n == 0) return true;
    if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) { _sel = (_sel + 1) % _n; return true; }
    if (c == KEY_PREV || c == KEY_LEFT || c == KEY_UP) { _sel = (_sel + _n - 1) % _n; return true; }
    if (c == KEY_ENTER) {
      const Line& l = _lines[_sel];
      if (rm().busy()) {
        _task->showAlert(ru() ? "Подождите..." : "Wait...", 800);
      } else if (l.kind == 0) {
        if (!rm().request(l.idx, RELAY_DO_TOGGLE)) _task->showAlert(rm().lastError()[0] ? rm().lastError() : "busy", 1200);
      } else if (l.kind == 1) {
        if (!rm().startScan()) _task->showAlert(rm().lastError()[0] ? rm().lastError() : "busy", 1200);
      } else {
        int slot = rm().addFound(l.idx, 1, NULL);
        if (slot < 0) _task->showAlert(ru() ? "Нет места (8)" : "Full (8)", 1200);
        else {
          char msg[24];
          snprintf(msg, sizeof(msg), ru() ? "Реле %d добавлено" : "Relay %d added", slot + 1);
          _task->showAlert(msg, 1200);
          _task->notify(UIEventType::ack);
        }
      }
      return true;
    }
    return false;
  }
};
