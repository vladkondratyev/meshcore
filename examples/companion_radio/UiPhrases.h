#pragma once

// Stock phrases of the Write page. Any of them can be replaced from the
// console: `set phrase N text`, `set phrase N` brings the default back.
// "ping" stays in English in both languages.

#define UI_PHRASE_COUNT  18
#define UI_PHRASE_LEN    32   // bytes with the terminating zero
#define UI_PHRASES_P1     6   // on the first screen, next to the keyboard button
#define UI_PHRASES_PER    8   // on each further screen, 4 rows of 2

static const char* const UI_PHRASES_EN[UI_PHRASE_COUNT] = {
  "ok", "ping", "yes", "no", "hello", "goodbye",
  "on my way", "arrived", "waiting", "where?", "thanks", "soon", "call back", "SOS",
  "weather today", "weather tomorrow", "rate", "moon",
};
static const char* const UI_PHRASES_RU[UI_PHRASE_COUNT] = {
  "ок", "ping", "да", "нет", "привет", "пока",
  "в пути", "на месте", "жду", "где ты?", "спасибо", "скоро", "перезвоню", "SOS",
  "погода сегодня", "погода завтра", "Курс", "луна",
};
