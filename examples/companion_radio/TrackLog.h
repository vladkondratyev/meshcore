#pragma once

// GPS track recorder. Tracks live in files /trkN (N = 1..TRK_MAX) on the
// file system that holds the contacts. Positions are in 1e-5 degree (about
// a metre). Records:
//   point (3 bytes):  int8 dlat, int8 dlon, uint8 dt (s), dlat != -128
//   wide  (8 bytes):  0x80 0x02, int16 dlat, int16 dlon, uint16 dt
//   key  (15 bytes):  0x80 0x01, uint8 kind, uint32 time, int32 lat, int32 lon
// A key starts the track, every segment after a pause, and any jump too big
// for a wide record.
//
// A point is taken after moving TRK step metres (and every 5 minutes while
// moving a little). Points wait in RAM and go to flash in small batches.

#include <Arduino.h>
#include <helpers/IdentityStore.h>   // FILESYSTEM

#ifndef TRK_MAX
  #define TRK_MAX 32
#endif
#define TRK_KEY_START   1
#define TRK_KEY_SEGMENT 2
#define TRK_KEY_JUMP    3
#define TRK_SKETCH      48     // points kept for the drawing on the screen

struct TrackInfo {
  uint8_t n;
  uint32_t start, end;   // RTC time of the first and the last point
  uint32_t pts;
  uint32_t dist_m;
  uint32_t moving_s;     // time between points, pauses not counted
  uint32_t bytes;
};

class TrackLog {
public:
  enum State { OFF = 0, REC = 1, PAUSED = 2 };

  void begin(FILESYSTEM* fs);
  // carry on after a reboot: the same file, a new segment
  void resumeAfterBoot(uint8_t n, bool paused);

  State state() const { return _state; }
  uint8_t current() const { return _n; }
  uint16_t step() const { return _step; }
  void setStep(uint16_t m) { _step = m < 5 ? 5 : (m > 1000 ? 1000 : m); }

  // returns the new track number, 0 when no number is free
  uint8_t start(uint32_t now);
  void pause();
  void resume();
  void stop();
  // a GPS fix, once a second or so (lat / lon in 1e-5 degree)
  void onFix(uint32_t now, int32_t lat5, int32_t lon5);
  // write what waits in RAM; true when something was written
  bool flush();
  bool flushDue() const { return _blen > 0 && (millis() - _flushed_at > 60000UL); }

  // the track being recorded
  uint32_t pts() const { return _pts; }
  uint32_t distM() const { return _dist; }
  uint32_t startTs() const { return _start_ts; }
  uint32_t movingS() const { return _moving; }
  uint32_t lastTs() const { return _last_ts; }
  float speedKmh() const { return _speed_kmh; }
  bool hasFix() const { return _have_last; }
  int sketchCount() const { return _sk_n; }
  void sketchPoint(int i, int32_t& lat5, int32_t& lon5) const { lat5 = _sk_lat[i]; lon5 = _sk_lon[i]; }

  bool exists(uint8_t n) const;
  bool info(uint8_t n, TrackInfo& out) const;
  int list(uint8_t* out, int max) const;   // track numbers, oldest first
  bool remove(uint8_t n);
  uint8_t oldest() const;
  uint32_t bytesUsed() const;
  // machine-readable dump: "TRK,n" then "S" (new segment) and "P,time,lat,lon" lines, "END,n,pts"
  bool dump(uint8_t n, Print& out) const;

  static uint32_t distance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2);
  static void path(char* buf, uint8_t n);

private:
  FILESYSTEM* _fs = NULL;
  State _state = OFF;
  uint8_t _n = 0;
  uint16_t _step = 20;
  bool _have_last = false;
  bool _new_segment = false;
  int32_t _last_lat = 0, _last_lon = 0;
  uint32_t _last_ts = 0;
  int32_t _cand_lat = 0, _cand_lon = 0;   // the latest fix, written as a point after a while
  uint32_t _cand_ts = 0;
  uint32_t _pts = 0, _dist = 0, _start_ts = 0, _moving = 0;
  float _speed_kmh = 0;
  uint8_t _buf[192];
  uint16_t _blen = 0;
  unsigned long _flushed_at = 0;
  int32_t _sk_lat[TRK_SKETCH], _sk_lon[TRK_SKETCH];
  int _sk_n = 0, _sk_stride = 1, _sk_skip = 0;

  void put(const void* p, int len);
  void putKey(uint16_t kind, uint32_t ts, int32_t lat, int32_t lon);
  void putPoint(uint32_t ts, int32_t lat, int32_t lon);
  void sketchAdd(int32_t lat, int32_t lon);
  void resetStats();
  void loadStats();   // after a reboot: the totals of the file so far
  File openAppend(const char* p) const;
  File openRd(const char* p) const;
};
