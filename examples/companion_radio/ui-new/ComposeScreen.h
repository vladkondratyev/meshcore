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
//   five   - leave (the text stays as a draft)   hold - type the key
// Keyboard rows: 3 rows of letters (or signs after "#+"), a row of two word
// suggestions, a row of wide keys: space, #+, del, SEND.

#include "UITask.h"
#include "../MyMesh.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>
#include "WordHints.h"
#include "SmartReplies.h"

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

  // keyboard: 3 rows of keys, a row of word suggestions, a row of wide keys.
  // English: 10 keys a row. Russian (`set lang ru`): 12 keys a row, all 33 letters.
  // Layout "std" is ЙЦУКЕН / QWERTY; "freq" puts frequent letters close together
  // and close to the space key, which saves button presses.
  #define KB_ROWS   5
  #define KB_KEY_ROWS  3
  #define KB_HINT_ROW  3
  #define KB_ACTION_ROW  4
  #define KB_MAX_COLS  12
  bool ru() const { return _task->langRu(); }
  int cols() const { return ru() ? 12 : 10; }
  bool _sym = false;   // the key rows show digits and signs
  // UTF-8 string of one row; every key is one letter
  const char* keyRow(int r) const {
    static const char* const en_std[KB_KEY_ROWS]  = { "qwertyuiop", "asdfghjkl.", "zxcvbnm,?!" };
    static const char* const en_freq[KB_KEY_ROWS] = { "itsup?qkyc", "aeolg.!vfd", "hnrw,xzjbm" };
    static const char* const ru_std[KB_KEY_ROWS]  = { "йцукенгшщзхъ", "фывапролджэ.", "ячсмитьбюё,?" };
    static const char* const ru_freq[KB_KEY_ROWS] = { "таилячшфэжзп", "ноеруб.щц,гм", "ксвдьх?ъёюйы" };
    static const char* const en_sym[KB_KEY_ROWS]  = { "1234567890", "-+=/()@#%&", "!?:;\"'*_~$" };
    static const char* const ru_sym[KB_KEY_ROWS]  = { "1234567890.,", "-+=/()@#%&*_", "!?:;\"'~$<>[]" };
    if (r < 0 || r >= KB_KEY_ROWS) return "";
    if (_sym) return ru() ? ru_sym[r] : en_sym[r];
    bool freq = _task->kbFreqLayout();
    if (ru()) return freq ? ru_freq[r] : ru_std[r];
    return freq ? en_freq[r] : en_std[r];
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
  enum Action : uint8_t { ACT_SPACE, ACT_SYM, ACT_DEL, ACT_SEND, ACT_COUNT };
  // first column each wide key covers
  int actionCol(int a) const {
    static const int c10[ACT_COUNT + 1] = { 0, 3, 5, 7, 10 };
    static const int c12[ACT_COUNT + 1] = { 0, 4, 6, 9, 12 };
    return ru() ? c12[a] : c10[a];
  }
  const char* actionLabel(int a) const {
    if (a == ACT_SYM) return _sym ? (ru() ? "абв" : "abc") : "#+";
    static const char* const l[ACT_COUNT] = { "space", "#+", "del", "SEND" };
    return l[a];
  }
  int actionAt(int col) const {
    for (int a = ACT_COUNT - 1; a >= 0; a--) if (col >= actionCol(a)) return a;
    return 0;
  }
  // the suggestion row has two halves; the cursor column picks one
  int hintAt(int col) const { return col < cols() / 2 ? 0 : 1; }

  // ------------------------------------------------------------ word suggestions
  char _hint[2][40];

  // byte offset where the word being typed starts
  int wordStart() const {
    int i = _len;
    while (i > 0 && _text[i - 1] != ' ') i--;
    return i;
  }

  bool hintTaken(const char* w) const {
    for (int k = 0; k < 2; k++) {
      if (!_hint[k][0]) continue;
      const char* a = _hint[k];
      const char* b = w;
      // case-blind equal
      while (*a && *b) {
        uint32_t x, y;
        a = u8next(a, x); b = u8next(b, y);
        if (cpLower(x) != cpLower(y)) break;
        if (!*a && !*b) return true;
      }
    }
    return false;
  }

  void offerHint(const char* w, int n) {
    if (n <= 0 || n >= (int)sizeof(_hint[0])) return;
    char word[40];
    memcpy(word, w, n);
    word[n] = 0;
    if (hintTaken(word)) return;
    for (int k = 0; k < 2; k++) {
      if (!_hint[k][0]) { memcpy(_hint[k], word, n + 1); return; }
    }
  }

  // two words that start with what is being typed: first from what this device
  // sent before, newest first, then from the built-in list
  void updateHints() {
    _hint[0][0] = _hint[1][0] = 0;
    if (_sym) return;
    int ws = wordStart();
    int plen = _len - ws;
    if (plen <= 0) {
      nextWordHints(ws);
      return;
    }
    const char* prefix = &_text[ws];
    SentLog& log = _task->sentLog();
    for (int i = 0; i < log.count() && !_hint[1][0]; i++) {
      const char* t = log.get(i)->text;
      while (*t && !_hint[1][0]) {
        while (*t == ' ') t++;
        const char* e = t;
        while (*e && *e != ' ') e++;
        // drop trailing signs
        const char* end = e;
        while (end > t && (end[-1] == '.' || end[-1] == ',' || end[-1] == '!' || end[-1] == '?' || end[-1] == ':')) end--;
        if (end > t) {
          char word[40];
          int n = end - t;
          if (n < (int)sizeof(word)) {
            memcpy(word, t, n);
            word[n] = 0;
            if (wordExtends(word, prefix, plen)) offerHint(word, n);
          }
        }
        t = e;
      }
    }
    const char* const* dict = ru() ? WORDS_RU : WORDS_EN;
    int nd = ru() ? sizeof(WORDS_RU) / sizeof(WORDS_RU[0]) : sizeof(WORDS_EN) / sizeof(WORDS_EN[0]);
    for (int i = 0; i < nd && !_hint[1][0]; i++) {
      if (wordExtends(dict[i], prefix, plen)) offerHint(dict[i], strlen(dict[i]));
    }
  }

  static bool isSign(char c) { return c == '.' || c == ',' || c == '!' || c == '?' || c == ':'; }

  // after a space: words that usually follow the last one. First pairs from
  // what this device sent, then the built-in pairs (SmartReplies.h).
  void nextWordHints(int ws) {
    int e = ws;
    while (e > 0 && _text[e - 1] == ' ') e--;
    if (e == 0 || isSign(_text[e - 1])) return;   // the start of a sentence
    int b = e;
    while (b > 0 && _text[b - 1] != ' ') b--;
    char prev[40], w[40];
    int n = e - b;
    if (n >= (int)sizeof(prev)) return;
    memcpy(w, &_text[b], n);
    w[n] = 0;
    lowerText(w, prev, sizeof(prev));

    SentLog& log = _task->sentLog();
    for (int i = 0; i < log.count() && !_hint[1][0]; i++) {
      const char* t = log.get(i)->text;
      char last[40];
      last[0] = 0;
      while (*t && !_hint[1][0]) {
        while (*t == ' ') t++;
        const char* x = t;
        while (*x && *x != ' ') x++;
        const char* end = x;
        while (end > t && isSign(end[-1])) end--;
        int len = end - t;
        if (len > 0 && len < (int)sizeof(w)) {
          memcpy(w, t, len);
          w[len] = 0;
          if (last[0] && strcmp(last, prev) == 0) offerHint(w, len);
          lowerText(w, last, sizeof(last));
          if (end < x) last[0] = 0;   // a sign ends the pair
        } else {
          last[0] = 0;
        }
        t = x;
      }
    }
    const NextWords* tbl = ru() ? NEXT_RU : NEXT_EN;
    int nt = ru() ? sizeof(NEXT_RU) / sizeof(NEXT_RU[0]) : sizeof(NEXT_EN) / sizeof(NEXT_EN[0]);
    for (int i = 0; i < nt && !_hint[1][0]; i++) {
      if (strcmp(tbl[i].word, prev) != 0) continue;
      for (int k = 0; listField(tbl[i].next, k, w, sizeof(w)); k++) offerHint(w, strlen(w));
    }
  }

  // replace the word being typed with suggestion k and put a space after it
  void takeHint(int k) {
    if (!_hint[k][0]) return;
    int ws = wordStart();
    bool cap = false;
    if (ws < _len) {
      uint32_t cp;
      u8next(&_text[ws], cp);
      cap = cp != cpLower(cp);   // the typed word starts with a capital
    } else {
      cap = capsPending();       // a whole next word at the start of a sentence
    }
    char word[48];
    int n = 0;
    const char* h = _hint[k];
    bool first = true;
    while (*h && n < (int)sizeof(word) - 4) {
      uint32_t cp;
      h = u8next(h, cp);
      if (first && cap) cp = cpUpper(cp);
      first = false;
      n += u8put(&word[n], cp);
    }
    word[n] = 0;
    if (ws + n + 1 > COMPOSE_MAX_TEXT) { _task->showAlert("Too long", 800); return; }
    memcpy(&_text[ws], word, n);
    _len = ws + n;
    _text[_len++] = ' ';
    _text[_len] = 0;
    updateHints();
  }

  // ------------------------------------------------------------ auto cursor
  // After a letter the cursor moves to the letter most words continue with
  // (or to space when the word is likely complete). Off unless `Auto cursor`.
  void jumpToLikely() {
    if (!_task->autoCursor() || _sym) return;
    int ws = wordStart();
    int plen = _len - ws;
    if (plen <= 0) return;
    const char* prefix = &_text[ws];
    int pcount = utf8Count(prefix);
    #define JUMP_CAND 12
    uint32_t cand[JUMP_CAND];
    long score[JUMP_CAND];
    int nc = 0;
    const char* const* dict = ru() ? WORDS_RU : WORDS_EN;
    int nd = ru() ? sizeof(WORDS_RU) / sizeof(WORDS_RU[0]) : sizeof(WORDS_EN) / sizeof(WORDS_EN[0]);
    // one word: what follows the typed part, weighted
    auto tally = [&](const char* word, long weight) {
      if (!wordExtends(word, prefix, plen) && !(utf8Count(word) == pcount)) return;
      // the word equal to the prefix: the next key is space
      const char* p = word;
      uint32_t cp = 0;
      for (int k = 0; k < pcount && *p; k++) p = u8next(p, cp);
      if (!wordExtends(word, prefix, plen)) {
        // same length: only counts when it is the same word
        char a[40], b[40];
        lowerText(word, a, sizeof(a));
        lowerText(prefix, b, sizeof(b));
        if (strcmp(a, b) != 0) return;
        cp = ' ';
      } else {
        u8next(p, cp);
        cp = cpLower(cp);
      }
      for (int i = 0; i < nc; i++) if (cand[i] == cp) { score[i] += weight; return; }
      if (nc < JUMP_CAND) { cand[nc] = cp; score[nc] = weight; nc++; }
    };
    SentLog& log = _task->sentLog();
    for (int i = 0; i < log.count(); i++) {   // words sent before count most
      const char* t = log.get(i)->text;
      while (*t) {
        while (*t == ' ') t++;
        const char* e = t;
        while (*e && *e != ' ') e++;
        const char* end = e;
        while (end > t && isSign(end[-1])) end--;
        char w[40];
        int len = end - t;
        if (len > 0 && len < (int)sizeof(w)) {
          memcpy(w, t, len);
          w[len] = 0;
          tally(w, nd * 2);
        }
        t = e;
      }
    }
    for (int i = 0; i < nd; i++) tally(dict[i], nd - i);
    int best = -1;
    for (int i = 0; i < nc; i++) if (best < 0 || score[i] > score[best]) best = i;
    if (best < 0) return;
    if (cand[best] == ' ') {   // the word looks complete: go to space
      _row = KB_ACTION_ROW;
      _col = actionCol(ACT_SPACE);
      return;
    }
    for (int r = 0; r < KB_KEY_ROWS; r++) {
      for (int c = 0; c < cols(); c++) {
        char key[5];
        keyAt(r, c, key);
        uint32_t cp;
        u8next(key, cp);
        if (cpLower(cp) == cand[best]) { _row = r; _col = c; return; }
      }
    }
  }

  // ------------------------------------------------------------ capitals
  // the next letter is a capital: at the start and after . ! ? and a space
  bool capsPending() const {
    if (!_task->autoCaps() || _sym) return false;
    if (_len == 0) return true;
    if (_text[_len - 1] != ' ') return false;
    int i = _len - 1;
    while (i > 0 && _text[i - 1] == ' ') i--;
    if (i == 0) return true;
    char c = _text[i - 1];
    return c == '.' || c == '!' || c == '?';
  }

  // key label as typed now: a capital while one is due
  void shownKey(const char* key, char* out) const {
    if (!*key) { out[0] = 0; return; }
    uint32_t cp;
    const char* e = u8next(key, cp);
    if (capsPending() && cpIsLetter(cp)) {
      int n = u8put(out, cpUpper(cp));
      strcpy(&out[n], e);
    } else {
      strcpy(out, key);
    }
  }

  // ------------------------------------------------------------ drafts
  // Text typed for a recipient and not sent stays here until it is sent or
  // the device restarts; it comes back when the keyboard opens.
  #define COMPOSE_DRAFTS 4
  struct Draft { ComposeTarget to; char text[COMPOSE_MAX_TEXT + 1]; };
  Draft _drafts[COMPOSE_DRAFTS];
  int _ndrafts = 0;
  bool _typed = false;   // _text was typed (keyboard), not a phrase

  int findDraft(const ComposeTarget& t) const {
    for (int i = 0; i < _ndrafts; i++) if (_drafts[i].to.same(t)) return i;
    return -1;
  }

  void clearDraft(const ComposeTarget& t) {
    int i = findDraft(t);
    if (i < 0) return;
    for (; i < _ndrafts - 1; i++) _drafts[i] = _drafts[i + 1];
    _ndrafts--;
  }

  void saveDraft() {
    if (!_typed || _target < 0) return;
    const ComposeTarget& t = _t[_target];
    clearDraft(t);
    if (_len == 0) return;
    if (_ndrafts == COMPOSE_DRAFTS) _ndrafts--;   // the oldest goes
    for (int i = _ndrafts; i > 0; i--) _drafts[i] = _drafts[i - 1];
    _drafts[0].to = t;
    StrHelper::strncpy(_drafts[0].text, _text, sizeof(_drafts[0].text));
    _ndrafts++;
  }

  // the keyboard opens: bring the draft back if nothing is typed yet
  void startTyping() {
    if (!_typed && _target >= 0) {
      _len = 0;
      _text[0] = 0;
      int i = findDraft(_t[_target]);
      if (i >= 0) {
        StrHelper::strncpy(_text, _drafts[i].text, sizeof(_text));
        _len = strlen(_text);
        _task->showAlert(ru() ? "Черновик" : "Draft", 900);
      }
    }
    _typed = true;
    updateHints();
  }

  unsigned long _keys_hint_until = 0;   // the button legend shows in the title until then

  // stock phrases (UiPhrases.h) or the ones set from the console. Screen 1
  // has the keyboard button on top and 3 rows of phrases below it, further
  // screens 4 rows. A phrase that fits half the width shares its row, a longer
  // one takes the whole row. The focus moves on to the next screen after the
  // last phrase of one; the layout is worked out when drawing.
  // A reply puts up to 3 ready answers in front of them (SmartReplies.h).
  #define SMART_MAX 3
  char _smart[SMART_MAX][UI_PHRASE_LEN];
  int _nsmart = 0;
  int numPhrases() const { return _nsmart + UI_PHRASE_COUNT; }
  const char* phraseAt(int i) const { return i < _nsmart ? _smart[i] : the_mesh.uiPhrase(i - _nsmart); }
  bool isSmart(int i) const { return i < _nsmart; }

  void addSmart(const char* a) {
    if (_nsmart >= SMART_MAX) return;
    for (int i = 0; i < _nsmart; i++) if (strcmp(_smart[i], a) == 0) return;
    StrHelper::strncpy(_smart[_nsmart++], a, UI_PHRASE_LEN);
  }

  // answers that fit the message being answered; the first matching rules win
  void findSmart(const char* msg) {
    _nsmart = 0;
    if (!msg || !msg[0]) return;
    char low[MAX_TEXT_LEN + 8];
    lowerText(msg, low, sizeof(low));
    // a channel message starts with "Name: ", the name is not part of the question
    char* body = strstr(low, ": ");
    if (body && body - low < 32) body += 2; else body = low;
    const SmartRule* rules = ru() ? SMART_RU : SMART_EN;
    int nr = ru() ? sizeof(SMART_RU) / sizeof(SMART_RU[0]) : sizeof(SMART_EN) / sizeof(SMART_EN[0]);
    for (int r = 0; r < nr && _nsmart < SMART_MAX; r++) {
      char key[32];
      bool hit = false;
      for (int k = 0; listField(rules[r].keys, k, key, sizeof(key)); k++) {
        if (strstr(body, key)) { hit = true; break; }
      }
      if (!hit) continue;
      char ans[UI_PHRASE_LEN];
      for (int k = 0; listField(rules[r].answers, k, ans, sizeof(ans)); k++) addSmart(ans);
    }
  }

  struct Slot { uint8_t page, row, col, wide; };   // col 0 left, 1 right
  Slot _slot[SMART_MAX + UI_PHRASE_COUNT];
  int _pages = 1;

  void layoutPhrases(DisplayDriver& display) {
    int half = display.width() / 2;
    int page = 0, row = 1, col = 0;   // row 0 of screen 1 is the keyboard button
    char shown[UI_PHRASE_LEN + 4];
    for (int i = 0; i < numPhrases(); i++) {
      display.translateUTF8ToBlocks(shown, phraseAt(i), sizeof(shown));
      bool wide = display.getTextWidth(shown) + (isSmart(i) ? 6 : 0) > half - 6;
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
    if (_typed) {
      if (_sent) clearDraft(_t[_target]);   // gone out: no draft any more
      else saveDraft();                    // failed: keep the text for later
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

  // PHRASE items: 0 the keyboard, 1 my position (while GPS is on), then the phrases
  bool gpsItem() const { return _task->gpsOn(); }
  int firstPhrase() const { return gpsItem() ? 2 : 1; }

  // screen 1: the keyboard button on top (item 0) with "my position" next to
  // it while GPS is on, phrases below;
  // further screens: the next phrases. The title shows 1/3, 2/3...
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
      if (gpsItem()) {
        drawCell(display, 2, 15, half - 4, ru() ? "клавиши" : "keys", _sel == 0, true);
        drawCell(display, half + 2, 15, half - 4, ru() ? "моё место" : "location", _sel == 1, true);
      } else {
        drawCell(display, 2, 15, w - 4, ru() ? "клавиатура" : "keyboard", _sel == 0, true);
      }
    }
    for (int i = 0; i < numPhrases(); i++) {
      const Slot& sl = _slot[i];
      if (sl.page != page) continue;
      display.translateUTF8ToBlocks(shown, phraseAt(i), sizeof(shown));
      if (isSmart(i)) {   // a ready answer to this message: "> да"
        char tmp[sizeof(shown)];
        snprintf(tmp, sizeof(tmp), ">%s", shown);
        StrHelper::strncpy(shown, tmp, sizeof(shown));
      }
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
    // letters typed / letters that fit (a Russian letter takes 2 bytes)
    char count[12];
    int per = ru() ? 2 : 1;
    snprintf(count, sizeof(count), "%d/%d", utf8Count(_text), utf8Count(_text) + (COMPOSE_MAX_TEXT - _len) / per);
    if (_mode == KEYS && (long)(millis() - _keys_hint_until) < 0) {
      drawTitle(display, ru() ? "1> 2< 3v 4^ 5:выход" : "1> 2< 3v 4^ 5:exit", NULL);
      return;
    }
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
      if (r == KB_HINT_ROW) {   // two suggested words; a dim dash when there is none
        int cur = hintAt(_col);
        for (int k = 0; k < 2; k++) {
          int x0 = k * w / 2, x1 = (k + 1) * w / 2;
          char shown[48];
          display.translateUTF8ToBlocks(shown, _hint[k][0] ? _hint[k] : "-", sizeof(shown));
          if (!_hint[k][0] && !(_row == r && cur == k)) display.setColor(UIColor::secondary_txt);
          drawCell(display, x0, y, x1 - x0 - 1, shown, _row == r && cur == k);
        }
        continue;
      }
      for (int c = 0; c < kc; c++) {
        char cell[5], shown[8];
        keyAt(r, c, cell);
        shownKey(cell, shown);
        int x0 = c * w / kc, x1 = (c + 1) * w / kc;
        drawCell(display, x0, y, x1 - x0, shown, _row == r && _col == c);
        if (c == _col && r != _row && _row < KB_KEY_ROWS) {   // the cursor column, faintly
          display.setColor(UIColor::secondary_txt);
          display.fillRect(x0 + 1, y + 8, x1 - x0 - 2, 1);
          display.setColor(UIColor::primary_txt);
        }
      }
      if (r == _row) {   // the cursor row is framed
        display.setColor(UIColor::secondary_txt);
        display.drawRect(0, y - 1, w, 10);
        display.setColor(UIColor::primary_txt);
      }
    }
  }

  bool canRetry() const {
    return _sent == 0 || ((_sent == 1 || _sent == 2) && the_mesh.uiAckState() == MyMesh::UI_ACK_LOST);
  }

  void drawResult(DisplayDriver& display) {
    drawTargetTitle(display);
    const char* line = "Send failed";
    char attempt_line[32];
    if (_sent == 3) {
      line = "Sent to channel";
    } else if (_sent) {
      switch (the_mesh.uiAckState()) {
        case MyMesh::UI_ACK_OK:   line = "Delivered"; break;
        case MyMesh::UI_ACK_LOST: line = "No reply"; break;
        default:
          snprintf(attempt_line, sizeof(attempt_line), "Try %d/%d (%s)...", the_mesh.uiAttempt(), UI_SEND_ATTEMPTS,
                   the_mesh.uiAttempt() == 1 ? "flood" : "retry");
          line = attempt_line;
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

  // one letter or sign from the keyboard, a capital when one is due
  void typeLetter(const char* key) {
    char out[8];
    shownKey(key, out);
    int kl = strlen(out);
    if (_len + kl > COMPOSE_MAX_TEXT) {
      _task->showAlert("Too long", 800);
      return;
    }
    memcpy(&_text[_len], out, kl + 1);
    _len += kl;
  }

  void typeKey() {
    if (_row == KB_HINT_ROW) {
      takeHint(hintAt(_col));
      return;
    }
    if (_row == KB_ACTION_ROW) {
      switch (actionAt(_col)) {
        case ACT_SPACE:
          if (_len > 0 && _len < COMPOSE_MAX_TEXT && _text[_len - 1] != ' ') { _text[_len++] = ' '; _text[_len] = 0; }
          break;
        case ACT_SYM:
          _sym = !_sym;
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
          return;
      }
      updateHints();
      return;
    }
    char key[5];
    keyAt(_row, _col, key);
    typeLetter(key);
    updateHints();
    jumpToLikely();
  }

public:
  ComposeScreen(UITask* task) : _task(task) { _text[0] = 0; _re[0] = 0; }

  void enter() {
    loadTargets();
    _mode = PICK;
    _sel = _top = 0;
    _target = -1;
    _len = 0;
    _text[0] = 0;
    _re[0] = 0;
    _typed = false;
    _sym = false;
    _hint[0][0] = _hint[1][0] = 0;
    _nsmart = 0;
  }

  // the Write page is left for another screen: typed text becomes a draft
  void onLeave() {
    if (_mode != RESULT) saveDraft();
    _typed = false;
  }

  // write to a given recipient, even one that is not a favourite; the phrases
  // come up first. re = the incoming message being answered, or NULL.
  bool enterTo(const Target& t, const char* re) {
    if (!exists(t)) return false;
    enter();
    findSmart(re);   // ready answers to this message go first
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
    if (_mode == PHRASE) {
      int items = numPhrases() + firstPhrase();
      if (_sel >= items) _sel = 0;
      if (next || c == KEY_DOWN) step(_sel, items, 1);
      else if (prev || c == KEY_UP) step(_sel, items, -1);
      else if (back) {
        saveDraft();   // typed text stays with its recipient
        _typed = false;
        _len = 0;
        _text[0] = 0;
        _mode = PICK;
        _sel = _target;
        _target = -1;
        _re[0] = 0;   // another recipient is not a reply
        _nsmart = 0;
      } else if (ok) {
        if (_sel == 0) {
          startTyping();
          _mode = KEYS;
          _sym = false;
          _row = 1;   // middle letter row
          _col = 0;
          _keys_hint_until = millis() + 2500;   // the button legend first
        } else if (gpsItem() && _sel == 1) {   // my position as geo:lat,lon
          char pos[48];
          if (!the_mesh.uiMyPosition(pos, sizeof(pos))) {
            _task->showAlert(ru() ? "Нет GPS-фикса" : "No GPS fix", 1200);
          } else {
            saveDraft();
            _typed = false;
            StrHelper::strncpy(_text, pos, sizeof(_text));
            _len = strlen(_text);
            send();
          }
        } else {
          saveDraft();   // a phrase does not replace what was typed
          _typed = false;
          StrHelper::strncpy(_text, phraseAt(_sel - firstPhrase()), sizeof(_text));
          _len = strlen(_text);
          send();
        }
      }
      return true;
    }

    // keyboard: 1 click right, 2 left, 3 down, 4 up (5 leaves, see above)
    if (c == KEY_NEXT || c == KEY_RIGHT || c == KEY_PREV || c == KEY_LEFT) {
      _keys_hint_until = 0;
      int d = (c == KEY_NEXT || c == KEY_RIGHT) ? 1 : -1;
      if (_row == KB_ACTION_ROW) {
        int a = (actionAt(_col) + ACT_COUNT + d) % ACT_COUNT;
        _col = actionCol(a);
      } else if (_row == KB_HINT_ROW) {
        _col = hintAt(_col) == 0 ? cols() / 2 : 0;   // the other word
      } else {
        step(_col, cols(), d);
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
