#pragma once

// Messages written on the device (Write page and replies), newest last.
// Kept in RAM: the list starts empty after a reboot.

#include <Arduino.h>
#include <string.h>

#ifndef COMPOSE_MAX_TEXT
  #define COMPOSE_MAX_TEXT  120
#endif
#ifndef SENT_LOG_SIZE
  #define SENT_LOG_SIZE  20
#endif
#define SENT_RE_LEN  48   // bytes of the message that was answered

// a recipient on the Write page: a device channel or a contact (pub key prefix)
struct ComposeTarget {
  uint8_t is_ch;
  uint8_t ch;
  uint8_t key[6];
  bool same(const ComposeTarget& o) const {
    return is_ch == o.is_ch && (is_ch ? ch == o.ch : memcmp(key, o.key, 6) == 0);
  }
};

struct SentEntry {
  unsigned long at;          // millis() when sent
  ComposeTarget to;
  uint8_t kind;              // 0 failed, 1 direct, 2 flood, 3 channel
  bool delivered;
  uint32_t ack;              // expected ack of a direct message
  unsigned long ack_until;   // millis() after which there is no reply
  char re[SENT_RE_LEN];      // the message this one answers, "" if not a reply
  char text[COMPOSE_MAX_TEXT + 1];

  enum { ST_FAILED, ST_WAIT, ST_DELIVERED, ST_NO_REPLY, ST_CHANNEL };
  uint8_t state() const {
    if (kind == 0) return ST_FAILED;
    if (kind == 3) return ST_CHANNEL;
    if (delivered) return ST_DELIVERED;
    return (long)(millis() - ack_until) < 0 ? ST_WAIT : ST_NO_REPLY;
  }
};

class SentLog {
  SentEntry _e[SENT_LOG_SIZE];
  int _n = 0;      // entries held
  int _last = -1;  // slot of the newest

public:
  int count() const { return _n; }

  // i = 0 is the newest
  SentEntry* get(int i) {
    if (i < 0 || i >= _n) return NULL;
    return &_e[(_last - i + SENT_LOG_SIZE) % SENT_LOG_SIZE];
  }

  SentEntry* add() {
    _last = (_last + 1) % SENT_LOG_SIZE;
    if (_n < SENT_LOG_SIZE) _n++;
    SentEntry* e = &_e[_last];
    memset(e, 0, sizeof(*e));
    e->at = millis();
    return e;
  }

  // the message went out again under a new ack
  void retry(uint32_t old_ack, uint32_t new_ack, unsigned long until) {
    for (int i = 0; i < _n; i++) {
      SentEntry* e = get(i);
      if ((e->kind == 1 || e->kind == 2) && !e->delivered && e->ack == old_ack) {
        e->ack = new_ack;
        e->ack_until = until;
        return;
      }
    }
  }

  // an ack arrived: returns the entry it confirms, or NULL
  SentEntry* ack(const uint8_t* data) {
    for (int i = 0; i < _n; i++) {
      SentEntry* e = get(i);
      if ((e->kind == 1 || e->kind == 2) && !e->delivered && e->ack != 0 && memcmp(data, &e->ack, 4) == 0) {
        e->delivered = true;
        return e;
      }
    }
    return NULL;
  }
};
