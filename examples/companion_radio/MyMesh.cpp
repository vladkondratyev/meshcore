#include "MyMesh.h"
#include <helpers/RadioProfiles.h>
#ifdef BOOK_READER
  #include "BookStore.h"
#endif

#include <Arduino.h> // needed for PlatformIO
#include <Mesh.h>

static const uint8_t CONSOLE_PUB_KEY[PUB_KEY_SIZE] = { 'M','C','-','C','L','I' };

class ConsoleReplyPrint : public Print {
  char* _buf;
  size_t _cap;
  size_t _len;
public:
  ConsoleReplyPrint(char* buf, size_t cap) : _buf(buf), _cap(cap), _len(0) {
    if (cap > 0) _buf[0] = 0;
  }
  size_t write(uint8_t c) override {
    if (_cap > 0 && _len + 1 < _cap) {
      _buf[_len++] = (char)c;
      _buf[_len] = 0;
    }
    return 1;
  }
};

#define CMD_APP_START                 1
#define CMD_SEND_TXT_MSG              2
#define CMD_SEND_CHANNEL_TXT_MSG      3
#define CMD_GET_CONTACTS              4 // with optional 'since' (for efficient sync)
#define CMD_GET_DEVICE_TIME           5
#define CMD_SET_DEVICE_TIME           6
#define CMD_SEND_SELF_ADVERT          7
#define CMD_SET_ADVERT_NAME           8
#define CMD_ADD_UPDATE_CONTACT        9
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_RESET_PATH                13
#define CMD_SET_ADVERT_LATLON         14
#define CMD_REMOVE_CONTACT            15
#define CMD_SHARE_CONTACT             16
#define CMD_EXPORT_CONTACT            17
#define CMD_IMPORT_CONTACT            18
#define CMD_REBOOT                    19
#define CMD_GET_BATT_AND_STORAGE      20   // was CMD_GET_BATTERY_VOLTAGE
#define CMD_SET_TUNING_PARAMS         21
#define CMD_DEVICE_QUERY              22
#define CMD_EXPORT_PRIVATE_KEY        23
#define CMD_IMPORT_PRIVATE_KEY        24
#define CMD_SEND_RAW_DATA             25
#define CMD_SEND_LOGIN                26
#define CMD_SEND_STATUS_REQ           27
#define CMD_HAS_CONNECTION            28
#define CMD_LOGOUT                    29 // 'Disconnect'
#define CMD_GET_CONTACT_BY_KEY        30
#define CMD_GET_CHANNEL               31
#define CMD_SET_CHANNEL               32
#define CMD_SIGN_START                33
#define CMD_SIGN_DATA                 34
#define CMD_SIGN_FINISH               35
#define CMD_SEND_TRACE_PATH           36
#define CMD_SET_DEVICE_PIN            37
#define CMD_SET_OTHER_PARAMS          38
#define CMD_SEND_TELEMETRY_REQ        39  // can deprecate this
#define CMD_GET_CUSTOM_VARS           40
#define CMD_SET_CUSTOM_VAR            41
#define CMD_GET_ADVERT_PATH           42
#define CMD_GET_TUNING_PARAMS         43
// NOTE: CMD range 44..49 parked, potentially for WiFi operations
#define CMD_SEND_BINARY_REQ           50
#define CMD_FACTORY_RESET             51
#define CMD_SEND_PATH_DISCOVERY_REQ   52
#define CMD_SET_FLOOD_SCOPE_KEY       54   // v8+
#define CMD_SEND_CONTROL_DATA         55   // v8+
#define CMD_GET_STATS                 56   // v8+, second byte is stats type
#define CMD_SEND_ANON_REQ             57
#define CMD_SET_AUTOADD_CONFIG        58
#define CMD_GET_AUTOADD_CONFIG        59
#define CMD_GET_ALLOWED_REPEAT_FREQ   60
#define CMD_SET_PATH_HASH_MODE        61
#define CMD_SEND_CHANNEL_DATA         62
#define CMD_SET_DEFAULT_FLOOD_SCOPE   63
#define CMD_GET_DEFAULT_FLOOD_SCOPE   64
#define CMD_SEND_RAW_PACKET           65

// Stats sub-types for CMD_GET_STATS
#define STATS_TYPE_CORE               0
#define STATS_TYPE_RADIO              1
#define STATS_TYPE_PACKETS             2

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2  // first reply to CMD_GET_CONTACTS
#define RESP_CODE_CONTACT             3  // multiple of these (after CMD_GET_CONTACTS)
#define RESP_CODE_END_OF_CONTACTS     4  // last reply to CMD_GET_CONTACTS
#define RESP_CODE_SELF_INFO           5  // reply to CMD_APP_START
#define RESP_CODE_SENT                6  // reply to CMD_SEND_TXT_MSG
#define RESP_CODE_CONTACT_MSG_RECV    7  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CHANNEL_MSG_RECV    8  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CURR_TIME           9  // a reply to CMD_GET_DEVICE_TIME
#define RESP_CODE_NO_MORE_MESSAGES    10 // a reply to CMD_SYNC_NEXT_MESSAGE
#define RESP_CODE_EXPORT_CONTACT      11
#define RESP_CODE_BATT_AND_STORAGE    12 // a reply to a CMD_GET_BATT_AND_STORAGE
#define RESP_CODE_DEVICE_INFO         13 // a reply to CMD_DEVICE_QUERY
#define RESP_CODE_PRIVATE_KEY         14 // a reply to CMD_EXPORT_PRIVATE_KEY
#define RESP_CODE_DISABLED            15
#define RESP_CODE_CONTACT_MSG_RECV_V3 16 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_MSG_RECV_V3 17 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_INFO        18 // a reply to CMD_GET_CHANNEL
#define RESP_CODE_SIGN_START          19
#define RESP_CODE_SIGNATURE           20
#define RESP_CODE_CUSTOM_VARS         21
#define RESP_CODE_ADVERT_PATH         22
#define RESP_CODE_TUNING_PARAMS       23
#define RESP_CODE_STATS               24   // v8+, second byte is stats type
#define RESP_CODE_AUTOADD_CONFIG      25
#define RESP_ALLOWED_REPEAT_FREQ      26
#define RESP_CODE_CHANNEL_DATA_RECV   27
#define RESP_CODE_DEFAULT_FLOOD_SCOPE 28

#define MAX_CHANNEL_DATA_LENGTH       (MAX_FRAME_SIZE - 9)

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250
#define LAZY_CONTACTS_WRITE_DELAY       5000

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

// these are _pushed_ to client app at any time
#define PUSH_CODE_ADVERT                0x80
#define PUSH_CODE_PATH_UPDATED          0x81
#define PUSH_CODE_SEND_CONFIRMED        0x82
#define PUSH_CODE_MSG_WAITING           0x83
#define PUSH_CODE_RAW_DATA              0x84
#define PUSH_CODE_LOGIN_SUCCESS         0x85
#define PUSH_CODE_LOGIN_FAIL            0x86
#define PUSH_CODE_STATUS_RESPONSE       0x87
#define PUSH_CODE_LOG_RX_DATA           0x88
#define PUSH_CODE_TRACE_DATA            0x89
#define PUSH_CODE_NEW_ADVERT            0x8A
#define PUSH_CODE_TELEMETRY_RESPONSE    0x8B
#define PUSH_CODE_BINARY_RESPONSE       0x8C
#define PUSH_CODE_PATH_DISCOVERY_RESPONSE 0x8D
#define PUSH_CODE_CONTROL_DATA          0x8E   // v8+
#define PUSH_CODE_CONTACT_DELETED       0x8F // used to notify client app of deleted contact when overwriting oldest
#define PUSH_CODE_CONTACTS_FULL         0x90 // used to notify client app that contacts storage is full

#define ERR_CODE_UNSUPPORTED_CMD        1
#define ERR_CODE_NOT_FOUND              2
#define ERR_CODE_TABLE_FULL             3
#define ERR_CODE_BAD_STATE              4
#define ERR_CODE_FILE_IO_ERROR          5
#define ERR_CODE_ILLEGAL_ARG            6

#define MAX_SIGN_DATA_LEN               (8 * 1024) // 8K

// Auto-add config bitmask
// Bit 0: If set, overwrite oldest non-favourite contact when contacts file is full
// Bits 1-4: these indicate which contact types to auto-add when manual_contact_mode = 0x01
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)

void MyMesh::writeOKFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_OK;
  _serial->writeFrame(buf, 1);
}
void MyMesh::writeErrFrame(uint8_t err_code) {
  uint8_t buf[2];
  buf[0] = RESP_CODE_ERR;
  buf[1] = err_code;
  _serial->writeFrame(buf, 2);
}

void MyMesh::writeDisabledFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_DISABLED;
  _serial->writeFrame(buf, 1);
}

void MyMesh::writeContactRespFrame(uint8_t code, const ContactInfo &contact) {
  int i = 0;
  out_frame[i++] = code;
  memcpy(&out_frame[i], contact.id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  out_frame[i++] = contact.type;
  out_frame[i++] = contact.flags;
  out_frame[i++] = contact.out_path_len;
  memcpy(&out_frame[i], contact.out_path, MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  StrHelper::strzcpy((char *)&out_frame[i], contact.name, 32);
  i += 32;
  memcpy(&out_frame[i], &contact.last_advert_timestamp, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lat, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lon, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.lastmod, 4);
  i += 4;
  _serial->writeFrame(out_frame, i);
}

void MyMesh::updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len) {
  int i = 0;
  uint8_t code = frame[i++]; // eg. CMD_ADD_UPDATE_CONTACT
  memcpy(contact.id.pub_key, &frame[i], PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  contact.type = frame[i++];
  contact.flags = frame[i++];
  contact.out_path_len = frame[i++];
  memcpy(contact.out_path, &frame[i], MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  memcpy(contact.name, &frame[i], 32);
  i += 32;
  memcpy(&contact.last_advert_timestamp, &frame[i], 4);
  i += 4;
  if (len >= i + 8) { // optional fields
    memcpy(&contact.gps_lat, &frame[i], 4);
    i += 4;
    memcpy(&contact.gps_lon, &frame[i], 4);
    i += 4;
    if (len >= i + 4) {
      memcpy(&last_mod, &frame[i], 4);
    }
  }
}

#define OQ_SHOWN  0x01   // entry flag: the message is also on the device screen

bool MyMesh::isChannelFrame(const uint8_t* buf) {
  return buf[0] == RESP_CODE_CHANNEL_MSG_RECV || buf[0] == RESP_CODE_CHANNEL_MSG_RECV_V3 ||
         buf[0] == RESP_CODE_CHANNEL_DATA_RECV;
}

void MyMesh::removeOfflineAt(int off) {
  int sz = 2 + offline_queue[off];
  memmove(&offline_queue[off], &offline_queue[off + sz], offline_queue_used - off - sz);
  offline_queue_used -= sz;
  offline_queue_len--;
}

// Returns bit 0 when the frame was queued; the bits above it count evicted
// channel messages that were also on the device screen.
int MyMesh::addToOfflineQueue(const uint8_t frame[], int len, bool shown) {
  if (len <= 0 || len > MAX_FRAME_SIZE) return 0;
  int need = 2 + len;
  int evicted_shown = 0;
  while (offline_queue_used + need > OFFLINE_QUEUE_BYTES) {
    MESH_DEBUG_PRINTLN("WARN: offline_queue is full!");
    int off = 0;
    while (off < offline_queue_used && !isChannelFrame(&offline_queue[off + 2])) {
      off += 2 + offline_queue[off];
    }
    if (off >= offline_queue_used) {
      MESH_DEBUG_PRINTLN("INFO: no channel messages to remove from queue.");
      return evicted_shown << 1;
    }
    if (offline_queue[off + 1] & OQ_SHOWN) evicted_shown++;
    removeOfflineAt(off);   // drop the oldest channel message
    MESH_DEBUG_PRINTLN("INFO: removed oldest channel message from queue.");
  }
  uint8_t* e = &offline_queue[offline_queue_used];
  e[0] = (uint8_t) len;
  e[1] = shown ? OQ_SHOWN : 0;
  memcpy(&e[2], frame, len);
  offline_queue_used += need;
  offline_queue_len++;
  return 1 | (evicted_shown << 1);
}

int MyMesh::getFromOfflineQueue(uint8_t frame[], bool* was_shown) {
  if (offline_queue_len > 0) {         // take from top of queue
    int len = offline_queue[0];
    memcpy(frame, &offline_queue[2], len);
    if (was_shown) *was_shown = (offline_queue[1] & OQ_SHOWN) != 0;
    removeOfflineAt(0);
    return len;
  }
  if (was_shown) *was_shown = false;
  return 0; // queue is empty
}

int MyMesh::uiSendDirect(const uint8_t* pub_prefix, const char* text) {
  if (lookupContactByPubKey(pub_prefix, 6) == NULL || text[0] == 0) return 0;
  if (strlen(text) > MAX_TEXT_LEN) return 0;
  memcpy(_ui_try_key, pub_prefix, 6);
  strcpy(_ui_try_text, text);
  _ui_try_ts = getRTCClock()->getCurrentTimeUnique();
  memset(_ui_try_acks, 0, sizeof(_ui_try_acks));
  _ui_try = 0;
  _ui_ack = 0;
  _ui_ack_ok = false;
  return uiSendAttempt();
}

// One attempt of the message in _ui_try_text. The first always floods (the
// stored path may be stale); later ones go direct when a path is known.
// Returns 0 failed, 1 direct, 2 flood.
int MyMesh::uiSendAttempt() {
  ContactInfo* recipient = lookupContactByPubKey(_ui_try_key, 6);
  if (recipient == NULL) return 0;
  uint32_t expected_ack = 0;
  mesh::Packet* pkt = composeMsgPacket(*recipient, _ui_try_ts, _ui_try, _ui_try_text, expected_ack);
  if (pkt == NULL) return 0;
  uint32_t airtime = _radio->getEstAirtimeFor(pkt->getRawLength());
  uint32_t timeout;
  int rc;
  if (_ui_try == 0 || recipient->out_path_len == OUT_PATH_UNKNOWN) {
    sendFloodScoped(*recipient, pkt);
    timeout = calcFloodTimeoutMillisFor(airtime);
    rc = 2;
  } else {
    sendDirect(pkt, recipient->out_path, recipient->out_path_len);
    timeout = calcDirectTimeoutMillisFor(airtime, recipient->out_path_len);
    rc = 1;
  }
  _ui_try_acks[_ui_try] = expected_ack;
  _ui_ack = expected_ack;
  _ui_ack_ok = false;
  _ui_ack_contact = recipient;
  _ui_ack_until = futureMillis(timeout + 2000);
  return rc;
}

// No ack in time and tries left: send again, and move the Sent list entry
// over to the new ack.
void MyMesh::uiRetryCheck() {
  if (_ui_ack == 0 || _ui_ack_ok || !millisHasNowPassed(_ui_ack_until)) return;
  if (_ui_try + 1 >= UI_SEND_ATTEMPTS) return;   // last one: "No reply"
  uint32_t old_ack = _ui_ack;
  _ui_try++;
  if (uiSendAttempt() == 0) {
    _ui_try = UI_SEND_ATTEMPTS - 1;   // contact gone: stop, the old deadline stands
    _ui_ack = old_ack;
    return;
  }
#ifdef DISPLAY_CLASS
  if (_ui) _ui->onUiRetry(old_ack, _ui_ack, _ui_ack_until);
#endif
}

bool MyMesh::uiMyPosition(char* buf, size_t n) {
#if ENV_INCLUDE_GPS == 1
  if (!_prefs.gps_enabled || !beaconHasFix()) return false;
  snprintf(buf, n, "geo:%.5f,%.5f", sensors.node_lat, sensors.node_lon);
  return true;
#else
  (void)buf; (void)n;
  return false;
#endif
}

#ifndef SOS_REPEAT_MS
  #define SOS_REPEAT_MS (2UL * 60UL * 1000UL)
#endif

void MyMesh::sosStart() {
  _sos_on = true;
  _sos_rounds = 0;
  _sos_next = 0;   // first round right away
#if ENV_INCLUDE_GPS == 1
  if (!_prefs.gps_enabled) {   // the next rounds carry the position
    _prefs.gps_enabled = 1;
    sensors.setSettingValue("gps", "1");
    savePrefs();
  }
#endif
}

void MyMesh::sosStop() {
  if (!_sos_on) return;
  _sos_on = false;
  if (_sos_rounds > 0) sosSend(true);
}

uint32_t MyMesh::sosNextSecs() const {
  if (!_sos_on) return 0;
  long left = (long)(_sos_next - millis());
  return left > 0 ? (uint32_t)(left / 1000) : 0;
}

void MyMesh::sosTargets(char* buf, size_t n) {
  int favs = 0;
  int total = getTotalContactSlots();
  for (int i = 0; i < total; i++) {
    ContactInfo c;
    if (getContactByIdx((uint32_t)i, c) && c.type == ADV_TYPE_CHAT && (c.flags & 0x01)) favs++;
  }
  ChannelDetails ch;
  const char* chan = (getChannel(_prefs.sos_chan, ch) && ch.name[0]) ? ch.name : "-";
  snprintf(buf, n, "%s +%d", chan, favs);
}

void MyMesh::sosSend(bool all_clear) {
  bool ru = _prefs.ui_lang != UI_LANG_EN;
  char text[80];
  char pos[40] = "";
  uiMyPosition(pos, sizeof(pos));   // stays "" without a fix
  if (all_clear) snprintf(text, sizeof(text), "%s %s", ru ? "SOS: отбой, всё в порядке." : "SOS: all clear.", pos);
  else snprintf(text, sizeof(text), "%s %s", ru ? "SOS! Нужна помощь." : "SOS! Need help.", pos[0] ? pos : (ru ? "(нет GPS)" : "(no GPS)"));
  uint32_t ts = getRTCClock()->getCurrentTimeUnique();
  ChannelDetails ch;
  if (getChannel(_prefs.sos_chan, ch) && ch.name[0]) sendGroupMessage(ts, ch.channel, _prefs.node_name, text, strlen(text));
  int total = getTotalContactSlots();
  for (int i = 0; i < total; i++) {
    ContactInfo c;
    if (!getContactByIdx((uint32_t)i, c) || c.type != ADV_TYPE_CHAT || (c.flags & 0x01) == 0) continue;
    ContactInfo* rc = lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
    if (rc == NULL) continue;
    uint32_t ack = 0, est = 0;
    sendMessage(*rc, getRTCClock()->getCurrentTimeUnique(), 0, text, ack, est);
  }
}

void MyMesh::checkSos() {
  if (!_sos_on || (_sos_next != 0 && !millisHasNowPassed(_sos_next))) return;
  sosSend(false);
  _sos_rounds++;
  _sos_next = futureMillis(SOS_REPEAT_MS);
}

bool MyMesh::uiGeoFromHere(const char* text, uint32_t& meters, int& bearing_deg) {
#if ENV_INCLUDE_GPS == 1
  const char* g = strstr(text, "geo:");
  if (g == NULL || !_prefs.gps_enabled || !beaconHasFix()) return false;
  char* end = NULL;
  double lat2 = strtod(g + 4, &end);
  if (end == g + 4 || *end != ',') return false;
  const char* lon_s = end + 1;
  double lon2 = strtod(lon_s, &end);
  if (end == lon_s || lat2 < -90 || lat2 > 90 || lon2 < -180 || lon2 > 180) return false;
  const double R = 6371000.0, D = M_PI / 180.0;
  double p1 = sensors.node_lat * D, p2 = lat2 * D, dl = (lon2 - sensors.node_lon) * D;
  double a = sin((p2 - p1) / 2) * sin((p2 - p1) / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  meters = (uint32_t)(2 * R * atan2(sqrt(a), sqrt(1 - a)));
  double b = atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) / D;
  bearing_deg = ((int)lround(b) + 360) % 360;
  return true;
#else
  (void)text; (void)meters; (void)bearing_deg;
  return false;
#endif
}

const char* MyMesh::uiPhrase(int i) const {
  if (i < 0 || i >= UI_PHRASE_COUNT) return "";
  if (_phrase_ovr[i][0]) return _phrase_ovr[i];
  return _prefs.ui_lang == UI_LANG_EN ? UI_PHRASES_EN[i] : UI_PHRASES_RU[i];
}

void MyMesh::setUiPhrase(int i, const char* text) {
  if (i < 0 || i >= UI_PHRASE_COUNT) return;
  char* d = _phrase_ovr[i];
  int n = 0;
  // whole UTF-8 letters only
  while (text[n]) {
    uint8_t b = (uint8_t)text[n];
    int l = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
    if (n + l > UI_PHRASE_LEN - 1) break;
    n += l;
  }
  memcpy(d, text, n);
  d[n] = 0;
  while (n > 0 && d[n - 1] == ' ') d[--n] = 0;
  _store->saveRaw("/phrases", _phrase_ovr, sizeof(_phrase_ovr));
}

void MyMesh::setUiBadge(const char* text) {
  int n = 0;
  while (text[n]) {   // whole UTF-8 letters only
    uint8_t b = (uint8_t)text[n];
    int l = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
    if (n + l > UI_BADGE_LEN - 1) break;
    n += l;
  }
  memcpy(_badge, text, n);
  _badge[n] = 0;
  while (n > 0 && _badge[n - 1] == ' ') _badge[--n] = 0;
  _store->saveRaw("/badge", _badge, sizeof(_badge));
}

bool MyMesh::uiSendChannel(int channel_idx, const char* text) {
  ChannelDetails channel;
  if (text[0] == 0 || !getChannel(channel_idx, channel) || channel.name[0] == 0) return false;
  uint32_t ts = getRTCClock()->getCurrentTimeUnique();
  return sendGroupMessage(ts, channel.channel, _prefs.node_name, text, strlen(text));
}

uint8_t MyMesh::uiAckState() const {
  if (_ui_ack_ok) return UI_ACK_OK;
  if (_ui_ack == 0) return UI_ACK_NONE;
  if (_ui_try + 1 < UI_SEND_ATTEMPTS) return UI_ACK_WAIT;   // another try is coming
  return millisHasNowPassed(_ui_ack_until) ? UI_ACK_LOST : UI_ACK_WAIT;
}

float MyMesh::getAirtimeBudgetFactor() const {
  return _prefs.airtime_factor;
}

int MyMesh::getInterferenceThreshold() const {
  return 0; // disabled for now, until currentRSSI() problem is resolved
}
bool MyMesh::getCADEnabled() const {
  return false; // hardware CAD before TX (disabled by default, until configurable)
}

int MyMesh::getAGCResetInterval() const {
  return 4000; // SX1262 analog/AGC can stick; companion had no reset
}

void MyMesh::restoreRadioAfterScan() {
  radio_driver.pauseNoiseFloor(false);
  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain);
  radio_driver.resetAGC();
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * 0.5f);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * 0.2f);
  return getRNG()->nextInt(0, 5*t + 1);
}

uint8_t MyMesh::getExtraAckTransmitCount() const {
  return _prefs.multi_acks;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  if (_serial->isConnected() && len + 3 <= MAX_FRAME_SIZE) {
    int i = 0;
    out_frame[i++] = PUSH_CODE_LOG_RX_DATA;
    out_frame[i++] = (int8_t)(snr * 4);
    out_frame[i++] = (int8_t)(rssi);
    memcpy(&out_frame[i], raw, len);
    i += len;

    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::isAutoAddEnabled() const {
  return (_prefs.manual_add_contacts & 1) == 0;
}

bool MyMesh::shouldAutoAddContactType(uint8_t contact_type) const {
  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;
  }

  uint8_t type_bit = 0;
  switch (contact_type) {
    case ADV_TYPE_CHAT:
      type_bit = AUTO_ADD_CHAT;
      break;
    case ADV_TYPE_REPEATER:
      type_bit = AUTO_ADD_REPEATER;
      break;
    case ADV_TYPE_ROOM:
      type_bit = AUTO_ADD_ROOM_SERVER;
      break;
    case ADV_TYPE_SENSOR:
      type_bit = AUTO_ADD_SENSOR;
      break;
    default:
      return false;  // Unknown type, don't auto-add
  }

  return (_prefs.autoadd_config & type_bit) != 0;
}

bool MyMesh::shouldOverwriteWhenFull() const {
  return (_prefs.autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) != 0;
}

uint8_t MyMesh::getAutoAddMaxHops() const {
  return _prefs.autoadd_max_hops;
}

void MyMesh::onContactOverwrite(const uint8_t* pub_key) {
    _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE); // delete from storage
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACT_DELETED;
    memcpy(&out_frame[1], pub_key, PUB_KEY_SIZE);
    _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
  }
}

void MyMesh::onContactsFull() {
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACTS_FULL;
    _serial->writeFrame(out_frame, 1);
  }
}

void MyMesh::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (_serial->isConnected()) {
    if (is_new) {
      writeContactRespFrame(PUSH_CODE_NEW_ADVERT, contact);
    } else {
      out_frame[0] = PUSH_CODE_ADVERT;
      memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
      _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
    }
  } else {
#ifdef DISPLAY_CLASS
    if (_ui) _ui->notify(UIEventType::newContactMessage);
#endif
  }

  // add inbound-path to mem cache
  if (path && mesh::Packet::isValidPathLen(path_len)) {  // check path is valid
    AdvertPath* p = advert_paths;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {   // check if already in table, otherwise evict oldest
      if (memcmp(advert_paths[i].pubkey_prefix, contact.id.pub_key, sizeof(AdvertPath::pubkey_prefix)) == 0) {
        p = &advert_paths[i];   // found
        break;
      }
      if (advert_paths[i].recv_timestamp < oldest) {
        oldest = advert_paths[i].recv_timestamp;
        p = &advert_paths[i];
      }
    }

    memcpy(p->pubkey_prefix, contact.id.pub_key, sizeof(p->pubkey_prefix));
    strcpy(p->name, contact.name);
    p->recv_timestamp = getRTCClock()->getCurrentTime();
    p->path_len = mesh::Packet::copyPath(p->path, path, path_len);
  }

  if (!is_new) dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY); // only schedule lazy write for contacts that are in contacts[]
}

static int sort_by_recent(const void *a, const void *b) {
  return ((AdvertPath *) b)->recv_timestamp - ((AdvertPath *) a)->recv_timestamp;
}

int MyMesh::getRecentlyHeard(AdvertPath dest[], int max_num) {
  if (max_num > ADVERT_PATH_TABLE_SIZE) max_num = ADVERT_PATH_TABLE_SIZE;
  qsort(advert_paths, ADVERT_PATH_TABLE_SIZE, sizeof(advert_paths[0]), sort_by_recent);

  for (int i = 0; i < max_num; i++) {
    dest[i] = advert_paths[i];
  }
  return max_num;
}

static int sort_neighbors_by_snr(const void *a, const void *b) {
  const NeighborInfo* na = (const NeighborInfo*)a;
  const NeighborInfo* nb = (const NeighborInfo*)b;
  if (na->recv_timestamp == 0 && nb->recv_timestamp == 0) return 0;
  if (na->recv_timestamp == 0) return 1;
  if (nb->recv_timestamp == 0) return -1;
  if (nb->snr_x4 != na->snr_x4) return (int)nb->snr_x4 - (int)na->snr_x4;
  if (nb->recv_timestamp > na->recv_timestamp) return 1;
  if (nb->recv_timestamp < na->recv_timestamp) return -1;
  return 0;
}

void MyMesh::putNeighbor(const uint8_t* pubkey, int8_t snr_x4) {
  NeighborInfo* slot = NULL;
  uint32_t oldest = 0xFFFFFFFF;
  for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
    if (memcmp(neighbors[i].pubkey, pubkey, PUB_KEY_SIZE) == 0) {
      slot = &neighbors[i];
      break;
    }
    if (neighbors[i].recv_timestamp < oldest) {
      oldest = neighbors[i].recv_timestamp;
      slot = &neighbors[i];
    }
  }
  if (!slot) return;

  memcpy(slot->pubkey, pubkey, PUB_KEY_SIZE);
  slot->snr_x4 = snr_x4;
  slot->recv_timestamp = getRTCClock()->getCurrentTime();

  ContactInfo* known = lookupContactByPubKey(pubkey, PUB_KEY_SIZE);
  if (known && known->name[0]) {
    StrHelper::strncpy(slot->name, known->name, sizeof(slot->name));
  } else {
    mesh::Utils::toHex(slot->name, pubkey, 4);
    slot->name[8] = 0;
  }
}

int MyMesh::getNeighbors(NeighborInfo dest[], int max_num) {
  if (max_num > NEIGHBOR_TABLE_SIZE) max_num = NEIGHBOR_TABLE_SIZE;
  qsort(neighbors, NEIGHBOR_TABLE_SIZE, sizeof(neighbors[0]), sort_neighbors_by_snr);
  int n = 0;
  for (int i = 0; i < NEIGHBOR_TABLE_SIZE && n < max_num; i++) {
    if (neighbors[i].recv_timestamp == 0) continue;
    dest[n++] = neighbors[i];
  }
  return n;
}

bool MyMesh::sendNeighborDiscover() {
  uint8_t data[10];
  data[0] = 0x80; // CTL_TYPE_NODE_DISCOVER_REQ
  data[1] = (1 << ADV_TYPE_REPEATER);
  getRNG()->random(&data[2], 4);
  memcpy(&pending_neighbor_discover_tag, &data[2], 4);
  pending_neighbor_discover_until = futureMillis(60000);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = createControlData(data, sizeof(data));
  if (pkt) {
    memset(neighbors, 0, sizeof(neighbors));
    sendZeroHop(pkt);
    return true;
  }
  pending_neighbor_discover_tag = 0;
  return false;
}

void MyMesh::onContactPathUpdated(const ContactInfo &contact) {
  out_frame[0] = PUSH_CODE_PATH_UPDATED;
  memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
  _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE); // NOTE: app may not be connected

  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
}

ContactInfo*  MyMesh::processAck(const uint8_t *data) {
#if ENV_INCLUDE_GPS == 1
  if (_bcn_try >= 0 && !_bcn_ok) {   // a beacon to a contact: any of its tries
    for (int i = 0; i <= _bcn_try && i < 3; i++) {
      if (_bcn_acks[i] != 0 && memcmp(data, &_bcn_acks[i], 4) == 0) {
        _bcn_ok = true;
        _bcn_try = -1;
        return lookupContactByPubKey(_prefs.beacon_pub, PUB_KEY_SIZE);
      }
    }
  }
#endif
  bool ui_hit = false;   // a message sent from the device UI: any of its attempts
  if (_ui_ack != 0) {
    for (int i = 0; i <= _ui_try && i < UI_SEND_ATTEMPTS; i++) {
      if (_ui_try_acks[i] != 0 && memcmp(data, &_ui_try_acks[i], 4) == 0) ui_hit = true;
    }
  }
  if (ui_hit) {
    uint32_t current = _ui_ack;   // the Sent list holds the latest attempt's ack
    _ui_ack = 0;
    _ui_ack_ok = true;
    memset(_ui_try_acks, 0, sizeof(_ui_try_acks));
#ifdef DISPLAY_CLASS
    if (_ui) {
      uint8_t key[6];
      _ui->onUiAck((const uint8_t*)&current, key);   // marks it in the Sent list
      _ui->notify(UIEventType::ack);
    }
#endif
    return _ui_ack_contact;
  }
#ifdef DISPLAY_CLASS
  {   // an earlier message from the device UI, confirmed late
    uint8_t key[6];
    if (_ui && _ui->onUiAck(data, key)) {
      _ui->notify(UIEventType::ack);
      return lookupContactByPubKey(key, 6);
    }
  }
#endif
  // see if matches any in a table
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; i++) {
    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient
      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;
      memcpy(&out_frame[1], data, 4);
      uint32_t trip_time = _ms->getMillis() - expected_ack_table[i].msg_sent;
      memcpy(&out_frame[5], &trip_time, 4);
      _serial->writeFrame(out_frame, 9);

      // NOTE: the same ACK can be received multiple times!
      expected_ack_table[i].ack = 0; // clear expected hash, now that we have received ACK
      return expected_ack_table[i].contact;
    }
  }
  return checkConnectionsAck(data);
}

void MyMesh::queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt,
                          uint32_t sender_timestamp, const uint8_t *extra, int extra_len, const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV;
  }
  memcpy(&out_frame[i], from.id.pub_key, 6);
  i += 6; // just 6-byte prefix
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = txt_type;
  memcpy(&out_frame[i], &sender_timestamp, 4);
  i += 4;
  if (extra_len > 0) {
    memcpy(&out_frame[i], extra, extra_len);
    i += extra_len;
  }
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  bool should_display = txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN;
  int queued = addToOfflineQueue(out_frame, i, should_display);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }

#ifdef DISPLAY_CLASS
  // we only want to show text messages on display, not cli data
  if (_ui) for (int k = queued >> 1; k > 0; k--) _ui->clearTakenMsg();
  if (should_display && (queued & 1) && _ui) {
    ui_rx_is_ch = 0;
    memcpy(ui_rx_key, from.id.pub_key, sizeof(ui_rx_key));
    _ui->newMsg(path_len, from.name, text, offline_queue_len, false);
    if (!_serial->isConnected()) {
      _ui->notify(UIEventType::contactMessage);
    }
  }
#endif
}

bool MyMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  // REVISIT: try to determine which Region (from transport_codes[1]) that Sender is indicating for replies/responses
  //    if unknown, fallback to finding Region from transport_codes[0], the 'scope' used by Sender
  return false;
}

bool MyMesh::allowPacketForward(const mesh::Packet* packet) {
  return _prefs.isRepeatEn();
}

uint8_t MyMesh::radioProfile() const {
  return radioProfileOf(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr,
                         _prefs.home_freq, _prefs.home_bw, _prefs.home_sf, _prefs.home_cr);
}

void MyMesh::applyRadioProfile(uint8_t profile) {
  bool repeat = applyRadioProfileChoice(profile, _prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr,
                                        _prefs.home_freq, _prefs.home_bw, _prefs.home_sf, _prefs.home_cr);
  uint32_t khz = (uint32_t)(_prefs.freq * 1000.0f + 0.5f);
  if (repeat && !isValidClientRepeatFreq(khz)) repeat = false;
  _prefs.setRepeatEn(repeat);
  savePrefs();
  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, _prefs.path_hash_mode + 1);
  }
}

void MyMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: dynamic send_scope, depending on recipient and current 'home' Region
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}
void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}

void MyMesh::onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);
  relayText(from, text);   // "реле 1 вкл" from an allowed contact switches a relay
}

// ------------------------------------------------------------ relays

void MyMesh::relayReply(const uint8_t* key, const char* text) {
  ContactInfo* c = lookupContactByPubKey(key, 6);
  if (c == NULL || c->type != ADV_TYPE_CHAT) return;
  uint32_t ack = 0, est = 0;
  sendMessage(*c, getRTCClock()->getCurrentTimeUnique(), 0, text, ack, est);
}

void MyMesh::relayStatus(int i, char* buf, size_t n) {
  RelayDef* r = relays.get(i);
  bool ru = _prefs.ui_lang != UI_LANG_EN;
  if (r == NULL) { snprintf(buf, n, ru ? "реле %d: нет" : "relay %d: none", i + 1); return; }
  const char* st = r->state == RELAY_ON ? (ru ? "ВКЛ" : "ON") : r->state == RELAY_OFF ? (ru ? "ВЫКЛ" : "OFF") : "?";
  snprintf(buf, n, "%s %d %s: %s", ru ? "реле" : "relay", i + 1, r->name, st);
}

// lower-case Latin and Cyrillic letters in place (UTF-8)
static void relayLower(char* s) {
  for (uint8_t* p = (uint8_t*)s; *p; p++) {
    if (*p >= 'A' && *p <= 'Z') *p += 0x20;
    else if (p[0] == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F) { p[1] += 0x20; p++; }            // А..П
    else if (p[0] == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF) { p[0] = 0xD1; p[1] -= 0x20; p++; } // Р..Я
    else if (p[0] == 0xD0 && p[1] == 0x81) { p[0] = 0xD1; p[1] = 0x91; p++; }                  // Ё
    else if (*p >= 0xC0 && p[1]) p++;
  }
}

bool MyMesh::relayText(const ContactInfo& from, const char* text) {
  if (!relays.supported() || !relays.meshOn() || !relays.allowed(from.id.pub_key)) return false;
  char t[64];
  strncpy(t, text, sizeof(t) - 1);
  t[sizeof(t) - 1] = 0;
  relayLower(t);
  const char* p = t;
  if (strncmp(p, "реле", 8) == 0) p += 8;
  else if (strncmp(p, "relay", 5) == 0) p += 5;
  else return false;
  if (*p != 0 && *p != ' ') return false;
  while (*p == ' ') p++;
  bool ru = _prefs.ui_lang != UI_LANG_EN;
  char reply[120];
  if (*p == 0) {   // "реле": all of them
    reply[0] = 0;
    for (int i = 0; i < RELAY_MAX; i++) {
      if (!relays.get(i)) continue;
      char one[40];
      relayStatus(i, one, sizeof(one));
      if (strlen(reply) + strlen(one) + 3 < sizeof(reply)) {
        if (reply[0]) strcat(reply, "; ");
        strcat(reply, one);
      }
    }
    if (!reply[0]) strcpy(reply, ru ? "реле не настроены" : "no relays");
    relayReply(from.id.pub_key, reply);
    return true;
  }
  // which relay: a number or a name
  char who[RELAY_NAME_LEN + 4];
  int n = 0;
  while (*p && *p != ' ' && n < (int)sizeof(who) - 1) who[n++] = *p++;
  who[n] = 0;
  while (*p == ' ') p++;
  int idx = -1;
  if (who[0] >= '1' && who[0] <= '9' && who[1] == 0) idx = who[0] - '1';
  else {
    for (int i = 0; i < RELAY_MAX && idx < 0; i++) {
      RelayDef* r = relays.get(i);
      if (!r) continue;
      char nm[RELAY_NAME_LEN];
      strcpy(nm, r->name);
      relayLower(nm);
      if (strcmp(nm, who) == 0) idx = i;
    }
  }
  if (!relays.get(idx)) {
    relayReply(from.id.pub_key, ru ? "нет такого реле" : "no such relay");
    return true;
  }
  int action = -1;
  if (strcmp(p, "вкл") == 0 || strcmp(p, "on") == 0 || strcmp(p, "1") == 0) action = RELAY_DO_ON;
  else if (strcmp(p, "выкл") == 0 || strcmp(p, "off") == 0 || strcmp(p, "0") == 0) action = RELAY_DO_OFF;
  else if (strcmp(p, "перекл") == 0 || strcmp(p, "toggle") == 0) action = RELAY_DO_TOGGLE;
  if (action < 0) {   // "?", "статус" or nothing: the state
    relayStatus(idx, reply, sizeof(reply));
    relayReply(from.id.pub_key, reply);
    return true;
  }
  if (!relays.request(idx, (uint8_t)action, from.id.pub_key)) {
    snprintf(reply, sizeof(reply), ru ? "реле %d: занято (%s)" : "relay %d: busy (%s)", idx + 1,
             relays.lastError()[0] ? relays.lastError() : "...");
    relayReply(from.id.pub_key, reply);
  }
  return true;
}

void MyMesh::onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                               const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                                 const uint8_t *sender_prefix, const char *text) {
  markConnectionActive(from);
  // from.sync_since change needs to be persisted
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);
}

void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;

  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  int queued = addToOfflineQueue(out_frame, i, true);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  } else {
#ifdef DISPLAY_CLASS
    if ((queued & 1) && _ui) _ui->notify(UIEventType::channelMessage);
#endif
  }
#ifdef DISPLAY_CLASS
  if (_ui) for (int k = queued >> 1; k > 0; k--) _ui->clearTakenMsg();
  if (queued & 1) {
    // Get the channel name from the channel index
    const char *channel_name = "Unknown";
    ChannelDetails channel_details;
    if (getChannel(channel_idx, channel_details)) {
      channel_name = channel_details.name;
    }
    ui_rx_is_ch = 1;
    ui_rx_ch = channel_idx;
    if (_ui) _ui->newMsg(path_len, channel_name, text, offline_queue_len, true);
  }
#endif
}

void MyMesh::onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                               const uint8_t *data, size_t data_len) {
  if (data_len > MAX_CHANNEL_DATA_LENGTH) {
    MESH_DEBUG_PRINTLN("onChannelDataRecv: dropping payload_len=%d exceeds frame limit=%d",
                       (uint32_t)data_len, (uint32_t)MAX_CHANNEL_DATA_LENGTH);
    return;
  }

  int i = 0;
  out_frame[i++] = RESP_CODE_CHANNEL_DATA_RECV;
  out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
  out_frame[i++] = 0; // reserved1
  out_frame[i++] = 0; // reserved2

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = (uint8_t)(data_type & 0xFF);
  out_frame[i++] = (uint8_t)(data_type >> 8);
  out_frame[i++] = (uint8_t)data_len;

  int copy_len = (int)data_len;
  if (copy_len > 0) {
    memcpy(&out_frame[i], data, copy_len);
    i += copy_len;
  }
  int queued = addToOfflineQueue(out_frame, i, false);
#ifdef DISPLAY_CLASS
  if (_ui) for (int k = queued >> 1; k > 0; k--) _ui->clearTakenMsg();
#endif

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
}

uint8_t MyMesh::onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                 uint8_t len, uint8_t *reply) {
  if (data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t permissions = 0;
    uint8_t cp = contact.flags >> 1; // LSB used as 'favourite' bit (so only use upper bits)

    if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_ALL) {
      permissions = TELEM_PERM_BASE;
    } else if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_FLAGS) {
      permissions = cp & TELEM_PERM_BASE;
    }

    if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_LOCATION;
    } else if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_LOCATION;
    }

    if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_ENVIRONMENT;
    } else if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_ENVIRONMENT;
    }

    uint8_t perm_mask = ~(data[1]);    // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions
    permissions &= perm_mask;

    if (permissions & TELEM_PERM_BASE) { // only respond if base permission bit is set
      telemetry.reset();
      telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      // query other sensors -- target specific
      sensors.querySensors(permissions, telemetry);

      float temperature = board.getMCUTemperature();
      if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
        telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
      }

      memcpy(reply, &sender_timestamp,
             4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

      uint8_t tlen = telemetry.getSize();
      memcpy(&reply[4], telemetry.getBuffer(), tlen);
      return 4 + tlen;
    }
  }
  return 0; // unknown
}

void MyMesh::onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) {
  uint32_t tag;
  memcpy(&tag, data, 4);

  if (_clock_pull_tag && tag == _clock_pull_tag && clockNodeListHas(_prefs.clock_nodes, contact.id.pub_key)) {
    onClockResponse(data, len);
    return;
  }

  if (pending_login && memcmp(&pending_login, contact.id.pub_key, 4) == 0) { // check for login response
    // yes, is response to pending sendLogin()
    pending_login = 0;

    int i = 0;
    if (memcmp(&data[4], "OK", 2) == 0) { // legacy Repeater login OK response
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = 0; // legacy: is_admin = false
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6;                                     // pub_key_prefix
    } else if (data[4] == RESP_SERVER_LOGIN_OK) { // new login response
      uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
      if (keep_alive_secs > 0) {
        startConnection(contact, keep_alive_secs);
      }
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = data[6]; // permissions (eg. is_admin)
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
      memcpy(&out_frame[i], &tag, 4);
      i += 4; // NEW: include server timestamp
      out_frame[i++] = data[7]; // NEW (v7): ACL permissions
      out_frame[i++] = data[12]; // FIRMWARE_VER_LEVEL
    } else {
      out_frame[i++] = PUSH_CODE_LOGIN_FAIL;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
    }
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && // check for status response
             pending_status &&
             memcmp(&pending_status, contact.id.pub_key, 4) == 0 // legacy matching scheme
                                                                 // FUTURE: tag == pending_status
  ) {
    pending_status = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_STATUS_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_telemetry) {  // check for matching response tag
    pending_telemetry = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_req) {  // check for matching response tag
    pending_req = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], &tag, 4);   // app needs to match this to RESP_CODE_SENT.tag
    i += 4;
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::onContactPathRecv(ContactInfo& contact, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 4) {
    uint32_t tag;
    memcpy(&tag, extra, 4);

    if (tag == pending_discovery) {  // check for matching response tag)
      pending_discovery = 0;

      if (!mesh::Packet::isValidPathLen(in_path_len) || !mesh::Packet::isValidPathLen(out_path_len)) {
        MESH_DEBUG_PRINTLN("onContactPathRecv, invalid path sizes: %d, %d", in_path_len, out_path_len);
      } else {
        int i = 0;
        out_frame[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
        out_frame[i++] = 0; // reserved
        memcpy(&out_frame[i], contact.id.pub_key, 6);
        i += 6; // pub_key_prefix
        out_frame[i++] = out_path_len;
        i += mesh::Packet::writePath(&out_frame[i], out_path, out_path_len);
        out_frame[i++] = in_path_len;
        i += mesh::Packet::writePath(&out_frame[i], in_path, in_path_len);
        // NOTE: telemetry data in 'extra' is discarded at present

        _serial->writeFrame(out_frame, i);
      }
      return false;  // DON'T send reciprocal path!
    }
  }
  // let base class handle received path and data
  return BaseChatMesh::onContactPathRecv(contact, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
}

void MyMesh::onControlDataRecv(mesh::Packet *packet) {
  uint8_t type = packet->payload[0] & 0xF0;
  if (type == 0x90 && packet->payload_len >= 6 + PUB_KEY_SIZE) { // CTL_TYPE_NODE_DISCOVER_RESP
    uint8_t node_type = packet->payload[0] & 0x0F;
    if (node_type == ADV_TYPE_REPEATER && pending_neighbor_discover_tag != 0
        && !millisHasNowPassed(pending_neighbor_discover_until)) {
      uint32_t tag;
      memcpy(&tag, &packet->payload[2], 4);
      if (tag == pending_neighbor_discover_tag) {
        mesh::Identity id(&packet->payload[6]);
        if (!id.matches(self_id)) {
          putNeighbor(id.pub_key, (int8_t)(packet->getSNR() * 4));
        }
      }
    }
  }

  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_CONTROL_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = packet->path_len;
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), data received while app offline");
  }
}

void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_RAW_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = 0xFF; // reserved (possibly path_len in future)
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), data received while app offline");
  }
}

void MyMesh::onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) {
  uint8_t path_sz = flags & 0x03;  // NEW v1.11+
  if (12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onTraceRecv(), path_len is too long: %d", (uint32_t)path_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_TRACE_DATA;
  out_frame[i++] = 0; // reserved
  out_frame[i++] = path_len;
  out_frame[i++] = flags;
  memcpy(&out_frame[i], &tag, 4);
  i += 4;
  memcpy(&out_frame[i], &auth_code, 4);
  i += 4;
  memcpy(&out_frame[i], path_hashes, path_len);
  i += path_len;

  memcpy(&out_frame[i], path_snrs, path_len >> path_sz);
  i += path_len >> path_sz;
  out_frame[i++] = (int8_t)(packet->getSNR() * 4); // extra/final SNR (to this node)

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onTraceRecv(), data received while app offline");
  }
}

uint32_t MyMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}
uint32_t MyMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  uint8_t path_hash_count = path_len & 63;
  return SEND_TIMEOUT_BASE_MILLIS +
         ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) *
          (path_hash_count + 1));
}

void MyMesh::onSendTimeout() {}

MyMesh::MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store, AbstractUITask* ui)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables),
      _serial(NULL), telemetry(MAX_PACKET_PAYLOAD - 4), _store(&store), _ui(ui), _iter(0) {
  _iter_started = false;
  _iter_console_pending = false;
  _console_push_msg = false;
  _console_ack = 0;
  _console_ack_at = 0;
  _cli_rescue = false;
  offline_queue_len = 0;
  offline_queue_used = 0;
  app_target_ver = 0;
  clearPendingReqs();
  next_ack_idx = 0;
  sign_data = NULL;
  dirty_contacts_expiry = 0;
  _clock_pull_tag = 0;
  memset(_clock_pull_peer, 0, sizeof(_clock_pull_peer));
  _clock_rr = 0;
  _clock_pull_sent_ms = 0;
  _clock_pull_deadline = 0;
  _clock_next_pull = 0;
  _clock_reply_after = 0;
  memset(advert_paths, 0, sizeof(advert_paths));
  memset(neighbors, 0, sizeof(neighbors));
  pending_neighbor_discover_tag = 0;
  pending_neighbor_discover_until = 0;
  memset(send_scope.key, 0, sizeof(send_scope.key));
  send_unscoped = false;

  // defaults
  _prefs.airtime_factor = 1.0;
  strcpy(_prefs.node_name, "NONAME");
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.gps_enabled = 0;       // GPS disabled by default
  _prefs.gps_interval = 0;      // No automatic GPS updates by default
#if ENV_INCLUDE_GPS == 1
  _beacon_next = 0;
  _beacon_sent_at = 0;
#endif
  _prefs.radio_fem_rxgain = 0;  // FEM RX LNA off (Heltec V4.3 bypass)
  _prefs.radio_fem_txgain = 0;
  //_prefs.rx_delay_base = 10.0f;  enable once new algo fixed
  _prefs.setRepeatEn(false);
#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default
#endif
#endif
}

void MyMesh::begin(bool has_display) {
  BaseChatMesh::begin();

  if (!_store->loadMainIdentity(self_id)) {
    self_id = radio_new_identity(); // create new random identity
    int count = 0;
    while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) { // reserved id hashes
      self_id = radio_new_identity();
      count++;
    }
    _store->saveMainIdentity(self_id);
  }

// if name is provided as a build flag, use that as default node name instead
#ifdef ADVERT_NAME
  strcpy(_prefs.node_name, ADVERT_NAME);
#else
  // use hex of first 4 bytes of identity public key as default node name
  char pub_key_hex[10];
  mesh::Utils::toHex(pub_key_hex, self_id.pub_key, 4);
  strcpy(_prefs.node_name, pub_key_hex);
#endif

  // if build provides default-scope, init with that
#ifdef DEFAULT_FLOOD_SCOPE_NAME
  strcpy(_prefs.default_scope_name, DEFAULT_FLOOD_SCOPE_NAME);
  {
    TransportKeyStore temp;
    TransportKey key;
    temp.getAutoKeyFor(0, "#" DEFAULT_FLOOD_SCOPE_NAME, key);
    memcpy(_prefs.default_scope_key, key.key, sizeof(key.key));
  }
#endif

  // load persisted prefs
  _store->loadPrefs(_prefs);
  memset(_phrase_ovr, 0, sizeof(_phrase_ovr));
  relays.begin(_store);
  if (_store->loadRaw("/badge", _badge, sizeof(_badge))) _badge[UI_BADGE_LEN - 1] = 0;
  else _badge[0] = 0;
  if (_store->loadRaw("/phrases", _phrase_ovr, sizeof(_phrase_ovr))) {
    for (int i = 0; i < UI_PHRASE_COUNT; i++) _phrase_ovr[i][UI_PHRASE_LEN - 1] = 0;
  } else {
    memset(_phrase_ovr, 0, sizeof(_phrase_ovr));
  }
  sensors.node_lat = _prefs.node_lat;
  sensors.node_lon = _prefs.node_lon;

  // sanitise bad pref values
  _prefs.rx_delay_base = constrain(_prefs.rx_delay_base, 0, 20.0f);
  _prefs.airtime_factor = constrain(_prefs.airtime_factor, 0, 9.0f);
  _prefs.freq = constrain(_prefs.freq, 150.0f, 2500.0f);
  _prefs.bw = constrain(_prefs.bw, 7.8f, 500.0f);
  _prefs.sf = constrain(_prefs.sf, 5, 12);
  _prefs.cr = constrain(_prefs.cr, 5, 8);
  _prefs.tx_power_dbm = constrain(_prefs.tx_power_dbm, -9, MAX_LORA_TX_POWER);
  _prefs.gps_enabled = constrain(_prefs.gps_enabled, 0, 1);  // Ensure boolean 0 or 1
  _prefs.gps_interval = constrain(_prefs.gps_interval, 0, 86400);  // Max 24 hours
  _prefs.beacon_mode = constrain(_prefs.beacon_mode, BEACON_OFF, BEACON_CHAT);
  // 0 = no timed send (the distance trigger alone)
  if (_prefs.beacon_group_mins != 0 && (_prefs.beacon_group_mins < 5 || _prefs.beacon_group_mins > 180)) _prefs.beacon_group_mins = 30;
  if (_prefs.beacon_chat_mins != 0 && (_prefs.beacon_chat_mins < 5 || _prefs.beacon_chat_mins > 180)) _prefs.beacon_chat_mins = 30;
  if (_prefs.beacon_move > 6) _prefs.beacon_move = 0;
#ifdef MAX_GROUP_CHANNELS
  if (MAX_GROUP_CHANNELS > 0) {
    _prefs.beacon_chan = constrain(_prefs.beacon_chan, 0, MAX_GROUP_CHANNELS - 1);
    _prefs.sos_chan = constrain(_prefs.sos_chan, 0, MAX_GROUP_CHANNELS - 1);
  }
#endif

#ifdef BLE_PIN_CODE // 123456 by default
  if (_prefs.ble_pin == 0) {
#ifdef DISPLAY_CLASS
    if (has_display && BLE_PIN_CODE == 123456) {
      StdRNG rng;
      _active_ble_pin = rng.nextInt(100000, 999999); // random pin each session
    } else {
      _active_ble_pin = BLE_PIN_CODE; // otherwise static pin
    }
#else
    _active_ble_pin = BLE_PIN_CODE; // otherwise static pin
#endif
  } else {
    _active_ble_pin = _prefs.ble_pin;
  }
#else
  _active_ble_pin = 0;
#endif

  resetContacts();
  _store->loadContacts(this);
  bootstrapRTCfromContacts();
  _clock_pull_tag = 0;
  _clock_next_pull = clockNodesConfigured(_prefs.clock_nodes) ? futureMillis(CLOCK_PULL_FIRST_MS) : 0;
  addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel
  _store->loadChannels(this);

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain);
  board.setLoRaFemPaGainEnabled(_prefs.radio_fem_txgain);
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
}

const char *MyMesh::getNodeName() {
  return _prefs.node_name;
}
NodePrefs *MyMesh::getNodePrefs() {
  return &_prefs;
}
uint32_t MyMesh::getBLEPin() {
  return _active_ble_pin;
}

struct FreqRange {
  uint32_t lower_freq, upper_freq;
};

static FreqRange repeat_freq_ranges[] = {
  #ifdef ALLOWED_REPEAT_FREQ_RANGE
  ALLOWED_REPEAT_FREQ_RANGE
  #else
  { 433000, 433000 },
  { 869495, 869495 },
  { 918000, 918000 }
  #endif
};

bool MyMesh::isValidClientRepeatFreq(uint32_t f) const {
  for (int i = 0; i < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]); i++) {
    auto r = &repeat_freq_ranges[i];
    if (f >= r->lower_freq && f <= r->upper_freq) return true;
  }
  return false;
}

void MyMesh::startInterface(BaseSerialInterface &serial) {
  _serial = &serial;
  serial.enable();
}

void MyMesh::handleCmdFrame(size_t len) {
  if (cmd_frame[0] == CMD_DEVICE_QUERY && len >= 2) { // sent when app establishes connection
    app_target_ver = cmd_frame[1];                    // which version of protocol does app understand

    int i = 0;
    out_frame[i++] = RESP_CODE_DEVICE_INFO;
    out_frame[i++] = FIRMWARE_VER_CODE;
    out_frame[i++] = MAX_CONTACTS / 2;   // v3+
    out_frame[i++] = MAX_GROUP_CHANNELS; // v3+
    memcpy(&out_frame[i], &_prefs.ble_pin, 4);
    i += 4;
    memset(&out_frame[i], 0, 12);
    strcpy((char *)&out_frame[i], FIRMWARE_BUILD_DATE);
    i += 12;
    StrHelper::strzcpy((char *)&out_frame[i], board.getManufacturerName(), 40);
    i += 40;
    StrHelper::strzcpy((char *)&out_frame[i], FIRMWARE_VERSION, 20);
    i += 20;
    out_frame[i++] = _prefs.isRepeatEn() ? 1 : 0;   // v9+
    out_frame[i++] = _prefs.path_hash_mode;  // v10+
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_APP_START &&
             len >= 8) { // sent when app establishes connection, respond with node ID
    //  cmd_frame[1..7]  reserved future
    char *app_name = (char *)&cmd_frame[8];
    cmd_frame[len] = 0; // make app_name null terminated
    MESH_DEBUG_PRINTLN("App %s connected", app_name);

    _iter_started = false; // stop any left-over ContactsIterator
    int i = 0;
    out_frame[i++] = RESP_CODE_SELF_INFO;
    out_frame[i++] = ADV_TYPE_CHAT; // what this node Advert identifies as (maybe node's pronouns too?? :-)
    out_frame[i++] = _prefs.tx_power_dbm;
    out_frame[i++] = MAX_LORA_TX_POWER;
    memcpy(&out_frame[i], self_id.pub_key, PUB_KEY_SIZE);
    i += PUB_KEY_SIZE;

    int32_t lat, lon;
    lat = (sensors.node_lat * 1000000.0);
    lon = (sensors.node_lon * 1000000.0);
    memcpy(&out_frame[i], &lat, 4);
    i += 4;
    memcpy(&out_frame[i], &lon, 4);
    i += 4;
    out_frame[i++] = _prefs.multi_acks; // new v7+
    out_frame[i++] = _prefs.advert_loc_policy;
    out_frame[i++] = (_prefs.telemetry_mode_env << 4) | (_prefs.telemetry_mode_loc << 2) |
                     (_prefs.telemetry_mode_base); // v5+
    out_frame[i++] = _prefs.manual_add_contacts;

    uint32_t freq = _prefs.freq * 1000;
    memcpy(&out_frame[i], &freq, 4);
    i += 4;
    uint32_t bw = _prefs.bw * 1000;
    memcpy(&out_frame[i], &bw, 4);
    i += 4;
    out_frame[i++] = _prefs.sf;
    out_frame[i++] = _prefs.cr;

    int tlen = strlen(_prefs.node_name); // revisit: UTF_8 ??
    memcpy(&out_frame[i], _prefs.node_name, tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_TXT_MSG && len >= 14) {
    int i = 1;
    uint8_t txt_type = cmd_frame[i++];
    uint8_t attempt = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    uint8_t *pub_key_prefix = &cmd_frame[i];
    i += 6;
    if (isConsoleKey(pub_key_prefix, 6)) {
      char *text = (char *)&cmd_frame[i];
      int tlen = len - i;
      if (tlen < 0) tlen = 0;
      text[tlen] = 0;
      // Same hash the app already expects for a normal text send.
      int text_len = strlen(text);
      if (text_len > MAX_TEXT_LEN) text_len = MAX_TEXT_LEN;
      uint8_t ack_src[5 + MAX_TEXT_LEN];
      memcpy(ack_src, &msg_timestamp, 4);
      ack_src[4] = (attempt & 3);
      memcpy(&ack_src[5], text, text_len);
      uint32_t expected_ack = 0;
      mesh::Utils::sha256((uint8_t *)&expected_ack, 4, ack_src, 5 + text_len, self_id.pub_key, PUB_KEY_SIZE);
      if (expected_ack == 0) expected_ack = 1;

      // Resends repeat the same text with a new timestamp. Running each one
      // filled the chat with copies.
      static char last_text[48];
      static unsigned long last_text_ms = 0;
      bool replay = last_text_ms != 0 && (unsigned long)(millis() - last_text_ms) < 8000 &&
                    strcmp(last_text, text) == 0;
      if (!replay) {
        strncpy(last_text, text, sizeof(last_text) - 1);
        last_text[sizeof(last_text) - 1] = 0;
        last_text_ms = millis();
        char reply[MAX_FRAME_SIZE];
        ConsoleReplyPrint printer(reply, sizeof(reply));
        runCLICommand(text, printer);
        queueConsoleReply(reply);
      }
      uint32_t est_timeout = 8000;
      out_frame[0] = RESP_CODE_SENT;
      out_frame[1] = 0;
      memcpy(&out_frame[2], &expected_ack, 4);
      memcpy(&out_frame[6], &est_timeout, 4);
      _serial->writeFrame(out_frame, 10);
      // Confirm on a later loop. Sending it in this same turn arrives before
      // the app has stored the ack, so the app retries.
      _console_ack = expected_ack;
      _console_ack_at = futureMillis(500);
      _console_push_msg = !replay;
      return;
    }
    ContactInfo *recipient = lookupContactByPubKey(pub_key_prefix, 6);
    if (recipient && (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_CLI_DATA)) {
      char *text = (char *)&cmd_frame[i];
      int tlen = len - i;
      uint32_t est_timeout;
      text[tlen] = 0; // ensure null
      int result;
      uint32_t expected_ack;
      if (txt_type == TXT_TYPE_CLI_DATA) {
        msg_timestamp = getRTCClock()->getCurrentTimeUnique(); // Use node's RTC instead of app timestamp to avoid tripping replay protection
        result = sendCommandData(*recipient, msg_timestamp, attempt, text, est_timeout);
        expected_ack = 0; // no Ack expected
      } else {
        result = sendMessage(*recipient, msg_timestamp, attempt, text, expected_ack, est_timeout);
      }
      // TODO: add expected ACK to table
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        if (expected_ack) {
          expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis(); // add to circular table
          expected_ack_table[next_ack_idx].ack = expected_ack;
          expected_ack_table[next_ack_idx].contact = recipient;
          next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
        }

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &expected_ack, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(recipient == NULL
                        ? ERR_CODE_NOT_FOUND
                        : ERR_CODE_UNSUPPORTED_CMD); // unknown recipient, or unsupported TXT_TYPE_*
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_TXT_MSG) { // send GroupChannel text msg
    int i = 1;
    uint8_t txt_type = cmd_frame[i++]; // should be TXT_TYPE_PLAIN
    uint8_t channel_idx = cmd_frame[i++];
    uint32_t msg_timestamp;
    memcpy(&msg_timestamp, &cmd_frame[i], 4);
    i += 4;
    const char *text = (char *)&cmd_frame[i];

    if (txt_type != TXT_TYPE_PLAIN) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    } else {
      ChannelDetails channel;
      bool success = getChannel(channel_idx, channel);
      if (success && sendGroupMessage(msg_timestamp, channel.channel, _prefs.node_name, text, len - i)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
      }
    }
  } else if (cmd_frame[0] == CMD_SEND_CHANNEL_DATA) { // send GroupChannel datagram
    if (len < 4) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }
    int i = 1;
    uint8_t channel_idx = cmd_frame[i++];
    uint8_t path_len = cmd_frame[i++];

    // validate path len, allowing 0xFF for flood
    if (!mesh::Packet::isValidPathLen(path_len) && path_len != OUT_PATH_UNKNOWN) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA invalid path size: %d", path_len);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      return;
    }

    // parse provided path if not flood
    uint8_t path[MAX_PATH_SIZE];
    if (path_len != OUT_PATH_UNKNOWN) {
      i += mesh::Packet::writePath(path, &cmd_frame[i], path_len);
    }

    uint16_t data_type = ((uint16_t)cmd_frame[i]) | (((uint16_t)cmd_frame[i + 1]) << 8);
    i += 2;
    const uint8_t *payload = &cmd_frame[i];
    int payload_len = (len > (size_t)i) ? (int)(len - i) : 0;

    ChannelDetails channel;
    if (!getChannel(channel_idx, channel)) {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    } else if (data_type == DATA_TYPE_RESERVED) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (payload_len > MAX_CHANNEL_DATA_LENGTH) {
      MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA payload too long: %d > %d", payload_len, MAX_CHANNEL_DATA_LENGTH);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (sendGroupData(channel.channel, path, path_len, data_type, payload, payload_len)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACTS) { // get Contact list
    if (_iter_started) {
      writeErrFrame(ERR_CODE_BAD_STATE); // iterator is currently busy
    } else {
      if (len >= 5) { // has optional 'since' param
        memcpy(&_iter_filter_since, &cmd_frame[1], 4);
      } else {
        _iter_filter_since = 0;
      }

      uint8_t reply[5];
      reply[0] = RESP_CODE_CONTACTS_START;
      uint32_t count = getNumContacts() + 1; // + local Console chat
      memcpy(&reply[1], &count, 4);
      _iter_console_pending = true;
      _serial->writeFrame(reply, 5);

      // start iterator
      _iter = startContactsIterator();
      _iter_started = true;
      _most_recent_lastmod = 0;
    }
  } else if (cmd_frame[0] == CMD_SET_ADVERT_NAME && len >= 2) {
    int nlen = len - 1;
    if (nlen > sizeof(_prefs.node_name) - 1) nlen = sizeof(_prefs.node_name) - 1; // max len
    memcpy(_prefs.node_name, &cmd_frame[1], nlen);
    _prefs.node_name[nlen] = 0; // null terminator
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_ADVERT_LATLON && len >= 9) {
    int32_t lat, lon, alt = 0;
    memcpy(&lat, &cmd_frame[1], 4);
    memcpy(&lon, &cmd_frame[5], 4);
    if (len >= 13) {
      memcpy(&alt, &cmd_frame[9], 4); // for FUTURE support
    }
    if (lat <= 90 * 1E6 && lat >= -90 * 1E6 && lon <= 180 * 1E6 && lon >= -180 * 1E6) {
      sensors.node_lat = ((double)lat) / 1000000.0;
      sensors.node_lon = ((double)lon) / 1000000.0;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid geo coordinate
    }
  } else if (cmd_frame[0] == CMD_GET_DEVICE_TIME) {
    uint8_t reply[5];
    reply[0] = RESP_CODE_CURR_TIME;
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply[1], &now, 4);
    _serial->writeFrame(reply, 5);
  } else if (cmd_frame[0] == CMD_SET_DEVICE_TIME && len >= 5) {
    uint32_t secs;
    memcpy(&secs, &cmd_frame[1], 4);
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (secs >= curr) {
      getRTCClock()->setCurrentTime(secs);
      _prefs.time_valid = 1;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SEND_SELF_ADVERT) {
    mesh::Packet* pkt;
    if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
      pkt = createSelfAdvert(_prefs.node_name);
    } else {
      pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
    }
    if (pkt) {
      if (len >= 2 && cmd_frame[1] == 1) { // optional param (1 = flood, 0 = zero hop)
        unsigned long delay_millis = 0;
        TransportKey default_scope;
        memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));
        sendFloodScoped(default_scope, pkt, delay_millis);
      } else {
        sendZeroHop(pkt);
      }
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_RESET_PATH && len >= 1 + 32) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      // recipient->lastmod = ??   shouldn't be needed, app already has this version of contact
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // unknown contact
    }
  } else if (cmd_frame[0] == CMD_ADD_UPDATE_CONTACT && len >= 1 + 32 + 2 + 1) {
    uint8_t *pub_key = &cmd_frame[1];
    if (isConsoleKey(pub_key, PUB_KEY_SIZE)) {
      writeOKFrame();
      return;
    }
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    uint32_t last_mod = getRTCClock()->getCurrentTime();  // fallback value if not present in cmd_frame
    if (recipient) {
      updateContactFromFrame(*recipient, last_mod, cmd_frame, len);
      recipient->lastmod = last_mod;
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      ContactInfo contact;
      updateContactFromFrame(contact, last_mod, cmd_frame, len);
      contact.lastmod = last_mod;
      contact.sync_since = 0;
      if (addContact(contact)) {
        dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_REMOVE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    if (isConsoleKey(pub_key, PUB_KEY_SIZE)) {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
      return;
    }
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient && removeContact(*recipient)) {
      _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE);
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found, or unable to remove
    }
  } else if (cmd_frame[0] == CMD_SHARE_CONTACT) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      if (shareContactZeroHop(*recipient)) {
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // unable to send
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_CONTACT_BY_KEY) {
    uint8_t *pub_key = &cmd_frame[1];
    if (isConsoleKey(pub_key, PUB_KEY_SIZE)) {
      ContactInfo console;
      fillConsoleContact(console);
      writeContactRespFrame(RESP_CODE_CONTACT, console);
      return;
    }
    ContactInfo *contact = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (contact) {
      writeContactRespFrame(RESP_CODE_CONTACT, *contact);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found
    }
  } else if (cmd_frame[0] == CMD_EXPORT_CONTACT) {
    if (len < 1 + PUB_KEY_SIZE) {
      // export SELF
      mesh::Packet* pkt;
      if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
        pkt = createSelfAdvert(_prefs.node_name);
      } else {
        pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
      }
      if (pkt) {
        pkt->header |= ROUTE_TYPE_FLOOD; // would normally be sent in this mode

        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        uint8_t out_len = pkt->writeTo(&out_frame[1]);
        releasePacket(pkt); // undo the obtainNewPacket()
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL); // Error
      }
    } else {
      uint8_t *pub_key = &cmd_frame[1];
      ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
      uint8_t out_len;
      if (recipient && (out_len = exportContact(*recipient, &out_frame[1])) > 0) {
        out_frame[0] = RESP_CODE_EXPORT_CONTACT;
        _serial->writeFrame(out_frame, out_len + 1);
      } else {
        writeErrFrame(ERR_CODE_NOT_FOUND); // not found
      }
    }
  } else if (cmd_frame[0] == CMD_IMPORT_CONTACT && len > 2 + 32 + 64) {
    if (importContact(&cmd_frame[1], len - 1)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SYNC_NEXT_MESSAGE) {
    int out_len;
    bool shown = false;
    if ((out_len = getFromOfflineQueue(out_frame, &shown)) > 0) {
      _serial->writeFrame(out_frame, out_len);
#ifdef DISPLAY_CLASS
      if (shown && _ui) _ui->clearTakenMsg();
      if (_ui) _ui->msgRead(offline_queue_len);
#endif
    } else {
      out_frame[0] = RESP_CODE_NO_MORE_MESSAGES;
      _serial->writeFrame(out_frame, 1);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_PARAMS) {
    int i = 1;
    uint32_t freq;
    memcpy(&freq, &cmd_frame[i], 4);
    i += 4;
    uint32_t bw;
    memcpy(&bw, &cmd_frame[i], 4);
    i += 4;
    uint8_t sf = cmd_frame[i++];
    uint8_t cr = cmd_frame[i++];
    uint8_t repeat = 0;  // default - false
    if (len > i) {
      repeat = cmd_frame[i++];   // FIRMWARE_VER_CODE  9+
    }

    if (repeat && !isValidClientRepeatFreq(freq)) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else if (freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
        bw <= 500000) {
      _prefs.sf = sf;
      _prefs.cr = cr;
      _prefs.freq = (float)freq / 1000.0;
      _prefs.bw = (float)bw / 1000.0;
      _prefs.setRepeatEn(repeat != 0);
      savePrefs();

      radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);

      writeOKFrame();
    } else {
      MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                         (uint32_t)cr);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_SET_RADIO_TX_POWER) {
    int8_t power = (int8_t)cmd_frame[1];
    if (power < -9 || power > MAX_LORA_TX_POWER) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.tx_power_dbm = power;
      savePrefs();
      radio_driver.setTxPower(_prefs.tx_power_dbm);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SET_TUNING_PARAMS) {
    int i = 1;
    uint32_t rx, af;
    memcpy(&rx, &cmd_frame[i], 4);
    i += 4;
    memcpy(&af, &cmd_frame[i], 4);
    i += 4;
    _prefs.rx_delay_base = ((float)rx) / 1000.0f;
    _prefs.airtime_factor = ((float)af) / 1000.0f;
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_TUNING_PARAMS) {
    uint32_t rx = _prefs.rx_delay_base * 1000, af = _prefs.airtime_factor * 1000;
    int i = 0;
    out_frame[i++] = RESP_CODE_TUNING_PARAMS;
    memcpy(&out_frame[i], &rx, 4); i += 4;
    memcpy(&out_frame[i], &af, 4); i += 4;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SET_OTHER_PARAMS) {
    _prefs.manual_add_contacts = cmd_frame[1];
    if (len >= 3) {
      _prefs.telemetry_mode_base = cmd_frame[2] & 0x03; // v5+
      _prefs.telemetry_mode_loc = (cmd_frame[2] >> 2) & 0x03;
      _prefs.telemetry_mode_env = (cmd_frame[2] >> 4) & 0x03;

      if (len >= 4) {
        _prefs.advert_loc_policy = cmd_frame[3];
        if (len >= 5) {
          _prefs.multi_acks = cmd_frame[4];
        }
      }
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_PATH_HASH_MODE && cmd_frame[1] == 0 && len >= 3) {
    if (cmd_frame[2] >= 3) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      _prefs.path_hash_mode = cmd_frame[2];
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_REBOOT && memcmp(&cmd_frame[1], "reboot", 6) == 0) {
    if (dirty_contacts_expiry) { // is there are pending dirty contacts write needed?
      saveContacts();
    }
    board.reboot();
  } else if (cmd_frame[0] == CMD_GET_BATT_AND_STORAGE) {
    uint8_t reply[11];
    int i = 0;
    reply[i++] = RESP_CODE_BATT_AND_STORAGE;
    uint16_t battery_millivolts = board.getBattMilliVolts();
    uint32_t used = _store->getStorageUsedKb();
    uint32_t total = _store->getStorageTotalKb();
    memcpy(&reply[i], &battery_millivolts, 2); i += 2;
    memcpy(&reply[i], &used, 4); i += 4;
    memcpy(&reply[i], &total, 4); i += 4;
    _serial->writeFrame(reply, i);
  } else if (cmd_frame[0] == CMD_EXPORT_PRIVATE_KEY) {
#if ENABLE_PRIVATE_KEY_EXPORT
    uint8_t reply[65];
    reply[0] = RESP_CODE_PRIVATE_KEY;
    self_id.writeTo(&reply[1], 64);
    _serial->writeFrame(reply, 65);
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
#if ENABLE_PRIVATE_KEY_IMPORT
    if (!mesh::LocalIdentity::validatePrivateKey(&cmd_frame[1])) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid key
    } else {
        mesh::LocalIdentity identity;
        identity.readFrom(&cmd_frame[1], 64);
        if (_store->saveMainIdentity(identity)) {
          self_id = identity;
          writeOKFrame();
          // re-load contacts, to invalidate ecdh shared_secrets
          resetContacts();
          _store->loadContacts(this);
        } else {
          writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        }
    }
#else
    writeDisabledFrame();
#endif
  } else if (cmd_frame[0] == CMD_SEND_RAW_DATA && len >= 6) {
    int i = 1;
    int8_t path_len = cmd_frame[i++];
    if (path_len >= 0 && i + path_len + 4 <= len) { // minimum 4 byte payload
      uint8_t *path = &cmd_frame[i];
      i += path_len;
      auto pkt = createRawData(&cmd_frame[i], len - i);
      if (pkt) {
        sendDirect(pkt, path, path_len);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    } else {
      writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // flood, not supported (yet)
    }
  } else if (cmd_frame[0] == CMD_SEND_LOGIN && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    char *password = (char *)&cmd_frame[1 + PUB_KEY_SIZE];
    cmd_frame[len] = 0; // ensure null terminator in password
    if (recipient) {
      uint32_t est_timeout;
      int result = sendLogin(*recipient, password, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        memcpy(&pending_login, recipient->id.pub_key, 4); // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &pending_login, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_ANON_REQ && len > 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    ContactInfo anon;
    if (recipient == NULL) { // FIRMWARE_VER_CODE 13+,  allow non-contact requests
      memset(&anon, 0, sizeof(anon));
      memcpy(anon.id.pub_key, pub_key, PUB_KEY_SIZE);
      anon.out_path_len = 0;   // default to zero-hop direct
      anon.type = ADV_TYPE_NONE;  // unknown
      anon.lastmod = getRTCClock()->getCurrentTime();

      if (addContact(anon)) recipient = &anon;
    }
    uint8_t *data = &cmd_frame[1 + PUB_KEY_SIZE];
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendAnonReq(*recipient, data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this to onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL); // contacts full
    }
  } else if (cmd_frame[0] == CMD_SEND_STATUS_REQ && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_STATUS, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        // FUTURE:  pending_status = tag;  // match this in onContactResponse()
        memcpy(&pending_status, recipient->id.pub_key, 4); // legacy matching scheme
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_PATH_DISCOVERY_REQ && cmd_frame[1] == 0 && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[2];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      // 'Path Discovery' is just a special case of flood + Telemetry req
      uint8_t req_data[9];
      req_data[0] = REQ_TYPE_GET_TELEMETRY_DATA;
      req_data[1] = ~(TELEM_PERM_BASE);  // NEW: inverse permissions mask (ie. we only want BASE telemetry)
      memset(&req_data[2], 0, 3);  // reserved
      getRNG()->random(&req_data[5], 4);   // random blob to help make packet-hash unique
      auto save = recipient->out_path_len;    // temporarily force sendRequest() to flood
      recipient->out_path_len = OUT_PATH_UNKNOWN;
      int result = sendRequest(*recipient, req_data, sizeof(req_data), tag, est_timeout);
      recipient->out_path_len = save;
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_discovery = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len >= 4 + PUB_KEY_SIZE) {  // can deprecate, in favour of CMD_SEND_BINARY_REQ
    uint8_t *pub_key = &cmd_frame[4];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_telemetry = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_SEND_TELEMETRY_REQ && len == 4) {  // 'self' telemetry request
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    // query other sensors -- target specific
    sensors.querySensors(0xFF, telemetry);

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], self_id.pub_key, 6);
    i += 6; // pub_key_prefix
    uint8_t tlen = telemetry.getSize();
    memcpy(&out_frame[i], telemetry.getBuffer(), tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_BINARY_REQ && len >= 2 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint8_t *req_data = &cmd_frame[1 + PUB_KEY_SIZE];
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, req_data, len - (1 + PUB_KEY_SIZE), tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_req = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
  } else if (cmd_frame[0] == CMD_HAS_CONNECTION && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    if (hasConnectionTo(pub_key)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_LOGOUT && len >= 1 + PUB_KEY_SIZE) {
    uint8_t *pub_key = &cmd_frame[1];
    stopConnection(pub_key);
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_CHANNEL && len >= 2) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    if (getChannel(channel_idx, channel)) {
      int i = 0;
      out_frame[i++] = RESP_CODE_CHANNEL_INFO;
      out_frame[i++] = channel_idx;
      strcpy((char *)&out_frame[i], channel.name);
      i += 32;
      memcpy(&out_frame[i], channel.channel.secret, 16);
      i += 16; // NOTE: only 128-bit supported
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 32) {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // not supported (yet)
  } else if (cmd_frame[0] == CMD_SET_CHANNEL && len >= 2 + 32 + 16) {
    uint8_t channel_idx = cmd_frame[1];
    ChannelDetails channel;
    StrHelper::strncpy(channel.name, (char *)&cmd_frame[2], 32);
    memset(channel.channel.secret, 0, sizeof(channel.channel.secret));
    memcpy(channel.channel.secret, &cmd_frame[2 + 32], 16); // NOTE: only 128-bit supported
    if (setChannel(channel_idx, channel)) {
      saveChannels();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
  } else if (cmd_frame[0] == CMD_SIGN_START) {
    out_frame[0] = RESP_CODE_SIGN_START;
    out_frame[1] = 0; // reserved
    uint32_t len = MAX_SIGN_DATA_LEN;
    memcpy(&out_frame[2], &len, 4);
    _serial->writeFrame(out_frame, 6);

    if (sign_data) {
      free(sign_data);
    }
    sign_data = (uint8_t *)malloc(MAX_SIGN_DATA_LEN);
    sign_data_len = 0;
  } else if (cmd_frame[0] == CMD_SIGN_DATA && len > 1) {
    if (sign_data == NULL || sign_data_len + (len - 1) > MAX_SIGN_DATA_LEN) {
      writeErrFrame(sign_data == NULL ? ERR_CODE_BAD_STATE : ERR_CODE_TABLE_FULL); // error: too long
    } else {
      memcpy(&sign_data[sign_data_len], &cmd_frame[1], len - 1);
      sign_data_len += (len - 1);
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_SIGN_FINISH) {
    if (sign_data) {
      self_id.sign(&out_frame[1], sign_data, sign_data_len);

      free(sign_data); // don't need sign_data now
      sign_data = NULL;

      out_frame[0] = RESP_CODE_SIGNATURE;
      _serial->writeFrame(out_frame, 1 + SIGNATURE_SIZE);
    } else {
      writeErrFrame(ERR_CODE_BAD_STATE);
    }
  } else if (cmd_frame[0] == CMD_SEND_TRACE_PATH && len > 10 && len - 10 < MAX_PACKET_PAYLOAD-5) {
    uint8_t path_len = len - 10;
    uint8_t flags = cmd_frame[9];
    uint8_t path_sz = flags & 0x03;  // NEW v1.11+
    if ((path_len >> path_sz) > MAX_PATH_SIZE || (path_len % (1 << path_sz)) != 0) { // make sure is multiple of path_sz
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      uint32_t tag, auth;
      memcpy(&tag, &cmd_frame[1], 4);
      memcpy(&auth, &cmd_frame[5], 4);
      auto pkt = createTrace(tag, auth, flags);
      if (pkt) {
        sendDirect(pkt, &cmd_frame[10], path_len);

        uint32_t t = _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2);
        uint32_t est_timeout = calcDirectTimeoutMillisFor(t, path_len >> path_sz);

        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else if (cmd_frame[0] == CMD_SET_DEVICE_PIN && len >= 5) {

    // get pin from command frame
    uint32_t pin;
    memcpy(&pin, &cmd_frame[1], 4);

    // ensure pin is zero, or a valid 6 digit pin
    if (pin == 0 || (pin >= 100000 && pin <= 999999)) {
      _prefs.ble_pin = pin;
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_CUSTOM_VARS) {
    out_frame[0] = RESP_CODE_CUSTOM_VARS;
    char *dp = (char *)&out_frame[1];
    for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
      if (i > 0) {
        *dp++ = ',';
      }
      strcpy(dp, sensors.getSettingName(i));
      dp = strchr(dp, 0);
      *dp++ = ':';
      strcpy(dp, sensors.getSettingValue(i));
      dp = strchr(dp, 0);
    }
    _serial->writeFrame(out_frame, dp - (char *)out_frame);
  } else if (cmd_frame[0] == CMD_SET_CUSTOM_VAR && len >= 4) {
    cmd_frame[len] = 0;
    char *sp = (char *)&cmd_frame[1];
    char *np = strchr(sp, ':'); // look for separator char
    if (np) {
      *np++ = 0; // modify 'cmd_frame', replace ':' with null
      bool success = sensors.setSettingValue(sp, np);
      if (success) {
        #if ENV_INCLUDE_GPS == 1
        // Update node preferences for GPS settings
        if (strcmp(sp, "gps") == 0) {
          _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
          savePrefs();
        } else if (strcmp(sp, "gps_interval") == 0) {
          uint32_t interval_seconds = atoi(np);
          _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
          savePrefs();
        }
        #endif
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else if (cmd_frame[0] == CMD_GET_ADVERT_PATH && len >= PUB_KEY_SIZE+2) {
    // FUTURE use:  uint8_t reserved = cmd_frame[1];
    uint8_t *pub_key = &cmd_frame[2];
    AdvertPath* found = NULL;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
      auto p = &advert_paths[i];
      if (memcmp(p->pubkey_prefix, pub_key, sizeof(p->pubkey_prefix)) == 0) {
        found = p;
        break;
      }
    }
    if (found) {
      int i = 0;
      out_frame[i++] = RESP_CODE_ADVERT_PATH;
      memcpy(&out_frame[i], &found->recv_timestamp, 4); i += 4;
      out_frame[i++] = found->path_len;
      i += mesh::Packet::writePath(&out_frame[i], found->path, found->path_len);
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND);
    }
  } else if (cmd_frame[0] == CMD_GET_STATS && len >= 2) {
    uint8_t stats_type = cmd_frame[1];
    if (stats_type == STATS_TYPE_CORE) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_CORE;
      uint16_t battery_mv = board.getBattMilliVolts();
      uint32_t uptime_secs = _ms->getMillis() / 1000;
      uint8_t queue_len = (uint8_t)_mgr->getOutboundTotal();
      memcpy(&out_frame[i], &battery_mv, 2); i += 2;
      memcpy(&out_frame[i], &uptime_secs, 4); i += 4;
      memcpy(&out_frame[i], &_err_flags, 2); i += 2;
      out_frame[i++] = queue_len;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_RADIO) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_RADIO;
      int16_t noise_floor = (int16_t)_radio->getNoiseFloor();
      int8_t last_rssi = (int8_t)radio_driver.getLastRSSI();
      int8_t last_snr = (int8_t)(radio_driver.getLastSNR() * 4); // scaled by 4 for 0.25 dB precision
      uint32_t tx_air_secs = getTotalAirTime() / 1000;
      uint32_t rx_air_secs = getReceiveAirTime() / 1000;
      memcpy(&out_frame[i], &noise_floor, 2); i += 2;
      out_frame[i++] = last_rssi;
      out_frame[i++] = last_snr;
      memcpy(&out_frame[i], &tx_air_secs, 4); i += 4;
      memcpy(&out_frame[i], &rx_air_secs, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else if (stats_type == STATS_TYPE_PACKETS) {
      int i = 0;
      out_frame[i++] = RESP_CODE_STATS;
      out_frame[i++] = STATS_TYPE_PACKETS;
      uint32_t recv = radio_driver.getPacketsRecv();
      uint32_t sent = radio_driver.getPacketsSent();
      uint32_t n_sent_flood = getNumSentFlood();
      uint32_t n_sent_direct = getNumSentDirect();
      uint32_t n_recv_flood = getNumRecvFlood();
      uint32_t n_recv_direct = getNumRecvDirect();
      uint32_t n_recv_errors = radio_driver.getPacketsRecvErrors();
      memcpy(&out_frame[i], &recv, 4); i += 4;
      memcpy(&out_frame[i], &sent, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_sent_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_flood, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_direct, 4); i += 4;
      memcpy(&out_frame[i], &n_recv_errors, 4); i += 4;
      _serial->writeFrame(out_frame, i);
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid stats sub-type
    }
  } else if (cmd_frame[0] == CMD_FACTORY_RESET && memcmp(&cmd_frame[1], "reset", 5) == 0) {
    if (_serial) {
      MESH_DEBUG_PRINTLN("Factory reset: disabling serial interface to prevent reconnects (BLE/WiFi)");
      _serial->disable(); // Phone app disconnects before we can send OK frame so it's safe here
    }
    bool success = _store->formatFileSystem();
    if (success) {
      writeOKFrame();
      delay(1000);
      board.reboot();  // doesn't return
    } else {
      writeErrFrame(ERR_CODE_FILE_IO_ERROR);
    }
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 0) {
    if (len >= 2 + 16) {
      memcpy(send_scope.key, &cmd_frame[2], sizeof(send_scope.key));  // set scope override TransportKey
    } else {
      memset(send_scope.key, 0, sizeof(send_scope.key));  // reset scope override
    }
    send_unscoped = false;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_FLOOD_SCOPE_KEY && len >= 2 && cmd_frame[1] == 1) {  // ver 12+
    send_unscoped = true;
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_SET_DEFAULT_FLOOD_SCOPE && len >= 1) {
    if (len >= 1+31+16) {
      int n = strlen((char *) &cmd_frame[1]);
      if (n > 0 && n < 31) {
        strcpy(_prefs.default_scope_name, (char *) &cmd_frame[1]);
        memcpy(_prefs.default_scope_key, &cmd_frame[1+31], 16);
        savePrefs();
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      memset(_prefs.default_scope_name, 0, sizeof(_prefs.default_scope_name));  // set default scope to null
      memset(_prefs.default_scope_key, 0, sizeof(_prefs.default_scope_key));
      savePrefs();
      writeOKFrame();
    }
  } else if (cmd_frame[0] == CMD_GET_DEFAULT_FLOOD_SCOPE) {
    out_frame[0] = RESP_CODE_DEFAULT_FLOOD_SCOPE;
    if (strlen(_prefs.default_scope_name) > 0) {
      memcpy(&out_frame[1], _prefs.default_scope_name, 31);
      memcpy(&out_frame[1+31], _prefs.default_scope_key, 16);
      _serial->writeFrame(out_frame, 1+31+16);
    } else {
      _serial->writeFrame(out_frame, 1);   // no name or key means null
    }
  } else if (cmd_frame[0] == CMD_SEND_CONTROL_DATA && len >= 2 && (cmd_frame[1] & 0x80) != 0) {
    auto resp = createControlData(&cmd_frame[1], len - 1);
    if (resp) {
      sendZeroHop(resp);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  } else if (cmd_frame[0] == CMD_SET_AUTOADD_CONFIG) {
    _prefs.autoadd_config = cmd_frame[1];
    if (len >= 3) {
      _prefs.autoadd_max_hops = min(cmd_frame[2], (uint8_t)64);
    }
    savePrefs();
    writeOKFrame();
  } else if (cmd_frame[0] == CMD_GET_AUTOADD_CONFIG) {
    int i = 0;
    out_frame[i++] = RESP_CODE_AUTOADD_CONFIG;
    out_frame[i++] = _prefs.autoadd_config;
    out_frame[i++] = _prefs.autoadd_max_hops;
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_GET_ALLOWED_REPEAT_FREQ) {
    int i = 0;
    out_frame[i++] = RESP_ALLOWED_REPEAT_FREQ;
    for (int k = 0; k < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]) && i + 8 < sizeof(out_frame); k++) {
      auto r = &repeat_freq_ranges[k];
      memcpy(&out_frame[i], &r->lower_freq, 4); i += 4;
      memcpy(&out_frame[i], &r->upper_freq, 4); i += 4;
    }
    _serial->writeFrame(out_frame, i);
  } else if (cmd_frame[0] == CMD_SEND_RAW_PACKET && len >= 4) {
    auto pkt = obtainNewPacket();
    if (pkt) {
      uint8_t priority = cmd_frame[1];
      if (tryParsePacket(pkt, &cmd_frame[2], len - 2)) {
        sendPacket(pkt, priority, 0);
        writeOKFrame();
      } else {
        releasePacket(pkt);
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
      }
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
#ifdef BOOK_READER
  } else if (cmd_frame[0] == 0xB0) {   // e-book upload (tools/book_uploader), see BookStore.cpp
    size_t n = book_store.handleCommand(cmd_frame, len, out_frame);
    _serial->writeFrame(out_frame, n);
#endif
  } else {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    MESH_DEBUG_PRINTLN("ERROR: unknown command: %02X", cmd_frame[0]);
  }
}

static bool save_filter(const ContactInfo& c) {
  return c.type != ADV_TYPE_NONE;   // don't save the transient/anon entries
}

void MyMesh::saveContacts() {
  _store->saveContacts(this, save_filter);
}

void MyMesh::enterCLIRescue() {
  _cli_rescue = true;
  cli_command[0] = 0;
  Serial.println("========= CLI Rescue =========");
}

void MyMesh::checkCLIRescueCmd() {
  int len = strlen(cli_command);
  while (Serial.available() && len < sizeof(cli_command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      cli_command[len++] = c;
      cli_command[len] = 0;
    }
    Serial.print(c);  // echo
  }
  if (len == sizeof(cli_command)-1) {  // command buffer full
    cli_command[sizeof(cli_command)-1] = '\r';
  }

  if (len > 0 && cli_command[len - 1] == '\r') {  // received complete line
    cli_command[len - 1] = 0;  // replace newline with C string null terminator
    runCLICommand(cli_command, Serial);
    cli_command[0] = 0;  // reset command buffer
  }
}

bool MyMesh::isConsoleKey(const uint8_t* key, int len) {
  if (len <= 0 || len > PUB_KEY_SIZE) return false;
  return memcmp(key, CONSOLE_PUB_KEY, len) == 0;
}

void MyMesh::fillConsoleContact(ContactInfo& contact) const {
  memset(&contact, 0, sizeof(contact));
  memcpy(contact.id.pub_key, CONSOLE_PUB_KEY, PUB_KEY_SIZE);
  strncpy(contact.name, "Console", sizeof(contact.name) - 1);
  contact.type = ADV_TYPE_CHAT;
  contact.out_path_len = 0;
  contact.last_advert_timestamp = 1;
  contact.lastmod = 1;
}

void MyMesh::queueConsoleReply(const char* text) {
  if (text == NULL || text[0] == 0) text = "(no output)";
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[i++] = 0;
    out_frame[i++] = 0;
    out_frame[i++] = 0;
  } else {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV;
  }
  memcpy(&out_frame[i], CONSOLE_PUB_KEY, 6);
  i += 6;
  out_frame[i++] = 0xFF; // direct, same as a normal contact reply
  out_frame[i++] = TXT_TYPE_PLAIN;
  uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  int tlen = strlen(text);
  if (i + tlen > MAX_FRAME_SIZE) tlen = MAX_FRAME_SIZE - i;
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i, false);
}

void MyMesh::serviceConsoleAck() {
  if (_console_ack_at == 0 || !millisHasNowPassed(_console_ack_at)) return;
  _console_ack_at = 0;
  if (_serial == NULL || !_serial->isConnected()) return;

  out_frame[0] = PUSH_CODE_SEND_CONFIRMED;
  memcpy(&out_frame[1], &_console_ack, 4);
  uint32_t trip_time = 0;
  memcpy(&out_frame[5], &trip_time, 4);
  _serial->writeFrame(out_frame, 9);
  if (_console_push_msg) {
    uint8_t tickle[1] = { PUSH_CODE_MSG_WAITING };
    _serial->writeFrame(tickle, 1);
    _console_push_msg = false;
  }
}

static void formatTzOffset(int16_t mins, char* buf, size_t n) {
  if (mins == 0) {
    snprintf(buf, n, "UTC");
    return;
  }
  char sign = mins < 0 ? '-' : '+';
  int abs_mins = mins < 0 ? -mins : mins;
  int hours = abs_mins / 60;
  int minutes = abs_mins % 60;
  if (minutes) snprintf(buf, n, "UTC%c%d:%02d", sign, hours, minutes);
  else snprintf(buf, n, "UTC%c%d", sign, hours);
}

static bool parseUiLang(const char* s, uint8_t& out) {
  while (*s == ' ') s++;
  char buf[8];
  int n = 0;
  while (*s && *s != ' ' && *s != '\r' && n < 7) {
    char c = *s++;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    buf[n++] = c;
  }
  buf[n] = 0;
  while (*s == ' ' || *s == '\r') s++;
  if (*s != 0) return false;
  if (strcmp(buf, "ru") == 0) { out = UI_LANG_RU; return true; }
  if (strcmp(buf, "en") == 0) { out = UI_LANG_EN; return true; }
  return false;
}

static bool parseTzOffset(const char* s, int16_t& out) {
  while (*s == ' ') s++;
  int sign = 1;
  if (*s == '+') s++;
  else if (*s == '-') { sign = -1; s++; }
  if (*s < '0' || *s > '9') return false;
  int hours = 0;
  while (*s >= '0' && *s <= '9') {
    hours = hours * 10 + (*s - '0');
    if (hours > 14) return false;
    s++;
  }
  int minutes = 0;
  if (*s == ':') {
    s++;
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
      minutes = minutes * 10 + (*s - '0');
      s++;
    }
    if (minutes > 59) return false;
  }
  while (*s == ' ' || *s == '\r') s++;
  if (*s != 0) return false;
  int total = sign * (hours * 60 + minutes);
  if (total < -14 * 60 || total > 14 * 60) return false;
  out = (int16_t)total;
  return true;
}

static bool readFixedNum(const char*& s, int& out, int digits) {
  int value = 0;
  for (int i = 0; i < digits; i++) {
    if (*s < '0' || *s > '9') return false;
    value = value * 10 + (*s - '0');
    s++;
  }
  out = value;
  return true;
}

static bool parseLocalDateTime(const char* s, int& year, int& month, int& day, int& hour, int& minute, int& second) {
  while (*s == ' ') s++;
  if (!readFixedNum(s, year, 4) || *s++ != '-') return false;
  if (!readFixedNum(s, month, 2) || *s++ != '-') return false;
  if (!readFixedNum(s, day, 2) || *s++ != ' ') return false;
  if (!readFixedNum(s, hour, 2) || *s++ != ':') return false;
  if (!readFixedNum(s, minute, 2)) return false;
  second = 0;
  if (*s == ':') {
    s++;
    if (!readFixedNum(s, second, 2)) return false;
  }
  while (*s == ' ' || *s == '\r') s++;
  if (*s != 0) return false;
  if (year < 2020 || year > 2099 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
  static const uint8_t mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int dim = mdays[month];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dim = 29;
  return day >= 1 && day <= dim;
}

static void formatLocalDateTime(uint32_t utc, int16_t tz_mins, char* buf, size_t n) {
  int64_t local = (int64_t)utc + (int64_t)tz_mins * 60;
  if (local < 0) local = 0;
  DateTime dt((uint32_t)local);
  char tz[16];
  formatTzOffset(tz_mins, tz, sizeof(tz));
  snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d %s",
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(), tz);
}

void MyMesh::runCLICommand(char* command, Print& out) {
    char line[96];
    if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
      out.print("help, ver, clock, clock.node, clock.node remove <index>, clock pull, set pin N, set tz ±H[:MM], set time YYYY-MM-DD HH:MM[:SS], set clock.node off|<pubkey>, set lang ru|en, set phrase N [text], phrases [1-3], set badge [text], set sport on|off, set kb std|freq, set caps on|off, set cursor on|off, set bigclock on|off, beacon, set beacon to|every|move|off, sos [stop], set sos #channel, stats [reset], relays, relay ..., ls [path], cat UserData/..|ExtraFS/.., rm path, rebuild, erase, reboot\n");
    } else if (strcmp(command, "ver") == 0) {
      snprintf(line, sizeof(line), "%s %s\n", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
      out.print(line);
    } else if (memcmp(command, "clock.node", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
      const char* arg = (command[10] == ' ') ? &command[11] : "";
      while (*arg == ' ') arg++;
      if (memcmp(arg, "remove", 6) == 0 && (arg[6] == 0 || arg[6] == ' ')) {
        const char* num = (arg[6] == ' ') ? &arg[7] : "";
        while (*num == ' ') num++;
        int index = 0;
        bool digits = num[0] >= '0' && num[0] <= '9';
        for (const char* p = num; digits && *p; p++) {
          if (*p < '0' || *p > '9') digits = false;
          else index = index * 10 + (*p - '0');
        }
        if (!digits) {
          out.print("  Error: index required\n");
        } else if (clockNodeRemoveAt(_prefs.clock_nodes, index)) {
          savePrefs();
          out.print("  > removed\n");
        } else {
          out.print("  Error: no such index\n");
        }
      } else if (!clockNodesConfigured(_prefs.clock_nodes)) {
        out.print("  > clock.node is off\n");
      } else {
        int shown = 0;
        for (int i = 0; i < CLOCK_NODE_MAX; i++) {
          if (!clockNodeSlotUsed(_prefs.clock_nodes[i])) continue;
          char name[32];
          lookupClockNodeName(_prefs.clock_nodes[i], name, sizeof(name));
          if (!name[0]) strcpy(name, "?");
          snprintf(line, sizeof(line), "  > %d %s\n", shown, name);
          out.print(line);
          shown++;
        }
      }
    } else if (strcmp(command, "clock pull") == 0) {
      if (!clockNodesConfigured(_prefs.clock_nodes)) {
        out.print("  Error: clock.node is off\n");
      } else if (pullClock()) {
        out.print("  > clock pull sent\n");
      } else {
        out.print("  Error: no direct path yet\n");
      }
    } else if (strcmp(command, "clock") == 0) {
      snprintf(line, sizeof(line), "%u\n", (unsigned)getRTCClock()->getCurrentTime());
      out.print(line);
    } else if (memcmp(command, "phrases", 7) == 0 && (command[7] == 0 || command[7] == ' ')) {
      // phrases [1-3]: six at a time, a console reply is short
      const int per = 6;
      int groups = (UI_PHRASE_COUNT + per - 1) / per;
      int g = atoi(&command[7]);
      if (g < 1 || g > groups) g = 1;
      int from = (g - 1) * per, to = from + per;
      if (to > UI_PHRASE_COUNT) to = UI_PHRASE_COUNT;
      snprintf(line, sizeof(line), "phrases %d-%d of %d (* = yours)\n", from + 1, to, UI_PHRASE_COUNT);
      out.print(line);
      for (int i = from; i < to; i++) {
        snprintf(line, sizeof(line), "%d%s %s\n", i + 1, uiPhraseCustom(i) ? "*" : "", uiPhrase(i));
        out.print(line);
      }
    } else if (memcmp(command, "relays", 6) == 0 && (command[6] == 0 || command[6] == ' ')) {
      // relays [2]: four at a time, a console reply is short
      int from = atoi(&command[6]) == 2 ? 4 : 0;
      if (!relays.supported()) {
        out.print("  relays need an ESP32 _ble or _wifi build\n");
      } else {
        int shown = 0;
        for (int i = from; i < from + 4 && i < RELAY_MAX; i++) {
          RelayDef* r = relays.get(i);
          if (!r) continue;
          const char* st = r->state == RELAY_ON ? "ON" : r->state == RELAY_OFF ? "OFF" : "?";
          if (r->kind == RELAY_LCTECH) {
            char mac[20];
            RelayManager::formatMac(r->mac, mac);
            snprintf(line, sizeof(line), "%d %s %s ch%d %s %s\n", i + 1, r->name, RelayManager::kindName(r->kind), r->ch, mac, st);
          } else {
            snprintf(line, sizeof(line), "%d %s %s ch%d %s %s\n", i + 1, r->name, RelayManager::kindName(r->kind), r->ch, r->host, st);
          }
          out.print(line);
          shown++;
        }
        if (!shown) out.print(from ? "  no relays 5-8\n" : "  no relays 1-4 (relays 2: 5-8)\n");
        snprintf(line, sizeof(line), "mesh %s\n", relays.meshOn() ? "on" : "off");
        out.print(line);
      }
    } else if (memcmp(command, "relay ", 6) == 0) {
      // relay add|del|scan|found|mesh|allow|deny|N on|off|toggle
      char* arg = &command[6];
      while (*arg == ' ') arg++;
      char* rest = arg;
      while (*rest && *rest != ' ') rest++;
      if (*rest) *rest++ = 0;
      while (*rest == ' ') rest++;
      if (!relays.supported()) {
        out.print("  relays need an ESP32 _ble or _wifi build\n");
      } else if (strcmp(arg, "scan") == 0) {
        out.print(relays.startScan() ? "  scanning 5 s, then: relay found\n" : "  Error: busy or no bluetooth\n");
      } else if (strcmp(arg, "found") == 0) {
        if (relays.scanning()) out.print("  still scanning...\n");
        else if (relays.foundCount() == 0) out.print("  nothing found (relay scan)\n");
        for (int k = 0; k < relays.foundCount() && k < 5; k++) {
          const RelayFound* f = relays.found(k);
          char mac[20];
          RelayManager::formatMac(f->mac, mac);
          snprintf(line, sizeof(line), "%d %s %s %ddBm\n", k + 1, f->name, mac, f->rssi);
          out.print(line);
        }
      } else if (strcmp(arg, "add") == 0) {
        // relay add N kind address [ch] [name]; kind found: address = number from `relay found`
        char* tok[5] = {0};
        int nt = 0;
        char* q = rest;
        while (*q && nt < 5) {
          tok[nt++] = q;
          if (nt == 5) break;   // the name keeps its spaces
          while (*q && *q != ' ') q++;
          if (*q) *q++ = 0;
          while (*q == ' ') q++;
        }
        int slot = nt > 0 ? atoi(tok[0]) - 1 : -1;
        uint8_t kind = RELAY_NONE;
        bool from_found = nt > 1 && strcmp(tok[1], "found") == 0;
        if (nt < 3 || slot < 0 || slot >= RELAY_MAX || (!from_found && !RelayManager::parseKind(tok[1], kind))) {
          out.print("  Error: relay add N lctech|shelly|shelly1|tasmota|found ADDRESS [CH] [NAME]\n");
        } else if (!from_found && !RelayManager::kindSupported(kind)) {
          out.print(kind == RELAY_LCTECH ? "  Error: lctech needs a _ble build\n" : "  Error: wi-fi relays need a _wifi build\n");
        } else {
          RelayDef d;
          memset(&d, 0, sizeof(d));
          d.ch = nt > 3 ? (uint8_t)atoi(tok[3]) : 1;
          if (d.ch == 0) d.ch = 1;
          bool good = true;
          if (from_found) {
            const RelayFound* f = relays.found(atoi(tok[2]) - 1);
            if (f) { d.kind = RELAY_LCTECH; memcpy(d.mac, f->mac, 6); }
            else good = false;
          } else {
            d.kind = kind;
            if (kind == RELAY_LCTECH) good = RelayManager::parseMac(tok[2], d.mac);
            else StrHelper::strncpy(d.host, tok[2], sizeof(d.host));
          }
          if (nt > 4) StrHelper::strncpy(d.name, tok[4], sizeof(d.name));
          else snprintf(d.name, sizeof(d.name), "relay %d", slot + 1);
          if (!good) out.print("  Error: bad address (AA:BB:CC:DD:EE:FF or number from relay found)\n");
          else if (!relays.set(slot, d)) out.print("  Error: busy, try again\n");
          else { snprintf(line, sizeof(line), "  > relay %d: %s\n", slot + 1, d.name); out.print(line); }
        }
      } else if (strcmp(arg, "del") == 0) {
        int slot = atoi(rest) - 1;
        out.print(relays.remove(slot) ? "  > deleted\n" : "  Error: relay del N\n");
      } else if (strcmp(arg, "mesh") == 0) {
        if (strcmp(rest, "on") == 0 || strcmp(rest, "off") == 0) relays.setMeshOn(strcmp(rest, "on") == 0);
        snprintf(line, sizeof(line), "  > mesh %s\n", relays.meshOn() ? "on" : "off");
        out.print(line);
      } else if (strcmp(arg, "allow") == 0 || strcmp(arg, "deny") == 0) {
        bool allow = strcmp(arg, "allow") == 0;
        if (*rest == 0) {   // the list
          int n = 0;
          for (int i = 0; i < RELAY_ALLOW_MAX; i++) {
            const uint8_t* k = relays.allowedKey(i);
            if (!k) continue;
            ContactInfo* c = lookupContactByPubKey(k, 6);
            snprintf(line, sizeof(line), "  %s\n", c ? c->name : "(unknown)");
            out.print(line);
            n++;
          }
          if (!n) out.print("  nobody: relay allow NAME\n");
        } else {
          ContactInfo* c = searchContactsByPrefix(rest);
          if (!c) out.print("  Error: no such contact\n");
          else if (allow && !relays.allow(c->id.pub_key)) out.print("  Error: 4 at most\n");
          else {
            if (!allow) relays.deny(c->id.pub_key);
            snprintf(line, sizeof(line), "  > %s %s\n", allow ? "allowed" : "denied", c->name);
            out.print(line);
          }
        }
      } else if (arg[0] >= '1' && arg[0] <= '8' && arg[1] == 0) {
        int i = arg[0] - '1';
        uint8_t action = strcmp(rest, "on") == 0 ? RELAY_DO_ON : strcmp(rest, "off") == 0 ? RELAY_DO_OFF : RELAY_DO_TOGGLE;
        if (!relays.get(i)) out.print("  Error: no such relay\n");
        else if (relays.request(i, action)) out.print("  > switching...\n");
        else { snprintf(line, sizeof(line), "  Error: %s\n", relays.lastError()[0] ? relays.lastError() : "busy"); out.print(line); }
      } else {
        out.print("  relay scan|found|add|del|mesh|allow|deny|N on|off\n");
      }
    } else if (strcmp(command, "sos") == 0 || strcmp(command, "sos stop") == 0) {
      // sos: status; sos stop: stop and send "all clear". It is started on the device (hold 5 s).
      if (command[3] == ' ') sosStop();
      char who[48];
      sosTargets(who, sizeof(who));
      if (_sos_on) snprintf(line, sizeof(line), "  > SOS on, %u sent, next in %lus, to %s\n", _sos_rounds, (unsigned long)sosNextSecs(), who);
      else snprintf(line, sizeof(line), "  > SOS off, goes to %s (channel + favourites)\n", who);
      out.print(line);
    } else if (memcmp(command, "set sos ", 8) == 0) {
      const char* name = command + 8;
      while (*name == ' ') name++;
      bool found = false;
#ifdef MAX_GROUP_CHANNELS
      for (int i = 0; i < MAX_GROUP_CHANNELS && !found; i++) {
        ChannelDetails ch;
        if (!getChannel(i, ch) || ch.name[0] == 0) continue;
        const char* a = ch.name[0] == '#' ? ch.name + 1 : ch.name;
        const char* b = name[0] == '#' ? name + 1 : name;
        if (strcasecmp(a, b) == 0) { _prefs.sos_chan = (uint8_t)i; found = true; }
      }
#endif
      if (found) { savePrefs(); snprintf(line, sizeof(line), "  > SOS channel %s\n", name); }
      else snprintf(line, sizeof(line), "  Error: no such channel\n");
      out.print(line);
    } else if (strcmp(command, "stats") == 0 || strcmp(command, "stats reset") == 0) {
      // main loop work time and lost packets: does the screen (running line,
      // games, clock) keep the radio waiting?
      if (command[5] == ' ') {
        _loop_max = _loop_ui_max = 0;
        _loop_slow = _loop_n = 0;
        _loop_since = millis();
        radio_driver.resetStats();
        RadioLibWrapper::resetIrqOverruns();
        out.print("  > stats reset\n");
      } else {
        unsigned long secs = (millis() - _loop_since) / 1000UL;
        snprintf(line, sizeof(line), "  > loop max %lu ms (screen %lu ms), >=100 ms: %lu of %lu, over %lus\n",
                 _loop_max, _loop_ui_max, (unsigned long)_loop_slow, (unsigned long)_loop_n, secs);
        out.print(line);
        snprintf(line, sizeof(line), "  > rx %lu, rx errors %lu, lost (overrun) %lu, tx %lu\n",
                 (unsigned long)radio_driver.getPacketsRecv(), (unsigned long)radio_driver.getPacketsRecvErrors(),
                 (unsigned long)RadioLibWrapper::getIrqOverruns(), (unsigned long)radio_driver.getPacketsSent());
        out.print(line);
      }
    } else if (memcmp(command, "set bigclock", 12) == 0 && (command[12] == 0 || command[12] == ' ')) {
      // set bigclock on|off: big clock instead of a dark screen (on the charger where it can be sensed)
      const char* arg = &command[12];
      while (*arg == ' ') arg++;
      if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
        _prefs.charge_clock = strcmp(arg, "on") == 0 ? 1 : 0;
        savePrefs();
        snprintf(line, sizeof(line), "  > bigclock is %s\n", arg);
      } else if (*arg) {
        snprintf(line, sizeof(line), "  Error: set bigclock on|off\n");
      } else {
        snprintf(line, sizeof(line), "  > bigclock is %s\n", _prefs.charge_clock ? "on" : "off");
      }
      out.print(line);
#if ENV_INCLUDE_GPS == 1
    } else if (strcmp(command, "beacon") == 0) {
      beaconStatus(out);
    } else if (memcmp(command, "set beacon", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
      // set beacon to NAME|#channel|neighbors|flood, every MIN, move METERS, off
      const char* arg = &command[10];
      while (*arg == ' ') arg++;
      const char* err = beaconCommand(arg);
      if (err) {
        snprintf(line, sizeof(line), "  Error: %s\n", err);
        out.print(line);
      } else {
        beaconStatus(out);
      }
#endif
    } else if (memcmp(command, "set cursor", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
      // set cursor on|off: the keyboard cursor jumps to the likely next letter
      const char* arg = &command[10];
      while (*arg == ' ') arg++;
      if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
        _prefs.auto_cursor = strcmp(arg, "on") == 0 ? 1 : 0;
        savePrefs();
        snprintf(line, sizeof(line), "  > cursor is %s\n", arg);
      } else if (*arg) {
        snprintf(line, sizeof(line), "  Error: set cursor on|off\n");
      } else {
        snprintf(line, sizeof(line), "  > cursor is %s\n", _prefs.auto_cursor ? "on" : "off");
      }
      out.print(line);
    } else if ((memcmp(command, "set kb", 6) == 0 && (command[6] == 0 || command[6] == ' ')) ||
               (memcmp(command, "set caps", 8) == 0 && (command[8] == 0 || command[8] == ' '))) {
      // set kb std|freq: keyboard layout; set caps on|off: automatic capitals
      bool kb = command[5] == 'b';
      const char* arg = &command[kb ? 6 : 8];
      while (*arg == ' ') arg++;
      if (kb) {
        if (strcmp(arg, "std") == 0 || strcmp(arg, "freq") == 0) {
          _prefs.kb_layout = strcmp(arg, "freq") == 0 ? 1 : 0;
          savePrefs();
        } else if (*arg) {
          out.print("  Error: set kb std|freq\n");
          return;
        }
        snprintf(line, sizeof(line), "  > kb is %s\n", _prefs.kb_layout ? "freq" : "std");
      } else {
        if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
          _prefs.auto_caps = strcmp(arg, "on") == 0 ? 1 : 0;
          savePrefs();
        } else if (*arg) {
          out.print("  Error: set caps on|off\n");
          return;
        }
        snprintf(line, sizeof(line), "  > caps is %s\n", _prefs.auto_caps ? "on" : "off");
      }
      out.print(line);
    } else if (strcmp(command, "badge") == 0 ||
               (memcmp(command, "set badge", 9) == 0 && (command[9] == 0 || command[9] == ' '))) {
      // set badge text | set badge (back to the node name); ten clicks show it
      if (command[0] == 's') {
        const char* arg = &command[9];
        while (*arg == ' ') arg++;
        setUiBadge(arg);
      }
      out.print("  > badge: ");
      out.print(uiBadge());
      out.print(uiBadgeCustom() ? "\n" : " (node name)\n");
    } else if (memcmp(command, "set sport", 9) == 0 && (command[9] == 0 || command[9] == ' ')) {
      // set sport on|off: the stopwatch shows milliseconds next to the seconds
      const char* arg = &command[9];
      while (*arg == ' ') arg++;
      if (strcmp(arg, "on") == 0 || strcmp(arg, "off") == 0) {
        _prefs.sw_sport = strcmp(arg, "on") == 0 ? 1 : 0;
        savePrefs();
      } else if (*arg) {
        out.print("  Error: set sport on|off\n");
        return;
      }
      out.print(_prefs.sw_sport ? "  > sport is on\n" : "  > sport is off\n");
    } else if (memcmp(command, "set ", 4) == 0 && memcmp(&command[4], "phrase", 6) == 0 &&
               (command[10] == 0 || command[10] == ' ')) {
      // set phrase N text | set phrase N (back to the stock phrase)
      const char* arg = command[10] ? &command[11] : "";
      while (*arg == ' ') arg++;
      int n = 0;
      bool digits = *arg >= '0' && *arg <= '9';
      while (*arg >= '0' && *arg <= '9') n = n * 10 + (*arg++ - '0');
      if (!digits || n < 1 || n > UI_PHRASE_COUNT || (*arg != 0 && *arg != ' ')) {
        snprintf(line, sizeof(line), "  Error: set phrase 1..%d [text]\n", UI_PHRASE_COUNT);
        out.print(line);
      } else {
        while (*arg == ' ') arg++;
        setUiPhrase(n - 1, arg);
        snprintf(line, sizeof(line), "  > phrase %d is %s%s\n", n, uiPhrase(n - 1),
                 uiPhraseCustom(n - 1) ? "" : " (stock)");
        out.print(line);
      }
    } else if (memcmp(command, "set ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "clock.node", 10) == 0 && (config[10] == 0 || config[10] == ' ')) {
        const char* arg = (config[10] == ' ') ? &config[11] : "";
        while (*arg == ' ') arg++;
        if (memcmp(arg, "remove", 6) == 0 && (arg[6] == 0 || arg[6] == ' ')) {
          const char* num = (arg[6] == ' ') ? &arg[7] : "";
          while (*num == ' ') num++;
          int index = 0;
          bool digits = num[0] >= '0' && num[0] <= '9';
          for (const char* p = num; digits && *p; p++) {
            if (*p < '0' || *p > '9') digits = false;
            else index = index * 10 + (*p - '0');
          }
          if (!digits) {
            out.print("  Error: index required\n");
          } else if (clockNodeRemoveAt(_prefs.clock_nodes, index)) {
            savePrefs();
            out.print("  > removed\n");
          } else {
            out.print("  Error: no such index\n");
          }
        } else if (*arg == 0 || strcmp(arg, "off") == 0) {
          clockNodesClear(_prefs.clock_nodes);
          savePrefs();
          out.print("  > clock.node is off\n");
        } else {
          uint8_t raw[PUB_KEY_SIZE];
          uint8_t full[PUB_KEY_SIZE];
          int hex_len = strlen(arg);
          int nbytes = hex_len / 2;
          const uint8_t* key = NULL;
          if ((hex_len % 2) != 0 || nbytes < 1 || nbytes > PUB_KEY_SIZE || !mesh::Utils::fromHex(raw, nbytes, arg)) {
            out.print("  Error: bad pubkey\n");
          } else if (nbytes == PUB_KEY_SIZE) {
            key = raw;
          } else if (resolveClockNode(raw, nbytes, full)) {
            key = full;
          } else {
            out.print("  Error: pubkey prefix not unique\n");
          }
          if (key) {
            int rc = clockNodeAdd(_prefs.clock_nodes, key);
            if (rc < 0) {
              out.print("  Error: clock.node list full\n");
            } else {
              savePrefs();
              _clock_next_pull = futureMillis(500);
              out.print(rc == 0 ? "  > already in list\n" : "  > clock.node added\n");
            }
          }
        }
      } else if (memcmp(config, "pin ", 4) == 0) {
        _prefs.ble_pin = atoi(&config[4]);
        savePrefs();
        snprintf(line, sizeof(line), "  > pin is now %06d\n", _prefs.ble_pin);
        out.print(line);
      } else if (memcmp(config, "tz", 2) == 0 && (config[2] == 0 || config[2] == ' ')) {
        const char* arg = (config[2] == ' ') ? &config[3] : "";
        while (*arg == ' ') arg++;
        if (*arg == 0) {
          char tz[16];
          formatTzOffset(_prefs.tz_offset_mins, tz, sizeof(tz));
          snprintf(line, sizeof(line), "  > tz is %s\n", tz);
          out.print(line);
        } else {
          int16_t mins;
          if (!parseTzOffset(arg, mins)) {
            out.print("  Error: tz is ±H or ±H:MM, from UTC-14 to UTC+14\n");
          } else {
            _prefs.tz_offset_mins = mins;
            savePrefs();
            char tz[16];
            formatTzOffset(mins, tz, sizeof(tz));
            snprintf(line, sizeof(line), "  > tz is now %s\n", tz);
            out.print(line);
          }
        }
      } else if (memcmp(config, "lang", 4) == 0 && (config[4] == 0 || config[4] == ' ')) {
        const char* arg = (config[4] == ' ') ? &config[5] : "";
        while (*arg == ' ') arg++;
        if (*arg == 0) {
          snprintf(line, sizeof(line), "  > lang is %s\n", _prefs.ui_lang == UI_LANG_EN ? "en" : "ru");
          out.print(line);
        } else {
          uint8_t lang;
          if (!parseUiLang(arg, lang)) {
            out.print("  Error: lang is ru or en\n");
          } else {
            _prefs.ui_lang = lang;
            savePrefs();
            snprintf(line, sizeof(line), "  > lang is now %s\n", lang == UI_LANG_EN ? "en" : "ru");
            out.print(line);
          }
        }
      } else if (memcmp(config, "time", 4) == 0 && (config[4] == 0 || config[4] == ' ')) {
        const char* arg = (config[4] == ' ') ? &config[5] : "";
        while (*arg == ' ') arg++;
        if (*arg == 0) {
          formatLocalDateTime(getRTCClock()->getCurrentTime(), _prefs.tz_offset_mins, line, sizeof(line));
          out.print("  > ");
          out.print(line);
          out.print("\n");
        } else {
          int year, month, day, hour, minute, second;
          if (!parseLocalDateTime(arg, year, month, day, hour, minute, second)) {
            out.print("  Error: time is YYYY-MM-DD HH:MM[:SS]\n");
          } else {
            DateTime local(year, month, day, hour, minute, second);
            int64_t utc = (int64_t)local.unixtime() - (int64_t)_prefs.tz_offset_mins * 60;
            if (utc < 1000000000LL || utc > 0xFFFFFFFFLL) {
              out.print("  Error: time out of range\n");
            } else {
              getRTCClock()->setCurrentTime((uint32_t)utc);
              _prefs.time_valid = 1;
              savePrefs();
              formatLocalDateTime(getRTCClock()->getCurrentTime(), _prefs.tz_offset_mins, line, sizeof(line));
              out.print("  > time is now ");
              out.print(line);
              out.print("\n");
            }
          }
        }
      } else {
        snprintf(line, sizeof(line), "  Error: unknown config: %s\n", config);
        out.print(line);
      }
    } else if (strcmp(command, "rebuild") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        _store->saveMainIdentity(self_id);
        savePrefs();
        saveContacts();
        saveChannels();
        out.print("  > erase and rebuild done\n");
      } else {
        out.print("  Error: erase failed\n");
      }
    } else if (strcmp(command, "erase") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        out.print("  > erase done\n");
      } else {
        out.print("  Error: erase failed\n");
      }
    } else if (memcmp(command, "ls", 2) == 0) {

      // get path from command e.g: "ls /adafruit"
      const char *path = (command[2] == ' ') ? &command[3] : "";

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }
      snprintf(line, sizeof(line), "Listing files in %s\n", path);
      out.print(line);

      // log each file and directory
      File root = _store->openRead(path);
      if (is_fs2 == false) {
        if (root) {
          File file = root.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              snprintf(line, sizeof(line), "[dir]  UserData%s/%s\n", path, file.name());
              out.print(line);
            } else {
              snprintf(line, sizeof(line), "[file] UserData%s/%s (%d bytes)\n", path, file.name(), file.size());
              out.print(line);
            }
            // move to next file
            file = root.openNextFile();
          }
          root.close();
        }
      }

      if (is_fs2 == true || strlen(path) == 0 || strcmp(path, "/") == 0) {
        if (_store->getSecondaryFS() != nullptr) {
          File root2 = _store->openRead(_store->getSecondaryFS(), path);
          File file = root2.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              snprintf(line, sizeof(line), "[dir]  ExtraFS%s/%s\n", path, file.name());
              out.print(line);
            } else {
              snprintf(line, sizeof(line), "[file] ExtraFS%s/%s (%d bytes)\n", path, file.name(), file.size());
              out.print(line);
            }
            // move to next file
            file = root2.openNextFile();
          }
          root2.close();
        }
      }
    } else if (memcmp(command, "cat", 3) == 0) {

      // get path from command e.g: "cat /contacts3"
      const char *path = (command[3] == ' ') ? &command[4] : "";

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      } else {
        out.print("Invalid path provided, must start with UserData/ or ExtraFS/\n");
        return;
      }

      // log file content as hex
      File file = _store->openRead(path);
      if (is_fs2 == true) {
        file = _store->openRead(_store->getSecondaryFS(), path);
      }
      if(file){

        // get file content
        int file_size = file.available();
        uint8_t buffer[file_size];
        file.read(buffer, file_size);

        // print hex
        mesh::Utils::printHex(out, buffer, file_size);
        out.print("\n");

        file.close();

      }

    } else if (memcmp(command, "rm ", 3) == 0) {
      // get path from command e.g: "rm /adv_blobs"
      const char *path = &command[3];
      MESH_DEBUG_PRINTLN("Removing file: %s", path);
      // ensure path is not empty, or root dir
      if(!path || strlen(path) == 0 || strcmp(path, "/") == 0){
        out.print("Invalid path provided\n");
      } else {
      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }

        // remove file
        bool removed;
        if (is_fs2) {
          MESH_DEBUG_PRINTLN("Removing file from ExtraFS: %s", path);
          removed = _store->removeFile(_store->getSecondaryFS(), path);
        } else {
          MESH_DEBUG_PRINTLN("Removing file from UserData: %s", path);
          removed = _store->removeFile(path);
        }
        if(removed){
          out.print("File removed\n");
        } else {
          out.print("Failed to remove file\n");
        }

      }

    } else if (strcmp(command, "reboot") == 0) {
      board.reboot();  // doesn't return
    } else {
      out.print("  Error: unknown command\n");
    }
}

void MyMesh::checkSerialInterface() {
  size_t len = _serial->checkRecvFrame(cmd_frame);
  if (len > 0) {
    handleCmdFrame(len);
  } else if (_iter_started              // check if our ContactsIterator is 'running'
             && !_serial->isWriteBusy() // don't spam the Serial Interface too quickly!
  ) {
    ContactInfo contact;
    if (_iter_console_pending) {
      ContactInfo console;
      fillConsoleContact(console);
      writeContactRespFrame(RESP_CODE_CONTACT, console);
      _iter_console_pending = false;
    } else if (_iter.hasNext(this, contact)) {
      if (contact.lastmod > _iter_filter_since) { // apply the 'since' filter
        writeContactRespFrame(RESP_CODE_CONTACT, contact);
        if (contact.lastmod > _most_recent_lastmod) {
          _most_recent_lastmod = contact.lastmod; // save for the RESP_CODE_END_OF_CONTACTS frame
        }
      }
    } else { // EOF
      out_frame[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&out_frame[1], &_most_recent_lastmod,
             4); // include the most recent lastmod, so app can update their 'since'
      _serial->writeFrame(out_frame, 5);
      _iter_started = false;
    }
  //} else if (!_serial->isWriteBusy()) {
  //  checkConnections();    // TODO - deprecate the 'Connections' stuff
  }
}

void MyMesh::lookupClockNodeName(const uint8_t* pub, char* dest, size_t dest_len) {
  if (dest_len == 0) return;
  dest[0] = 0;
  ContactInfo* c = lookupContactByPubKey(pub, PUB_KEY_SIZE);
  if (c && c->name[0]) {
    StrHelper::strncpy(dest, c->name, dest_len);
    return;
  }
  for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
    if (advert_paths[i].recv_timestamp == 0 || !advert_paths[i].name[0]) continue;
    if (memcmp(advert_paths[i].pubkey_prefix, pub, sizeof(advert_paths[i].pubkey_prefix)) != 0) continue;
    StrHelper::strncpy(dest, advert_paths[i].name, dest_len);
    return;
  }
  for (int i = 0; i < NEIGHBOR_TABLE_SIZE; i++) {
    if (neighbors[i].recv_timestamp == 0 || !neighbors[i].name[0]) continue;
    if (memcmp(neighbors[i].pubkey, pub, PUB_KEY_SIZE) != 0) continue;
    StrHelper::strncpy(dest, neighbors[i].name, dest_len);
    return;
  }
}

bool MyMesh::resolveClockNode(const uint8_t* prefix, int len, uint8_t dest[32]) {
  uint8_t found[PUB_KEY_SIZE];
  bool have = false;
  int matches = 0;
  int slots = getTotalContactSlots();
  for (int i = 0; i < slots; i++) {
    ContactInfo c;
    if (!getContactByIdx(i, c)) continue;
    if (c.id.pub_key[0] == 0 && c.name[0] == 0) continue;
    if (memcmp(c.id.pub_key, prefix, len) != 0) continue;
    if (!have || memcmp(found, c.id.pub_key, PUB_KEY_SIZE) != 0) {
      memcpy(found, c.id.pub_key, PUB_KEY_SIZE);
      have = true;
      matches++;
    }
  }
  if (matches != 1) return false;
  memcpy(dest, found, PUB_KEY_SIZE);
  return true;
}

bool MyMesh::sendClockPullTo(const uint8_t* pub) {
  ContactInfo* c = lookupContactByPubKey(pub, PUB_KEY_SIZE);
  if (c == NULL) return false;

  uint8_t saved_len = c->out_path_len;
  uint8_t use_len = saved_len;
  if (use_len == OUT_PATH_UNKNOWN) {
    bool zero_hop = false;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
      if (advert_paths[i].recv_timestamp == 0) continue;
      if (advert_paths[i].path_len != 0) continue;
      if (memcmp(advert_paths[i].pubkey_prefix, pub, sizeof(advert_paths[i].pubkey_prefix)) == 0) {
        zero_hop = true;
        break;
      }
    }
    if (!zero_hop) return false;
    use_len = 0;
    c->out_path_len = 0;
  }

  uint8_t reply_path[MAX_PATH_SIZE];
  uint8_t reply_len = use_len;
  if (use_len == 0) {
    reply_len = 0;
  } else {
    reversePath(reply_path, c->out_path, use_len);
  }

  uint8_t body[2 + MAX_PATH_SIZE];
  uint8_t blen = buildClockReqBody(body, reply_len, reply_path);
  uint32_t tag = 0, est = 0;
  int rc = sendAnonReq(*c, body, blen, tag, est);
  if (saved_len == OUT_PATH_UNKNOWN) c->out_path_len = saved_len;
  if (rc != MSG_SEND_SENT_DIRECT) return false;

  memcpy(_clock_pull_peer, pub, PUB_KEY_SIZE);
  _clock_pull_tag = tag;
  _clock_pull_sent_ms = millis();
  _clock_pull_deadline = futureMillis(CLOCK_PULL_TIMEOUT_MS);
  return true;
}

bool MyMesh::sendClockPull() {
  if (!clockNodesConfigured(_prefs.clock_nodes)) return false;
  for (int n = 0; n < CLOCK_NODE_MAX; n++) {
    int i = (_clock_rr + n) % CLOCK_NODE_MAX;
    if (!clockNodeSlotUsed(_prefs.clock_nodes[i])) continue;
    if (sendClockPullTo(_prefs.clock_nodes[i])) {
      _clock_rr = (i + 1) % CLOCK_NODE_MAX;
      return true;
    }
  }
  return false;
}

void MyMesh::onClockResponse(const uint8_t* data, size_t len) {
  if (_clock_pull_tag == 0 || len < 8) return;
  uint32_t remote;
  memcpy(&remote, &data[4], 4);
  uint32_t rtt = millis() - _clock_pull_sent_ms;
  _clock_pull_tag = 0;

  uint32_t accepted;
  if (!acceptRemoteClock(getRTCClock()->getCurrentTime(), _prefs.time_valid != 0, remote, rtt, &accepted)) {
    return;
  }
  getRTCClock()->setCurrentTime(accepted);
  _prefs.time_valid = 1;
  savePrefs();
}

void MyMesh::noteGpsClock() {
#if ENV_INCLUDE_GPS == 1
  LocationProvider* gps = sensors.getLocationProvider();
  if (gps != NULL && gps->consumeClockSet() && !_prefs.time_valid) {
    _prefs.time_valid = 1;
    savePrefs();
  }
#endif
}

void MyMesh::checkClockPull(bool force) {
  noteGpsClock();
  if (!clockNodesConfigured(_prefs.clock_nodes)) return;

  if (_clock_pull_tag != 0) {
    if (!millisHasNowPassed(_clock_pull_deadline)) return;
    _clock_pull_tag = 0;
    _clock_next_pull = futureMillis(CLOCK_PULL_RETRY_MS);
  }
  if (!force && _clock_next_pull != 0 && !millisHasNowPassed(_clock_next_pull)) return;

  if (sendClockPull()) {
    _clock_next_pull = futureMillis(_prefs.time_valid ? CLOCK_PULL_INTERVAL_MS : CLOCK_PULL_RETRY_MS);
  } else {
    _clock_next_pull = futureMillis(CLOCK_PULL_RETRY_MS);
  }
}

void MyMesh::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret, const mesh::Identity &sender,
                            uint8_t *data, size_t len) {
  if (!_prefs.time_valid || !packet->isRouteDirect() || len < 6) return;
  if ((long)(millis() - _clock_reply_after) < 0) return;
  uint32_t sender_timestamp;
  memcpy(&sender_timestamp, data, 4);
  if (data[4] != 0x03) return;

  uint8_t reply_path_len = data[5];
  if (!mesh::Packet::isValidPathLen(reply_path_len)) return;
  uint8_t hash_count = reply_path_len & 63;
  uint8_t hash_size = (reply_path_len >> 6) + 1;
  if (len < (size_t)(6 + hash_count * hash_size)) return;

  uint8_t reply[8];
  memcpy(reply, &sender_timestamp, 4);
  uint32_t now = getRTCClock()->getCurrentTime();
  memcpy(&reply[4], &now, 4);
  mesh::Packet* pkt = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply, sizeof(reply));
  if (pkt) {
    sendDirect(pkt, &data[6], reply_path_len, 300);
    _clock_reply_after = futureMillis(5000);
  }
}

bool MyMesh::pullClock() {
  if (!clockNodesConfigured(_prefs.clock_nodes)) return false;
  _clock_pull_tag = 0;
  if (!sendClockPull()) return false;
  _clock_next_pull = futureMillis(_prefs.time_valid ? CLOCK_PULL_INTERVAL_MS : CLOCK_PULL_RETRY_MS);
  return true;
}

void MyMesh::loop() {
  BaseChatMesh::loop();
  uiRetryCheck();
  checkSos();
#if ENV_INCLUDE_GPS == 1
  beaconRetryCheck();
#endif

  {   // a relay switched (or failed): tell whoever asked by a mesh text
    int idx; bool ok, scan, reply; uint8_t key[6];
    if (relays.poll(idx, ok, scan, reply, key) && reply && !scan) {
      char text[80];
      if (ok) relayStatus(idx, text, sizeof(text));
      else snprintf(text, sizeof(text), "%s %d: %s", _prefs.ui_lang != UI_LANG_EN ? "реле" : "relay", idx + 1,
                    relays.lastError());
      relayReply(key, text);
    }
  }
  serviceConsoleAck();

  if (_cli_rescue) {
    checkCLIRescueCmd();
  } else {
    checkSerialInterface();
  }

  // is there are pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    saveContacts();
    dirty_contacts_expiry = 0;
  }

#ifdef DISPLAY_CLASS
  if (_ui) _ui->setHasConnection(_serial->isConnected());
#endif

#if ENV_INCLUDE_GPS == 1
  checkBeacon();
#endif
  checkClockPull(false);
}

#if ENV_INCLUDE_GPS == 1
#define BEACON_FAV_CHATS     16   // favourite contacts offered as beacon targets
#define BEACON_CHECK_MS      5000UL   // how often the position is compared with the last one sent
// Distance trigger steps. Walking: 50-200 m; cycling, car: 500 m - 2 km.
static const uint16_t BEACON_MOVE_METERS[] = { 0, 50, 100, 200, 500, 1000, 2000 };
#define BEACON_MOVE_STEPS  (sizeof(BEACON_MOVE_METERS) / sizeof(BEACON_MOVE_METERS[0]))
// The distance trigger never sends more often than this: GPS jitter and
// a fast car must not flood the mesh.
#define BEACON_MOVE_GAP_MS        (60UL * 1000UL)
#define BEACON_MOVE_GAP_FLOOD_MS  (5UL * 60UL * 1000UL)
#define BEACON_LOCAL_MS      (10UL * 60UL * 1000UL)
#define BEACON_FLOOD_MS      (30UL * 60UL * 1000UL)
#define BEACON_FIRST_MS      20000UL
#define BEACON_NOFIX_MS      15000UL
#define BEACON_RETRY_MS      20000UL

static void beaconCopy(char* dest, size_t n, const char* src) {
  if (n == 0) return;
  if (src == NULL) src = "";
  strncpy(dest, src, n - 1);
  dest[n - 1] = 0;
}

static void formatBeaconAge(char* dest, size_t n, uint32_t secs) {
  if (secs < 60) snprintf(dest, n, "%us", (unsigned)secs);
  else if (secs < 3600) snprintf(dest, n, "%um", (unsigned)(secs / 60));
  else snprintf(dest, n, "%uh", (unsigned)(secs / 3600));
}

int MyMesh::buildBeaconOptions(BeaconOpt* out, int max_out) {
  int n = 0;
  if (n < max_out) {
    out[n].mode = BEACON_OFF;
    out[n].chan = 0;
    memset(out[n].pub, 0, PUB_KEY_SIZE);
    n++;
  }
  if (n < max_out) {
    out[n].mode = BEACON_ADVERT_LOCAL;
    out[n].chan = 0;
    memset(out[n].pub, 0, PUB_KEY_SIZE);
    n++;
  }
  if (n < max_out) {
    out[n].mode = BEACON_ADVERT_FLOOD;
    out[n].chan = 0;
    memset(out[n].pub, 0, PUB_KEY_SIZE);
    n++;
  }
#ifdef MAX_GROUP_CHANNELS
  for (int i = 0; i < MAX_GROUP_CHANNELS && n < max_out; i++) {
    ChannelDetails ch;
    if (!getChannel(i, ch) || ch.name[0] == 0) continue;
    out[n].mode = BEACON_GROUP;
    out[n].chan = (uint8_t)i;
    memset(out[n].pub, 0, PUB_KEY_SIZE);
    n++;
  }
#endif
  // favourite contacts (the star in the app), as on the Write page; a contact
  // chosen earlier stays on the list even if it lost its star
  bool have_current = false;
  int nfav = 0;
  int total = getTotalContactSlots();
  for (int i = 0; i < total && n < max_out; i++) {
    ContactInfo c;
    if (!getContactByIdx((uint32_t)i, c)) continue;
    if (c.type != ADV_TYPE_CHAT || c.name[0] == 0) continue;
    bool current = memcmp(c.id.pub_key, _prefs.beacon_pub, PUB_KEY_SIZE) == 0;
    if ((c.flags & 0x01) == 0 && !current) continue;   // bit 0 = favourite
    if (!current && nfav >= BEACON_FAV_CHATS) continue;
    if (current) have_current = true;
    else nfav++;
    out[n].mode = BEACON_CHAT;
    out[n].chan = 0;
    memcpy(out[n].pub, c.id.pub_key, PUB_KEY_SIZE);
    n++;
  }
  (void)have_current;
  return n;
}

uint32_t MyMesh::beaconIntervalMs() const {
  if (_prefs.beacon_mode == BEACON_ADVERT_LOCAL) return BEACON_LOCAL_MS;
  if (_prefs.beacon_mode == BEACON_GROUP) return (uint32_t)_prefs.beacon_group_mins * 60UL * 1000UL;
  if (_prefs.beacon_mode == BEACON_CHAT) return (uint32_t)_prefs.beacon_chat_mins * 60UL * 1000UL;
  return BEACON_FLOOD_MS;
}

bool MyMesh::beaconHasFix() const {
  LocationProvider* loc = sensors.getLocationProvider();
  return loc != NULL && loc->isValid();
}

void MyMesh::formatBeacon(char* mode, size_t mode_sz, char* target, size_t target_sz, char* pos, size_t pos_sz, char* sent, size_t sent_sz) {
  const char* mode_name = "off";
  const char* target_name = "hold to start";
  char target_buf[32];
  target_buf[0] = 0;
  switch (_prefs.beacon_mode) {
    case BEACON_ADVERT_LOCAL:
      mode_name = "advert";
      target_name = "neighbors";
      break;
    case BEACON_ADVERT_FLOOD:
      mode_name = "advert";
      target_name = "flood";
      break;
    case BEACON_GROUP: {
      mode_name = "group";
      ChannelDetails ch;
      if (getChannel(_prefs.beacon_chan, ch) && ch.name[0]) {
        beaconCopy(target_buf, sizeof(target_buf), ch.name);
        target_name = target_buf;
      } else {
        target_name = "no channel";
      }
      break;
    }
    case BEACON_CHAT: {
      mode_name = "chat";
      ContactInfo* c = lookupContactByPubKey(_prefs.beacon_pub, PUB_KEY_SIZE);
      if (c && c->name[0]) {
        beaconCopy(target_buf, sizeof(target_buf), c->name);
        target_name = target_buf;
      } else {
        target_name = "no contact";
      }
      break;
    }
    default:
      break;
  }
  if (_prefs.beacon_mode == BEACON_OFF) {
    beaconCopy(mode, mode_sz, mode_name);
  } else {
    char every[12], moved[12];
    formatBeaconRules(every, sizeof(every), moved, sizeof(moved));
    snprintf(mode, mode_sz, "%s %s", mode_name, every);
  }
  beaconCopy(target, target_sz, target_name);

  if (_beacon_sent_at != 0) {
    uint32_t now = getRTCClock()->getCurrentTime();
    uint32_t secs = (now >= _beacon_sent_at) ? (now - _beacon_sent_at) : 0;
    char age[8];
    formatBeaconAge(age, sizeof(age), secs);
    const char* ack = "";
    if (_prefs.beacon_mode == BEACON_CHAT) {
      ack = _bcn_ok ? " ok" : (_bcn_try >= 0 ? " ..." : " no reply");
    }
    snprintf(sent, sent_sz, "sent %s ago%s", age, ack);
  } else if (_prefs.beacon_mode == BEACON_OFF) {
    beaconCopy(sent, sent_sz, "");
  } else {
    beaconCopy(sent, sent_sz, "not sent");
  }

  if (_prefs.beacon_mode == BEACON_OFF) {
    beaconCopy(pos, pos_sz, "");
    return;
  }
  if (!beaconHasFix()) {
    LocationProvider* loc = sensors.getLocationProvider();
    beaconCopy(pos, pos_sz, (loc == NULL) ? "no gps" : "no fix");
    return;
  }
  snprintf(pos, pos_sz, "%.4f  %.4f", sensors.node_lat, sensors.node_lon);
}

bool MyMesh::stopBeacon() {
  if (_prefs.beacon_mode == BEACON_OFF) return false;
  _prefs.beacon_mode = BEACON_OFF;
  _beacon_next = 0;
  savePrefs();
  return true;
}

void MyMesh::cycleBeacon() {
  bool was_off = (_prefs.beacon_mode == BEACON_OFF);
#ifndef MAX_GROUP_CHANNELS
  #define BEACON_OPT_MAX (3 + BEACON_FAV_CHATS + 1)
#else
  #define BEACON_OPT_MAX (3 + MAX_GROUP_CHANNELS + BEACON_FAV_CHATS + 1)
#endif
  static BeaconOpt opts[BEACON_OPT_MAX];
  int n = buildBeaconOptions(opts, BEACON_OPT_MAX);
  if (n <= 0) return;
  int cur = -1;
  for (int i = 0; i < n; i++) {
    const BeaconOpt& opt = opts[i];
    if (opt.mode != _prefs.beacon_mode) continue;
    if (opt.mode == BEACON_GROUP && opt.chan != _prefs.beacon_chan) continue;
    if (opt.mode == BEACON_CHAT && memcmp(opt.pub, _prefs.beacon_pub, PUB_KEY_SIZE) != 0) continue;
    cur = i;
    break;
  }
  const BeaconOpt& next = opts[(cur + 1) % n];
  _prefs.beacon_mode = next.mode;
  if (next.mode == BEACON_GROUP) _prefs.beacon_chan = next.chan;
  if (next.mode == BEACON_CHAT) memcpy(_prefs.beacon_pub, next.pub, PUB_KEY_SIZE);
  if (next.mode != BEACON_OFF) {
    _prefs.gps_enabled = 1;
    sensors.setSettingValue("gps", "1");
    // a new target gets the position soon, then follows the time / distance rules
    _beacon_have_pos = false;
    _beacon_next = futureMillis(BEACON_FIRST_MS);
  }
  (void)was_off;
  savePrefs();
}

bool MyMesh::cycleBeaconInterval() {
  uint8_t* slot = NULL;
  if (_prefs.beacon_mode == BEACON_GROUP) slot = &_prefs.beacon_group_mins;
  else if (_prefs.beacon_mode == BEACON_CHAT) slot = &_prefs.beacon_chat_mins;
  else return false;

  // 0 = no timed send; offered only while the distance trigger is on,
  // otherwise the beacon would never send
  static const uint8_t steps[] = { 0, 5, 10, 15, 30, 60, 120 };
  const int nsteps = (int)(sizeof(steps) / sizeof(steps[0]));
  int i = 0;
  while (i < nsteps && steps[i] != *slot) i++;
  do {
    i = (i >= nsteps) ? 1 : (i + 1) % nsteps;
  } while (steps[i] == 0 && _prefs.beacon_move == 0);
  *slot = steps[i];
  savePrefs();
  return true;
}

bool MyMesh::cycleBeaconMove() {
  _prefs.beacon_move = (uint8_t)((_prefs.beacon_move + 1) % BEACON_MOVE_STEPS);
  if (_prefs.beacon_move == 0) {   // distance off: the timer must run again
    if (_prefs.beacon_group_mins == 0) _prefs.beacon_group_mins = 30;
    if (_prefs.beacon_chat_mins == 0) _prefs.beacon_chat_mins = 30;
  }
  savePrefs();
  return true;
}

void MyMesh::beaconStatus(Print& out) {
  char mode[20], target[32], pos[40], sent[24], every[12], moved[12], line[96];
  formatBeacon(mode, sizeof(mode), target, sizeof(target), pos, sizeof(pos), sent, sizeof(sent));
  formatBeaconRules(every, sizeof(every), moved, sizeof(moved));
  if (_prefs.beacon_mode == BEACON_OFF) {
    out.print("  > beacon off\n");
    return;
  }
  const char* sp = strchr(mode, ' ');
  int klen = sp ? (int)(sp - mode) : (int)strlen(mode);
  snprintf(line, sizeof(line), "  > beacon %.*s %s, every %s%s, move %s\n", klen, mode, target, every,
           beaconIntervalFixed() ? " (fixed)" : "", moved);
  out.print(line);
  snprintf(line, sizeof(line), "  > %s, %s\n", pos[0] ? pos : "-", sent[0] ? sent : "-");
  out.print(line);
}

// Console form of the Beacon settings. Returns NULL or an error to print.
const char* MyMesh::beaconCommand(const char* arg) {
  if (strcmp(arg, "off") == 0) {
    stopBeacon();
    return NULL;
  }
  if (memcmp(arg, "to ", 3) == 0) {
    const char* name = arg + 3;
    while (*name == ' ') name++;
    uint8_t mode = BEACON_OFF, chan = 0;
    ContactInfo* contact = NULL;
    if (strcmp(name, "neighbors") == 0) {
      mode = BEACON_ADVERT_LOCAL;
    } else if (strcmp(name, "flood") == 0) {
      mode = BEACON_ADVERT_FLOOD;
    } else {
#ifdef MAX_GROUP_CHANNELS
      for (int i = 0; i < MAX_GROUP_CHANNELS && mode == BEACON_OFF; i++) {
        ChannelDetails ch;
        if (!getChannel(i, ch) || ch.name[0] == 0) continue;
        const char* a = ch.name[0] == '#' ? ch.name + 1 : ch.name;
        const char* b = name[0] == '#' ? name + 1 : name;
        if (strcasecmp(a, b) == 0) { mode = BEACON_GROUP; chan = (uint8_t)i; }
      }
#endif
      if (mode == BEACON_OFF && name[0] != '#') {
        contact = searchContactsByPrefix(name);
        if (contact && contact->type == ADV_TYPE_CHAT) mode = BEACON_CHAT;
      }
    }
    if (mode == BEACON_OFF) return "no such channel or contact";
    _prefs.beacon_mode = mode;
    if (mode == BEACON_GROUP) _prefs.beacon_chan = chan;
    if (mode == BEACON_CHAT) memcpy(_prefs.beacon_pub, contact->id.pub_key, PUB_KEY_SIZE);
    _prefs.gps_enabled = 1;
    sensors.setSettingValue("gps", "1");
    _beacon_have_pos = false;
    _beacon_next = futureMillis(BEACON_FIRST_MS);
    savePrefs();
    return NULL;
  }
  if (memcmp(arg, "every ", 6) == 0) {
    const char* v = arg + 6;
    while (*v == ' ') v++;
    int mins = strcmp(v, "off") == 0 ? 0 : atoi(v);
    if (mins != 0 && (mins < 5 || mins > 180)) return "every off|5..180 (minutes)";
    if (mins == 0 && _prefs.beacon_move == 0) return "set beacon move first, or the beacon never sends";
    if (beaconIntervalFixed() || _prefs.beacon_mode == BEACON_OFF) return "adverts keep a fixed time";
    if (_prefs.beacon_mode == BEACON_GROUP) _prefs.beacon_group_mins = (uint8_t)mins;
    else _prefs.beacon_chat_mins = (uint8_t)mins;
    savePrefs();
    return NULL;
  }
  if (memcmp(arg, "move ", 5) == 0) {
    const char* v = arg + 5;
    while (*v == ' ') v++;
    int m = strcmp(v, "off") == 0 ? 0 : atoi(v);
    if (strstr(v, "km")) m *= 1000;
    for (uint8_t i = 0; i < BEACON_MOVE_STEPS; i++) {
      if (BEACON_MOVE_METERS[i] != m) continue;
      _prefs.beacon_move = i;
      if (i == 0) {
        if (_prefs.beacon_group_mins == 0) _prefs.beacon_group_mins = 30;
        if (_prefs.beacon_chat_mins == 0) _prefs.beacon_chat_mins = 30;
      }
      savePrefs();
      return NULL;
    }
    return "move off|50|100|200|500|1km|2km";
  }
  return "set beacon to NAME|#channel|neighbors|flood, every off|MIN, move off|METERS, off";
}

bool MyMesh::beaconIntervalFixed() const {
  return _prefs.beacon_mode == BEACON_ADVERT_LOCAL || _prefs.beacon_mode == BEACON_ADVERT_FLOOD;
}

uint32_t MyMesh::beaconMoveMeters() const {
  return _prefs.beacon_move < BEACON_MOVE_STEPS ? BEACON_MOVE_METERS[_prefs.beacon_move] : 0;
}

uint32_t MyMesh::beaconMoveGapMs() const {
  return _prefs.beacon_mode == BEACON_ADVERT_FLOOD ? BEACON_MOVE_GAP_FLOOD_MS : BEACON_MOVE_GAP_MS;
}

// Short distances: a flat-earth approximation is good to well under 1%.
uint32_t MyMesh::beaconMovedMeters() const {
  if (!_beacon_have_pos) return 0;
  double dlat = (sensors.node_lat - _beacon_lat) * 111320.0;
  double dlon = (sensors.node_lon - _beacon_lon) * 111320.0 * cos(_beacon_lat * M_PI / 180.0);
  return (uint32_t)sqrt(dlat * dlat + dlon * dlon);
}

static void formatMeters(char* buf, size_t n, uint32_t m) {
  if (m >= 1000) snprintf(buf, n, "%lukm", (unsigned long)(m / 1000));
  else snprintf(buf, n, "%lum", (unsigned long)m);
}

void MyMesh::formatBeaconRules(char* every, size_t every_sz, char* moved, size_t moved_sz) {
  uint32_t iv = beaconIntervalMs();
  if (iv == 0) snprintf(every, every_sz, "off");
  else if (iv >= 3600000UL) snprintf(every, every_sz, "%luh", (unsigned long)(iv / 3600000UL));
  else snprintf(every, every_sz, "%lumin", (unsigned long)(iv / 60000UL));
  uint32_t mv = beaconMoveMeters();
  if (mv == 0) snprintf(moved, moved_sz, "off");
  else formatMeters(moved, moved_sz, mv);
}

// One try of the beacon to a contact. Tries 1 and 2 use the known path (a
// periodic beacon should not flood the mesh), the last one floods in case
// the path broke. The same timestamp: the receiver shows one message.
bool MyMesh::beaconChatAttempt() {
  ContactInfo* c = lookupContactByPubKey(_prefs.beacon_pub, PUB_KEY_SIZE);
  if (c == NULL || c->type != ADV_TYPE_CHAT || _bcn_try < 0 || _bcn_try > 2) return false;
  uint32_t ack = 0;
  mesh::Packet* pkt = composeMsgPacket(*c, _bcn_ts, (uint8_t)_bcn_try, _bcn_text, ack);
  if (pkt == NULL) return false;
  uint32_t airtime = _radio->getEstAirtimeFor(pkt->getRawLength());
  uint32_t timeout;
  if (_bcn_try == 2 || c->out_path_len == OUT_PATH_UNKNOWN) {
    sendFloodScoped(*c, pkt);
    timeout = calcFloodTimeoutMillisFor(airtime);
  } else {
    sendDirect(pkt, c->out_path, c->out_path_len);
    timeout = calcDirectTimeoutMillisFor(airtime, c->out_path_len);
  }
  _bcn_acks[_bcn_try] = ack;
  _bcn_until = futureMillis(timeout + 2000);
  return true;
}

void MyMesh::beaconRetryCheck() {
  if (_bcn_try < 0 || _bcn_ok || !millisHasNowPassed(_bcn_until)) return;
  if (_bcn_try >= 2) { _bcn_try = -1; return; }   // three tries without an ack: "no reply"
  _bcn_try++;
  if (!beaconChatAttempt()) _bcn_try = -1;
}

// Sends when the interval has passed or the node moved far enough since the
// last position sent, whichever comes first. The first send after a target is
// picked goes out as soon as there is a fix.
void MyMesh::checkBeacon() {
  if (_prefs.beacon_mode == BEACON_OFF) return;
  if (_beacon_next == 0) _beacon_next = futureMillis(BEACON_FIRST_MS);
  if (!millisHasNowPassed(_beacon_next)) return;

  if (!beaconHasFix()) {
    _beacon_next = futureMillis(BEACON_NOFIX_MS);
    return;
  }

  unsigned long since = millis() - _beacon_last_ms;
  uint32_t iv = beaconIntervalMs();
  uint32_t mv = beaconMoveMeters();
  bool due = !_beacon_have_pos
          || (iv > 0 && since >= iv)
          || (mv > 0 && since >= beaconMoveGapMs() && beaconMovedMeters() >= mv);
  if (!due) {
    _beacon_next = futureMillis(BEACON_CHECK_MS);
    return;
  }

  char text[48];
  snprintf(text, sizeof(text), "geo:%.5f,%.5f", sensors.node_lat, sensors.node_lon);
  uint32_t now = getRTCClock()->getCurrentTime();
  bool sent = false;

  if (_prefs.beacon_mode == BEACON_ADVERT_LOCAL || _prefs.beacon_mode == BEACON_ADVERT_FLOOD) {
    mesh::Packet* pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
    if (pkt) {
      if (_prefs.beacon_mode == BEACON_ADVERT_FLOOD) {
        TransportKey scope;
        memcpy(&scope.key, _prefs.default_scope_key, sizeof(scope.key));
        sendFloodScoped(scope, pkt, 0);
      } else {
        sendZeroHop(pkt);
      }
      sent = true;
    }
  } else if (_prefs.beacon_mode == BEACON_GROUP) {
    ChannelDetails ch;
    if (getChannel(_prefs.beacon_chan, ch) && ch.name[0]) {
      sent = sendGroupMessage(now, ch.channel, _prefs.node_name, text, strlen(text));
    }
  } else if (_prefs.beacon_mode == BEACON_CHAT) {
    if (isTextAckPending()) {
      _beacon_next = futureMillis(BEACON_RETRY_MS);
      return;
    }
    strcpy(_bcn_text, text);
    _bcn_ts = now;
    _bcn_try = 0;
    _bcn_ok = false;
    memset(_bcn_acks, 0, sizeof(_bcn_acks));
    sent = beaconChatAttempt();
    if (!sent) _bcn_try = -1;
  }

  if (sent) {
    _beacon_sent_at = now;
    _beacon_last_ms = millis();
    _beacon_lat = sensors.node_lat;
    _beacon_lon = sensors.node_lon;
    _beacon_have_pos = true;
    _beacon_next = futureMillis(BEACON_CHECK_MS);
  } else {
    _beacon_next = futureMillis(BEACON_RETRY_MS);
  }
}
#endif

bool MyMesh::advert() {
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (pkt) {
    sendZeroHop(pkt);
    return true;
  } else {
    return false;
  }
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
  return _mgr->getOutboundTotal() > 0 || dirty_contacts_expiry != 0;
}
