#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "SentLog.h"

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
  int next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_preview;
  UIScreen* clock;
  UIScreen* charge_clock;   // big clock on the charger (non-e-ink)
  UIScreen* stopwatch;      // stopwatch and timer: six clicks
  UIScreen* badge;          // badge line: ten clicks
  UIScreen* games;          // Games page: Runner, Tower, Snake
  UIScreen* sos;            // SOS: 5 s hold
  UIScreen* reader;    // e-book reader (BOOK_READER builds)
  UIScreen* compose;   // Write page: message typed on the device
  UIScreen* sent;      // Sent page: what the device sent, to whom, in reply to what
  UIScreen* relays;    // Relays page: smart switches
  SentLog _sent_log;
  UIScreen* curr;
  unsigned long _idle_since;
  uint32_t _glance_minute = 0;   // big clock on battery: the minute already shown

  bool canSensePower() const;
  void updateChargeClock();

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);
  char handleQuadClick(char c);
  char handleManyClicks(bool ten = false);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
    clock = NULL;
    charge_clock = NULL;
    stopwatch = NULL;
    badge = NULL;
    games = NULL;
    sos = NULL;
    reader = NULL;
    compose = NULL;
    sent = NULL;
    relays = NULL;
    _idle_since = 0;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() { setCurrScreen(home); }
  void openReader();
  bool inReader() const { return curr != NULL && curr == reader; }
  void openCompose();
  bool openReply(const ComposeTarget& to, const char* re = NULL);
  bool openWriteTo(const ComposeTarget& to);
  void openSent();
  bool inSent() const { return curr != NULL && curr == sent; }
  void openRelays();
  bool inRelays() const { return curr != NULL && curr == relays; }
  void openStopwatch();
  bool inStopwatch() const { return curr != NULL && curr == stopwatch; }
  void openBadge();
  bool inBadge() const { return curr != NULL && curr == badge; }
  void openGames();
  bool inGames() const { return curr != NULL && curr == games; }
  bool inSos() const { return curr != NULL && curr == sos; }
  SentLog& sentLog() { return _sent_log; }
  int  getUnreadCount() const;
  bool inCompose() const { return curr != NULL && curr == compose; }
  void showMsgPreview() { setCurrScreen(msg_preview); }
  bool reopenMsgPreview();
  // UI language from `set lang ru|en`: picks the Write keyboard and phrases
  bool langRu() const { return _node_prefs == NULL || _node_prefs->ui_lang != UI_LANG_EN; }
  bool tickerEnabled() const {
#ifdef UI_MSG_TICKER
    return _node_prefs != NULL && _node_prefs->msg_ticker != 0;
#else
    return false;
#endif
  }
  void toggleTicker();
  // the GPS receiver is on: the Write page offers to send the own position
  bool gpsOn() const {
#if ENV_INCLUDE_GPS == 1
    return _node_prefs != NULL && _node_prefs->gps_enabled != 0;
#else
    return false;
#endif
  }
  bool kbFreqLayout() const { return _node_prefs != NULL && _node_prefs->kb_layout != 0; }
  bool autoCursor() const { return _node_prefs != NULL && _node_prefs->auto_cursor != 0; }
  bool chargeClockEnabled() const { return _node_prefs != NULL && _node_prefs->charge_clock != 0; }
  bool swSport() const { return _node_prefs != NULL && _node_prefs->sw_sport != 0; }
  bool autoCaps() const { return _node_prefs == NULL || _node_prefs->auto_caps != 0; }
  void keepDisplayAwake();
  void requestRefresh() { _next_refresh = 0; }
  void showAlert(const char* text, int duration_millis);
  int  getMsgCount() const { return _msgcount; }
  int  getDeviceStored() const;
  bool hasDisplay() const { return _display != NULL; }
  bool isEink() const { return _display != NULL && _display->isEink(); }
  void syncDisplayRefresh() {
    if (_display && _node_prefs) _display->setRefreshMode(_node_prefs->eink_refresh);
  }
  bool isButtonPressed() const;

  bool isBuzzerQuiet() { 
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();
  bool canControlFemLna() const { return _board && _board->canControlLoRaFemLna(); }
  bool isFemLnaEnabled() const { return _board && _board->isLoRaFemLnaEnabled(); }
  void toggleFemLna();
  void cycleRadioProfile();
  // Most economical first. Parentheses on the page show CPU, RX boost and LNA.
  void applyPowerProfile(bool rx_boost, bool fem_lna, bool cpu_sleep);
  bool canSelectMcuSleep() const { return _board && _board->canSelectMcuSleep(); }
  bool gpsBlocksCpuSleep() const {
#if ENV_INCLUDE_GPS == 1
    return _node_prefs && _node_prefs->gps_enabled != 0;
#else
    return false;
#endif
  }
  // ESP32 manual light sleep stops the BLE controller. This Arduino build
  // has no BT modem sleep, so CPU-off modes wait until Bluetooth is off.
  bool bleBlocksCpuSleep() const {
#if defined(BLE_PIN_CODE)
    return isBluetoothEnabled();
#else
    return false;
#endif
  }
  bool isDisplayOn() const override;


  // from AbstractUITask
  void msgRead(int msgcount) override;
  void clearTakenMsg() override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, bool group) override;
  void notify(UIEventType t = UIEventType::none) override;
  bool onUiAck(const uint8_t* ack, uint8_t* key_out) override;
  void onUiRetry(uint32_t old_ack, uint32_t new_ack, unsigned long until) override;
  void loop() override;

  void shutdown(bool restart = false);
};
