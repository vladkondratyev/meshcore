#pragma once

// Chats page: the last messages per contact or channel, both ways - what came
// in (kept here even after the phone took it) and what was sent from the
// device. Included from UITask.cpp only.
//
//   list    click - next chat   double - previous   hold - open   triple - back
//   chat    click - older       double - newer      hold - reply  triple - list
//           four clicks - ping the contact: how long the reply took, hops, SNR

#include "UITask.h"
#include "../MyMesh.h"
#include "SentLog.h"
#include "ChatLog.h"

#define CHAT_MAX         12   // chats in the list
#define CHAT_ITEMS       (CHAT_LOG_SIZE + SENT_LOG_SIZE)
#ifndef CHAT_PAGE_MILLIS
  #define CHAT_PAGE_MILLIS 4000
#endif

class ChatScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  ChatLog& _log;

  struct Chat { ComposeTarget to; uint32_t last; uint8_t n; };
  Chat _chats[CHAT_MAX];
  int _nchats = 0;
  int _sel = 0;
  bool _open = false;

  // one chat: items newest first; >= 0 incoming (ChatLog index), < 0 sent (-1 - SentLog index)
  int16_t _items[CHAT_ITEMS];
  uint32_t _item_ts[CHAT_ITEMS];
  int _nitems = 0;
  int _i = 0;
  int _page_start = 0, _page_next = 0;
  unsigned long _page_until = 0;
  int _shown = -1, _shown_page = -1;
  bool _ping_here = false;      // a ping to this chat's contact is on
  uint8_t _ping_last = MyMesh::PING_NONE;

  bool ru() const { return _task->langRu(); }

  uint32_t sentTs(const SentEntry* e) const {
    uint32_t now = _rtc->getCurrentTime();
    uint32_t ago = (millis() - e->at) / 1000UL;
    return now > ago ? now - ago : 0;
  }

  static void targetName(const ComposeTarget& t, char* buf, size_t n) {
    if (t.is_ch) {
      ChannelDetails ch;
      if (the_mesh.getChannel(t.ch, ch) && ch.name[0]) {
        snprintf(buf, n, "#%s", ch.name[0] == '#' ? ch.name + 1 : ch.name);
        return;
      }
      snprintf(buf, n, "#%d", t.ch);
    } else {
      ContactInfo* c = the_mesh.lookupContactByPubKey(t.key, 6);
      if (c) snprintf(buf, n, "@%s", c->name);
      else snprintf(buf, n, "@?");
    }
  }

  static void fmtAge(uint32_t secs, char* buf) {
    if (secs < 60) sprintf(buf, "%lus", (unsigned long)secs);
    else if (secs < 3600) sprintf(buf, "%lum", (unsigned long)(secs / 60));
    else if (secs < 86400) sprintf(buf, "%luh", (unsigned long)(secs / 3600));
    else sprintf(buf, "%lud", (unsigned long)(secs / 86400));
  }

  void addChat(const ComposeTarget& t, uint32_t ts) {
    for (int i = 0; i < _nchats; i++) {
      if (_chats[i].to.same(t)) {
        if (ts > _chats[i].last) _chats[i].last = ts;
        if (_chats[i].n < 255) _chats[i].n++;
        return;
      }
    }
    if (_nchats < CHAT_MAX) {
      _chats[_nchats].to = t;
      _chats[_nchats].last = ts;
      _chats[_nchats].n = 1;
      _nchats++;
      return;
    }
    int old = 0;   // full: the quietest chat makes room
    for (int i = 1; i < _nchats; i++) if (_chats[i].last < _chats[old].last) old = i;
    if (ts > _chats[old].last) { _chats[old].to = t; _chats[old].last = ts; _chats[old].n = 1; }
  }

  void buildChats() {
    _nchats = 0;
    for (int i = 0; i < _log.count(); i++) addChat(_log.get(i)->from, _log.get(i)->ts);
    SentLog& s = _task->sentLog();
    for (int i = 0; i < s.count(); i++) addChat(s.get(i)->to, sentTs(s.get(i)));
    // newest chat first
    for (int i = 1; i < _nchats; i++)
      for (int j = i; j > 0 && _chats[j].last > _chats[j - 1].last; j--) {
        Chat t = _chats[j]; _chats[j] = _chats[j - 1]; _chats[j - 1] = t;
      }
    if (_sel >= _nchats) _sel = 0;
  }

  void buildItems() {
    _nitems = 0;
    const ComposeTarget& t = _chats[_sel].to;
    for (int i = 0; i < _log.count() && _nitems < CHAT_ITEMS; i++) {
      if (!_log.get(i)->from.same(t)) continue;
      _items[_nitems] = i;
      _item_ts[_nitems++] = _log.get(i)->ts;
    }
    SentLog& s = _task->sentLog();
    for (int i = 0; i < s.count() && _nitems < CHAT_ITEMS; i++) {
      if (!s.get(i)->to.same(t)) continue;
      _items[_nitems] = -1 - i;
      _item_ts[_nitems++] = sentTs(s.get(i));
    }
    for (int i = 1; i < _nitems; i++)   // newest first
      for (int j = i; j > 0 && _item_ts[j] > _item_ts[j - 1]; j--) {
        int16_t a = _items[j]; _items[j] = _items[j - 1]; _items[j - 1] = a;
        uint32_t b = _item_ts[j]; _item_ts[j] = _item_ts[j - 1]; _item_ts[j - 1] = b;
      }
    _i = 0;
    resetPage();
  }

  void resetPage() {
    _page_start = _page_next = 0;
    _page_until = millis() + CHAT_PAGE_MILLIS;
  }

  void header(DisplayDriver& d, const char* left, const char* right) {
    char shown[48];
    d.translateUTF8ToBlocks(shown, left, sizeof(shown));
    int rw = right ? d.getTextWidth(right) : 0;
    if (right) {
      d.setColor(UIColor::corp_blue);
      d.setCursor(d.width() - rw - 1, 0);
      d.print(right);
    }
    d.setColor(UIColor::secondary_txt);
    d.drawTextEllipsized(0, 0, d.width() - rw - 4, shown);
    d.setColor(UIColor::corp_blue);
    d.drawRect(0, 11, d.width(), 1);
  }

  int renderList(DisplayDriver& d) {
    char pos[12];
    snprintf(pos, sizeof(pos), "%d", _nchats);
    header(d, ru() ? "Переписка" : "Chats", pos);
    if (_nchats == 0) {
      d.setColor(UIColor::primary_txt);
      char shown[40];
      d.translateUTF8ToBlocks(shown, ru() ? "пока пусто" : "nothing yet", sizeof(shown));
      d.drawTextCentered(d.width() / 2, 26, shown);
      return 5000;
    }
    int rows = 4;
    int top = _sel >= rows ? _sel - rows + 1 : 0;
    uint32_t now = _rtc->getCurrentTime();
    for (int i = top; i < _nchats && i < top + rows; i++) {
      int y = 14 + (i - top) * 10;
      char name[40], shown[40], age[8], right[16];
      targetName(_chats[i].to, name, sizeof(name));
      d.translateUTF8ToBlocks(shown, name, sizeof(shown));
      fmtAge(now > _chats[i].last ? now - _chats[i].last : 0, age);
      snprintf(right, sizeof(right), "%u %s", _chats[i].n, age);
      d.setColor(UIColor::primary_txt);
      if (i == _sel) {
        d.fillRect(0, y - 1, d.width(), 10);
        d.setColor(UIColor::window_bkg);
      }
      int rw = d.getTextWidth(right);
      d.drawTextEllipsized(2, y, d.width() - rw - 8, shown);
      d.drawTextRightAlign(d.width() - 2, y, right);
    }
    return 5000;
  }

  int renderChat(DisplayDriver& d) {
    if (_nitems == 0) { _open = false; return renderList(d); }
    if (_i >= _nitems) _i = _nitems - 1;
    int k = _items[_i];
    const char* text;
    char who[48];
    char state[12] = "";
    if (k >= 0) {
      ChatIn* e = _log.get(k);
      text = e->text;
      snprintf(who, sizeof(who), "< %s", e->who);
    } else {
      SentEntry* e = _task->sentLog().get(-1 - k);
      text = e->text;
      snprintf(who, sizeof(who), ru() ? "> я" : "> me");
      switch (e->state()) {
        case SentEntry::ST_DELIVERED: strcpy(state, " ok"); break;
        case SentEntry::ST_WAIT:      strcpy(state, " ..."); break;
        case SentEntry::ST_NO_REPLY:  strcpy(state, " ?"); break;
        case SentEntry::ST_FAILED:    strcpy(state, " x"); break;
        default: break;
      }
    }
    char age[8], right[24];
    uint32_t now = _rtc->getCurrentTime();
    fmtAge(now > _item_ts[_i] ? now - _item_ts[_i] : 0, age);
    snprintf(right, sizeof(right), "%s%s %d/%d", age, state, _i + 1, _nitems);
    header(d, who, right);

    char filtered[MAX_TEXT_LEN + 1];
    d.translateUTF8ToBlocks(filtered, text, sizeof(filtered));
    int len = strlen(filtered);
    if ((_page_next > 0 || _page_start > 0) && (long)(millis() - _page_until) >= 0) {
      _page_start = _page_next > 0 ? _page_next : 0;
      _page_until = millis() + CHAT_PAGE_MILLIS;
    }
    if (_page_start >= len) _page_start = 0;
    d.setColor(UIColor::primary_txt);
    d.setCursor(0, 14);
    const char* rest = d.printWordWrap(&filtered[_page_start], d.width());
    _page_next = (*rest && rest > &filtered[_page_start]) ? (int)(rest - filtered) : 0;

    // bottom row: the ping to this contact, while there is one
    if (_ping_here && !_chats[_sel].to.is_ch) {
      uint8_t st = the_mesh.uiPingState();
      char line[48], shown[48];
      if (st == MyMesh::PING_WAIT) {
        snprintf(line, sizeof(line), ru() ? "пинг... %lu с" : "ping... %lu s", (unsigned long)((millis() - _ping_started) / 1000));
      } else if (st == MyMesh::PING_OK) {
        unsigned long rtt = the_mesh.uiPingRtt();
        int hops = the_mesh.uiPingHops();
        char h[12];
        if (hops < 0) strcpy(h, "?");
        else snprintf(h, sizeof(h), "%d", hops);
        snprintf(line, sizeof(line), ru() ? "пинг %lu.%lu с, хопов %s, SNR %d" : "ping %lu.%lu s, hops %s, SNR %d",
                 rtt / 1000, (rtt % 1000) / 100, h, the_mesh.uiPingSnr() / 4);
      } else {
        snprintf(line, sizeof(line), ru() ? "пинг: нет ответа" : "ping: no reply");
      }
      if (st != _ping_last) {
        if (st == MyMesh::PING_OK || st == MyMesh::PING_LOST) _task->notify(UIEventType::ack);
        _ping_last = st;
      }
      d.translateUTF8ToBlocks(shown, line, sizeof(shown));
      int y = d.height() - 10;
      d.setColor(UIColor::window_bkg);
      d.fillRect(0, y - 1, d.width(), 10);
      d.setColor(UIColor::corp_blue);
      d.drawRect(0, y - 2, d.width(), 1);
      d.setColor(UIColor::primary_txt);
      d.drawTextLeftAlign(0, y, shown);
      if (st == MyMesh::PING_WAIT) return 1000;
    }
    if (_i != _shown || _page_start != _shown_page) {
      d.markFull();
      _shown = _i;
      _shown_page = _page_start;
    }
    if (_page_next > 0 || _page_start > 0) {
      long w = (long)(_page_until - millis());
      return w > 200 ? (int)w : 200;
    }
    return 5000;
  }

  unsigned long _ping_started = 0;

public:
  ChatScreen(UITask* task, mesh::RTCClock* rtc, ChatLog& log) : _task(task), _rtc(rtc), _log(log) {}

  void enter() {
    _open = false;
    _sel = 0;
    _ping_here = false;
    buildChats();
  }
  int chatCount() { buildChats(); return _nchats; }

  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    return _open ? renderChat(d) : renderList(d);
  }

  bool handleInput(char c) override {
    if (!_open) {
      if (c == KEY_SELECT || c == KEY_QUAD) { _task->gotoHomeScreen(); return true; }
      if (_nchats == 0) { if (c == KEY_ENTER) _task->gotoHomeScreen(); return true; }
      if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) _sel = (_sel + 1) % _nchats;
      else if (c == KEY_PREV || c == KEY_LEFT || c == KEY_UP) _sel = (_sel + _nchats - 1) % _nchats;
      else if (c == KEY_ENTER) {
        _open = true;
        _shown = -1;
        _ping_here = false;
        buildItems();
      }
      return true;
    }
    if (c == KEY_SELECT) { _open = false; buildChats(); return true; }   // back to the list
    if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_DOWN) {             // older
      if (_i + 1 < _nitems) { _i++; resetPage(); }
      return true;
    }
    if (c == KEY_PREV || c == KEY_LEFT || c == KEY_UP) {                // newer
      if (_i > 0) { _i--; resetPage(); }
      return true;
    }
    if (c == KEY_ENTER) {                                               // reply
      const char* re = NULL;
      if (_nitems > 0 && _items[_i] >= 0) re = _log.get(_items[_i])->text;
      if (!_task->openReply(_chats[_sel].to, re)) _task->showAlert(ru() ? "Нельзя ответить" : "Can't reply", 1000);
      return true;
    }
    if (c == KEY_QUAD) {                                                // ping the contact
      if (_chats[_sel].to.is_ch) {
        _task->showAlert(ru() ? "Пинг - только контакту" : "Ping: contacts only", 1200);
      } else if (the_mesh.uiPing(_chats[_sel].to.key)) {
        _ping_here = true;
        _ping_started = millis();
        _ping_last = MyMesh::PING_WAIT;
      } else {
        _task->showAlert(ru() ? "Пинг не ушёл" : "Ping failed", 1200);
      }
      return true;
    }
    return true;
  }
};
