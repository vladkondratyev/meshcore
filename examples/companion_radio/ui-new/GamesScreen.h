#pragma once

// Games page: three small games for one button. Included from UITask.cpp only.
// Everything is drawn on the 128x64 UI grid, so it scales to every panel.
//
//   menu    click - next game   double - previous   hold - play   triple - back
//   Runner  press - jump (taken at the press, without the double-click wait)
//   Tower   press - drop the moving block onto the tower
//   Snake   click - turn right  double - turn left
//   game over: click - again, triple - back to the menu
// Triple click leaves any game.

#include "UITask.h"
#include "../MyMesh.h"
#include "icons.h"

class GamesScreen : public UIScreen {
  enum Game { RUNNER, TOWER, SNAKE, GAME_COUNT };
  enum State { MENU, PLAY, OVER };

  UITask* _task;
  State _state = MENU;
  uint8_t _game = RUNNER;
  uint16_t* _best;   // records, kept in the node prefs
  uint16_t _no_prefs[GAME_COUNT] = {0, 0, 0};
  uint16_t _score = 0;
  bool _was_pressed = true;   // a press counts only after the button was seen up
  unsigned long _next_step = 0;

  // ---- grid ----
  static const int W = 128, H = 64, TOP = 10;   // score line on top

  bool pressEdge() {
    bool p = _task->isButtonPressed();
    bool edge = p && !_was_pressed;
    _was_pressed = p;
    return edge;
  }

  // ---- Runner ----
  #define RUN_GROUND   60
  #define RUN_PX       16
  #define RUN_PW        6
  #define RUN_PH        9
  #define RUN_OBST      3
  int16_t _ry = 0;             // player height above ground, 1/16 px
  int16_t _rv = 0;             // vertical speed, 1/16 px per step
  int16_t _ox[RUN_OBST], _ow[RUN_OBST], _oh[RUN_OBST];
  uint16_t _rdist = 0;         // steps run; speed grows with it
  uint8_t _rsub = 0;           // sub-pixel scroll, 1/16 px

  int runSpeed() const { return 24 + _rdist / 40; }   // 1/16 px per step: 1.5 px at start
  void runSpawn(int i, int after_x) {
    int gap = 48 + random(0, 56) + runSpeed();
    _ox[i] = after_x + gap;
    _ow[i] = 3 + random(0, 5);
    _oh[i] = 5 + random(0, 7);
  }
  void runStart() {
    _ry = 0; _rv = 0; _rdist = 0; _rsub = 0;
    int x = W + 20;
    for (int i = 0; i < RUN_OBST; i++) { runSpawn(i, x); x = _ox[i]; }
  }
  bool runStep(bool press) {
    if (press && _ry == 0) _rv = 58;            // jump
    if (_ry > 0 || _rv > 0) {
      _ry += _rv;
      _rv -= 5;                                  // gravity
      if (_ry <= 0) { _ry = 0; _rv = 0; }
    }
    int move = runSpeed() + _rsub;
    int px = move / 16;
    _rsub = move % 16;
    int far = 0;
    for (int i = 0; i < RUN_OBST; i++) if (_ox[i] > far) far = _ox[i];
    for (int i = 0; i < RUN_OBST; i++) {
      _ox[i] -= px;
      if (_ox[i] + _ow[i] < 0) { runSpawn(i, far > W ? far : W); far = _ox[i]; }
    }
    _rdist++;
    _score = _rdist / 10;
    // hit: the player's box against each block
    int py_bot = RUN_GROUND - _ry / 16;
    for (int i = 0; i < RUN_OBST; i++) {
      if (_ox[i] < RUN_PX + RUN_PW - 1 && _ox[i] + _ow[i] > RUN_PX + 1 && py_bot > RUN_GROUND - _oh[i]) return false;
    }
    return true;
  }
  void runDraw(DisplayDriver& d) {
    d.fillRect(0, RUN_GROUND, W, 1);
    int py = RUN_GROUND - _ry / 16 - RUN_PH;
    d.fillRect(RUN_PX, py, RUN_PW, RUN_PH - 3);                 // body
    bool step = (_rdist / 4) % 2 == 0 || _ry > 0;
    d.fillRect(RUN_PX + (step ? 0 : 3), py + RUN_PH - 3, 2, 3);   // legs
    d.fillRect(RUN_PX + (step ? 4 : 1), py + RUN_PH - 3, 2, 3);
    for (int i = 0; i < RUN_OBST; i++) {
      if (_ox[i] < W && _ox[i] + _ow[i] > 0) d.fillRect(_ox[i], RUN_GROUND - _oh[i], _ow[i], _oh[i]);
    }
  }

  // ---- Tower ----
  #define TOW_ROW   5
  #define TOW_MAX  32
  int16_t _tx[TOW_MAX], _tw[TOW_MAX];   // placed rows, newest last (older ones scroll away)
  int _tn = 0;
  int16_t _mx = 0, _mw = 0;            // moving block
  int8_t _mdir = 1;
  uint8_t _msub = 0;

  int towSpeed() const { return 12 + _score * 2 > 64 ? 64 : 12 + _score * 2; }   // 1/16 px per step
  void towPush(int x, int w) {
    if (_tn == TOW_MAX) { memmove(_tx, _tx + 1, sizeof(_tx[0]) * (TOW_MAX - 1)); memmove(_tw, _tw + 1, sizeof(_tw[0]) * (TOW_MAX - 1)); _tn--; }
    _tx[_tn] = x; _tw[_tn] = w; _tn++;
  }
  void towStart() {
    _tn = 0;
    towPush(W / 2 - 24, 48);
    _mw = 48; _mx = 0; _mdir = 1; _msub = 0;
  }
  bool towStep(bool press) {
    if (press) {
      int bx = _tx[_tn - 1], bw = _tw[_tn - 1];
      int l = _mx > bx ? _mx : bx;
      int r = (_mx + _mw) < (bx + bw) ? (_mx + _mw) : (bx + bw);
      if (r - l <= 0) return false;              // missed the tower
      if (abs(_mx - bx) <= 1) { l = bx; r = bx + bw; }   // near-perfect keeps the width
      towPush(l, r - l);
      _score++;
      _mw = r - l;
      _mdir = (_score % 2) ? -1 : 1;
      _mx = _mdir > 0 ? 0 : W - _mw;
      _msub = 0;
      return true;
    }
    int move = towSpeed() + _msub;
    _mx += _mdir * (move / 16);
    _msub = move % 16;
    if (_mx <= 0) { _mx = 0; _mdir = 1; }
    if (_mx + _mw >= W) { _mx = W - _mw; _mdir = -1; }
    return true;
  }
  void towDraw(DisplayDriver& d) {
    int rows = (H - TOP - TOW_ROW - 2) / TOW_ROW;   // rows of the tower that fit under the moving one
    int first = _tn > rows ? _tn - rows : 0;
    int y = H - TOW_ROW;
    for (int i = first; i < _tn; i++, y -= TOW_ROW) d.fillRect(_tx[i], y + 1, _tw[i], TOW_ROW - 1);
    d.fillRect(_mx, y + 1, _mw, TOW_ROW - 1);
  }

  // ---- Snake ----
  #define SN_CELL  4
  #define SN_COLS  (W / SN_CELL)               // 32
  #define SN_ROWS  ((H - TOP - 2) / SN_CELL)   // 13
  #define SN_MAX   (SN_COLS * SN_ROWS)
  uint8_t _sx[SN_MAX], _sy[SN_MAX];
  int _shead = 0, _slen = 0;
  uint8_t _sdir = 0;          // 0 right, 1 down, 2 left, 3 up
  int8_t _sturn = 0;          // turn waiting for the next step: +1 right, -1 left
  uint8_t _fx = 0, _fy = 0;
  int snY0() const { return TOP + 2; }

  bool snOn(int x, int y, bool skip_tail) const {
    for (int i = 0; i < _slen - (skip_tail ? 1 : 0); i++) {
      int k = (_shead - i + SN_MAX) % SN_MAX;
      if (_sx[k] == x && _sy[k] == y) return true;
    }
    return false;
  }
  void snFood() {
    for (int tries = 0; tries < 200; tries++) {
      int x = random(0, SN_COLS), y = random(0, SN_ROWS);
      if (!snOn(x, y, false)) { _fx = x; _fy = y; return; }
    }
    for (int y = 0; y < SN_ROWS; y++)    // nearly full board: the first free cell
      for (int x = 0; x < SN_COLS; x++)
        if (!snOn(x, y, false)) { _fx = x; _fy = y; return; }
  }
  void snStart() {
    _slen = 3; _shead = 2; _sdir = 0; _sturn = 0;
    for (int i = 0; i < 3; i++) { _sx[i] = 4 + i; _sy[i] = SN_ROWS / 2; }
    snFood();
  }
  int snStepMs() const { int ms = 220 - _score * 8; return ms < 90 ? 90 : ms; }
  bool snStep() {
    if (_sturn) { _sdir = (_sdir + (_sturn > 0 ? 1 : 3)) % 4; _sturn = 0; }
    int x = _sx[_shead], y = _sy[_shead];
    if (_sdir == 0) x++; else if (_sdir == 1) y++; else if (_sdir == 2) x--; else y--;
    if (x < 0 || y < 0 || x >= SN_COLS || y >= SN_ROWS) return false;   // wall
    bool eat = x == _fx && y == _fy;
    if (snOn(x, y, !eat)) return false;                                    // own tail
    _shead = (_shead + 1) % SN_MAX;
    _sx[_shead] = x; _sy[_shead] = y;
    if (eat) {
      if (_slen < SN_MAX) _slen++;
      _score++;
      snFood();
    }
    return true;
  }
  void snDraw(DisplayDriver& d) {
    int y0 = snY0();
    d.drawRect(0, y0 - 1, W, SN_ROWS * SN_CELL + 2);
    for (int i = 0; i < _slen; i++) {
      int k = (_shead - i + SN_MAX) % SN_MAX;
      d.fillRect(_sx[k] * SN_CELL, y0 + _sy[k] * SN_CELL, SN_CELL - 1, SN_CELL - 1);
    }
    d.drawRect(_fx * SN_CELL, y0 + _fy * SN_CELL, SN_CELL - 1, SN_CELL - 1);
  }

  // ---- common ----
  static const char* name(int g) {
    return g == RUNNER ? "Runner" : g == TOWER ? "Tower" : "Snake";
  }
  void start() {
    _score = 0;
    _state = PLAY;
    _was_pressed = true;   // the press that started the game is not a jump
    randomSeed(millis());
    if (_game == RUNNER) runStart();
    else if (_game == TOWER) towStart();
    else snStart();
    _next_step = millis() + (_game == SNAKE ? 400 : 0);
  }
  void over() {
    _state = OVER;
    if (_score > _best[_game]) {
      _best[_game] = _score;
      the_mesh.savePrefs();   // a record survives a reboot
    }
    _task->notify(UIEventType::ack);
  }

public:
  GamesScreen(UITask* task) : _task(task) {
    NodePrefs* p = the_mesh.getNodePrefs();
    _best = p ? p->game_best : _no_prefs;
  }

  void enter() { _state = MENU; }

  void poll() override {
    if (_state != PLAY) return;
    bool press = pressEdge();
    unsigned long now = millis();
    if (_game == SNAKE) {
      if ((long)(now - _next_step) < 0) return;
      _next_step = now + snStepMs();
      if (!snStep()) over();
      return;
    }
    // Runner and Tower: fixed 20 ms steps, presses taken as they happen
    if (press) {
      bool ok = _game == RUNNER ? runStep(true) : towStep(true);
      if (!ok) { over(); return; }
    }
    int guard = 0;
    while ((long)(now - _next_step) >= 0 && guard++ < 5) {
      _next_step += 20;
      bool ok = _game == RUNNER ? runStep(false) : towStep(false);
      if (!ok) { over(); return; }
    }
    if (guard >= 5) _next_step = now + 20;
  }

  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);
    char buf[32];
    if (_state == MENU) {
      d.setColor(UIColor::secondary_txt);
      d.drawTextLeftAlign(0, 0, "Games");
      for (int i = 0; i < GAME_COUNT; i++) {
        int y = 14 + i * 12;
        d.setColor(UIColor::primary_txt);
        if (i == _game) {
          d.fillRect(0, y - 1, W, 10);
          d.setColor(UIColor::window_bkg);
        }
        d.drawTextLeftAlign(2, y, name(i));
        if (_best[i]) {
          snprintf(buf, sizeof(buf), "best %u", _best[i]);
          d.drawTextRightAlign(W - 3, y, buf);
        }
      }
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(W / 2, H - 11, "hold: play  3x: back");
      return 1000;
    }

    snprintf(buf, sizeof(buf), "%u", _score);
    d.drawTextRightAlign(W - 1, 0, buf);
    d.setColor(UIColor::secondary_txt);
    d.drawTextLeftAlign(0, 0, name(_game));
    int unread = _task->getUnreadCount();   // a message came in: an envelope, the game goes on
    if (unread > 0) {
      d.drawXbm(W / 2 - 10, 1, envelope_icon, 8, 7);
      snprintf(buf, sizeof(buf), "%d", unread);
      d.drawTextLeftAlign(W / 2, 0, buf);
    }
    d.setColor(UIColor::primary_txt);
    if (_game == RUNNER) runDraw(d);
    else if (_game == TOWER) towDraw(d);
    else snDraw(d);

    if (_state == OVER) {
      d.setColor(UIColor::window_bkg);
      d.fillRect(6, 14, W - 12, 38);
      d.setColor(UIColor::primary_txt);
      d.drawRect(6, 14, W - 12, 38);
      snprintf(buf, sizeof(buf), "Score %u", _score);
      d.drawTextCentered(W / 2, 18, buf);
      snprintf(buf, sizeof(buf), "best %u", _best[_game]);
      d.drawTextCentered(W / 2, 29, buf);
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(W / 2, 40, "click:again 3x:back");
      return 1000;
    }
    return _task->isEink() ? 2000 : 40;   // ~25 frames a second
  }

  bool handleInput(char c) override {
    if (c == KEY_SELECT) {                      // triple: back one level
      if (_state == MENU) _task->gotoHomeScreen();
      else _state = MENU;
      return true;
    }
    if (_state == MENU) {
      if (c == KEY_NEXT || c == KEY_RIGHT) _game = (_game + 1) % GAME_COUNT;
      else if (c == KEY_PREV || c == KEY_LEFT) _game = (_game + GAME_COUNT - 1) % GAME_COUNT;
      else if (c == KEY_ENTER) start();
      else if (c == KEY_QUAD) _task->gotoHomeScreen();
      return true;
    }
    if (_state == OVER) {
      if (c == KEY_NEXT || c == KEY_ENTER) start();
      else if (c == KEY_QUAD) _state = MENU;
      return true;
    }
    // playing: Runner and Tower read the button directly in poll()
    if (_game == SNAKE) {
      if (c == KEY_NEXT || c == KEY_RIGHT) _sturn = 1;
      else if (c == KEY_PREV || c == KEY_LEFT) _sturn = -1;
    }
    return true;
  }
};
