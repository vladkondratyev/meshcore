#pragma once

// Ready answers for a reply: the incoming message is matched against a few
// key words and the matching answers come first on the phrase screen.
// Next-word pairs for the keyboard suggestions live here too.

#include <string.h>
#include "WordHints.h"

struct SmartRule {
  const char* keys;      // key words separated by '|', lower case; a match anywhere in the text
  const char* answers;   // up to 3 answers separated by '|'
};

static const SmartRule SMART_RU[] = {
  { "sos|помоги|помощь|спасите|авария", "иду|где ты?|вызываю помощь" },
  { "где ты|где вы|ты где|вы где|где сейчас|куда", "в пути|на месте|дома" },
  { "когда|во сколько|скоро ли|долго", "скоро|через 10 мин|через час" },
  { "как дела|как ты|как вы|как сам|всё ок|все ок|нормально?", "нормально|хорошо|всё ок" },
  { "слышишь|слышно|связь|приём|прием|ping|тест|test", "слышу хорошо|слышу плохо|есть связь" },
  { "сколько", "мало|много|не знаю" },
  { "спасибо|благодар", "пожалуйста|не за что" },
  { "привет|здравствуй|добрый|доброе", "привет|на связи" },
  { "пока|до встречи|спокойной", "пока|до связи" },
  { "можно|можешь|сможешь|придёшь|придешь|будешь", "да|нет|позже" },
  { "?", "да|нет|не знаю" },
};

static const SmartRule SMART_EN[] = {
  { "sos|help|emergency|accident", "coming|where are you?|calling help" },
  { "where are|where r u|where is|location", "on my way|arrived|at home" },
  { "when|what time|how long|eta", "soon|in 10 min|in an hour" },
  { "how are|how r u|all ok|you ok", "fine|good|all ok" },
  { "hear me|copy|signal|ping|test|radio check", "loud and clear|weak signal|copy" },
  { "how many|how much", "few|many|don't know" },
  { "thanks|thank you|thx", "you're welcome|no problem" },
  { "hello|hi |hey|good morning|good evening", "hello|here" },
  { "bye|see you|good night", "bye|talk later" },
  { "can you|could you|will you|are you coming", "yes|no|later" },
  { "?", "yes|no|don't know" },
};

// word -> likely next words, separated by '|'
struct NextWords { const char* word; const char* next; };

static const NextWords NEXT_RU[] = {
  { "буду", "через|дома|скоро" }, { "через", "минут|час|10" }, { "я", "буду|еду|дома" },
  { "на", "месте|связи|работе" }, { "в", "пути|лесу|машине" }, { "у", "моста|машины|дома" },
  { "не", "знаю|могу|слышу" }, { "все", "хорошо|нормально" }, { "всё", "хорошо|нормально" },
  { "где", "ты|вы" }, { "ты", "где|скоро" }, { "скоро", "буду" }, { "жду", "тебя|у" },
  { "до", "встречи|завтра" }, { "доброе", "утро" }, { "добрый", "день|вечер" },
  { "спокойной", "ночи" }, { "как", "дела|ты" }, { "иду", "домой|к" }, { "еду", "домой|к" },
  { "позвони", "мне" }, { "мы", "на|в" }, { "приеду", "через|завтра" }, { "слышу", "хорошо|плохо" },
  { "есть", "связь" }, { "очень", "хорошо" }, { "всем", "привет" },
};

static const NextWords NEXT_EN[] = {
  { "i", "am|will|can" }, { "i'm", "on|at|here" }, { "on", "my|the" }, { "my", "way" },
  { "at", "home|the" }, { "see", "you" }, { "will", "be" }, { "be", "there|back" },
  { "in", "10|an" }, { "an", "hour" }, { "good", "morning|night" }, { "thank", "you" },
  { "where", "are" }, { "are", "you" }, { "how", "are" }, { "call", "me" }, { "all", "ok|good" },
  { "loud", "and" }, { "and", "clear" }, { "copy", "that" },
};

// lower-cases UTF-8 text (Latin and Cyrillic, ё -> е) into buf
static inline void lowerText(const char* s, char* buf, size_t n) {
  size_t o = 0;
  while (*s && o + 4 < n) {
    uint32_t cp;
    s = u8next(s, cp);
    cp = cpLower(cp);
    if (cp == 0x451) cp = 0x435;
    o += u8put(&buf[o], cp);
  }
  buf[o] = 0;
}

// copies field k of a '|' list into out; false when there is no such field
static inline bool listField(const char* list, int k, char* out, size_t n) {
  for (int i = 0; i < k; i++) {
    list = strchr(list, '|');
    if (!list) return false;
    list++;
  }
  const char* e = strchr(list, '|');
  size_t len = e ? (size_t)(e - list) : strlen(list);
  if (len == 0 || len >= n) return false;
  memcpy(out, list, len);
  out[len] = 0;
  return true;
}
