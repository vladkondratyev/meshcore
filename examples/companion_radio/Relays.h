#pragma once

// Smart switches the companion turns on and off: from its own Relays page,
// from the console (`relay ...`) and by a text from an allowed mesh contact
// ("реле 1 вкл" / "relay 1 on").
//
// Kinds:
//   lctech  - Bluetooth relay boards LCTech / JDY-16 / JDY-33 (BLE builds on ESP32):
//             4 bytes A0 <ch> <0|1> <sum> to characteristic FFE1 of service FFE0
//   shelly  - Shelly Gen2/Gen3 over Wi-Fi: http://<ip>/rpc/Switch.Set?id=<ch-1>&on=true
//   shelly1 - Shelly Gen1 over Wi-Fi: http://<ip>/relay/<ch-1>?turn=on
//   tasmota - Tasmota over Wi-Fi: http://<ip>/cm?cmnd=Power<ch>%20On
//
// Bluetooth relays need a `_ble` build, Wi-Fi relays a `_wifi` build, both on ESP32.
// -D RELAY_DISABLE=1 leaves relays out (boards whose flash is too small).

#include <Arduino.h>

#if defined(ESP32) && (defined(BLE_PIN_CODE) || defined(WIFI_SSID)) && !defined(RELAY_DISABLE)
  #define RELAY_SUPPORT 1
#endif
#if defined(ESP32) && defined(BLE_PIN_CODE) && !defined(RELAY_DISABLE)
  #define RELAY_BLE 1
#endif
#if defined(ESP32) && defined(WIFI_SSID) && !defined(RELAY_DISABLE)
  #define RELAY_WIFI 1
#endif

#define RELAY_MAX        8
#define RELAY_NAME_LEN   16
#define RELAY_HOST_LEN   40
#define RELAY_ALLOW_MAX  4
#define RELAY_FOUND_MAX  8

enum RelayKind : uint8_t { RELAY_NONE = 0, RELAY_LCTECH, RELAY_SHELLY, RELAY_SHELLY1, RELAY_TASMOTA };
enum RelayState : uint8_t { RELAY_UNKNOWN = 0, RELAY_OFF, RELAY_ON };
enum RelayAction : uint8_t { RELAY_DO_OFF = 0, RELAY_DO_ON, RELAY_DO_TOGGLE };

struct RelayDef {
  uint8_t kind;      // RelayKind
  uint8_t ch;        // channel, from 1
  uint8_t state;     // RelayState, the last one set from here
  uint8_t pad;
  uint8_t mac[6];    // Bluetooth address of an lctech board
  char host[RELAY_HOST_LEN];   // IP or host name of a Wi-Fi relay
  char name[RELAY_NAME_LEN];
};

struct RelayStore {
  uint8_t version;
  uint8_t mesh_on;   // 1 = allowed contacts may switch relays by a text
  uint8_t pad[2];
  RelayDef r[RELAY_MAX];
  uint8_t allow[RELAY_ALLOW_MAX][6];   // pub key prefixes of contacts allowed to switch
};

struct RelayFound {
  uint8_t mac[6];
  int8_t rssi;
  char name[RELAY_NAME_LEN];
};

class DataStore;

class RelayManager {
  RelayStore _s;
  DataStore* _store = NULL;

  // one job at a time runs in its own task: Bluetooth connects and HTTP
  // requests take seconds and must not hold up the radio
  volatile bool _busy = false;
  volatile bool _done = false;
  volatile bool _ok = false;
  int _job_idx = -1;
  uint8_t _job_state = RELAY_UNKNOWN;
  bool _job_scan = false;
  uint8_t _reply_key[6];
  bool _reply = false;
  char _last_err[32];

  RelayFound _found[RELAY_FOUND_MAX];
  volatile int _nfound = 0;
  bool _scanned = false;
  uint32_t _seq = 0;
  bool _last_ok = false;

  static void taskMain(void* arg);
  bool runJob();
  bool runScan();
  bool sendLctech(const RelayDef& r, bool on);
  bool sendHttp(const RelayDef& r, bool on);

public:
  RelayManager();
  void begin(DataStore* store);
  void save();

  bool supported() const;
  static bool kindSupported(uint8_t kind);
  static const char* kindName(uint8_t kind);
  static bool parseKind(const char* s, uint8_t& kind);
  static bool parseMac(const char* s, uint8_t mac[6]);
  static void formatMac(const uint8_t mac[6], char* buf);

  int count() const;                  // relays set up
  RelayDef* get(int i);               // slot 0..RELAY_MAX-1, NULL when empty
  int firstFree() const;
  bool set(int i, const RelayDef& d);
  bool remove(int i);
  int findByName(const char* name) const;

  // switching: false when another job is running or the slot is empty
  bool request(int i, uint8_t action, const uint8_t* reply_key = NULL);
  bool busy() const { return _busy; }
  int busyIndex() const { return _busy && !_job_scan ? _job_idx : -1; }
  const char* lastError() const { return _last_err; }
  // every finished job bumps this; the page compares it to show the outcome
  uint32_t resultSeq() const { return _seq; }
  bool lastOk() const { return _last_ok; }

  // looking for Bluetooth relay boards nearby (about 5 s)
  bool startScan();
  bool scanning() const { return _busy && _job_scan; }
  bool scanned() const { return _scanned; }
  int foundCount() const { return _nfound; }
  const RelayFound* found(int i) const { return i >= 0 && i < _nfound ? &_found[i] : NULL; }
  int addFound(int k, uint8_t ch, const char* name);   // returns the slot, -1 when full

  // switching by a mesh text
  bool meshOn() const { return _s.mesh_on != 0; }
  void setMeshOn(bool on) { _s.mesh_on = on ? 1 : 0; save(); }
  bool allowed(const uint8_t* key) const;
  bool allow(const uint8_t* key);
  bool deny(const uint8_t* key);
  const uint8_t* allowedKey(int i) const;

  // finished jobs: returns true once per finished job, with the relay and outcome
  bool poll(int& idx, bool& ok, bool& scan, bool& reply, uint8_t reply_key[6]);
};
