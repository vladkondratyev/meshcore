#pragma once

// Messages that came in, for the Chats page: kept in RAM, newest last, and
// not dropped when the phone takes them.

#include <Arduino.h>
#include <string.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/TxtDataHelpers.h>
#include "SentLog.h"

#ifndef CHAT_LOG_SIZE
  #define CHAT_LOG_SIZE  24
#endif

struct ChatIn {
  uint32_t ts;                  // RTC time it came in
  ComposeTarget from;           // contact, or the channel it came in on
  char who[32];                 // contact or channel name as it came
  char text[MAX_TEXT_LEN + 1];  // channel messages start with "Name: "
};

// Messages that came in, newest last; kept in RAM
class ChatLog {
  ChatIn _e[CHAT_LOG_SIZE];
  int _n = 0, _last = -1;
public:
  int count() const { return _n; }
  ChatIn* get(int i) {   // 0 = newest
    if (i < 0 || i >= _n) return NULL;
    return &_e[(_last - i + CHAT_LOG_SIZE) % CHAT_LOG_SIZE];
  }
  void add(uint32_t ts, const ComposeTarget& from, const char* who, const char* text) {
    _last = (_last + 1) % CHAT_LOG_SIZE;
    if (_n < CHAT_LOG_SIZE) _n++;
    ChatIn* e = &_e[_last];
    e->ts = ts;
    e->from = from;
    StrHelper::strncpy(e->who, who, sizeof(e->who));
    StrHelper::strncpy(e->text, text, sizeof(e->text));
  }
  uint32_t newestTs() const { return _n ? _e[_last].ts : 0; }
};

