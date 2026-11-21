#pragma once

// Games page: five small games for one button. Included from UITask.cpp only.
// Everything is drawn on the 128x64 UI grid, so it scales to every panel.
//
//   menu    click - next game   double - previous   hold - play   triple - back
//   Runner  press - jump (taken at the press, without the double-click wait)
//   Tower   press - drop the moving block onto the tower
//   Snake   click - turn right  double - turn left
//   Blocks  click - one column right (past the right wall: back to the left)
//           double - turn   hold - drop
//   Raid    the ship flies by itself and fires; press - turn back (taken at
//           the press), keep it pressed - rapid fire
//   game over: click - again, triple - back to the menu
// Triple click leaves any game.

#include "UITask.h"
#include "../MyMesh.h"
#include "icons.h"

class GamesScreen : public UIScreen {
  enum Game { RUNNER, TOWER, SNAKE, BLOCKS, RAID, GAME_COUNT };
  enum State { MENU, PLAY, OVER };

  UITask* _task;
  State _state = MENU;
  uint8_t _game = RUNNER;
  uint16_t* _best;   // records, kept in the node prefs
  uint16_t _no_prefs[GAME_COUNT] = {0, 0, 0, 0, 0};
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


  // ---- Blocks: shapes of four cells fall, full rows go away ----
  #define BL_COLS  10
  #define BL_CELL   3
  #define BL_ROWS  ((H - TOP - 2) / BL_CELL)   // 17
  #define BL_FULL  ((1 << BL_COLS) - 1)
  // 4x4 masks, four turns each; bit 15 is the top-left cell
  static uint16_t blShape(int p, int r) {
    static const uint16_t S[7][4] = {
      {0x0F00, 0x2222, 0x00F0, 0x4444},
      {0x44C0, 0x8E00, 0x6440, 0x0E20},
      {0x4460, 0x0E80, 0xC440, 0x2E00},
      {0xCC00, 0xCC00, 0xCC00, 0xCC00},
      {0x06C0, 0x8C40, 0x6C00, 0x4620},
      {0x0E40, 0x4C40, 0x4E00, 0x4640},
      {0x0C60, 0x4C80, 0xC600, 0x2640},
    };
    return S[p][r & 3];
  }
  uint16_t _brow[BL_ROWS];       // bit c = column c is filled
  int8_t _bp = 0, _brot = 0, _bx = 0, _by = 0, _bnext = 0;
  int blX0() const { return (W - BL_COLS * BL_CELL) / 2; }

  bool blFits(int rot, int x, int y) const {
    uint16_t m = blShape(_bp, rot);
    for (int i = 0; i < 16; i++) {
      if (!(m & (0x8000 >> i))) continue;
      int c = x + i % 4, r = y + i / 4;
      if (c < 0 || c >= BL_COLS || r >= BL_ROWS) return false;
      if (r >= 0 && (_brow[r] & (1 << c))) return false;
    }
    return true;
  }
  bool blSpawn() {
    _bp = _bnext;
    _bnext = random(0, 7);
    _brot = 0;
    _bx = 3;
    uint16_t m = blShape(_bp, 0);
    _by = (m & 0xF000) ? 0 : -1;   // the first filled row starts at the top
    return blFits(_brot, _bx, _by);
  }
  void blStart() {
    memset(_brow, 0, sizeof(_brow));
    _bnext = random(0, 7);
    blSpawn();
  }
  int blStepMs() const { int ms = 650 - _score * 20; return ms < 120 ? 120 : ms; }
  // the shape stays where it is; full rows go; false: no room for the next one
  bool blLock() {
    uint16_t m = blShape(_bp, _brot);
    for (int i = 0; i < 16; i++) {
      if (!(m & (0x8000 >> i))) continue;
      int r = _by + i / 4;
      if (r < 0) return false;                 // stuck above the top
      _brow[r] |= 1 << (_bx + i % 4);
    }
    int lines = 0;
    for (int r = BL_ROWS - 1; r >= 0; ) {
      if (_brow[r] == BL_FULL) {
        memmove(_brow + 1, _brow, sizeof(_brow[0]) * r);
        _brow[0] = 0;
        lines++;
      } else {
        r--;
      }
    }
    if (lines) { _score += lines; _task->notify(UIEventType::ack); }
    return blSpawn();
  }
  bool blStep() {
    if (blFits(_brot, _bx, _by + 1)) { _by++; return true; }
    return blLock();
  }
  void blRight() {
    if (blFits(_brot, _bx + 1, _by)) { _bx++; return; }
    for (int x = -3; x < _bx; x++) if (blFits(_brot, x, _by)) { _bx = x; return; }   // round to the left wall
  }
  void blTurn() {
    static const int8_t kick[] = {0, -1, 1, -2, 2};
    int r = (_brot + 1) & 3;
    for (int k = 0; k < 5; k++) if (blFits(r, _bx + kick[k], _by)) { _brot = r; _bx += kick[k]; return; }
  }
  bool blDrop() {
    while (blFits(_brot, _bx, _by + 1)) _by++;
    return blLock();
  }
  static void blCell(DisplayDriver& d, int x, int y) { d.fillRect(x, y, BL_CELL - 1, BL_CELL - 1); }
  void blDraw(DisplayDriver& d) {
    int x0 = blX0(), y0 = TOP + 2;
    d.fillRect(x0 - 2, y0, 1, BL_ROWS * BL_CELL);
    d.fillRect(x0 + BL_COLS * BL_CELL, y0, 1, BL_ROWS * BL_CELL);
    d.fillRect(x0 - 2, y0 + BL_ROWS * BL_CELL, BL_COLS * BL_CELL + 3, 1);
    for (int r = 0; r < BL_ROWS; r++)
      for (int c = 0; c < BL_COLS; c++)
        if (_brow[r] & (1 << c)) blCell(d, x0 + c * BL_CELL, y0 + r * BL_CELL);
    if (_state == PLAY) {
      uint16_t m = blShape(_bp, _brot);
      for (int i = 0; i < 16; i++) {
        int r = _by + i / 4;
        if ((m & (0x8000 >> i)) && r >= 0) blCell(d, x0 + (_bx + i % 4) * BL_CELL, y0 + r * BL_CELL);
      }
    }
    // the next shape, right of the well
    int nx = x0 + BL_COLS * BL_CELL + 10;
    d.setColor(UIColor::secondary_txt);
    d.drawTextLeftAlign(nx, y0 + 2, "next");
    d.setColor(UIColor::primary_txt);
    uint16_t m = blShape(_bnext, 0);
    for (int i = 0; i < 16; i++)
      if (m & (0x8000 >> i)) blCell(d, nx + 4 + (i % 4) * BL_CELL, y0 + 14 + (i / 4) * BL_CELL);
  }

  // ---- Raid: the ship below, waves of targets above ----
  #define RD_ROWS     3
  #define RD_COLS     8
  #define RD_DX      12               // target spacing
  #define RD_DY       8
  #define RD_EW       7               // target size
  #define RD_EH       5
  #define RD_SHIP_Y  (H - 5)
  #define RD_SW       7
  #define RD_SHOTS    6
  #define RD_BOMBS    4
  uint8_t _ralive[RD_ROWS];           // bit c = target in column c is still there
  int16_t _rfx = 0, _rfy = 0;         // top-left of the formation
  int8_t _rfdir = 1;
  uint8_t _rfcnt = 0;                 // steps until the formation moves
  int16_t _rsx = 0;                   // ship x, 1/16 px
  int8_t _rsdir = 1;
  uint8_t _rcool = 0;                 // steps until the next shot
  int8_t _rbx[RD_SHOTS], _rby[RD_SHOTS];   // shots up; y < 0: free
  int8_t _rmx[RD_BOMBS], _rmy[RD_BOMBS];   // shots down; y < 0: free
  uint8_t _rlives = 3, _rwave = 0, _rhit = 0;
  uint16_t _rstep = 0;

  int rdCount() const {
    int n = 0;
    for (int r = 0; r < RD_ROWS; r++) for (int c = 0; c < RD_COLS; c++) if (_ralive[r] & (1 << c)) n++;
    return n;
  }
  void rdWave() {
    for (int r = 0; r < RD_ROWS; r++) _ralive[r] = (1 << RD_COLS) - 1;
    _rfx = (W - ((RD_COLS - 1) * RD_DX + RD_EW)) / 2;
    _rfy = TOP + 2 + (_rwave < 4 ? _rwave : 4) * 2;
    _rfdir = 1;
    _rfcnt = 0;
    for (int i = 0; i < RD_BOMBS; i++) _rmy[i] = -1;
  }
  void rdStart() {
    _rlives = 3; _rwave = 0; _rhit = 0; _rstep = 0; _rcool = 10;
    _rsx = (W / 2 - RD_SW / 2) * 16; _rsdir = 1;
    for (int i = 0; i < RD_SHOTS; i++) _rby[i] = -1;
    rdWave();
  }
  // steps between formation moves: fewer targets and later waves go faster
  int rdPeriod() const {
    int p = 9 - _rwave - (RD_ROWS * RD_COLS - rdCount()) / 5;
    return p < 1 ? 1 : p;
  }
  bool rdStep(bool press) {
    _rstep++;
    if (press) _rsdir = -_rsdir;
    // ship
    _rsx += _rsdir * 15;
    if (_rsx <= 0) { _rsx = 0; _rsdir = 1; }
    if (_rsx >= (W - RD_SW) * 16) { _rsx = (W - RD_SW) * 16; _rsdir = -1; }
    int sx = _rsx / 16;
    // fire: on its own, faster while the button is held
    if (_rcool) _rcool--;
    if (!_rcool) {
      for (int i = 0; i < RD_SHOTS; i++) if (_rby[i] < 0) { _rbx[i] = sx + RD_SW / 2; _rby[i] = RD_SHIP_Y - 3; break; }
      _rcool = _task->isButtonPressed() ? 6 : 22;
    }
    // shots up and what they hit
    for (int i = 0; i < RD_SHOTS; i++) {
      if (_rby[i] < 0) continue;
      _rby[i] -= 3;
      if (_rby[i] < TOP) { _rby[i] = -1; continue; }
      int c = (_rbx[i] - _rfx) / RD_DX, r = (_rby[i] - _rfy) / RD_DY;
      if (_rbx[i] < _rfx || _rby[i] < _rfy || c >= RD_COLS || r >= RD_ROWS) continue;
      if ((_rbx[i] - _rfx) % RD_DX >= RD_EW || (_rby[i] - _rfy) % RD_DY >= RD_EH + 2) continue;
      if (!(_ralive[r] & (1 << c))) continue;
      _ralive[r] &= ~(1 << c);
      _rby[i] = -1;
      _score++;
    }
    if (rdCount() == 0) { _rwave++; rdWave(); _task->notify(UIEventType::ack); return true; }
    // the formation: sideways, a step down at each edge
    if (++_rfcnt >= rdPeriod()) {
      _rfcnt = 0;
      int lo = RD_COLS, hi = -1, bottom = 0;
      for (int r = 0; r < RD_ROWS; r++)
        for (int c = 0; c < RD_COLS; c++)
          if (_ralive[r] & (1 << c)) { if (c < lo) lo = c; if (c > hi) hi = c; bottom = r; }
      int nx = _rfx + _rfdir;
      if (nx + lo * RD_DX < 0 || nx + hi * RD_DX + RD_EW > W) { _rfdir = -_rfdir; _rfy += 3; }
      else _rfx = nx;
      if (_rfy + bottom * RD_DY + RD_EH >= RD_SHIP_Y) return false;   // they reached the ship
    }
    // shots down, from the lowest target of a random column
    int chance = 45 - _rwave * 5;
    if (chance < 12) chance = 12;
    if (random(0, chance) == 0) {
      int c = random(0, RD_COLS);
      for (int r = RD_ROWS - 1; r >= 0; r--) {
        if (!(_ralive[r] & (1 << c))) continue;
        for (int i = 0; i < RD_BOMBS; i++)
          if (_rmy[i] < 0) { _rmx[i] = _rfx + c * RD_DX + RD_EW / 2; _rmy[i] = _rfy + r * RD_DY + RD_EH; break; }
        break;
      }
    }
    if (_rhit) _rhit--;
    for (int i = 0; i < RD_BOMBS; i++) {
      if (_rmy[i] < 0) continue;
      if (_rstep % 2 == 0) _rmy[i] += 2; else _rmy[i] += 1;
      if (_rmy[i] >= H) { _rmy[i] = -1; continue; }
      if (!_rhit && _rmy[i] + 2 >= RD_SHIP_Y && _rmx[i] >= sx && _rmx[i] < sx + RD_SW) {
        _rmy[i] = -1;
        if (--_rlives == 0) return false;
        _rhit = 60;                            // a second of blinking, no new hits
        _task->notify(UIEventType::ack);
      }
    }
    return true;
  }
  void rdDraw(DisplayDriver& d) {
    static const uint8_t TGT[2][RD_EH] = {{0x1C, 0x3E, 0x6B, 0x7F, 0x22}, {0x1C, 0x3E, 0x6B, 0x7F, 0x41}};
    static const uint8_t SHIP[4] = {0x08, 0x1C, 0x7F, 0x63};
    int f = (_rstep / 25) % 2;
    for (int r = 0; r < RD_ROWS; r++)
      for (int c = 0; c < RD_COLS; c++) {
        if (!(_ralive[r] & (1 << c))) continue;
        int x = _rfx + c * RD_DX, y = _rfy + r * RD_DY;
        for (int yy = 0; yy < RD_EH; yy++)
          for (int xx = 0; xx < RD_EW; xx++)
            if (TGT[f][yy] & (0x40 >> xx)) d.fillRect(x + xx, y + yy, 1, 1);
      }
    for (int i = 0; i < RD_SHOTS; i++) if (_rby[i] >= 0) d.fillRect(_rbx[i], _rby[i], 1, 3);
    for (int i = 0; i < RD_BOMBS; i++) if (_rmy[i] >= 0) d.fillRect(_rmx[i], _rmy[i], 1, 3);
    if (!_rhit || (_rhit / 6) % 2) {
      int sx = _rsx / 16;
      for (int yy = 0; yy < 4; yy++)
        for (int xx = 0; xx < RD_SW; xx++)
          if (SHIP[yy] & (0x40 >> xx)) d.fillRect(sx + xx, RD_SHIP_Y - 3 + yy + 1, 1, 1);
    }
    for (int i = 0; i < _rlives; i++) d.fillRect(30 + i * 5, 2, 3, 3);   // lives, after the name
  }

  // ---- common ----
  static const char* name(int g) {
    return g == RUNNER ? "Runner" : g == TOWER ? "Tower" : g == SNAKE ? "Snake" : g == BLOCKS ? "Blocks" : "Raid";
  }
  void start() {
    _score = 0;
    _state = PLAY;
    _was_pressed = true;   // the press that started the game is not a jump
    randomSeed(millis());
    if (_game == RUNNER) runStart();
    else if (_game == TOWER) towStart();
    else if (_game == SNAKE) snStart();
    else if (_game == BLOCKS) blStart();
    else rdStart();
    _next_step = millis() + (_game == SNAKE || _game == BLOCKS ? 400 : 0);
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
    if (_game == BLOCKS) {
      if ((long)(now - _next_step) < 0) return;
      _next_step = now + blStepMs();
      if (!blStep()) over();
      return;
    }
    // Runner, Tower and Raid: fixed 20 ms steps, presses taken as they happen
    if (press) {
      bool ok = _game == RUNNER ? runStep(true) : _game == TOWER ? towStep(true) : rdStep(true);
      if (!ok) { over(); return; }
    }
    int guard = 0;
    while ((long)(now - _next_step) >= 0 && guard++ < 5) {
      _next_step += 20;
      bool ok = _game == RUNNER ? runStep(false) : _game == TOWER ? towStep(false) : rdStep(false);
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
        int y = 10 + i * 9;
        d.setColor(UIColor::primary_txt);
        if (i == _game) {
          d.fillRect(0, y - 1, W, 9);
          d.setColor(UIColor::window_bkg);
        }
        d.drawTextLeftAlign(2, y, name(i));
        if (_best[i]) {
          snprintf(buf, sizeof(buf), "best %u", _best[i]);
          d.drawTextRightAlign(W - 3, y, buf);
        }
      }
      d.setColor(UIColor::secondary_txt);
      d.drawTextCentered(W / 2, H - 9, "hold: play  3x: back");
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
    else if (_game == SNAKE) snDraw(d);
    else if (_game == BLOCKS) blDraw(d);
    else rdDraw(d);

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
    // playing: Runner, Tower and Raid read the button directly in poll()
    if (_game == SNAKE) {
      if (c == KEY_NEXT || c == KEY_RIGHT) _sturn = 1;
      else if (c == KEY_PREV || c == KEY_LEFT) _sturn = -1;
    } else if (_game == BLOCKS) {
      if (c == KEY_NEXT || c == KEY_RIGHT) blRight();
      else if (c == KEY_PREV || c == KEY_LEFT) blTurn();
      else if (c == KEY_ENTER && !blDrop()) over();
    }
    return true;
  }
};
