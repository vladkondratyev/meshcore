#pragma once

// Word completion for the Write keyboard: frequent words of chats on the
// air, most useful first, and UTF-8 helpers for case-blind matching.
// Words the user sent from the device (Sent list) are offered before these.

#include <stdint.h>
#include <string.h>

static const char* const WORDS_RU[] = {
  "привет", "пока", "спасибо", "хорошо", "сейчас", "сегодня", "завтра", "где", "когда",
  "буду", "будем", "будет", "приеду", "приду", "еду", "иду", "жду", "через", "минут",
  "минуту", "часа", "час", "дома", "работе", "связь", "слышу", "слышно", "сигнал",
  "нормально", "понял", "поняла", "понятно", "давай", "давайте", "можно", "нужно", "надо",
  "конечно", "тоже", "только", "уже", "ещё", "очень", "сколько", "почему", "потому",
  "что", "это", "как", "все", "всё", "всем", "есть", "нет", "был", "была", "было",
  "мы", "вы", "они", "меня", "тебя", "тебе", "мне", "нас", "вас", "вам", "нам",
  "здесь", "там", "туда", "сюда", "рядом", "около", "возле", "место", "встреча",
  "встретимся", "позвони", "позвоню", "перезвоню", "напиши", "напишу", "ответь", "помощь",
  "помоги", "срочно", "внимание", "осторожно", "вода", "еда", "лагерь", "машина",
  "машине", "дорога", "дороге", "мост", "моста", "река", "лес", "лесу", "север", "юг",
  "запад", "восток", "километров", "метров", "направо", "налево", "прямо", "назад",
  "вперёд", "быстро", "медленно", "скоро", "потом", "позже", "раньше", "вчера", "ночью",
  "днём", "утром", "вечером", "утро", "вечер", "время", "сообщение", "сообщения",
  "канал", "сеть", "ретранслятор", "репитер", "антенна", "зарядка", "батарея", "телефон",
  "проверка", "тест", "работает", "получил", "получила", "отправил", "доброе", "добрый",
  "день", "спокойной", "ночи", "удачи", "хорошего", "отлично", "плохо", "холодно",
  "тепло", "жарко", "дождь", "снег", "ветер", "погода", "температура", "градусов",
  "пожалуйста", "извини", "прости", "согласен", "согласна", "вопрос", "ответ",
  "новости", "ребята", "друзья", "выезжаю", "выходим", "выезжаем", "вернусь",
  "вернулся", "добрался", "добрались", "приехал", "приехали", "пришёл", "пришли",
  "ушёл", "уехал", "остался", "задерживаюсь", "опаздываю", "подожди", "подождите",
  "стою", "сижу", "делаешь", "делаете", "дела", "знаю", "знаешь", "думаю", "может",
  "могу", "можешь", "хочу", "хочешь", "вижу", "видишь", "слушаю", "говорю", "скажи",
  "тогда", "если", "или", "но", "да", "ладно", "точно", "правда", "вместе", "один",
  "одна", "два", "три", "много", "мало", "больше", "меньше", "первый", "последний",
  "новый", "старый", "большой", "маленький", "сразу", "опять", "снова", "всегда",
};

static const char* const WORDS_EN[] = {
  "hello", "thanks", "thank", "please", "sorry", "okay", "good", "great", "fine",
  "where", "when", "what", "there", "here", "the", "you", "your", "and", "are", "with",
  "from", "about", "after", "before", "still", "just", "also", "very", "now", "soon",
  "later", "today", "tomorrow", "tonight", "morning", "evening", "afternoon", "minutes",
  "hours", "home", "work", "back", "coming", "going", "leaving", "arrived", "waiting",
  "wait", "call", "message", "signal", "battery", "weather", "rain", "snow", "wind",
  "cold", "warm", "water", "food", "camp", "car", "road", "bridge", "river", "north",
  "south", "east", "west", "left", "right", "straight", "meet", "meeting", "help",
  "emergency", "safe", "careful", "see", "hear", "copy", "roger", "test", "testing",
  "check", "received", "sent", "send", "channel", "network", "repeater", "antenna",
  "charge", "phone", "can", "will", "need", "have", "know", "think", "want", "where's",
  "everyone", "everything", "something", "nothing", "maybe", "sure", "yes", "nope",
  "near", "close", "far", "fast", "slow", "again", "always", "never", "first", "last",
  "location", "position", "status", "working", "problem", "question", "answer", "news",
  "friends", "people", "night", "good night", "goodbye", "welcome", "understood",
};

// next UTF-8 code point
static inline const char* u8next(const char* s, uint32_t& cp) {
  uint8_t b = (uint8_t)*s;
  if (b < 0x80) { cp = b; return s + 1; }
  int n = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1;
  cp = b & (0x3F >> n);
  s++;
  for (int i = 0; i < n && ((uint8_t)*s & 0xC0) == 0x80; i++, s++) cp = (cp << 6) | ((uint8_t)*s & 0x3F);
  return s;
}

// writes cp as UTF-8, returns bytes written (1..3)
static inline int u8put(char* d, uint32_t cp) {
  if (cp < 0x80) { d[0] = (char)cp; return 1; }
  if (cp < 0x800) { d[0] = (char)(0xC0 | (cp >> 6)); d[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  d[0] = (char)(0xE0 | (cp >> 12)); d[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); d[2] = (char)(0x80 | (cp & 0x3F));
  return 3;
}

static inline uint32_t cpLower(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 0x20;
  if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;   // А..Я
  if (cp == 0x401) return 0x451;                       // Ё
  return cp;
}

static inline uint32_t cpUpper(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - 0x20;
  if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;
  if (cp == 0x451) return 0x401;
  return cp;
}

static inline bool cpIsLetter(uint32_t cp) {
  cp = cpLower(cp);
  return (cp >= 'a' && cp <= 'z') || (cp >= 0x430 && cp <= 0x44F) || cp == 0x451;
}

// word starts with prefix (case-blind, ё = е) and is longer than it
static inline bool wordExtends(const char* word, const char* prefix, int prefix_len) {
  const char* w = word;
  const char* p = prefix;
  const char* pe = prefix + prefix_len;
  while (p < pe) {
    if (!*w) return false;
    uint32_t a, b;
    w = u8next(w, a);
    p = u8next(p, b);
    a = cpLower(a); b = cpLower(b);
    if (a == 0x451) a = 0x435;
    if (b == 0x451) b = 0x435;
    if (a != b) return false;
  }
  return *w != 0;
}
