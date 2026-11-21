#include "TrackLog.h"
#include <math.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #define TRK_READ  FILE_O_READ
#endif

void TrackLog::path(char* buf, uint8_t n) { sprintf(buf, "/trk%u", (unsigned)n); }

File TrackLog::openAppend(const char* p) const {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(p, FILE_O_WRITE);   // Adafruit LittleFS: write at the end
#elif defined(RP2040_PLATFORM)
  return _fs->open(p, "a");
#else
  return _fs->open(p, "a", true);
#endif
}

File TrackLog::openRd(const char* p) const {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(p, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return _fs->open(p, "r");
#else
  return _fs->open(p, "r", false);
#endif
}

void TrackLog::begin(FILESYSTEM* fs) { _fs = fs; }

bool TrackLog::exists(uint8_t n) const {
  if (_fs == NULL || n == 0) return false;
  char p[12];
  path(p, n);
  return _fs->exists(p);
}

int TrackLog::list(uint8_t* out, int max) const {
  // oldest first: numbers go round 1..TRK_MAX, the newest is _n (or the highest)
  int k = 0;
  uint8_t last = _n;
  if (last == 0) {
    for (int i = TRK_MAX; i >= 1; i--) if (exists(i)) { last = i; break; }
  }
  if (last == 0) return 0;
  for (int i = 1; i <= TRK_MAX && k < max; i++) {
    uint8_t n = (uint8_t)(((last + i - 1) % TRK_MAX) + 1);   // the one after `last` first
    if (exists(n)) out[k++] = n;
  }
  return k;
}

uint8_t TrackLog::oldest() const {
  uint8_t l[TRK_MAX];
  int k = list(l, TRK_MAX);
  for (int i = 0; i < k; i++) if (l[i] != _n || _state == OFF) return l[i];
  return 0;
}

bool TrackLog::remove(uint8_t n) {
  if (!exists(n)) return false;
  if (_state != OFF && n == _n) return false;   // not the one being recorded
  char p[12];
  path(p, n);
  return _fs->remove(p);
}

uint32_t TrackLog::bytesUsed() const {
  uint32_t total = 0;
  for (int i = 1; i <= TRK_MAX; i++) {
    if (!exists(i)) continue;
    char p[12];
    path(p, i);
    File f = openRd(p);
    if (f) { total += f.size(); f.close(); }
  }
  return total;
}

uint32_t TrackLog::distance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
  const double D = M_PI / 180.0 / 100000.0;
  double x = (double)(lon2 - lon1) * D * cos((double)(lat1 + lat2) * 0.5 * D);
  double y = (double)(lat2 - lat1) * D;
  return (uint32_t)(sqrt(x * x + y * y) * 6371000.0 + 0.5);
}

void TrackLog::resetStats() {
  _pts = _dist = _moving = 0;
  _start_ts = 0;
  _speed_kmh = 0;
  _have_last = false;
  _sk_n = 0; _sk_stride = 1; _sk_skip = 0;
  _blen = 0;
  _flushed_at = millis();
}

void TrackLog::sketchAdd(int32_t lat, int32_t lon) {
  if (_sk_skip > 0) { _sk_skip--; return; }
  if (_sk_n == TRK_SKETCH) {   // full: keep every other point, take half as many from now on
    for (int i = 0; i < TRK_SKETCH / 2; i++) { _sk_lat[i] = _sk_lat[i * 2]; _sk_lon[i] = _sk_lon[i * 2]; }
    _sk_n = TRK_SKETCH / 2;
    _sk_stride *= 2;
  }
  _sk_lat[_sk_n] = lat;
  _sk_lon[_sk_n] = lon;
  _sk_n++;
  _sk_skip = _sk_stride - 1;
}

uint8_t TrackLog::start(uint32_t now) {
  if (_fs == NULL) return 0;
  if (_state != OFF) stop();
  uint8_t n = 0;
  uint8_t l[TRK_MAX];
  int k = list(l, TRK_MAX);
  uint8_t last = k > 0 ? l[k - 1] : 0;
  for (int i = 1; i <= TRK_MAX; i++) {
    uint8_t c = (uint8_t)(((last + i - 1) % TRK_MAX) + 1);
    if (!exists(c)) { n = c; break; }
  }
  if (n == 0) return 0;   // all numbers taken: the caller removes the oldest first
  _n = n;
  resetStats();
  _state = REC;
  _new_segment = false;
  _start_ts = now;
  return n;
}

void TrackLog::resumeAfterBoot(uint8_t n, bool paused) {
  if (_fs == NULL || !exists(n)) return;
  _n = n;
  resetStats();
  loadStats();
  _have_last = false;
  _new_segment = true;
  _state = paused ? PAUSED : REC;
}

void TrackLog::pause() {
  if (_state != REC) return;
  // the latest fix closes the segment
  if (_have_last && _cand_ts > _last_ts && (_cand_lat != _last_lat || _cand_lon != _last_lon)) putPoint(_cand_ts, _cand_lat, _cand_lon);
  flush();
  _state = PAUSED;
  _have_last = false;
  _new_segment = true;
}

void TrackLog::resume() {
  if (_state != PAUSED) return;
  _state = REC;
}

void TrackLog::stop() {
  if (_state == REC) pause();
  flush();
  _state = OFF;
}

void TrackLog::put(const void* p, int len) {
  if (_blen + len > (int)sizeof(_buf)) flush();
  memcpy(_buf + _blen, p, len);
  _blen += len;
}

void TrackLog::putKey(uint16_t kind, uint32_t ts, int32_t lat, int32_t lon) {
  uint8_t r[15];
  r[0] = 0x80; r[1] = 0x01; r[2] = (uint8_t)kind;
  memcpy(r + 3, &ts, 4);
  memcpy(r + 7, &lat, 4);
  memcpy(r + 11, &lon, 4);
  put(r, sizeof(r));
}

void TrackLog::putPoint(uint32_t ts, int32_t lat, int32_t lon) {
  int32_t dlat = lat - _last_lat, dlon = lon - _last_lon;
  uint32_t dt = ts - _last_ts;
  if (dlat >= -127 && dlat <= 127 && dlon >= -128 && dlon <= 127 && dt <= 255) {
    uint8_t r[3] = {(uint8_t)(int8_t)dlat, (uint8_t)(int8_t)dlon, (uint8_t)dt};
    put(r, 3);
  } else if (dlat >= -32768 && dlat <= 32767 && dlon >= -32768 && dlon <= 32767 && dt <= 65535) {
    int16_t a = (int16_t)dlat, b = (int16_t)dlon;
    uint16_t t = (uint16_t)dt;
    uint8_t r[8];
    r[0] = 0x80; r[1] = 0x02;
    memcpy(r + 2, &a, 2); memcpy(r + 4, &b, 2); memcpy(r + 6, &t, 2);
    put(r, 8);
  } else {
    putKey(TRK_KEY_JUMP, ts, lat, lon);
  }
  uint32_t d = distance(_last_lat, _last_lon, lat, lon);
  _dist += d;
  if (dt <= 600) _moving += dt;
  _speed_kmh = dt > 0 ? (float)d * 3.6f / (float)dt : 0;
  _last_lat = lat; _last_lon = lon; _last_ts = ts;
  _pts++;
  sketchAdd(lat, lon);
}

void TrackLog::onFix(uint32_t now, int32_t lat5, int32_t lon5) {
  if (_state != REC || _fs == NULL) return;
  if (!_have_last) {   // the first fix of the track or of a segment
    putKey(_pts == 0 && !_new_segment ? TRK_KEY_START : TRK_KEY_SEGMENT, now, lat5, lon5);
    if (_start_ts == 0) _start_ts = now;
    _last_lat = _cand_lat = lat5; _last_lon = _cand_lon = lon5; _last_ts = _cand_ts = now;
    _have_last = true;
    _new_segment = false;
    _pts++;
    sketchAdd(lat5, lon5);
    return;
  }
  _cand_lat = lat5; _cand_lon = lon5; _cand_ts = now;
  uint32_t d = distance(_last_lat, _last_lon, lat5, lon5);
  uint32_t dt = now - _last_ts;
  // a step away, or five minutes and still a few metres (walking slowly, GPS drift is less)
  if (d >= _step || (dt >= 300 && d >= 8)) putPoint(now, lat5, lon5);
}

bool TrackLog::flush() {
  if (_blen == 0 || _fs == NULL || _n == 0) { _flushed_at = millis(); return false; }
  char p[12];
  path(p, _n);
  File f = openAppend(p);
  if (!f) return false;
  f.write(_buf, _blen);
  f.close();
  _blen = 0;
  _flushed_at = millis();
  return true;
}

// read a track file record by record; cb gets each point
template <typename CB>
static bool readTrack(File& f, CB cb) {
  uint8_t r[16];
  int32_t lat = 0, lon = 0;
  uint32_t ts = 0;
  while (f.read(r, 1) == 1) {
    if (r[0] != 0x80) {
      if (f.read(r + 1, 2) != 2) break;
      lat += (int8_t)r[0]; lon += (int8_t)r[1]; ts += r[2];
      cb(0, ts, lat, lon);
      continue;
    }
    if (f.read(r + 1, 1) != 1) break;
    if (r[1] == 0x01) {          // key
      if (f.read(r + 2, 13) != 13) break;
      memcpy(&ts, r + 3, 4); memcpy(&lat, r + 7, 4); memcpy(&lon, r + 11, 4);
      cb(r[2] == TRK_KEY_JUMP ? 0 : (int)r[2], ts, lat, lon);
    } else if (r[1] == 0x02) {   // wide point
      if (f.read(r + 2, 6) != 6) break;
      int16_t a, b; uint16_t t;
      memcpy(&a, r + 2, 2); memcpy(&b, r + 4, 2); memcpy(&t, r + 6, 2);
      lat += a; lon += b; ts += t;
      cb(0, ts, lat, lon);
    } else {
      break;                     // unknown record: stop here
    }
  }
  return true;
}

void TrackLog::loadStats() {
  char p[12];
  path(p, _n);
  File f = openRd(p);
  if (!f) return;
  bool first = true;
  int32_t plat = 0, plon = 0;
  uint32_t pts_ = 0;
  readTrack(f, [&](int key, uint32_t ts, int32_t lat, int32_t lon) {
    if (first) { _start_ts = ts; first = false; }
    else if (key == 0) {
      uint32_t dt = ts - _last_ts;
      _dist += distance(plat, plon, lat, lon);
      if (dt <= 600) _moving += dt;
    }
    plat = lat; plon = lon; _last_ts = ts;
    pts_++;
    sketchAdd(lat, lon);
  });
  f.close();
  _pts = pts_;
}

bool TrackLog::info(uint8_t n, TrackInfo& out) const {
  memset(&out, 0, sizeof(out));
  char p[12];
  path(p, n);
  if (_fs == NULL || !_fs->exists(p)) return false;
  File f = openRd(p);
  if (!f) return false;
  out.n = n;
  out.bytes = f.size();
  bool first = true;
  int32_t plat = 0, plon = 0;
  uint32_t pts_ = 0;
  readTrack(f, [&](int key, uint32_t ts, int32_t lat, int32_t lon) {
    if (first) { out.start = ts; first = false; }
    else if (key == 0) {
      out.dist_m += distance(plat, plon, lat, lon);
      uint32_t dt = ts - out.end;
      if (dt <= 600) out.moving_s += dt;
    }
    plat = lat; plon = lon; out.end = ts;
    pts_++;
  });
  f.close();
  out.pts = pts_;
  // the part still in RAM
  if (_state != OFF && n == _n) {
    out.pts = _pts;
    out.dist_m = _dist;
    out.moving_s = _moving;
    if (_last_ts > out.end) out.end = _last_ts;
  }
  return true;
}

bool TrackLog::dump(uint8_t n, Print& out) const {
  char p[12];
  path(p, n);
  if (_fs == NULL || !_fs->exists(p)) { out.print("ERR,no track\n"); return false; }
  if (_state != OFF && n == _n) const_cast<TrackLog*>(this)->flush();
  File f = openRd(p);
  if (!f) { out.print("ERR,read\n"); return false; }
  char line[48];
  snprintf(line, sizeof(line), "TRK,%u\n", (unsigned)n);
  out.print(line);
  uint32_t cnt = 0;
  readTrack(f, [&](int key, uint32_t ts, int32_t lat, int32_t lon) {
    if (key == TRK_KEY_START || key == TRK_KEY_SEGMENT) out.print("S\n");
    snprintf(line, sizeof(line), "P,%lu,%ld,%ld\n", (unsigned long)ts, (long)lat, (long)lon);
    out.print(line);
    cnt++;
  });
  f.close();
  snprintf(line, sizeof(line), "END,%u,%lu\n", (unsigned)n, (unsigned long)cnt);
  out.print(line);
  return true;
}
