#include "Relays.h"
#include "DataStore.h"
#include <string.h>

#ifdef RELAY_BLE
  #include <BLEDevice.h>
  #include <BLEClient.h>
  #include <BLEScan.h>
#endif
#ifdef RELAY_WIFI
  #include <WiFi.h>
  #include <HTTPClient.h>
#endif

#define RELAY_FILE      "/relays"
#define RELAY_VERSION   1

RelayManager::RelayManager() {
  memset(&_s, 0, sizeof(_s));
  _s.version = RELAY_VERSION;
  _last_err[0] = 0;
  memset(_reply_key, 0, sizeof(_reply_key));
}

void RelayManager::begin(DataStore* store) {
  _store = store;
  RelayStore tmp;
  if (_store && _store->loadRaw(RELAY_FILE, &tmp, sizeof(tmp)) && tmp.version == RELAY_VERSION) {
    _s = tmp;
    for (int i = 0; i < RELAY_MAX; i++) {
      _s.r[i].name[RELAY_NAME_LEN - 1] = 0;
      _s.r[i].host[RELAY_HOST_LEN - 1] = 0;
      if (_s.r[i].ch == 0) _s.r[i].ch = 1;
    }
  }
}

void RelayManager::save() {
  if (_store) _store->saveRaw(RELAY_FILE, &_s, sizeof(_s));
}

bool RelayManager::supported() const {
#ifdef RELAY_SUPPORT
  return true;
#else
  return false;
#endif
}

bool RelayManager::kindSupported(uint8_t kind) {
  switch (kind) {
#ifdef RELAY_BLE
    case RELAY_LCTECH: return true;
#endif
#ifdef RELAY_WIFI
    case RELAY_SHELLY:
    case RELAY_SHELLY1:
    case RELAY_TASMOTA: return true;
#endif
    default: return false;
  }
}

const char* RelayManager::kindName(uint8_t kind) {
  switch (kind) {
    case RELAY_LCTECH:  return "lctech";
    case RELAY_SHELLY:  return "shelly";
    case RELAY_SHELLY1: return "shelly1";
    case RELAY_TASMOTA: return "tasmota";
    default:            return "-";
  }
}

bool RelayManager::parseKind(const char* s, uint8_t& kind) {
  for (uint8_t k = RELAY_LCTECH; k <= RELAY_TASMOTA; k++) {
    if (strcmp(s, kindName(k)) == 0) { kind = k; return true; }
  }
  return false;
}

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool RelayManager::parseMac(const char* s, uint8_t mac[6]) {
  for (int i = 0; i < 6; i++) {
    int a = hexv(s[0]), b = a >= 0 ? hexv(s[1]) : -1;
    if (a < 0 || b < 0) return false;
    mac[i] = (uint8_t)(a * 16 + b);
    s += 2;
    if (i < 5) {
      if (*s != ':' && *s != '-') return false;
      s++;
    }
  }
  return *s == 0 || *s == ' ';
}

void RelayManager::formatMac(const uint8_t mac[6], char* buf) {
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int RelayManager::count() const {
  int n = 0;
  for (int i = 0; i < RELAY_MAX; i++) if (_s.r[i].kind != RELAY_NONE) n++;
  return n;
}

RelayDef* RelayManager::get(int i) {
  if (i < 0 || i >= RELAY_MAX || _s.r[i].kind == RELAY_NONE) return NULL;
  return &_s.r[i];
}

int RelayManager::firstFree() const {
  for (int i = 0; i < RELAY_MAX; i++) if (_s.r[i].kind == RELAY_NONE) return i;
  return -1;
}

bool RelayManager::set(int i, const RelayDef& d) {
  if (i < 0 || i >= RELAY_MAX || _busy) return false;
  _s.r[i] = d;
  if (_s.r[i].ch == 0) _s.r[i].ch = 1;
  _s.r[i].name[RELAY_NAME_LEN - 1] = 0;
  _s.r[i].host[RELAY_HOST_LEN - 1] = 0;
  save();
  return true;
}

bool RelayManager::remove(int i) {
  if (i < 0 || i >= RELAY_MAX || _busy) return false;
  memset(&_s.r[i], 0, sizeof(_s.r[i]));
  save();
  return true;
}

int RelayManager::findByName(const char* name) const {
  for (int i = 0; i < RELAY_MAX; i++) {
    if (_s.r[i].kind != RELAY_NONE && strcasecmp(_s.r[i].name, name) == 0) return i;
  }
  return -1;
}

bool RelayManager::allowed(const uint8_t* key) const {
  for (int i = 0; i < RELAY_ALLOW_MAX; i++) {
    static const uint8_t zero[6] = {0};
    if (memcmp(_s.allow[i], zero, 6) != 0 && memcmp(_s.allow[i], key, 6) == 0) return true;
  }
  return false;
}

bool RelayManager::allow(const uint8_t* key) {
  if (allowed(key)) return true;
  static const uint8_t zero[6] = {0};
  for (int i = 0; i < RELAY_ALLOW_MAX; i++) {
    if (memcmp(_s.allow[i], zero, 6) == 0) {
      memcpy(_s.allow[i], key, 6);
      save();
      return true;
    }
  }
  return false;
}

bool RelayManager::deny(const uint8_t* key) {
  for (int i = 0; i < RELAY_ALLOW_MAX; i++) {
    if (memcmp(_s.allow[i], key, 6) == 0) {
      memset(_s.allow[i], 0, 6);
      save();
      return true;
    }
  }
  return false;
}

const uint8_t* RelayManager::allowedKey(int i) const {
  static const uint8_t zero[6] = {0};
  if (i < 0 || i >= RELAY_ALLOW_MAX || memcmp(_s.allow[i], zero, 6) == 0) return NULL;
  return _s.allow[i];
}

// ------------------------------------------------------------ jobs

bool RelayManager::request(int i, uint8_t action, const uint8_t* reply_key) {
  RelayDef* r = get(i);
  if (r == NULL || _busy) return false;
  if (!kindSupported(r->kind)) {
    strcpy(_last_err, "not in this build");
    return false;
  }
  bool on;
  if (action == RELAY_DO_TOGGLE) on = r->state != RELAY_ON;
  else on = action == RELAY_DO_ON;
  _job_idx = i;
  _job_state = on ? RELAY_ON : RELAY_OFF;
  _job_scan = false;
  _reply = reply_key != NULL;
  if (reply_key) memcpy(_reply_key, reply_key, 6);
  _last_err[0] = 0;
  _done = false;
  _busy = true;
#ifdef RELAY_SUPPORT
  if (xTaskCreate(taskMain, "relay", 6144, this, 1, NULL) != pdPASS) {
    _busy = false;
    strcpy(_last_err, "no memory");
    return false;
  }
  return true;
#else
  _busy = false;
  return false;
#endif
}

bool RelayManager::startScan() {
#ifdef RELAY_BLE
  if (_busy) return false;
  _job_idx = -1;
  _job_scan = true;
  _reply = false;
  _last_err[0] = 0;
  _done = false;
  _busy = true;
  if (xTaskCreate(taskMain, "relay", 6144, this, 1, NULL) != pdPASS) {
    _busy = false;
    strcpy(_last_err, "no memory");
    return false;
  }
  return true;
#else
  strcpy(_last_err, "needs a _ble build");
  return false;
#endif
}

void RelayManager::taskMain(void* arg) {
  RelayManager* self = (RelayManager*)arg;
  bool ok = self->_job_scan ? self->runScan() : self->runJob();
  self->_ok = ok;
  self->_done = true;
#ifdef RELAY_SUPPORT
  vTaskDelete(NULL);
#endif
}

bool RelayManager::runJob() {
  const RelayDef& r = _s.r[_job_idx];
  bool on = _job_state == RELAY_ON;
  switch (r.kind) {
    case RELAY_LCTECH: return sendLctech(r, on);
    case RELAY_SHELLY:
    case RELAY_SHELLY1:
    case RELAY_TASMOTA: return sendHttp(r, on);
    default: return false;
  }
}

bool RelayManager::sendLctech(const RelayDef& r, bool on) {
#ifdef RELAY_BLE
  if (!BLEDevice::getInitialized()) { strcpy(_last_err, "bluetooth off"); return false; }
  char addr[20];
  formatMac(r.mac, addr);
  BLEClient* c = BLEDevice::createClient();
  if (c == NULL) { strcpy(_last_err, "no memory"); return false; }
  bool ok = false;
  if (!c->connect(BLEAddress(addr))) {
    strcpy(_last_err, "not reachable");
  } else {
    BLERemoteService* svc = c->getService(BLEUUID((uint16_t)0xFFE0));
    BLERemoteCharacteristic* ch = svc ? svc->getCharacteristic(BLEUUID((uint16_t)0xFFE1)) : NULL;
    if (ch == NULL) {
      strcpy(_last_err, "not a relay board");
    } else {
      uint8_t frame[4] = { 0xA0, r.ch, (uint8_t)(on ? 1 : 0), 0 };
      frame[3] = (uint8_t)(frame[0] + frame[1] + frame[2]);
      ch->writeValue(frame, sizeof(frame), false);
      ok = true;
    }
    c->disconnect();
  }
  delete c;
  return ok;
#else
  (void)r; (void)on;
  strcpy(_last_err, "needs a _ble build");
  return false;
#endif
}

bool RelayManager::sendHttp(const RelayDef& r, bool on) {
#ifdef RELAY_WIFI
  if (WiFi.status() != WL_CONNECTED) { strcpy(_last_err, "no wi-fi"); return false; }
  char url[120];
  int ch0 = r.ch > 0 ? r.ch - 1 : 0;
  if (r.kind == RELAY_SHELLY) {
    snprintf(url, sizeof(url), "http://%s/rpc/Switch.Set?id=%d&on=%s", r.host, ch0, on ? "true" : "false");
  } else if (r.kind == RELAY_SHELLY1) {
    snprintf(url, sizeof(url), "http://%s/relay/%d?turn=%s", r.host, ch0, on ? "on" : "off");
  } else {
    snprintf(url, sizeof(url), "http://%s/cm?cmnd=Power%d%%20%s", r.host, r.ch, on ? "On" : "Off");
  }
  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(url)) { strcpy(_last_err, "bad address"); return false; }
  int code = http.GET();
  http.end();
  if (code != 200) {
    snprintf(_last_err, sizeof(_last_err), code < 0 ? "not reachable" : "http %d", code);
    return false;
  }
  return true;
#else
  (void)r; (void)on;
  strcpy(_last_err, "needs a _wifi build");
  return false;
#endif
}

bool RelayManager::runScan() {
#ifdef RELAY_BLE
  _nfound = 0;
  if (!BLEDevice::getInitialized()) { strcpy(_last_err, "bluetooth off"); return false; }
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  BLEScanResults res = scan->start(5, false);
  int n = res.getCount();
  int k = 0;
  for (int i = 0; i < n && k < RELAY_FOUND_MAX; i++) {
    BLEAdvertisedDevice d = res.getDevice(i);
    // relay boards announce the FFE0 serial service or a module name
    bool serial = d.haveServiceUUID() && d.isAdvertisingService(BLEUUID((uint16_t)0xFFE0));
    String nm = d.haveName() ? String(d.getName().c_str()) : String("");
    String up = nm;
    up.toUpperCase();
    bool named = up.indexOf("JDY") >= 0 || up.indexOf("LC") >= 0 || up.indexOf("RELAY") >= 0 ||
                 up.indexOf("HM") >= 0 || up.indexOf("BT0") >= 0 || up.indexOf("HC-") >= 0;
    if (!serial && !named) continue;
    RelayFound& f = _found[k];
    memcpy(f.mac, *d.getAddress().getNative(), 6);
    f.rssi = (int8_t)d.getRSSI();
    strncpy(f.name, nm.length() ? nm.c_str() : "BLE relay", RELAY_NAME_LEN - 1);
    f.name[RELAY_NAME_LEN - 1] = 0;
    k++;
  }
  scan->clearResults();
  _nfound = k;
  return true;
#else
  return false;
#endif
}

int RelayManager::addFound(int k, uint8_t ch, const char* name) {
  const RelayFound* f = found(k);
  int slot = firstFree();
  if (f == NULL || slot < 0) return -1;
  RelayDef d;
  memset(&d, 0, sizeof(d));
  d.kind = RELAY_LCTECH;
  d.ch = ch ? ch : 1;
  memcpy(d.mac, f->mac, 6);
  if (name && name[0]) strncpy(d.name, name, RELAY_NAME_LEN - 1);
  else snprintf(d.name, RELAY_NAME_LEN, "relay %d", slot + 1);
  return set(slot, d) ? slot : -1;
}

bool RelayManager::poll(int& idx, bool& ok, bool& scan, bool& reply, uint8_t reply_key[6]) {
  if (!_busy || !_done) return false;
  idx = _job_idx;
  ok = _ok;
  scan = _job_scan;
  reply = _reply;
  memcpy(reply_key, _reply_key, 6);
  if (scan) {
    _scanned = true;
  } else if (ok && _job_idx >= 0) {
    _s.r[_job_idx].state = _job_state;
    save();
  }
  _last_ok = ok;
  _seq++;
  _busy = false;
  _done = false;
  return true;
}
