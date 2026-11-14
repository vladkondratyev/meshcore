#pragma once

// Sent page: messages written on this device, newest first. Each one shows
// who or which channel it went to, how long ago, its delivery state and,
// for a reply, the message it answers ("RE: ..."). Included from UITask.cpp only.
//
//   click  - older message
//   double - newer message
//   triple - back to the home pages
//   hold   - write to the same recipient again
//   quad   - make this text a stock phrase: pick the number with clicks,
//            hold saves, triple cancels

#include "UITask.h"
#include "../MyMesh.h"

#ifndef SENT_PAGE_MILLIS
  #define SENT_PAGE_MILLIS  4000
#endif

class SentScreen : public UIScreen {
  UITask* _task;
  int _i = 0;            // 0 = newest
  int _page_start = 0;   // offset of the text page on screen
  int _page_next = 0;    // offset of the next page, 0 when the text ends here
  unsigned long _page_until = 0;
  int _shown_i = -1, _shown_page = -1;   // what is on the glass, for e-ink full refreshes
  bool _saving = false;   // choosing which phrase this text replaces
  int _slot = 0;          // phrase number - 1

  bool ru() const { return _task->langRu(); }

  void renderSave(DisplayDriver& display, SentEntry* e) {
    char line[64], shown[64];
    display.setTextSize(1);
    display.setColor(UIColor::secondary_txt);
    display.drawTextEllipsized(0, 0, display.width(), ru() ? "Сделать фразой" : "Save as phrase");
    display.setColor(UIColor::corp_blue);
    display.drawRect(0, 11, display.width(), 1);
    snprintf(line, sizeof(line), ru() ? "фраза %d: %s" : "phrase %d: %s", _slot + 1, the_mesh.uiPhrase(_slot));
    display.translateUTF8ToBlocks(shown, line, sizeof(shown));
    display.setColor(UIColor::secondary_txt);
    display.drawTextEllipsized(0, 14, display.width(), shown);
    snprintf(line, sizeof(line), ru() ? "станет: %s" : "becomes: %s", e->text);
    display.translateUTF8ToBlocks(shown, line, sizeof(shown));
    display.setColor(UIColor::primary_txt);
    display.drawTextEllipsized(0, 26, display.width(), shown);
    if (strlen(e->text) > UI_PHRASE_LEN - 1) {
      display.setColor(UIColor::warning_txt);
      display.drawTextEllipsized(0, 38, display.width(), ru() ? "длинный: обрежется" : "too long: cut short");
    }
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(display.width() / 2, 52, ru() ? "1:номер держ:да 3:нет" : "1:next hold:yes 3:no");
  }

  void resetPage() {
    _page_start = _page_next = 0;
    _page_until = millis() + SENT_PAGE_MILLIS;
  }

  static void targetName(const ComposeTarget& t, char* buf, size_t n) {
    if (t.is_ch) {
      ChannelDetails ch;
      if (the_mesh.getChannel(t.ch, ch) && ch.name[0]) {
        snprintf(buf, n, "#%s", ch.name[0] == '#' ? ch.name + 1 : ch.name);
        return;
      }
    } else {
      ContactInfo* c = the_mesh.lookupContactByPubKey(t.key, 6);
      if (c) {
        snprintf(buf, n, "@%s", c->name);
        return;
      }
    }
    snprintf(buf, n, "?");
  }

  static void fmtAge(unsigned long ms, char* buf) {
    unsigned long s = ms / 1000;
    if (s < 60) sprintf(buf, "%lus", s);
    else if (s < 3600) sprintf(buf, "%lum", s / 60);
    else if (s < 86400) sprintf(buf, "%luh", s / 3600);
    else sprintf(buf, "%lud", s / 86400);
  }

  static const char* stateLabel(const SentEntry& e) {
    switch (e.state()) {
      case SentEntry::ST_DELIVERED: return "Delivered";
      case SentEntry::ST_NO_REPLY:  return "No reply";
      case SentEntry::ST_CHANNEL:   return "To channel";
      case SentEntry::ST_WAIT:      return "Waiting...";
      default:                      return "Failed";
    }
  }

public:
  SentScreen(UITask* task) : _task(task) {}

  void enter() {
    _i = 0;
    _shown_i = -1;
    _saving = false;
    resetPage();
  }

  int render(DisplayDriver& display) override {
    SentLog& log = _task->sentLog();
    display.setTextSize(1);
    if (log.count() == 0) {
      display.setColor(UIColor::secondary_txt);
      display.drawTextEllipsized(0, 0, display.width(), "Sent");
      display.setColor(UIColor::corp_blue);
      display.drawRect(0, 11, display.width(), 1);
      display.setColor(UIColor::primary_txt);
      display.drawTextCentered(display.width() / 2, 26, "Nothing sent yet");
      return 5000;
    }
    if (_i >= log.count()) _i = log.count() - 1;
    SentEntry* e = log.get(_i);
    if (_saving) {
      renderSave(display, e);
      return 5000;
    }

    // title: recipient on the left, position in the list on the right
    char name[48], shown[48], pos[12];
    targetName(e->to, name, sizeof(name));
    display.translateUTF8ToBlocks(shown, name, sizeof(shown));
    snprintf(pos, sizeof(pos), "%d/%d", _i + 1, log.count());
    int pw = display.getTextWidth(pos);
    display.setColor(UIColor::corp_blue);
    display.setCursor(display.width() - pw - 1, 0);
    display.print(pos);
    display.setColor(UIColor::primary_txt);
    display.drawTextEllipsized(0, 0, display.width() - pw - 4, shown);
    display.setColor(UIColor::corp_blue);
    display.drawRect(0, 11, display.width(), 1);

    // state and age
    char line[40], age[8];
    fmtAge(millis() - e->at, age);
    snprintf(line, sizeof(line), "%s  %s ago", stateLabel(*e), age);
    display.setColor(e->state() == SentEntry::ST_NO_REPLY || e->state() == SentEntry::ST_FAILED
                     ? UIColor::warning_txt : UIColor::secondary_txt);
    display.drawTextEllipsized(0, 14, display.width(), line);

    int y = 25;
    if (e->re[0]) {   // the message this one answers
      char re[SENT_RE_LEN + 8], re_shown[SENT_RE_LEN + 8];
      snprintf(re, sizeof(re), "RE: %s", e->re);
      display.translateUTF8ToBlocks(re_shown, re, sizeof(re_shown));
      display.setColor(UIColor::secondary_txt);
      display.drawTextEllipsized(0, y, display.width(), re_shown);
      y += 11;
    }

    // the text itself; a long one turns its pages by itself
    char text[COMPOSE_MAX_TEXT + 1];
    display.translateUTF8ToBlocks(text, e->text, sizeof(text));
    int len = strlen(text);
    if (_page_next > 0 && (long)(millis() - _page_until) >= 0) {
      _page_start = _page_next;
      _page_until = millis() + SENT_PAGE_MILLIS;
    } else if (_page_next == 0 && _page_start > 0 && (long)(millis() - _page_until) >= 0) {
      resetPage();
    }
    if (_page_start >= len) resetPage();
    display.setColor(UIColor::primary_txt);
    display.setCursor(0, y);
    const char* rest = display.printWordWrap(&text[_page_start], display.width());
    _page_next = (*rest && rest > &text[_page_start]) ? (int)(rest - text) : 0;

    if (_i != _shown_i || _page_start != _shown_page) {
      display.markFull();
      _shown_i = _i;
      _shown_page = _page_start;
    }
    if (_page_next > 0 || _page_start > 0) {
      long wait = (long)(_page_until - millis());
      return wait > 200 ? (wait < 1000 ? wait : 1000) : 200;
    }
    return e->state() == SentEntry::ST_WAIT ? 500 : 1000;
  }

  bool handleInput(char c) override {
    SentLog& log = _task->sentLog();
    int n = log.count();
    if (_saving) {
      if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) _slot = (_slot + 1) % UI_PHRASE_COUNT;
      else if (c == KEY_PREV || c == KEY_LEFT || c == KEY_UP) _slot = (_slot + UI_PHRASE_COUNT - 1) % UI_PHRASE_COUNT;
      else if (c == KEY_ENTER) {
        SentEntry* e = log.get(_i);
        if (e) {
          the_mesh.setUiPhrase(_slot, e->text);
          char msg[24];
          snprintf(msg, sizeof(msg), ru() ? "Фраза %d" : "Phrase %d", _slot + 1);
          _task->showAlert(msg, 1000);
          _task->notify(UIEventType::ack);
        }
        _saving = false;
        _shown_i = -1;
      } else {   // triple, quad, five: cancel
        _saving = false;
        _shown_i = -1;
      }
      return true;
    }
    if (c == KEY_QUAD && n > 0) {   // make this text a phrase
      _saving = true;
      // start at the first stock phrase of the last screen, or after the last own one
      _slot = UI_PHRASE_COUNT - 1;
      for (int i = UI_PHRASE_COUNT - 1; i >= 0; i--) {
        if (the_mesh.uiPhraseCustom(i)) { _slot = (i + 1) % UI_PHRASE_COUNT; break; }
      }
      return true;
    }
    if (c == KEY_QUAD || c == KEY_SELECT || c == KEY_CANCEL || c == KEY_PENTA) {
      _task->gotoHomeScreen();
      return true;
    }
    if (n == 0) {
      _task->gotoHomeScreen();
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) {
      _i = (_i + 1) % n;   // older
      resetPage();
      return true;
    }
    if (c == KEY_PREV || c == KEY_LEFT || c == KEY_UP) {
      _i = (_i + n - 1) % n;   // newer
      resetPage();
      return true;
    }
    if (c == KEY_ENTER) {
      SentEntry* e = log.get(_i);
      if (e && !_task->openWriteTo(e->to)) _task->showAlert("Not found", 1000);
      return true;
    }
    return false;
  }
};
