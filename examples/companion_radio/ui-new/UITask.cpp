#include "UITask.h"
#include <helpers/RadioProfiles.h>
#include <helpers/ui/BatteryLevel.h>
#include <helpers/ui/ClockFont.h>
#include <helpers/ui/ChargeClockFont.h>
#include <math.h>
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_AUTHOR
  #define UI_AUTHOR "R3XED"
#endif

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif
#define NEIGHBOR_SCROLL_MS  3000   // one page of neighbours stays this long

#define SCAN_SAMPLE_MILLIS    50   // noise scan RSSI sampling, independent of the frame rate
#ifndef SCAN_REFRESH_MILLIS
  #define SCAN_REFRESH_MILLIS       200
#endif
#ifndef SCAN_REFRESH_MILLIS_EINK
  #define SCAN_REFRESH_MILLIS_EINK 3000   // each e-ink update blocks the loop
#endif
#define FSCAN_REFRESH_MILLIS  250   // progress bar while the freq scan runs

#ifndef MSG_PAGE_MILLIS
  #if AUTO_OFF_MILLIS==0   // e-ink: every page flip is a full refresh
    #define MSG_PAGE_MILLIS 10000
  #else
    #define MSG_PAGE_MILLIS  4000
  #endif
#endif

#ifndef CHARGE_CLOCK_GLANCE_MILLIS
  #define CHARGE_CLOCK_GLANCE_MILLIS 3000   // on battery: the big clock lights up this long once a minute
#endif
#define CHARGE_FLASH_STEP_MILLIS 250       // top of the hour: inverted / normal, three times
#define CHARGE_FLASH_STEPS         6

#ifndef FSCAN_MAX
  #define FSCAN_MAX 17
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"
#include "ComposeScreen.h"
#include "SentScreen.h"
#include "RelayScreen.h"
#include "GamesScreen.h"

#ifdef BOOK_READER
  #include "ReaderScreen.h"
  #include <helpers/reader/Cp1251.h>
#endif

#ifdef UI_MSG_TICKER
  #include <helpers/ui/CyrillicFont.h>   // utf8Next()
  #include <helpers/ui/TickerFont.h>
  #ifndef TICKER_DOT
    #define TICKER_DOT      8     // one font dot = 8x8 physical pixels (5x7 font -> 40x56 px letter)
  #endif
  #ifndef TICKER_DOT_GAP
    #define TICKER_DOT_GAP  2     // dark gap between dots, gives the LED-board look
  #endif
  #ifndef TICKER_STEP_MS
    #define TICKER_STEP_MS  50    // text moves one dot per step: 20 dots/s, about 3 letters/s
  #endif
  #ifndef TICKER_PASSES
    #define TICKER_PASSES   1     // how many times a new message runs across the screen
  #endif
  #define TICKER_MAX_COLS  ((MAX_TEXT_LEN + 2) * 6)
  #define BADGE_MAX_COLS   (UI_BADGE_LEN * 6)

static const TickerGlyph* tickerGlyph(uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') cp -= 0x20;
  else if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;   // а..я -> А..Я
  else if (cp == 0x451) cp = 0x401;                  // ё -> Ё
  for (int i = 0; i < TICKER_GLYPH_COUNT; i++) {
    if (TICKER_GLYPHS[i].cp == cp) return &TICKER_GLYPHS[i];
  }
  return NULL;
}

// text -> dot columns (bit 0 = top row), one dot between letters, three per
// blank; the trailing gap is dropped. Returns the number of columns.
static int tickerBuild(const char* text, uint8_t* cols, int max_cols) {
  int len = 0;
  bool gap = true;   // swallow leading and repeated blanks
  for (const char* s = text; *s && len < max_cols - 6; ) {
    uint32_t cp = 0;
    s = utf8Next(s, cp);
    const TickerGlyph* gl = tickerGlyph(cp);
    if (!gl) {            // space, emoji, anything without a glyph -> one blank
      if (!gap) { for (int i = 0; i < 3; i++) cols[len++] = 0; gap = true; }
      continue;
    }
    for (int i = 0; i < gl->w; i++) cols[len++] = gl->cols[i];
    cols[len++] = 0;   // one dot between letters
    gap = false;
  }
  while (len > 0 && cols[len - 1] == 0) len--;
  return len;
}

// digits of a time such as "12:34" in equal 5-dot cells, so they don't shift
static int tickerBuildDigits(const char* text, uint8_t* cols, int max_cols) {
  int len = 0;
  for (const char* s = text; *s && len < max_cols - 6; s++) {
    const TickerGlyph* gl = tickerGlyph((uint8_t)*s);
    if (!gl) continue;
    int cell = (*s >= '0' && *s <= '9') ? 5 : gl->w;
    int pad = (cell - gl->w) / 2;
    for (int i = 0; i < cell; i++) {
      int g = i - pad;
      cols[len++] = (g >= 0 && g < gl->w) ? gl->cols[g] : 0;
    }
    cols[len++] = 0;
  }
  if (len > 0) len--;   // no gap after the last one
  return len;
}

// dot columns at (x, y) in physical pixels; each dot is `dot` wide, `lit` of it lit
static void tickerDraw(DisplayDriver& display, const uint8_t* cols, int n, int x, int y, int dot, int lit) {
  int fw = display.frameWidth();
  for (int c = 0; c < n; c++) {
    int px = x + c * dot;
    if (px < 0 || px + lit > fw) continue;
    uint8_t bits = cols[c];
    for (int r = 0; bits; r++, bits >>= 1) {
      if (bits & 1) display.fillRectPx(px, y + r * dot, lit, lit);
    }
  }
}
#endif

struct PowerChoice {
  bool boost;
  bool lna;
  bool cpu;
  const char* name;
};

// Names run from most economical to least. Missing options (no FEM, no CPU
// sleep) are skipped, so Eco max is always the cheapest mode on this board.
static const char* savingsName(int index, int count) {
  static const char* labels[] = {"Eco max", "Eco high", "Eco good", "Eco fair", "Eco low", "Eco min"};
  if (count <= 1) return labels[0];
  int slot = index * 5 / (count - 1);
  if (slot < 0) slot = 0;
  if (slot > 5) slot = 5;
  return labels[slot];
}

static int buildPowerChoices(bool fem, bool cpu_ok, PowerChoice* out) {
  int n = 0;
  if (cpu_ok) {
    out[n++] = {false, false, true, NULL};
    out[n++] = {true, false, true, NULL};
    if (fem) out[n++] = {true, true, true, NULL};
  }
  out[n++] = {false, false, false, NULL};
  out[n++] = {true, false, false, NULL};
  if (fem) out[n++] = {true, true, false, NULL};
  for (int i = 0; i < n; i++) out[i].name = savingsName(i, n);
  return n;
}

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[24];

public:
  SplashScreen(UITask* task) : _task(task) {
    firmwareVersionNoHash(_version_info, sizeof(_version_info), FIRMWARE_VERSION);

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // meshcore logo
    display.setColor(UIColor::corp_blue);
    int logoWidth = 128;
    display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    display.setColor(UIColor::primary_txt);
    display.setTextSize(1);
    uint16_t websiteWidth = display.getTextWidth(website);
    display.setCursor((display.width() - websiteWidth) / 2, 22);
    display.print(website);

    // version info
    display.setColor(UIColor::primary_txt);
    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, 35, _version_info);

    display.setColor(UIColor::secondary_txt);
    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, 48, "by " UI_AUTHOR);

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

static void clockYmdFromUnix(uint32_t unix_s, int& year, int& month, int& day, int& wday) {
  uint32_t days = unix_s / 86400UL;
  wday = (int)((days + 4) % 7);  // 1970-01-01 was Thursday; 0 = Sunday
  year = 1970;
  for (;;) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    uint32_t diy = leap ? 366 : 365;
    if (days < diy) break;
    days -= diy;
    year++;
  }
  static const uint8_t mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  month = 1;
  for (int i = 0; i < 12; i++) {
    int dim = mdays[i];
    if (i == 1 && leap) dim = 29;
    if (days < (uint32_t)dim) {
      day = (int)days + 1;
      return;
    }
    days -= dim;
    month++;
  }
  day = 1;
}

static void formatClockHm(uint32_t t, int16_t tz_mins, char* buf, size_t n) {
  int64_t local = (int64_t)t + (int64_t)tz_mins * 60;
  if (local < 0) local = 0;
  uint32_t mins = ((uint32_t)local / 60UL) % (24UL * 60UL);
  snprintf(buf, n, "%02u:%02u", (unsigned)(mins / 60UL), (unsigned)(mins % 60UL));
}

static void formatClockDate(uint32_t t, int16_t tz_mins, uint8_t lang, char* buf, size_t n) {
  int64_t local = (int64_t)t + (int64_t)tz_mins * 60;
  if (local < 0) local = 0;
  int year, month, day, wday;
  clockYmdFromUnix((uint32_t)local, year, month, day, wday);
  static const char* en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* ru[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
  const char* name = (lang == UI_LANG_EN) ? en[wday] : ru[wday];
  snprintf(buf, n, "%s, %02d.%02d", name, day, month);
  (void)year;
}

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
#ifdef BOOK_READER
    BOOKS,
#endif
    WRITE,   // write a message on the device
    SENT,    // what the device sent: to whom, in reply to what, delivered or not
#ifdef RELAY_SUPPORT
    RELAYS,  // smart switches: on / off from here
#endif
    RECENT,
    NEIGHBORS,
    RADIO,
    POWER,
    SCAN,
    FSCAN,
    BLUETOOTH,
#if ENV_INCLUDE_GPS == 1
    GPS,
    BEACON,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    SETUP,
#ifdef UI_MSG_TICKER
    TICKER,  // running line on/off, right before hibernate
#endif
    GAMES,   // Runner, Tower, Snake; not on e-ink
    SHUTDOWN,
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  uint8_t _setup_row;
  bool _setup_edit;
  uint8_t _msg_row = 0;      // Msg scroll page: the setting under the cursor
  bool _msg_edit = false;    // Msg scroll page: changing settings
  uint8_t _bcn_row = 0;      // Beacon page: To / Every / Moved
  bool _bcn_edit = false;    // Beacon page: changing settings

  // Msg scroll page: running line and keyboard settings in one place
  #define MSG_SET_ROWS 6
  #define MSG_SET_SHOWN 3
  void changeMsgSetting() {
    if (!_node_prefs) return;
    if (_msg_row == 0) {
      _task->toggleTicker();
      return;
    }
    if (_msg_row == 1) {
      _node_prefs->kb_layout = _node_prefs->kb_layout ? 0 : 1;
    } else if (_msg_row == 2) {
      _node_prefs->auto_caps = _node_prefs->auto_caps ? 0 : 1;
    } else if (_msg_row == 3) {
      _node_prefs->auto_cursor = _node_prefs->auto_cursor ? 0 : 1;
    } else if (_msg_row == 4) {
      _node_prefs->charge_clock = _node_prefs->charge_clock ? 0 : 1;
    } else {
      _node_prefs->sw_sport = _node_prefs->sw_sport ? 0 : 1;
    }
    the_mesh.savePrefs();
    _task->notify(UIEventType::ack);
  }
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];
  NeighborInfo neighbor_list[NEIGHBOR_TABLE_SIZE];
  int neighbors_scroll_offset = 0;   // page of the neighbour list on screen
  static int neighborsPerPage(DisplayDriver& display) {
    int per = (display.height() - 20) / 11;
    return per < 1 ? 1 : per;
  }
  unsigned long neighbors_next_scroll = 0;
  int16_t scan_peak;
  int16_t scan_live = -127;
  int32_t scan_sum = 0;       // RSSI samples collected since the last frame
  uint16_t scan_cnt = 0;
  bool scan_pkt = false;
  unsigned long scan_next_sample = 0;

  void sampleNoise() {
    float rssi = radio_driver.getCurrentRSSI();
    int v = (int)(rssi + (rssi < 0 ? -0.5f : 0.5f));
    if (radio_driver.isReceivingPacket()) {
      scan_pkt = true;
      return;
    }
    scan_sum += v;
    scan_cnt++;
    if (scan_peak < -120 || v > scan_peak) scan_peak = (int16_t)v;
  }

  struct FreqScanBin {
    float freq;
    int16_t rssi;
  };
  enum { FSCAN_IDLE, FSCAN_RUN, FSCAN_DONE };
  uint8_t fscan_state;
  uint8_t fscan_i;
  uint8_t fscan_n;
  unsigned long fscan_ready_at;
  FreqScanBin fscan[FSCAN_MAX];
  FreqScanBin fscan_best[3];

  static int cmpFreqScan(const void* a, const void* b) {
    return ((const FreqScanBin*)a)->rssi - ((const FreqScanBin*)b)->rssi;
  }

  void freqScanBand(float center, float& lo, float& hi) {
    if (center >= 850.0f && center < 890.0f) { lo = 863.0f; hi = 870.0f; }
    else if (center >= 900.0f && center < 932.0f) { lo = 902.0f; hi = 928.0f; }
    else { lo = center - 1.0f; hi = center + 1.0f; }
  }

  void applyScanFreq(float f) {
    radio_driver.setFrequency(f);
  }

  void stopFreqScan() {
    if (fscan_state != FSCAN_RUN) return;
    if (fscan_i > 0) {
      fscan_n = fscan_i;
      finishFreqScan();
    } else {
      the_mesh.restoreRadioAfterScan();
      fscan_state = FSCAN_IDLE;
    }
  }

  void startFreqScan() {
    float step = _node_prefs->bw / 1000.0f;
    if (step < 0.025f) step = 0.025f;
    float center = _node_prefs->freq;
    float lo, hi;
    freqScanBand(center, lo, hi);

    fscan_n = 0;
    fscan[fscan_n++].freq = center;
    for (int k = 1; fscan_n < FSCAN_MAX; k++) {
      float down = center - (float)k * step;
      float up = center + (float)k * step;
      bool added = false;
      if (down >= lo - 0.0001f) {
        fscan[fscan_n++].freq = down;
        added = true;
        if (fscan_n >= FSCAN_MAX) break;
      }
      if (up <= hi + 0.0001f) {
        fscan[fscan_n++].freq = up;
        added = true;
      }
      if (!added) break;
    }
    for (int i = 0; i < fscan_n; i++) fscan[i].rssi = 0;
    fscan_i = 0;
    fscan_state = FSCAN_RUN;
    radio_driver.pauseNoiseFloor(true);
    applyScanFreq(fscan[0].freq);
    fscan_ready_at = millis() + 35;
  }

  void finishFreqScan() {
    the_mesh.restoreRadioAfterScan();
    FreqScanBin sorted[FSCAN_MAX];
    memcpy(sorted, fscan, fscan_n * sizeof(FreqScanBin));
    qsort(sorted, fscan_n, sizeof(FreqScanBin), cmpFreqScan);
    int out = 0;
    for (int i = 0; i < fscan_n && out < 3; i++) {
      fscan_best[out++] = sorted[i];
    }
    fscan_state = FSCAN_DONE;
  }

  void stepFreqScan() {
    if (millis() < fscan_ready_at || fscan_i >= fscan_n) return;
    int sum = 0, n = 0;
    for (int s = 0; s < 8; s++) {
      if (!radio_driver.isReceivingPacket()) {
        sum += (int)radio_driver.getCurrentRSSI();
        n++;
      }
      delay(2);
    }
    fscan[fscan_i].rssi = (int16_t)(n > 0 ? (sum / n) : (int)radio_driver.getCurrentRSSI());
    fscan_i++;
    if (fscan_i >= fscan_n) {
      finishFreqScan();
      _task->requestRefresh();
    } else {
      applyScanFreq(fscan[fscan_i].freq);
      fscan_ready_at = millis() + 35;
    }
  }


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    // Convert millivolts to percentage
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
    const int minMilliVolts = BATT_MIN_MILLIVOLTS;
    const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
    static BatteryLevelFilter batt_filter;
    int batteryPercentage = batt_filter.push(batteryMilliVolts, minMilliVolts, maxMilliVolts)
                            * 100 / BatteryLevelFilter::LEVELS;

    // battery icon
    int iconWidth = 24;
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(UIColor::title_txt);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // show muted icon if buzzer is muted
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(UIColor::warning_txt);
      display.drawXbm(iconX - 9, iconY + 1, muted_icon, 8, 8);
    }
#endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;

  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

  // The gps setting is published only after the module is detected.
  // Its value is "1" only while that module is switched on.
  bool isPageVisible(uint8_t page) const {
    if (page == HomePage::SETUP) return _task->isEink();
    if (page == HomePage::GAMES) return !_task->isEink() && _task->hasDisplay();
#ifdef UI_MSG_TICKER
    if (page == HomePage::TICKER) return !_task->isEink();
#endif
#if ENV_INCLUDE_GPS == 1
    if (page == HomePage::BEACON) return _task->getGPSState();
#endif
    return true;
  }

  void stepSetupRow(int dir) {
    int next = (int)_setup_row + dir;
    if (next < 0) next = 1;
    if (next > 1) next = 0;
    _setup_row = (uint8_t)next;
  }

  void changeSetupValue() {
    if (!_node_prefs) return;
    if (_setup_row == 0) {
      _node_prefs->clock_saver = _node_prefs->clock_saver ? 0 : 1;
    } else {
      uint8_t mode = _node_prefs->eink_refresh;
      if (mode >= EINK_REFRESH_COUNT) mode = 0;
      _node_prefs->eink_refresh = (uint8_t)((mode + 1) % EINK_REFRESH_COUNT);
    }
    the_mesh.savePrefs();
    _task->syncDisplayRefresh();
    _task->notify(UIEventType::ack);
  }

  uint8_t stepPage(int dir) {
    uint8_t page = _page;
    for (uint8_t n = 0; n < HomePage::Count; n++) {
      int next = (int)page + dir;
      if (next < 0) next += HomePage::Count;
      else if (next >= HomePage::Count) next -= HomePage::Count;
      page = (uint8_t)next;
      if (isPageVisible(page)) return page;
    }
    return _page;
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0),
       _setup_row(0), _setup_edit(false), _shutdown_init(false), scan_peak(-127), fscan_state(FSCAN_IDLE), fscan_n(0),
       sensors_lpp(200) {  }

  bool shuttingDown() const { return _shutdown_init; }   // hibernate waits for the button release

  bool allowsClock() const {
    if (_node_prefs && _node_prefs->clock_saver == 0) return false;
    if (_page == HomePage::SCAN || _page == HomePage::FSCAN) return false;
    if (_page == HomePage::NEIGHBORS) return false;
#if ENV_INCLUDE_GPS == 1
    if (_page == HomePage::BEACON) return false;
#endif
    return true;
  }

  void poll() override {
    if (_page == HomePage::SCAN || _page == HomePage::FSCAN) {
      _task->keepDisplayAwake();
    }
    if (_page == HomePage::SCAN && (long)(millis() - scan_next_sample) >= 0) {
      scan_next_sample = millis() + SCAN_SAMPLE_MILLIS;
      sampleNoise();
    }
    if (_page == HomePage::FSCAN && fscan_state == FSCAN_RUN) {
      stepFreqScan();
    }
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    if (!isPageVisible(_page)) _page = stepPage(1);

    display.setColor(UIColor::title_bkg);
    display.fillRect(0, 0, display.width(), 12);
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(UIColor::title_txt);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    // unread messages: an envelope and the count left of the battery
    int unread = _task->getUnreadCount();
    char badge[8];
    badge[0] = 0;
    int badge_w = 0;
    if (unread > 0) {
      snprintf(badge, sizeof(badge), "%d", unread);
      badge_w = 8 + 2 + display.getTextWidth(badge) + 4;
    }
    int header_right = display.width() - 24 - 5 - 3 - badge_w;   // up to the badge / battery icon
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) header_right -= 9;
#endif
    if (unread > 0) {
      int bx = header_right + 2;
      display.drawXbm(bx, 3, envelope_icon, 8, 7);
      display.setCursor(bx + 10, 2);
      display.print(badge);
    }
    if (_page == HomePage::NEIGHBORS) {
      // "Name [7] 1/2": neighbours in total and the page on screen when they do not fit
      int n = the_mesh.getNeighbors(neighbor_list, NEIGHBOR_TABLE_SIZE);
      int per = neighborsPerPage(display);
      int pages = (n + per - 1) / per;
      if (neighbors_scroll_offset >= pages) neighbors_scroll_offset = 0;
      if (pages > 1 && neighbors_next_scroll != 0 && (long)(millis() - neighbors_next_scroll) >= 0) {
        neighbors_scroll_offset = (neighbors_scroll_offset + 1) % pages;   // next page of neighbours
        neighbors_next_scroll = millis() + NEIGHBOR_SCROLL_MS;
      }
      if (pages > 1) sprintf(tmp, " [%d] %d/%d", n, neighbors_scroll_offset + 1, pages);
      else sprintf(tmp, " [%d]", n);
      int room = header_right - display.getTextWidth(tmp);
      display.drawTextEllipsized(0, 2, room, filtered_name);
      int name_w = display.getTextWidth(filtered_name);
      display.setCursor(name_w < room ? name_w : room, 2);
      display.print(tmp);
    } else {
      display.drawTextEllipsized(0, 2, header_right, filtered_name);
    }

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    if (UIColor::title_bkg == UIColor::window_bkg) {
      display.setColor(UIColor::title_txt);
    } else {
      display.setColor(UIColor::title_bkg);
    }
    int y = 14;
    uint8_t visible_count = 0;
    uint8_t visible_index = 0;
    for (uint8_t i = 0; i < HomePage::Count; i++) {
      if (!isPageVisible(i)) continue;
      if (i == _page) visible_index = visible_count;
      visible_count++;
    }
    int x = display.width() / 2 - 5 * (visible_count > 0 ? visible_count - 1 : 0);
    uint8_t drawn = 0;
    for (uint8_t i = 0; i < HomePage::Count; i++) {
      if (!isPageVisible(i)) continue;
      if (drawn == visible_index) {
        display.fillRect(x-1, y-1, 4, 4);
      } else {
        display.fillRect(x, y, 2, 2);
      }
      x += 10;
      drawn++;
    }

    if (_page == HomePage::FIRST) {
      display.setColor(UIColor::primary_txt);
      // MSG: still waiting for the phone / still kept on this device
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d/%d", _task->getMsgCount(), _task->getDeviceStored());
      display.drawTextCentered(display.width() / 2, 22, tmp);

      bool show_date = !display.isEink() && _rtc->getCurrentTime() >= 1000000000UL;
      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        int ip_y = display.height() - 22;
        if (display.height() <= 64) ip_y = show_date ? -1 : 54;
        if (ip_y >= 0) display.drawTextCentered(display.width() / 2, ip_y, tmp);
      #endif
      if (_task->hasConnection()) {
        display.setColor(UIColor::warning_txt);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 43, "< Connected >");

      } else if (the_mesh.getBLEPin() != 0) { // BT pin
        display.setColor(UIColor::warning_txt);
        bool room = !show_date || display.height() > 64;
        display.setTextSize(room ? 2 : 1);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 43, tmp);
      }
      if (show_date) {
        int16_t tz = _node_prefs ? _node_prefs->tz_offset_mins : 0;
        uint32_t now = _rtc->getCurrentTime();
        char hm[8];
        char date_buf[24];
        char line[36];
        formatClockHm(now, tz, hm, sizeof(hm));
        formatClockDate(now, tz, _node_prefs ? _node_prefs->ui_lang : UI_LANG_RU,
                        date_buf, sizeof(date_buf));
        snprintf(line, sizeof(line), "%s  %s", hm, date_buf);
        display.setTextSize(1);
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(display.width() / 2, display.height() - 11, line);
      }
#ifdef BOOK_READER
    } else if (_page == HomePage::BOOKS) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      if (!book_store.ready()) {
        display.drawTextCentered(display.width() / 2, 28, "storage error");
      } else if (book_store.count() == 0) {
        display.drawTextCentered(display.width() / 2, 22, "No books");
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(display.width() / 2, 36, "upload via Bluetooth");
      } else {
        sprintf(tmp, "Books: %d", (int)book_store.count());
        display.drawTextCentered(display.width() / 2, 20, tmp);
        int16_t cur = book_store.current();
        const BookEntry* e = cur >= 0 ? book_store.find((uint8_t)cur) : NULL;
        if (e) {
          char utf8[BOOK_TITLE_MAX * 3];
          char shown[BOOK_TITLE_MAX * 3];
          cp1251ToUtf8(utf8, sizeof(utf8), e->title);
          display.translateUTF8ToBlocks(shown, utf8, sizeof(shown));
          int w = display.getTextWidth(shown);
          int x = w < display.width() - 4 ? (display.width() - w) / 2 : 2;
          display.drawTextEllipsized(x, 32, display.width() - 4, shown);
          sprintf(tmp, "%d%%", e->size ? (int)((uint64_t)e->pos * 100 / e->size) : 0);
          display.setColor(UIColor::secondary_txt);
          display.drawTextCentered(display.width() / 2, 42, tmp);
        }
      }
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 64 - 11, "read: " PRESS_LABEL);
#endif
    } else if (_page == HomePage::WRITE) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 22, "Write message");
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 34, "channel or contact");
      display.drawTextCentered(display.width() / 2, 64 - 11, "open: " PRESS_LABEL);
    } else if (_page == HomePage::SENT) {
      SentLog& log = _task->sentLog();
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      char line[32];
      snprintf(line, sizeof(line), "Sent: %d", log.count());
      display.drawTextCentered(display.width() / 2, 22, line);
      display.setColor(UIColor::secondary_txt);
      SentEntry* e = log.get(0);
      if (e) {
        int replies = 0;
        for (int i = 0; i < log.count(); i++) if (log.get(i)->re[0]) replies++;
        snprintf(line, sizeof(line), "replies: %d", replies);
        display.drawTextCentered(display.width() / 2, 34, line);
      } else {
        display.drawTextCentered(display.width() / 2, 34, "from this device");
      }
      display.drawTextCentered(display.width() / 2, 64 - 11, "open: " PRESS_LABEL);
#ifdef RELAY_SUPPORT
    } else if (_page == HomePage::RELAYS) {
      char line[32];
      int n = the_mesh.relays.count(), on = 0;
      for (int i = 0; i < RELAY_MAX; i++) {
        RelayDef* r = the_mesh.relays.get(i);
        if (r && r->state == RELAY_ON) on++;
      }
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      snprintf(line, sizeof(line), "Relays: %d", n);
      display.drawTextCentered(display.width() / 2, 22, line);
      display.setColor(UIColor::secondary_txt);
      if (n > 0) snprintf(line, sizeof(line), "on: %d", on);
      else snprintf(line, sizeof(line), "none set up");
      display.drawTextCentered(display.width() / 2, 34, line);
      display.drawTextCentered(display.width() / 2, 64 - 11, "open: " PRESS_LABEL);
#endif
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      int shown = 0;
      display.setColor(UIColor::primary_txt);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        shown++;
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }

        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;

        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
      if (shown == 0) {
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(display.width() / 2, 28, "no adverts");
      }
      // last slot stays free until the list is full, so the send hint fits there
      if (recent[UI_RECENT_LIST_SIZE - 1].name[0] == 0) {
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(display.width() / 2, 20 + (UI_RECENT_LIST_SIZE - 1) * 11, "advert: " PRESS_LABEL);
      }
    } else if (_page == HomePage::NEIGHBORS) {
      int n = the_mesh.getNeighbors(neighbor_list, NEIGHBOR_TABLE_SIZE);
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      if (n == 0) {
        neighbors_scroll_offset = 0;
        neighbors_next_scroll = 0;
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(display.width() / 2, 28, "no neighbors");
        display.drawTextCentered(display.width() / 2, 64 - 11, "poll: " PRESS_LABEL);
      } else {
        const int line_h = 11;
        const int y0 = 20;
        int visible = neighborsPerPage(display);
        int pages = (n + visible - 1) / visible;
        // the header (drawn above) already shows this page; the next page comes after the next frame
        if (pages <= 1) {
          neighbors_scroll_offset = 0;
          neighbors_next_scroll = 0;
        } else {
          if (neighbors_scroll_offset >= pages) neighbors_scroll_offset = 0;
          if (neighbors_next_scroll == 0) {
            neighbors_next_scroll = millis() + NEIGHBOR_SCROLL_MS;
          }
        }

        int first = neighbors_scroll_offset * visible;
        int rows = n - first < visible ? n - first : visible;
        for (int i = 0; i < rows; i++) {
          auto a = &neighbor_list[first + i];
          int y = y0 + i * line_h;
          float snr = a->snr_x4 / 4.0f;
          sprintf(tmp, "%+.0f", snr);

          int snr_width = display.getTextWidth(tmp);
          int max_name_width = display.width() - snr_width - 1;

          char filtered_name[sizeof(a->name)];
          display.translateUTF8ToBlocks(filtered_name, a->name, sizeof(filtered_name));
          display.drawTextEllipsized(0, y, max_name_width, filtered_name);
          display.setCursor(display.width() - snr_width - 1, y);
          display.print(tmp);
        }
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      if (_task->canControlFemLna()) {
        sprintf(tmp, "TX: %ddBm  LNA: %s", _node_prefs->tx_power_dbm,
                _task->isFemLnaEnabled() ? "on" : "off");
      } else {
        sprintf(tmp, "TX: %ddBm  LNA: n/a", _node_prefs->tx_power_dbm);
      }
      display.print(tmp);
      display.setCursor(0, 53);
      display.drawTextLeftAlign(0, 53, radioProfileLabel(the_mesh.radioProfile()));
      display.setColor(UIColor::secondary_txt);
      display.drawTextRightAlign(display.width() - 1, 53, "3x: profile");
    } else if (_page == HomePage::POWER) {
      bool fem = _task->canControlFemLna();
      bool cpu_hw = _task->canSelectMcuSleep();
      bool cpu_ok = cpu_hw && !_task->gpsBlocksCpuSleep() && !_task->bleBlocksCpuSleep();
      bool boost = _node_prefs->rx_boosted_gain != 0;
      bool lna = fem && _node_prefs->radio_fem_rxgain != 0;
      bool cpu = cpu_ok && _node_prefs->mcu_sleep != 0;
      PowerChoice choices[6];
      int n = buildPowerChoices(fem, cpu_ok, choices);
      const char* name = "Custom";
      for (int i = 0; i < n; i++) {
        if (choices[i].boost == boost && choices[i].lna == lna && choices[i].cpu == cpu) {
          name = choices[i].name;
          break;
        }
      }

      // Beacon uses these rows on the 128x64 OLED: title is 12px, dots at y=14.
      display.setTextSize(1);
      int y = 18;
      display.setColor(UIColor::primary_txt);
      display.drawTextLeftAlign(0, y, "Power");
      display.setColor(UIColor::secondary_txt);
      display.drawTextRightAlign(display.width() - 1, y, name);
      y += 12;
      display.setColor(UIColor::primary_txt);
      display.drawTextLeftAlign(0, y, cpu ? "CPU off" : "CPU on");
      display.drawTextRightAlign(display.width() - 1, y, boost ? "RX on" : "RX off");
      y += 12;
      if (fem) {
        display.drawTextLeftAlign(0, y, lna ? "LNA on" : "LNA off");
        y += 12;
      }
      if (y + 8 <= display.height()) {
        const char* hold = NULL;
        if (cpu_hw && _task->gpsBlocksCpuSleep()) hold = "GPS needs CPU on";
        else if (cpu_hw && _task->bleBlocksCpuSleep()) hold = "BLE needs CPU on";
        display.setColor(hold ? UIColor::warning_txt : UIColor::secondary_txt);
        const char* hint = hold ? hold : "mode: " PRESS_LABEL;
        display.drawTextCentered(display.width() / 2, y, hint);
      }
    } else if (_page == HomePage::SCAN) {
      if (scan_cnt == 0) sampleNoise();
      if (scan_cnt > 0) {
        scan_live = (int16_t)((scan_sum - (int32_t)scan_cnt / 2) / (int32_t)scan_cnt);
      }
      int live = scan_live;
      bool pkt = scan_pkt;
      scan_sum = 0;
      scan_cnt = 0;
      scan_pkt = false;
      int nl = radio_driver.getNoiseFloor();

      display.setColor(UIColor::primary_txt);
      display.setTextSize(2);
      sprintf(tmp, "%d", live);
      display.drawTextCentered(display.width() / 2, 18, tmp);

      display.setTextSize(1);
      if (pkt) {
        display.setColor(UIColor::warning_txt);
        display.drawTextRightAlign(display.width() - 1, 18, "PKT");
      }

      display.setColor(UIColor::secondary_txt);
      if (nl == 0) {
        sprintf(tmp, "NL --");
      } else {
        sprintf(tmp, "NL %d", nl);
      }
      display.drawTextLeftAlign(0, 36, tmp);
      if (scan_peak < -120) {
        sprintf(tmp, "pk --");
      } else {
        sprintf(tmp, "pk %d", (int)scan_peak);
      }
      display.drawTextRightAlign(display.width() - 1, 36, tmp);

      const int rssi_min = -120;
      const int rssi_max = -50;
      int bar_x = 2;
      int bar_y = 48;
      int bar_h = 8;
      int bar_w = display.width() - 4;
      int clamped = live;
      if (clamped < rssi_min) clamped = rssi_min;
      if (clamped > rssi_max) clamped = rssi_max;
      int fill = ((clamped - rssi_min) * bar_w) / (rssi_max - rssi_min);

      display.setColor(UIColor::secondary_txt);
      display.drawRect(bar_x, bar_y, bar_w, bar_h);
      display.setColor(UIColor::primary_txt);
      if (fill > 2) {
        display.fillRect(bar_x + 1, bar_y + 1, fill - 2, bar_h - 2);
      }
    } else if (_page == HomePage::FSCAN) {
      display.setTextSize(1);
      if (fscan_state == FSCAN_RUN) {
        display.setColor(UIColor::primary_txt);
        if (fscan_n > 0) {
          sprintf(tmp, "scan %d/%d", (int)fscan_i, (int)fscan_n);
          display.drawTextCentered(display.width() / 2, 22, tmp);
          if (fscan_i < fscan_n) {
            sprintf(tmp, "%06.3f", fscan[fscan_i].freq);
            display.drawTextCentered(display.width() / 2, 36, tmp);
          }
          int bar_w = display.width() - 4;
          int fill = ((int)fscan_i * bar_w) / fscan_n;
          display.setColor(UIColor::secondary_txt);
          display.drawRect(2, 50, bar_w, 8);
          display.setColor(UIColor::primary_txt);
          if (fill > 2) display.fillRect(3, 51, fill - 2, 6);
        }
      } else if (fscan_state == FSCAN_DONE) {
        display.setColor(UIColor::secondary_txt);
        sprintf(tmp, "now %06.3f", _node_prefs->freq);
        display.drawTextLeftAlign(0, 18, tmp);
        display.setColor(UIColor::primary_txt);
        int y = 29;
        for (int i = 0; i < 3; i++, y += 11) {
          if (i >= fscan_n) break;
          bool here = fabsf(fscan_best[i].freq - _node_prefs->freq) < 0.001f;
          sprintf(tmp, "%06.3f  %d%s", fscan_best[i].freq, (int)fscan_best[i].rssi, here ? " *" : "");
          display.drawTextLeftAlign(0, y, tmp);
        }
      } else {
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(display.width() / 2, 28, "find quiet freq");
        display.drawTextCentered(display.width() / 2, display.height() - 11, "scan: " PRESS_LABEL);
      }
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(UIColor::corp_blue);
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isBluetoothEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setColor(UIColor::secondary_txt);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 20;
      bool gps_state = _task->getGPSState();
      const char* module = gps_state ? "on" : "off";
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        module = gps_state ? "off(hw)" : "off(sw)";
      }
#endif
      display.setColor(UIColor::secondary_txt);
      display.drawTextLeftAlign(0, y, "module");
      display.setColor(gps_state ? UIColor::primary_txt : UIColor::warning_txt);
      display.drawTextRightAlign(display.width() - 1, y, module);
      y += 12;
      if (nmea == NULL) {
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else if (!gps_state) {
        // isEnabled() reads the enable pin. With no pin it stays true, so the
        // chip can keep drawing current after the software switch is off.
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, nmea->isEnabled() ? "stays powered" : "power off");
        y += 12;
      } else {
        display.setColor(UIColor::secondary_txt);
        sprintf(buf, "fix %s", nmea->isValid() ? "yes" : "no");
        display.drawTextLeftAlign(0, y, buf);
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "sat %d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width() - 1, y, buf);
        y += 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "pos");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.4f %.4f",
          nmea->getLatitude() / 1000000., nmea->getLongitude() / 1000000.);
        display.drawTextRightAlign(display.width() - 1, y, buf);
        y += 12;
        if (y + 8 <= display.height()) {
          display.setColor(UIColor::secondary_txt);
          display.drawTextLeftAlign(0, y, "alt");
          display.setColor(UIColor::primary_txt);
          sprintf(buf, "%.2f", nmea->getAltitude() / 1000.);
          display.drawTextRightAlign(display.width() - 1, y, buf);
          y += 12;
        }
      }
      if (display.height() - 11 >= y) {
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(display.width() / 2, display.height() - 11, "toggle: " PRESS_LABEL);
      }
    } else if (_page == HomePage::BEACON && _bcn_edit) {
      // settings: who gets the position, how often, after how far
      char mode[20], target[32], pos[40], sent[24], every[12], moved[12], to[40];
      the_mesh.formatBeacon(mode, sizeof(mode), target, sizeof(target), pos, sizeof(pos), sent, sizeof(sent));
      the_mesh.formatBeaconRules(every, sizeof(every), moved, sizeof(moved));
      const char* sp = strchr(mode, ' ');
      if (_node_prefs && _node_prefs->beacon_mode == BEACON_OFF) strcpy(to, "off");
      else if (_node_prefs && (_node_prefs->beacon_mode == BEACON_GROUP || _node_prefs->beacon_mode == BEACON_CHAT))
        snprintf(to, sizeof(to), "%s%s", _node_prefs->beacon_mode == BEACON_GROUP ? "" : "@", target);
      else snprintf(to, sizeof(to), "advert %s", target);
      (void)sp;
      if (the_mesh.beaconIntervalFixed() && _node_prefs && _node_prefs->beacon_mode != BEACON_OFF) {
        char fixed[16];
        snprintf(fixed, sizeof(fixed), "%s fix", every);
        strcpy(every, fixed);
      }
      char to_shown[40];
      display.translateUTF8ToBlocks(to_shown, to, sizeof(to_shown));
      const char* rows_l[3] = { "To", "Every", "Moved" };
      const char* rows_r[3] = { to_shown, every, moved };
      display.setTextSize(1);
      for (int i = 0; i < 3; i++) {
        int y = 20 + i * 11;
        bool off = strcmp(rows_r[i], "off") == 0;
        display.setColor(UIColor::primary_txt);
        if ((uint8_t)i == _bcn_row) {
          display.fillRect(0, y - 1, display.width(), 10);
          display.setColor(UIColor::window_bkg);
        } else if (off) {
          display.setColor(UIColor::secondary_txt);
        }
        display.drawTextLeftAlign(2, y, rows_l[i]);
        int lw = display.getTextWidth("Moved") + 8;
        int rw = display.getTextWidth(rows_r[i]);
        if (rw > display.width() - 3 - lw) {
          display.drawTextEllipsized(lw, y, display.width() - 3 - lw, rows_r[i]);
        } else {
          display.drawTextRightAlign(display.width() - 3, y, rows_r[i]);
        }
      }
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 64 - 11, "hold:change 3x:done");
    } else if (_page == HomePage::BEACON) {
      char mode[20];
      char target[32];
      char pos[40];
      char sent[24];
      the_mesh.formatBeacon(mode, sizeof(mode), target, sizeof(target), pos, sizeof(pos), sent, sizeof(sent));
      char every_s[12], moved_s[12];
      the_mesh.formatBeaconRules(every_s, sizeof(every_s), moved_s, sizeof(moved_s));
      bool move_on = strcmp(moved_s, "off") != 0 && _node_prefs && _node_prefs->beacon_mode != BEACON_OFF;
      char kind[16];
      char iv[8];
      kind[0] = iv[0] = 0;
      const char* sp = strchr(mode, ' ');
      if (sp) {
        size_t klen = (size_t)(sp - mode);
        if (klen >= sizeof(kind)) klen = sizeof(kind) - 1;
        memcpy(kind, mode, klen);
        kind[klen] = 0;
        strncpy(iv, sp + 1, sizeof(iv) - 1);
        iv[sizeof(iv) - 1] = 0;
      } else {
        strncpy(kind, mode, sizeof(kind) - 1);
        kind[sizeof(kind) - 1] = 0;
      }
      int y = 18;
      display.setColor(UIColor::primary_txt);
      display.drawTextLeftAlign(0, y, kind);
      if (iv[0]) {
        display.setColor(UIColor::secondary_txt);
        display.drawTextRightAlign(display.width() - 1, y, iv);
      }
      y += 12;
      char shown[32];
      display.translateUTF8ToBlocks(shown, target, sizeof(shown));
      display.setColor(UIColor::primary_txt);
      int tw = display.width() - 1;
      if (move_on) {   // distance trigger at the right: "+200m"
        char mv[14];
        snprintf(mv, sizeof(mv), "+%s", moved_s);
        display.setColor(UIColor::secondary_txt);
        display.drawTextRightAlign(display.width() - 1, y, mv);
        tw -= display.getTextWidth(mv) + 4;
        display.setColor(UIColor::primary_txt);
      }
      display.drawTextEllipsized(0, y, tw, shown);
      y += 12;
      display.setColor(strcmp(pos, "no fix") == 0 || strcmp(pos, "no gps") == 0
          ? UIColor::warning_txt : UIColor::secondary_txt);
      display.drawTextEllipsized(0, y, display.width() - 1, pos);
      y += 12;
      display.setColor(UIColor::secondary_txt);
      display.drawTextLeftAlign(0, y, sent);
      if (display.height() > 80 && strcmp(kind, "off") != 0) {
        display.drawTextCentered(display.width() / 2, display.height() - 11, "hold: settings  4x off");
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.setColor(UIColor::secondary_txt);
        display.print(name);
        display.setColor(UIColor::primary_txt);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::SETUP) {
      uint8_t mode = _node_prefs ? _node_prefs->eink_refresh : EINK_REFRESH_CLEAR;
      if (mode >= EINK_REFRESH_COUNT) mode = EINK_REFRESH_CLEAR;
      bool clock_on = _node_prefs && _node_prefs->clock_saver != 0;
      sprintf(tmp, "%u", (unsigned)mode + 1);
      const char* rows_l[2] = { "Clock", "ScreenMode" };
      const char* rows_r[2] = { clock_on ? "on" : "off", tmp };
      display.setTextSize(1);
      for (int i = 0; i < 2; i++) {
        int y = 22 + i * 14;
        display.setColor(UIColor::primary_txt);
        if (_setup_edit && (uint8_t)i == _setup_row) display.drawTextLeftAlign(0, y, ">");
        display.drawTextLeftAlign(10, y, rows_l[i]);
        display.drawTextRightAlign(display.width() - 8, y, rows_r[i]);
      }
      if (display.height() >= 64) {
        display.setColor(UIColor::secondary_txt);
        display.drawTextCentered(display.width() / 2, 52, _setup_edit ? "triple click" : PRESS_LABEL);
      }
#ifdef UI_MSG_TICKER
    } else if (_page == HomePage::TICKER) {
      // settings of incoming and outgoing messages; hold to change them
      bool ru = _task->langRu();
      const char* layout = _task->kbFreqLayout() ? (ru ? "частотн." : "freq") : (ru ? "ЙЦУКЕН" : "QWERTY");
      const char* rows_l[MSG_SET_ROWS] = { "Msg scroll", "Keys layout", "Auto caps", "Auto cursor", "Big clock", "Sport watch" };
      const char* rows_r[MSG_SET_ROWS] = { _task->tickerEnabled() ? "ON" : "OFF", layout,
                                           _task->autoCaps() ? "ON" : "OFF",
                                           _task->autoCursor() ? "ON" : "OFF",
                                           _task->chargeClockEnabled() ? "ON" : "OFF",
                                           _task->swSport() ? "ON" : "OFF" };
      display.setTextSize(1);
      // three rows on screen; the window follows the cursor
      int top = 0;
      if (_msg_edit && _msg_row >= MSG_SET_SHOWN) top = _msg_row - MSG_SET_SHOWN + 1;
      for (int i = top; i < top + MSG_SET_SHOWN && i < MSG_SET_ROWS; i++) {
        int y = 20 + (i - top) * 11;
        bool off = strcmp(rows_r[i], "OFF") == 0;
        display.setColor(UIColor::primary_txt);
        if (_msg_edit && (uint8_t)i == _msg_row) {
          display.fillRect(0, y - 1, display.width(), 10);
          display.setColor(UIColor::window_bkg);
        } else if (off) {
          display.setColor(UIColor::secondary_txt);
        }
        display.drawTextLeftAlign(2, y, rows_l[i]);
        display.drawTextRightAlign(display.width() - 3, y, rows_r[i]);
      }
      // scroll bar on the right edge: where the three rows are among all of them
      display.setColor(UIColor::secondary_txt);
      int bar_h = 32 * MSG_SET_SHOWN / MSG_SET_ROWS;
      display.fillRect(display.width() - 1, 19 + 32 * top / MSG_SET_ROWS, 1, bar_h);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 64 - 11,
                               _msg_edit ? "hold:change 3x:done" : "settings: " PRESS_LABEL);
#endif
    } else if (_page == HomePage::GAMES) {
      display.setTextSize(1);
      display.setColor(UIColor::primary_txt);
      display.drawTextCentered(display.width() / 2, 20, "Runner  Tower  Snake");
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 34, "one button games");
      display.drawTextCentered(display.width() / 2, 64 - 11, "games: " PRESS_LABEL);
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(UIColor::corp_blue);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.setColor(UIColor::warning_txt);
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.setColor(UIColor::secondary_txt);
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
      }
    }
    if (_page == HomePage::SCAN) {
      return display.isEink() ? SCAN_REFRESH_MILLIS_EINK : SCAN_REFRESH_MILLIS;
    }
    if (_page == HomePage::FSCAN && fscan_state == FSCAN_RUN) {
      // e-ink skips the progress bar, finishing the scan requests a redraw
      return display.isEink() ? 5000 : FSCAN_REFRESH_MILLIS;
    }
    if (_page == HomePage::FIRST && !display.isEink() && _rtc->getCurrentTime() >= 1000000000UL) {
      uint32_t sec = _rtc->getCurrentTime() % 60UL;
      int wait = (int)((60UL - sec) * 1000UL);
      if (wait < 1000) wait = 1000;
      return wait;
    }
    if (_page == HomePage::NEIGHBORS) return 1000;
#if ENV_INCLUDE_GPS == 1
    if (_page == HomePage::BEACON) return 1000;
#endif
    return 5000;   // next render after 5000 ms
  }

  bool handleInput(char c) override {
    if (c == KEY_QUAD) {
#if ENV_INCLUDE_GPS == 1
      if (_page != HomePage::BEACON) { _task->openGlance(); return true; }   // four clicks: time at a glance
      _bcn_edit = false;
      if (the_mesh.stopBeacon()) {
        _task->notify(UIEventType::ack);
        _task->showAlert("beacon off", 900);
      }
      return true;
#else
      _task->openGlance();
      return true;
#endif
    }
    if (c == KEY_SELECT) {
      if (_page == HomePage::RADIO) {
        _task->cycleRadioProfile();
        return true;
      }
      if (_page == HomePage::SETUP && _setup_edit) {
        changeSetupValue();
        return true;
      }
#ifdef UI_MSG_TICKER
      if (_page == HomePage::TICKER && _msg_edit) {
        _msg_edit = false;
        return true;
      }
#endif
#if ENV_INCLUDE_GPS == 1
      if (_page == HomePage::BEACON && _bcn_edit) {   // triple: settings done
        _bcn_edit = false;
        return true;
      }
      if (_page == HomePage::BEACON && the_mesh.cycleBeaconInterval()) {
        char mode[20];
        char target[32];
        char pos[40];
        char sent[24];
        char alert[48];
        the_mesh.formatBeacon(mode, sizeof(mode), target, sizeof(target), pos, sizeof(pos), sent, sizeof(sent));
        snprintf(alert, sizeof(alert), "%s", mode);
        _task->notify(UIEventType::ack);
        _task->showAlert(alert, 900);
        return true;
      }
#endif
      _task->toggleBuzzer();
      return true;
    }
    if ((c == KEY_UP || c == KEY_DOWN) && _page == HomePage::SETUP && _setup_edit) {
      stepSetupRow(c == KEY_UP ? -1 : 1);
      return true;
    }
    if (c == KEY_LEFT || c == KEY_PREV) {
      if (_page == HomePage::FSCAN) stopFreqScan();
      if (_page == HomePage::SETUP && _setup_edit) {
        stepSetupRow(-1);
        return true;
      }
#ifdef UI_MSG_TICKER
      if (_page == HomePage::TICKER && _msg_edit) {
        _msg_row = (_msg_row + MSG_SET_ROWS - 1) % MSG_SET_ROWS;
        return true;
      }
#endif
#if ENV_INCLUDE_GPS == 1
      if (_page == HomePage::BEACON && _bcn_edit) {
        _bcn_row = (_bcn_row + 2) % 3;
        return true;
      }
#endif
      _page = stepPage(-1);
      _msg_edit = false;
      _bcn_edit = false;
      if (_page == HomePage::SETUP) {
        _setup_row = 0;
        _setup_edit = false;
      }
      if (_page == HomePage::NEIGHBORS) {
        neighbors_scroll_offset = 0;
        neighbors_next_scroll = 0;
      }
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      if (_page == HomePage::FSCAN) stopFreqScan();
      if (_page == HomePage::SETUP && _setup_edit) {
        stepSetupRow(1);
        return true;
      }
#ifdef UI_MSG_TICKER
      if (_page == HomePage::TICKER && _msg_edit) {
        _msg_row = (_msg_row + 1) % MSG_SET_ROWS;
        return true;
      }
#endif
#if ENV_INCLUDE_GPS == 1
      if (_page == HomePage::BEACON && _bcn_edit) {
        _bcn_row = (_bcn_row + 1) % 3;
        return true;
      }
#endif
      _page = stepPage(1);
      _msg_edit = false;
      _bcn_edit = false;
      if (_page == HomePage::SETUP) {
        _setup_row = 0;
        _setup_edit = false;
      }
      if (_page == HomePage::NEIGHBORS) {
        neighbors_scroll_offset = 0;
        neighbors_next_scroll = 0;
      }
      // Page-name plaques force a second full refresh on e-ink.
      if (!_task->isEink()) {
        if (_page == HomePage::RECENT) {
          _task->showAlert("Recent adverts", 800);
        }
        if (_page == HomePage::NEIGHBORS) {
          _task->showAlert("Neighbors", 800);
        }
        if (_page == HomePage::RADIO) {
          _task->showAlert("Radio", 800);
        }
        if (_page == HomePage::POWER) {
          _task->showAlert("Power", 800);
        }
        if (_page == HomePage::SCAN) {
          _task->showAlert("Noise scan", 800);
        }
        if (_page == HomePage::FSCAN) {
          _task->showAlert("Freq scan", 800);
        }
#ifdef UI_MSG_TICKER
        if (_page == HomePage::TICKER) {
          _task->showAlert("Msg scroll", 800);
        }
#endif
        if (_page == HomePage::GAMES) {
          _task->showAlert("Games", 800);
        }
#if ENV_INCLUDE_GPS == 1
        if (_page == HomePage::BEACON) {
          _task->showAlert("Beacon", 800);
        }
#endif
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::FSCAN) {
      if (fscan_state != FSCAN_RUN) {
        _task->notify(UIEventType::ack);
        startFreqScan();
        _task->showAlert("Scanning...", 600);
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::SCAN) {
      scan_peak = -127;
      scan_sum = 0;
      scan_cnt = 0;
      _task->showAlert("Peak reset", 600);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::NEIGHBORS) {
      neighbors_scroll_offset = 0;
      neighbors_next_scroll = 0;
      _task->notify(UIEventType::ack);
      if (the_mesh.sendNeighborDiscover()) {
        _task->showAlert("Polling...", 1000);
      } else {
        _task->showAlert("Poll failed", 1000);
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::RADIO) {
      _task->toggleFemLna();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::POWER) {
      bool fem = _task->canControlFemLna();
      bool cpu_ok = _task->canSelectMcuSleep() && !_task->gpsBlocksCpuSleep() && !_task->bleBlocksCpuSleep();
      bool boost = _node_prefs->rx_boosted_gain != 0;
      bool lna = fem && _node_prefs->radio_fem_rxgain != 0;
      bool cpu = cpu_ok && _node_prefs->mcu_sleep != 0;
      PowerChoice choices[6];
      int n = buildPowerChoices(fem, cpu_ok, choices);
      int cur = -1;
      for (int i = 0; i < n; i++) {
        if (choices[i].boost == boost && choices[i].lna == lna && choices[i].cpu == cpu) {
          cur = i;
          break;
        }
      }
      int next = (cur < 0) ? 0 : (cur + 1) % n;
      _task->applyPowerProfile(choices[next].boost, choices[next].lna, choices[next].cpu);
      return true;
    }
#ifdef BOOK_READER
    if (c == KEY_ENTER && _page == HomePage::BOOKS) {
      _task->openReader();
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::WRITE) {
      _task->openCompose();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::SENT) {
      _task->openSent();
      return true;
    }
#ifdef RELAY_SUPPORT
    if (c == KEY_ENTER && _page == HomePage::RELAYS) {
      _task->openRelays();
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isBluetoothEnabled()) {  // toggle Bluetooth on/off
        _task->disableBluetooth();
      } else {
        _task->enableBluetooth();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::RECENT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BEACON) {
      // first hold opens the settings; then hold changes the row under the cursor
      if (!_bcn_edit) {
        _bcn_edit = true;
        _bcn_row = 0;
        return true;
      }
      if (_bcn_row == 1) {
        if (the_mesh.cycleBeaconInterval()) _task->notify(UIEventType::ack);
        else _task->showAlert("adverts: fixed time", 900);
        return true;
      }
      if (_bcn_row == 2) {
        the_mesh.cycleBeaconMove();
        _task->notify(UIEventType::ack);
        return true;
      }
      the_mesh.cycleBeacon();
      _task->notify(UIEventType::ack);
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::SETUP) {
      _setup_edit = !_setup_edit;
      return true;
    }
#ifdef UI_MSG_TICKER
    if (c == KEY_ENTER && _page == HomePage::TICKER) {
      if (!_msg_edit) {   // the first hold opens the settings
        _msg_edit = true;
        _msg_row = 0;
      } else {
        changeMsgSetting();
      }
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::GAMES) {
      _task->openGames();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
    // long press on the main page reopens messages already previewed or dismissed
    if (c == KEY_ENTER && _page == HomePage::FIRST) {
      return _task->reopenMsgPreview();
    }
    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[MAX_TEXT_LEN + 1];
    bool group;
    ComposeScreen::Target from;   // who to reply to
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int stored = 0;   // messages still held in the ring after they are read or dismissed
  int groups = 0;
  int chats = 0;
  int head = MAX_UNREAD_MSGS - 1;   // message currently on screen
  int latest = MAX_UNREAD_MSGS - 1; // most recently received message
  bool replay = false;
  MsgEntry unread[MAX_UNREAD_MSGS];

  // long messages are split into screen-sized pages that rotate automatically
  int page_start = 0;   // offset into the filtered text of the page on screen
  int page_next = 0;    // offset of the following page, 0 when the text ends on this page
  int page_num = 0;
  int shown_head = -1;  // last message/page actually drawn; -1 forces a full refresh
  int shown_page = -1;
  unsigned long page_until = 0;

  void resetPage() {
    page_start = page_next = page_num = 0;
    page_until = millis() + MSG_PAGE_MILLIS;
  }

#ifdef UI_MSG_TICKER
  // Running line: a just-received message crosses the screen in big dots,
  // then the normal preview below takes over unchanged.
  uint8_t tk_cols[TICKER_MAX_COLS];   // one byte per dot column, bit 0 = top row
  int tk_len = 0;
  int tk_pos = 0;                     // scroll offset in dot columns
  int tk_pass = 0;
  bool tk_active = false;
  char tk_origin[62];

  void startTicker(const char* origin, const char* msg) {
    tk_len = tickerBuild(msg, tk_cols, TICKER_MAX_COLS);
    if (tk_len == 0) { tk_active = false; return; }
    StrHelper::strncpy(tk_origin, origin, sizeof(tk_origin));
    tk_pos = 0;
    tk_pass = 0;
    tk_active = true;
  }

  int renderTicker(DisplayDriver& display) {
    _task->keepDisplayAwake();

    // header in the usual small font: who sent it
    char filtered_origin[sizeof(tk_origin)];
    display.translateUTF8ToBlocks(filtered_origin, tk_origin, sizeof(filtered_origin));
    display.setTextSize(1);
    display.setColor(UIColor::secondary_txt);
    display.drawTextEllipsized(0, 0, display.width(), filtered_origin);
    display.setColor(UIColor::corp_blue);
    display.drawRect(0, 11, display.width(), 1);

    int fw = display.frameWidth(), fh = display.frameHeight();
    int top = (13 * fh + display.height() - 1) / display.height();   // first pixel row under the line
    int dot = TICKER_DOT;
    if (dot * 7 > fh - top) dot = (fh - top) / 7;
    if (dot < 2) dot = 2;
    int lit = dot > TICKER_DOT_GAP + 1 ? dot - TICKER_DOT_GAP : dot;
    int screen_cols = (fw + dot - 1) / dot;
    int x0 = (fw - screen_cols * dot) / 2;
    int y0 = top + (fh - top - 7 * dot) / 2;

    display.setColor(UIColor::primary_txt);
    for (int c = 0; c < screen_cols; c++) {
      int src = tk_pos - screen_cols + c;   // text enters from the right edge
      if (src < 0 || src >= tk_len) continue;
      uint8_t bits = tk_cols[src];
      for (int r = 0; bits; r++, bits >>= 1) {
        if (bits & 1) display.fillRectPx(x0 + c * dot, y0 + r * dot, lit, lit);
      }
    }
    display.markFull();

    if (++tk_pos > tk_len + screen_cols) {   // the last letter has left the screen
      tk_pos = 0;
      if (++tk_pass >= TICKER_PASSES) {
        tk_active = false;
        shown_head = -1;
        resetPage();
        return 50;   // draw the normal preview right away
      }
    }
    return TICKER_STEP_MS;
  }
#endif

  void nextPage() {
    if (page_next > 0) {
      page_start = page_next;
      page_num++;
    } else {
      page_start = page_num = 0;
    }
    page_until = millis() + MSG_PAGE_MILLIS;
  }

  int oldestIndex() const {
    if (stored <= 0) return latest;
    return (latest + MAX_UNREAD_MSGS - (stored - 1)) % MAX_UNREAD_MSGS;
  }

  // oldest message that is still counted as unread
  int oldestUnread() const {
    if (num_unread <= 0) return latest;
    return (latest + MAX_UNREAD_MSGS - (num_unread - 1)) % MAX_UNREAD_MSGS;
  }

  void dropHead() {
    if (stored <= 0) return;
    int oldest = oldestIndex();
    int pos = 0;
    for (int idx = oldest; idx != head && pos < stored; pos++) {
      idx = (idx + 1) % MAX_UNREAD_MSGS;
    }
    if (pos >= stored) return;

    bool was_group = unread[head].group;
    for (int i = pos; i < stored - 1; i++) {
      int dst = (oldest + i) % MAX_UNREAD_MSGS;
      unread[dst] = unread[(dst + 1) % MAX_UNREAD_MSGS];
    }
    stored--;
    if (num_unread > 0) num_unread--;
    if (was_group) { if (groups > 0) groups--; }
    else if (chats > 0) chats--;

    if (stored == 0 || num_unread == 0) {
      num_unread = 0;
      if (stored == 0) groups = chats = 0;
      _task->gotoHomeScreen();
      return;
    }
    latest = (oldest + stored - 1) % MAX_UNREAD_MSGS;
    head = (pos >= stored) ? latest : (oldest + pos) % MAX_UNREAD_MSGS;
    resetPage();
  }

public:
  // The phone took the oldest message that was also shown here. Remove that copy.
  bool dropOldest() {
    if (stored <= 0) return true;
    int oldest = oldestIndex();
    bool viewing = (head == oldest);
    bool was_group = unread[oldest].group;
    bool counted = (num_unread > 0 && num_unread == stored);

    for (int i = 0; i < stored - 1; i++) {
      int dst = (oldest + i) % MAX_UNREAD_MSGS;
      unread[dst] = unread[(dst + 1) % MAX_UNREAD_MSGS];
    }
    stored--;
    if (counted) {
      num_unread--;
      if (was_group) { if (groups > 0) groups--; }
      else if (chats > 0) chats--;
    }
    if (stored == 0) {
      num_unread = 0;
      groups = chats = 0;
      return true;
    }
    latest = (oldest + stored - 1) % MAX_UNREAD_MSGS;
    if (viewing) {
      head = oldest;
      resetPage();
    } else {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
    }
    return false;
  }

  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  int groupCount() const { return groups; }
  int chatCount() const { return chats; }
  int unreadCount() const { return num_unread; }
  int storedCount() const { return stored; }

  bool reopen() {
    if (stored == 0) return false;
    num_unread = stored;
    head = oldestUnread();
    groups = chats = 0;
    for (int i = 0; i < stored; i++) {
      int idx = (latest + MAX_UNREAD_MSGS - i) % MAX_UNREAD_MSGS;
      if (unread[idx].group) groups++;
      else chats++;
    }
    replay = true;
    resetPage();
    return true;
  }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg, bool group) {
    int prev_unread = num_unread;
    int slot = (latest + 1) % MAX_UNREAD_MSGS;
    bool full = stored == MAX_UNREAD_MSGS;
    bool head_overwritten = full && head == slot;
    if (full && num_unread == MAX_UNREAD_MSGS) {
      if (unread[slot].group) { if (groups > 0) groups--; }
      else if (chats > 0) chats--;
    } else if (!full) {
      stored++;
      num_unread++;
    } else if (num_unread < MAX_UNREAD_MSGS) {
      num_unread++;
    }
    latest = slot;
    replay = false;

    auto p = &unread[latest];
    p->group = group;
    if (group) groups++; else chats++;
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
    p->from.is_ch = the_mesh.ui_rx_is_ch;
    p->from.ch = the_mesh.ui_rx_ch;
    memcpy(p->from.key, the_mesh.ui_rx_key, sizeof(p->from.key));
#ifdef UI_MSG_TICKER
    if (_task->tickerEnabled()) {
      startTicker(p->origin, p->msg);   // the newest message runs across the screen first
    }
#endif
    // start at the oldest unread; a message that arrives mid-read waits at the end
    if (prev_unread == 0 || head_overwritten) {
      head = oldestUnread();
      resetPage();
    }
  }

  int render(DisplayDriver& display) override {
#ifdef UI_MSG_TICKER
    if (tk_active) {
      if (!display.isEink()) return renderTicker(display);
      tk_active = false;   // a running line makes no sense on e-ink
    }
#endif
    auto p = &unread[head];
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    int msg_len = strlen(filtered_msg);

    if (page_next > 0 || page_num > 0) {
      if ((long)(millis() - page_until) >= 0) nextPage();
    }
    if (page_start >= msg_len) resetPage();

    // short screens (128x64 OLED and alike) merge the sender into the title row to fit one more text line
    bool compact = display.height() < 100;
    int text_y = compact ? 14 : 25;

    display.setTextSize(1);
    display.setCursor(0, text_y);
    display.setColor(UIColor::primary_txt);
    const char* rest = display.printWordWrap(&filtered_msg[page_start], display.width());
    page_next = (*rest && rest > &filtered_msg[page_start]) ? (int)(rest - filtered_msg) : 0;
    bool paged = page_next > 0 || page_num > 0;

    char tmp[24];
    char age[8];
    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(age, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(age, "%dm", secs / 60);
    } else {
      sprintf(age, "%dh", secs / (60*60));
    }
    char page_tag[8] = "";
    if (paged) sprintf(page_tag, "p%d%s ", page_num + 1, page_next > 0 ? "+" : "");

    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));

    if (compact) {
      if (num_unread > 1) {
        sprintf(tmp, "%s+%d %s", page_tag, num_unread - 1, age);
      } else {
        sprintf(tmp, "%s%s", page_tag, age);
      }
      int right_w = display.getTextWidth(tmp);
      display.setColor(UIColor::corp_blue);
      display.setCursor(display.width() - right_w - 1, 0);
      display.print(tmp);
      display.setColor(UIColor::secondary_txt);
      display.drawTextEllipsized(0, 0, display.width() - right_w - 4, filtered_origin);
      display.setColor(UIColor::corp_blue);
      display.drawRect(0, 11, display.width(), 1);  // horiz line
    } else {
      display.setCursor(0, 0);
      display.setColor(UIColor::corp_blue);
      sprintf(tmp, "%s: %d", replay ? "Recent" : "Unread", num_unread);
      display.print(tmp);

      sprintf(tmp, "%s%s", page_tag, age);
      display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
      display.print(tmp);

      display.drawRect(0, 11, display.width(), 1);  // horiz line

      display.setColor(UIColor::secondary_txt);
      display.drawTextEllipsized(0, 14, display.width(), filtered_origin);
    }

    // "geo:" in the message and a GPS fix here: distance and direction on the
    // bottom row, e.g. "1.2 km СВ" - where the sender is from this device
    {
      uint32_t m;
      int brg;
      if (the_mesh.uiGeoFromHere(p->msg, m, brg)) {
        static const char* en8[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        static const char* ru8[] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};
        const char* dir = (_task->langRu() ? ru8 : en8)[((brg + 22) % 360) / 45];
        char where[32], shown[32];
        if (m < 1000) snprintf(where, sizeof(where), "%s %lu %s %s", _task->langRu() ? "до точки" : "to it",
                               (unsigned long)m, _task->langRu() ? "м" : "m", dir);
        else snprintf(where, sizeof(where), "%s %lu.%lu %s %s", _task->langRu() ? "до точки" : "to it",
                      (unsigned long)(m / 1000), (unsigned long)((m % 1000) / 100), _task->langRu() ? "км" : "km", dir);
        display.translateUTF8ToBlocks(shown, where, sizeof(shown));
        int y = display.height() - 10;
        display.setColor(UIColor::window_bkg);
        display.fillRect(0, y - 1, display.width(), 10);
        display.setColor(UIColor::corp_blue);
        display.drawRect(0, y - 2, display.width(), 1);
        display.setColor(UIColor::primary_txt);
        display.drawTextLeftAlign(0, y, shown);
      }
    }

    // A new message or text page replaces most of the ink. The partial wave
    // leaves the previous letters on the glass, so this frame is a full refresh.
    // The age counter alone stays partial.
    if (head != shown_head || page_start != shown_page) {
      display.markFull();
      shown_head = head;
      shown_page = page_start;
    }
#if AUTO_OFF_MILLIS==0 // probably e-ink
    if (paged) {
      long wait = (long)(page_until - millis());
      return wait > 200 ? wait : 200;
    }
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
#ifdef UI_MSG_TICKER
    if (tk_active) {   // any press stops the running line and shows the usual preview
      tk_active = false;
      shown_head = -1;
      resetPage();
      return true;
    }
#endif
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      bool was_group = unread[head].group;
      num_unread--;
      if (was_group) { if (groups > 0) groups--; }
      else if (chats > 0) chats--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      } else {
        head = (head + 1) % MAX_UNREAD_MSGS;  // older message first, then newer
        resetPage();
      }
      return true;
    }
    if (c == KEY_SELECT) {  // triple click: drop this message from the buffer
      dropHead();
      return true;
    }
    if (c == KEY_PREV || c == KEY_DOWN) {   // manual page flip (double click on single-button boards)
      if (page_next > 0 || page_num > 0) nextPage();
      return true;
    }
    if (c == KEY_UP) {
      resetPage();
      return true;
    }
    if (c == KEY_ENTER) {   // hold: reply to the message on screen
      ComposeScreen::Target to = unread[head].from;
      char re[sizeof(unread[head].msg)];
      StrHelper::strncpy(re, unread[head].msg, sizeof(re));   // before head moves on
      if (num_unread > 0) {     // it counts as read, like a click
        bool was_group = unread[head].group;
        num_unread--;
        if (was_group) { if (groups > 0) groups--; }
        else if (chats > 0) chats--;
        if (num_unread > 0) {
          head = (head + 1) % MAX_UNREAD_MSGS;
          resetPage();
        }
      }
      if (!_task->openReply(to, re)) {
        _task->showAlert("Can't reply", 1000);
        if (num_unread == 0) _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_QUAD) {    // four clicks: mark everything read
      num_unread = 0;
      groups = chats = 0;
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

static void clockDraw(DisplayDriver& display, const ClockFace& face, int x, int y, const char* text) {
  for (const char* s = text; *s; ) {
    uint16_t cp;
    s = clockNext(s, cp);
    ClockGlyph g;
    if (!clockFind(face, cp, g)) continue;
    if (g.w && g.h) display.blit1(x, y + g.top, g.w, g.h, face.bits + g.offset);
    x += g.advance;
  }
}

class ClockScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  MsgPreviewScreen* _preview;

  NodePrefs* _prefs;

  static void formatTime(uint32_t t, int16_t tz_mins, char* buf) {
    if (t < 1000000000UL) {
      strcpy(buf, "--:--");
      return;
    }
    int64_t local = (int64_t)t + (int64_t)tz_mins * 60;
    if (local < 0) local = 0;
    uint32_t mins = ((uint32_t)local / 60UL) % (24UL * 60UL);
    sprintf(buf, "%02u:%02u", (unsigned)(mins / 60UL), (unsigned)(mins % 60UL));
  }

public:
  ClockScreen(UITask* task, mesh::RTCClock* rtc, MsgPreviewScreen* preview, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _preview(preview), _prefs(prefs) {}

  int render(DisplayDriver& display) override {
    display.setColor(UIColor::primary_txt);
    int fw = display.frameWidth();
    int fh = display.frameHeight();
    const ClockFace& digits = (fw >= 220) ? CLOCK_DIGIT_WIDE : CLOCK_DIGIT_NARROW;

    char time_buf[8];
    uint32_t now = _rtc->getCurrentTime();
    int16_t tz = _prefs ? _prefs->tz_offset_mins : 0;
    formatTime(now, tz, time_buf);

    char date_buf[24] = "";
    int header_h = 0;
    if (now >= 1000000000UL) {
      formatClockDate(now, tz, _prefs ? _prefs->ui_lang : UI_LANG_RU, date_buf, sizeof(date_buf));
      int topd, botd;
      clockInk(CLOCK_HEADER, date_buf, topd, botd);
      int inkd = botd - topd;
      int y = 4 - topd;
      if (y < 1) y = 1;
      clockDraw(display, CLOCK_HEADER, (fw - clockWidth(CLOCK_HEADER, date_buf)) / 2, y, date_buf);
      header_h = inkd + 8;
    }

    char line1[24] = "";
    char line2[24] = "";
    int groups = _preview->groupCount();
    int chats = _preview->chatCount();
    bool en = _prefs && _prefs->ui_lang == UI_LANG_EN;
    if (groups > 0) sprintf(line1, en ? "Groups: %d" : "Группы: %d", groups);
    if (chats > 0) sprintf(line2, en ? "Chat: %d" : "Чаты: %d", chats);
    bool two = line1[0] && line2[0];
    char both[40];
    if (two) sprintf(both, "%s    %s", line1, line2);

    int footer_h = 0;
    if (line1[0] || line2[0]) {
      const char* shown = two ? both : (line1[0] ? line1 : line2);
      bool stack = two && clockWidth(CLOCK_FOOTER, both) > fw - 8;
      int top0, bot0, top1 = 0, bot1 = 0;
      clockInk(CLOCK_FOOTER, stack ? line1 : shown, top0, bot0);
      int ink = bot0 - top0;
      if (stack) {
        clockInk(CLOCK_FOOTER, line2, top1, bot1);
        ink += 4 + (bot1 - top1);
      }
      int y = fh - 6 - ink;
      if (!stack) {
        clockDraw(display, CLOCK_FOOTER, (fw - clockWidth(CLOCK_FOOTER, shown)) / 2, y - top0, shown);
      } else {
        clockDraw(display, CLOCK_FOOTER, (fw - clockWidth(CLOCK_FOOTER, line1)) / 2, y - top0, line1);
        int y2 = y + (bot0 - top0) + 4;
        clockDraw(display, CLOCK_FOOTER, (fw - clockWidth(CLOCK_FOOTER, line2)) / 2, y2 - top1, line2);
      }
      footer_h = ink + 12;
    }

    int top, bot;
    clockInk(digits, time_buf, top, bot);
    int ink = bot - top;
    int area = fh - footer_h - header_h;
    int y = header_h + (area - ink) / 2 - top;
    if (y < 2) y = 2;
    clockDraw(display, digits, (fw - clockWidth(digits, time_buf)) / 2, y, time_buf);

    if (now < 1000000000UL) return 60000;
    uint32_t sec = now % 60UL;
    int wait = (int)((60UL - sec) * 1000UL);
    if (wait < 1000) wait = 1000;
    return wait;
  }

  bool handleInput(char c) override {
    if (c == KEY_ENTER && _preview->reopen()) {
      _task->showMsgPreview();
      return true;
    }
    if (_preview->unreadCount() > 0) _task->showMsgPreview();
    else _task->gotoHomeScreen();
    return true;
  }
};

// Big digits (ChargeClockFont): ink width of a string such as "12:34" or "--:--".
static int bigInkWidth(const ClockFace& face, const char* text) {
  int w = 0, last_gap = 0;
  for (const char* s = text; *s; s++) {
    ClockGlyph g;
    if (!clockFind(face, (uint8_t)(*s == '-' ? '0' : *s), g)) continue;
    w += g.advance;
    last_gap = g.advance - g.w;
  }
  return w - last_gap;
}

// '-' (no time yet) is a bar across a digit cell
static void bigDraw(DisplayDriver& display, const ClockFace& face, int x, int y, const char* text) {
  for (const char* s = text; *s; s++) {
    ClockGlyph g;
    if (*s == '-') {
      if (!clockFind(face, '0', g)) continue;
      int t = g.h / 12 > 3 ? g.h / 12 : 3;
      display.fillRectPx(x, y + (g.h - t) / 2, g.w, t);
      x += g.advance;
      continue;
    }
    if (!clockFind(face, (uint8_t)*s, g)) continue;
    if (g.w && g.h) display.blit1(x, y + g.top, g.w, g.h, face.bits + g.offset);
    x += g.advance;
  }
}

// The largest face whose digits fit in max_w x max_h physical pixels.
static const ClockFace& bigFace(const char* text, int max_w, int max_h) {
  const int n = sizeof(CHARGE_DIGIT_FACES) / sizeof(CHARGE_DIGIT_FACES[0]);
  for (int i = 0; i < n; i++) {
    ClockGlyph g;
    if (!clockFind(*CHARGE_DIGIT_FACES[i], '0', g)) continue;
    if (g.h <= max_h && bigInkWidth(*CHARGE_DIGIT_FACES[i], text) <= max_w) return *CHARGE_DIGIT_FACES[i];
  }
  return *CHARGE_DIGIT_FACES[n - 1];
}

// Draws text centred between the corner bands; x_shift moves it sideways.
static void bigDrawCentred(DisplayDriver& display, const char* text, int band, int x_shift = 0) {
  int fw = display.frameWidth(), fh = display.frameHeight();
  int area = fh - 2 * band;
  const ClockFace& face = bigFace(text, fw - 2, area);
  ClockGlyph g0;
  clockFind(face, '0', g0);
  int x = (fw - bigInkWidth(face, text)) / 2;
  int room = x < 2 ? x : 2;
  if (x_shift > room) x_shift = room;
  if (x_shift < -room) x_shift = -room;
  bigDraw(display, face, x + x_shift, band + (area - g0.h) / 2, text);
}

// Big clock: on the charger (boards that sense USB power) or always (boards
// that can't). HH:MM fills the screen between two rows of corners:
//   weekday            battery % / new messages
//          HH:MM
//   day                month
// At the top of every hour the screen flashes inverted three times.
class ChargeClockScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  MsgPreviewScreen* _preview;
  NodePrefs* _prefs;
  int _flash_hour = -1;               // the hour whose flash already ran
  unsigned long _flash_start = 0;
  bool _flashing = false;

public:
  ChargeClockScreen(UITask* task, mesh::RTCClock* rtc, MsgPreviewScreen* preview, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _preview(preview), _prefs(prefs) {}

  int render(DisplayDriver& display) override {
    int fw = display.frameWidth(), fh = display.frameHeight();
    int W = display.width(), H = display.height();
    bool en = _prefs && _prefs->ui_lang == UI_LANG_EN;

    uint32_t now = _rtc->getCurrentTime();
    bool valid = now >= 1000000000UL;
    int64_t local = (int64_t)now + (int64_t)(_prefs ? _prefs->tz_offset_mins : 0) * 60;
    if (local < 0) local = 0;
    uint32_t lt = (uint32_t)local;
    int hh = (int)((lt / 3600UL) % 24UL), mm = (int)((lt / 60UL) % 60UL), ss = (int)(lt % 60UL);

    // top of the hour: start the flash once per hour
    if (valid && mm == 0 && ss < 30 && _flash_hour != hh) {
      _flash_hour = hh;
      _flash_start = millis();
      _flashing = true;
    }
    bool inverted = false;
    int next = 0;
    if (_flashing) {
      unsigned long el = millis() - _flash_start;
      int step = (int)(el / CHARGE_FLASH_STEP_MILLIS);
      if (step >= CHARGE_FLASH_STEPS) {
        _flashing = false;
      } else {
        inverted = (step % 2) == 0;
        next = CHARGE_FLASH_STEP_MILLIS - (int)(el % CHARGE_FLASH_STEP_MILLIS);
      }
    }
    if (inverted) {
      display.setColor(UIColor::primary_txt);
      display.fillRectPx(0, 0, fw, fh);
    }
    display.setColor(inverted ? UIColor::window_bkg : UIColor::primary_txt);

    // corners, in the logical grid; a row of text is 9 of 64 rows
    display.setTextSize(1);
    int band = (9 * fh + H - 1) / H;
    char buf[16];
    if (valid) {
      int year, month, day, wday;
      clockYmdFromUnix(lt, year, month, day, wday);
      static const char* wd_en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      static const char* wd_ru[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
      display.drawTextLeftAlign(0, 0, en ? wd_en[wday] : wd_ru[wday]);
      sprintf(buf, "%02d", day);
      display.drawTextLeftAlign(0, H - 9, buf);
      sprintf(buf, "%02d", month);
      display.drawTextRightAlign(W - 1, H - 9, buf);
      (void)year;
    }
    int unread = _preview ? _preview->unreadCount() : 0;
    if (unread > 0) {
      sprintf(buf, en ? "new: %d" : "нов: %d", unread);
    } else {
      int mv = _task->getBattMilliVolts();
      int pct = (mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      sprintf(buf, "%d%%", pct);
      if (mv <= 0) buf[0] = 0;
    }
    if (buf[0]) display.drawTextRightAlign(W - 1, 0, buf);

    // HH:MM: the largest face that fits between the corner rows
    char t[6];
    if (valid) sprintf(t, "%02d:%02d", hh, mm);
    else strcpy(t, "--:--");
    // OLED burn-in: the digits wander a couple of pixels sideways, one step a minute
    bigDrawCentred(display, t, band, mm % 5 - 2);
    display.markFull();

    if (next > 0) return next;
    if (!valid) return 60000;
    int wait = (60 - ss) * 1000;
    return wait < 1000 ? 1000 : wait;
  }

  bool handleInput(char c) override {
    if (_preview && _preview->unreadCount() > 0) _task->showMsgPreview();
    else _task->gotoHomeScreen();
    return true;
  }
};

// Stopwatch and timer, one button. Six clicks or more open it (the Write
// keyboard keeps its own five-click key). The stopwatch works like a watch:
//   stopwatch: counts from the moment it opens.
//              press - stop / go on from the same time,
//              double - running: split (the shown time stops, the count goes on;
//                       double again shows it running), stopped: reset to 00:00,
//              triple - exit, it keeps counting (or stays stopped); six clicks
//                       or a hold on the time view (four clicks) bring it back,
//              hold - timer (the stopwatch keeps its time)
//   timer set: press +1 min, double +5 min, hold - start, triple - exit
//   timer run: press - exit (it keeps running), hold - cancel
//   time is up: the screen flashes (and beeps where there is a buzzer); press - exit
class TimerScreen : public UIScreen {
public:
  enum Mode { SW, SET, RUN, DONE };
private:
  UITask* _task;
  Mode _mode = SW;
  bool _sw_run = false;            // counting now
  bool _sw_split = false;          // the shown time is held, the count goes on
  unsigned long _sw_start = 0;     // millis() when the current run began
  unsigned long _sw_acc = 0;       // ms counted before the current run
  unsigned long _sw_split_ms = 0;
  unsigned long _end = 0;          // timer: millis() when it rings
  unsigned long _rang_at = 0;
  unsigned long _next_beep = 0;
  int _minutes = 1;

public:
  static void fmt(char* buf, unsigned long secs) {
    unsigned long h = secs / 3600UL, m = (secs / 60UL) % 60UL, sec = secs % 60UL;
    if (h > 0) sprintf(buf, "%lu:%02lu:%02lu", h, m, sec);
    else sprintf(buf, "%02lu:%02lu", m, sec);
  }

  TimerScreen(UITask* task) : _task(task) {}

  unsigned long swElapsed() const { return _sw_acc + (_sw_run ? millis() - _sw_start : 0UL); }
  bool swRunning() const { return _sw_run; }
  bool swSplit() const { return _sw_run && _sw_split; }
  bool swActive() const { return _sw_run || _sw_acc > 0; }   // running or stopped on a time
  bool timerRunning() const { return _mode == RUN; }
  unsigned long timerLeft() const {
    long left = (long)(_end - millis());
    return left > 0 ? (unsigned long)left : 0UL;
  }
  // the screen stays lit while something counts; a stopped stopwatch may go dark
  bool keepsAwake() const { return _mode != SW || _sw_run; }

  // six clicks: a running timer or a stopwatch with a time is shown, otherwise a fresh stopwatch starts
  void open() {
    if (_mode == RUN || _mode == DONE) return;
    _mode = SW;
    if (swActive()) return;
    _sw_acc = 0;
    _sw_split = false;
    _sw_start = millis();
    _sw_run = true;
  }

#ifdef UI_MSG_TICKER
  // sport view: MM:SS in dots with the milliseconds next to it in smaller dots
  static void drawSport(DisplayDriver& display, const char* t, unsigned long ms, int band) {
    uint8_t big[48], small[24];
    char m[5];
    sprintf(m, ".%03lu", ms % 1000UL);
    int nb = tickerBuildDigits(t, big, sizeof(big));
    int ns = tickerBuildDigits(m, small, sizeof(small));
    int fw = display.frameWidth(), fh = display.frameHeight();
    int area = fh - 2 * band;
    for (int d = 16; d >= 2; d--) {
      int sd = (d + 1) / 2;
      int gap = sd;
      int w = nb * d + gap + ns * sd;
      if (7 * d > area || w > fw - 2) continue;
      int x = (fw - w) / 2;
      int y = band + (area - 7 * d) / 2;
      int lit = d >= 4 ? d - 1 : d;
      int slit = sd >= 4 ? sd - 1 : sd;
      tickerDraw(display, big, nb, x, y, d, lit);
      tickerDraw(display, small, ns, x + nb * d + gap, y + 7 * d - 7 * sd, sd, slit);
      return;
    }
    bigDrawCentred(display, t, band);
  }
#endif
  // the timer ran out while another screen was up
  bool due() const { return _mode == RUN && (long)(millis() - _end) >= 0; }

  int render(DisplayDriver& display) override {
    bool ru = _task->langRu();
    bool eink = _task->isEink();
    int fw = display.frameWidth(), fh = display.frameHeight();
    int W = display.width(), H = display.height();
    int band = (9 * fh + H - 1) / H;
    unsigned long now = millis();

    if (_mode == RUN && (long)(now - _end) >= 0) {
      _mode = DONE;
      _rang_at = now;
      _next_beep = now;
    }

    bool inverted = false;
    if (_mode == DONE) {
      unsigned long el = now - _rang_at;
      if (el < 60000UL) {                  // a minute of flashing, then it stays quiet
        inverted = !eink && (el / 500UL) % 2 == 0;
        if ((long)(now - _next_beep) >= 0) {
          _task->notify(UIEventType::contactMessage);
          _next_beep = now + 2000;
        }
      }
    }
    if (inverted) {
      display.setColor(UIColor::primary_txt);
      display.fillRectPx(0, 0, fw, fh);
    }
    display.setColor(inverted ? UIColor::window_bkg : UIColor::primary_txt);
    display.setTextSize(1);

    const char* title;
    const char* hint;
    char t[12];
    int wait = 1000;
    switch (_mode) {
      case SW: {
        unsigned long ms = _sw_split ? _sw_split_ms : swElapsed();
        fmt(t, ms / 1000UL);
        // the hint takes turns: what the presses do now, then how to leave
        bool exit_hint = ((now / 3000UL) % 2UL) == 1UL;
        if (!_sw_run) {
          title = ru ? "Стоп" : "Stopped";
          hint = exit_hint ? (ru ? "3х выход, держ. таймер" : "3x exit  hold: timer")
                           : (ru ? "1х пуск  2х сброс" : "1x go  2x reset");
        } else if (_sw_split) {
          title = ru ? "Круг" : "Split";
          hint = exit_hint ? (ru ? "3х выход, счёт идёт" : "3x exit, keeps going")
                           : (ru ? "1х стоп  2х снять круг" : "1x stop  2x release");
        } else {
          title = ru ? "Секундомер" : "Stopwatch";
          hint = exit_hint ? (ru ? "3х выход, счёт идёт" : "3x exit, keeps going")
                           : (ru ? "1х стоп  2х круг" : "1x stop  2x split");
        }
        int hint_wait = 3000 - (int)(now % 3000UL);
        bool moving = _sw_run && !_sw_split;
#ifdef UI_MSG_TICKER
        if (!eink && _task->swSport()) {
          display.drawTextLeftAlign(0, 0, title);
          display.drawTextCentered(W / 2, H - 9, hint);
          drawSport(display, t, ms, band);
          display.markFull();
          return moving ? 47 : hint_wait;   // milliseconds keep running
        }
#endif
        if (!eink) {   // tenths in the top-right corner
          char d[4];
          sprintf(d, ".%lu", (ms / 100UL) % 10UL);
          display.drawTextRightAlign(W - 1, 0, d);
          wait = moving ? 100 - (int)(ms % 100UL) : hint_wait;
        } else {
          wait = moving ? 1000 - (int)(ms % 1000UL) : hint_wait;
        }
        if (wait > hint_wait) wait = hint_wait;
        break;
      }
      case SET:
        sprintf(t, "%02d:00", _minutes);
        title = ru ? "Таймер" : "Timer";
        hint = ru ? "клик +1, держать: старт" : "click +1, hold: start";
        wait = 60000;
        break;
      case RUN: {
        unsigned long left = _end - now;
        fmt(t, (left + 999UL) / 1000UL);
        title = ru ? "Таймер" : "Timer";
        hint = ru ? "держать: отмена" : "hold: cancel";
        wait = (int)(left % 1000UL) + 1;
        break;
      }
      default:
        strcpy(t, "00:00");
        title = ru ? "Время вышло!" : "Time is up!";
        hint = ru ? "нажать: выход" : "press: exit";
        wait = 250;
        break;
    }
    if (eink && wait < 5000 && _mode != DONE) wait = 5000;   // e-ink: no second-by-second refresh
    display.drawTextLeftAlign(0, 0, title);
    display.drawTextCentered(W / 2, H - 9, hint);
    bigDrawCentred(display, t, band);
    display.markFull();
    return wait;
  }

  bool handleInput(char c) override {
    bool ru = _task->langRu();
    switch (_mode) {
      case SW:
        if (c == KEY_NEXT || c == KEY_RIGHT) {   // press: stop / go on
          if (_sw_run) {
            _sw_acc = swElapsed();
            _sw_run = false;
            _sw_split = false;
          } else {
            _sw_start = millis();
            _sw_run = true;
          }
          return true;
        }
        if (c == KEY_PREV || c == KEY_LEFT) {    // double: split / release, or reset when stopped
          if (_sw_run) {
            _sw_split = !_sw_split;
            if (_sw_split) _sw_split_ms = swElapsed();
          } else {
            _sw_acc = 0;
          }
          return true;
        }
        if (c == KEY_ENTER) { _mode = SET; _minutes = 1; return true; }   // the stopwatch keeps its time
        // triple / quad: leave; a running stopwatch keeps counting, a stopped one keeps its time
        if (_sw_run) _task->showAlert(ru ? "Секундомер идёт" : "Stopwatch runs", 900);
        _task->gotoHomeScreen();
        return true;
      case SET:
        if (c == KEY_NEXT || c == KEY_RIGHT) { _minutes = _minutes >= 99 ? 1 : _minutes + 1; return true; }
        if (c == KEY_PREV || c == KEY_LEFT) { _minutes = _minutes + 5 > 99 ? 99 : _minutes + 5; return true; }
        if (c == KEY_ENTER) {
          _mode = RUN;
          _end = millis() + (unsigned long)_minutes * 60000UL;
          _task->notify(UIEventType::ack);
          return true;
        }
        _mode = SW;   // triple / quad: leave without a timer
        _task->gotoHomeScreen();
        return true;
      case RUN:
        if (c == KEY_ENTER) {
          _mode = SW;
          _task->notify(UIEventType::ack);
        }
        _task->gotoHomeScreen();
        return true;
      default:   // DONE
        _mode = SW;
        _task->gotoHomeScreen();
        return true;
    }
  }
};

// Time at a glance: four clicks on the menu pages (not Beacon, where four
// stop the beacon). Weekday and date on top, HH:MM:SS big in the middle, a
// line below for the stopwatch and one for the timer while they count or hold
// a time. Hold - open the stopwatch / timer (when there is one), any other
// press or 15 s without presses - back to the same menu page.
#ifndef GLANCE_MILLIS
  #define GLANCE_MILLIS 15000UL
#endif
class GlanceScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  TimerScreen* _sw;
  unsigned long _opened = 0;
public:
  GlanceScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* prefs, TimerScreen* sw)
    : _task(task), _rtc(rtc), _prefs(prefs), _sw(sw) {}

  void open() { _opened = millis(); }

  void poll() override {
    if ((long)(millis() - _opened) >= (long)GLANCE_MILLIS) _task->gotoHomeScreen();
  }

  int render(DisplayDriver& display) override {
    bool ru = _task->langRu();
    bool eink = _task->isEink();
    int fw = display.frameWidth(), fh = display.frameHeight();
    int W = display.width(), H = display.height();
    int band = (9 * fh + H - 1) / H;
    display.setColor(UIColor::primary_txt);
    display.setTextSize(1);

    uint32_t now = _rtc->getCurrentTime();
    bool valid = now >= 1000000000UL;
    int64_t local = (int64_t)now + (int64_t)(_prefs ? _prefs->tz_offset_mins : 0) * 60;
    if (local < 0) local = 0;
    uint32_t lt = (uint32_t)local;
    char buf[32];

    // top row: weekday and date left, battery right
    if (valid) {
      int year, month, day, wday;
      clockYmdFromUnix(lt, year, month, day, wday);
      static const char* wd_en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      static const char* wd_ru[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
      sprintf(buf, "%s %02d.%02d.%04d", ru ? wd_ru[wday] : wd_en[wday], day, month, year);
      display.drawTextLeftAlign(0, 0, buf);
    } else {
      display.drawTextLeftAlign(0, 0, ru ? "время не задано" : "no time set");
    }
    int mv = _task->getBattMilliVolts();
    if (mv > 0) {
      int pct = (mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      sprintf(buf, "%d%%", pct);
      display.drawTextRightAlign(W - 1, 0, buf);
    }

    // bottom rows: stopwatch and timer, when there are any
    int rows = 0;
    bool fast = false;
    if (_sw && _sw->timerRunning()) {
      char t[12];
      TimerScreen::fmt(t, (_sw->timerLeft() + 999UL) / 1000UL);
      sprintf(buf, ru ? "Таймер  %s" : "Timer  %s", t);
      rows++;
      display.drawTextCentered(W / 2, H - 9 * rows, buf);
      fast = true;
    }
    if (_sw && _sw->swActive()) {
      char t[12];
      TimerScreen::fmt(t, _sw->swElapsed() / 1000UL);
      const char* state = !_sw->swRunning() ? (ru ? " стоп" : " stop") : "";
      sprintf(buf, ru ? "Секундомер  %s%s" : "Stopwatch  %s%s", t, state);
      rows++;
      display.drawTextCentered(W / 2, H - 9 * rows, buf);
      if (_sw->swRunning()) fast = true;
    }

    // the clock: HH:MM:SS (HH:MM on e-ink), as big as fits between the rows
    char t[10];
    uint32_t hh = (lt / 3600UL) % 24UL, mm = (lt / 60UL) % 60UL, ss = lt % 60UL;
    if (!valid) strcpy(t, eink ? "--:--" : "--:--:--");
    else if (eink) sprintf(t, "%02lu:%02lu", (unsigned long)hh, (unsigned long)mm);
    else sprintf(t, "%02lu:%02lu:%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    int bottom = rows > 0 ? (9 * rows * fh + H - 1) / H : band;
    int area = fh - band - bottom;
    const ClockFace& face = bigFace(t, fw - 2, area);
    ClockGlyph g0;
    clockFind(face, '0', g0);
    bigDraw(display, face, (fw - bigInkWidth(face, t)) / 2, band + (area - g0.h) / 2, t);
    display.markFull();

    if (eink) return fast ? 5000 : 30000;
    return 1000 - (int)(millis() % 1000UL);
  }

  bool handleInput(char c) override {
    if (c == KEY_ENTER && _sw && (_sw->swActive() || _sw->timerRunning())) {
      _task->openStopwatch();
      return true;
    }
    _task->gotoHomeScreen();
    return true;
  }
};

#ifdef UI_MSG_TICKER
// Badge for a show: ten clicks or more. The line from `set badge` (the node
// name until one is set) first fills the screen width in one line, then after
// BADGE_STILL_MS runs across in big dots, and again. Any press leaves.
#ifndef BADGE_STILL_MS
  #define BADGE_STILL_MS  2500
#endif
class BadgeScreen : public UIScreen {
  UITask* _task;
  uint8_t _cols[BADGE_MAX_COLS];
  int _len = 0;
  int _pos = 0;
  bool _running = false;
  unsigned long _still_at = 0;
public:
  BadgeScreen(UITask* task) : _task(task) {}

  void open() {
    _len = tickerBuild(the_mesh.uiBadge(), _cols, BADGE_MAX_COLS);
    _running = false;
    _still_at = millis();
  }

  int render(DisplayDriver& display) override {
    _task->keepDisplayAwake();
    int fw = display.frameWidth(), fh = display.frameHeight();
    display.setColor(UIColor::primary_txt);
    if (_len == 0) {
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, display.height() / 2 - 4, "set badge <text>");
      return 1000;
    }
    int run_dot = fh / 7;
    if (run_dot > TICKER_DOT * 2) run_dot = TICKER_DOT * 2;
    if (run_dot < 2) run_dot = 2;

    if (!_running) {   // the whole line across the screen
      int dot = (fw - 2) / _len;
      if (dot * 7 > fh) dot = fh / 7;
      if (dot < 1) dot = 1;
      int lit = dot > TICKER_DOT_GAP + 2 ? dot - TICKER_DOT_GAP : dot;
      int x0 = (fw - _len * dot) / 2;
      if (x0 < 0) x0 = 0;
      tickerDraw(display, _cols, _len, x0, (fh - 7 * dot) / 2, dot, lit);
      display.markFull();
      // e-ink, or the line already as big as it would run: it just stays
      if (_task->isEink() || dot >= run_dot) return 60000;
      unsigned long el = millis() - _still_at;
      if (el < BADGE_STILL_MS) return (int)(BADGE_STILL_MS - el);
      _running = true;
      _pos = 0;
      return 50;
    }

    int lit = run_dot > TICKER_DOT_GAP + 1 ? run_dot - TICKER_DOT_GAP : run_dot;
    int screen_cols = (fw + run_dot - 1) / run_dot;
    int x0 = (fw - screen_cols * run_dot) / 2;
    int y0 = (fh - 7 * run_dot) / 2;
    int from = _pos - screen_cols;   // text enters from the right edge
    int first = from < 0 ? -from : 0;
    if (first < screen_cols) {
      int n = screen_cols - first;
      if (from + first + n > _len) n = _len - (from + first);
      if (n > 0) tickerDraw(display, _cols + from + first, n, x0 + first * run_dot, y0, run_dot, lit);
    }
    display.markFull();
    if (++_pos > _len + screen_cols) {   // gone: show the whole line again
      _running = false;
      _still_at = millis();
      return 50;
    }
    return TICKER_STEP_MS;
  }

  bool handleInput(char c) override {
    (void)c;
    _task->gotoHomeScreen();
    return true;
  }
};
#endif

// SOS: a 5 s hold anywhere (but in a game) arms it; after a 5 s countdown it
// sends "SOS! geo:..." to the SOS channel and every favourite, then again every
// 2 minutes. A press during the countdown cancels; a press later stops SOS and
// sends "all clear".
class SosScreen : public UIScreen {
  UITask* _task;
  unsigned long _armed_at = 0;
public:
  SosScreen(UITask* task) : _task(task) {}
  void arm() { _armed_at = millis(); }

  void poll() override {
    if (!the_mesh.sosActive() && _armed_at && (long)(millis() - _armed_at) >= 5000) {
      _armed_at = 0;
      the_mesh.sosStart();
      _task->notify(UIEventType::contactMessage);
    }
  }

  int render(DisplayDriver& display) override {
    bool ru = _task->langRu();
    int W = display.width(), H = display.height();
    bool on = the_mesh.sosActive();
    bool blink = (millis() / 500) % 2 == 0;
    if (on && blink) {   // the whole screen flashes while SOS is on
      display.setColor(UIColor::primary_txt);
      display.fillRect(0, 0, W, H);
      display.setColor(UIColor::window_bkg);
    } else {
      display.setColor(UIColor::primary_txt);
    }
    display.setTextSize(2);
    display.drawTextCentered(W / 2, 4, "SOS");
    display.setTextSize(1);
    char buf[48], shown[48];
    if (!on) {
      long left = 5 - (long)(millis() - _armed_at) / 1000;
      if (left < 0) left = 0;
      snprintf(buf, sizeof(buf), ru ? "отправка через %ld с" : "sending in %ld s", left);
      display.translateUTF8ToBlocks(shown, buf, sizeof(shown));
      display.drawTextCentered(W / 2, 28, shown);
      display.translateUTF8ToBlocks(shown, ru ? "нажать: отмена" : "press: cancel", sizeof(shown));
      display.drawTextCentered(W / 2, H - 11, shown);
      return 250;
    }
    char who[40];
    the_mesh.sosTargets(who, sizeof(who));
    snprintf(buf, sizeof(buf), ru ? "отправлено: %u, след. %lu с" : "sent: %u, next %lu s",
             the_mesh.sosRounds(), (unsigned long)the_mesh.sosNextSecs());
    display.translateUTF8ToBlocks(shown, buf, sizeof(shown));
    display.drawTextCentered(W / 2, 24, shown);
    display.translateUTF8ToBlocks(shown, who, sizeof(shown));
    display.drawTextCentered(W / 2, 35, shown);
    display.translateUTF8ToBlocks(shown, ru ? "нажать: стоп" : "press: stop", sizeof(shown));
    display.drawTextCentered(W / 2, H - 11, shown);
    return 500;
  }

  bool handleInput(char c) override {
    (void)c;
    if (the_mesh.sosActive()) {
      the_mesh.sosStop();
      _task->showAlert(_task->langRu() ? "SOS: отбой отправлен" : "SOS: all clear sent", 1500);
    }
    _armed_at = 0;
    _task->gotoHomeScreen();
    return true;
  }
};

void UITask::openGames() {
  if (!games) return;
  ((GamesScreen*)games)->enter();
  setCurrScreen(games);
}

void UITask::openStopwatch() {
  if (!stopwatch) return;
  ((TimerScreen*)stopwatch)->open();
  setCurrScreen(stopwatch);
}

void UITask::openGlance() {
  if (!glance) return;
  ((GlanceScreen*)glance)->open();
  setCurrScreen(glance);
}

void UITask::openBadge() {
#ifdef UI_MSG_TICKER
  if (!badge) return;
  ((BadgeScreen*)badge)->open();
  setCurrScreen(badge);
#endif
}

void UITask::toggleTicker() {
#ifdef UI_MSG_TICKER
  _node_prefs->msg_ticker = _node_prefs->msg_ticker ? 0 : 1;
  the_mesh.savePrefs();
  notify(UIEventType::ack);
  showAlert(_node_prefs->msg_ticker ? "Msg scroll: ON" : "Msg scroll: OFF", 800);
  _next_refresh = 0;
#endif
}

bool UITask::reopenMsgPreview() {
  if (!((MsgPreviewScreen *) msg_preview)->reopen()) return false;
  showMsgPreview();
  return true;
}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  clock = (display != NULL && display->isEink())
      ? new ClockScreen(this, &rtc_clock, (MsgPreviewScreen*)msg_preview, node_prefs) : NULL;
  stopwatch = display != NULL ? new TimerScreen(this) : NULL;
  glance = stopwatch != NULL ? new GlanceScreen(this, &rtc_clock, node_prefs, (TimerScreen*)stopwatch) : NULL;
#ifdef UI_MSG_TICKER
  badge = display != NULL ? new BadgeScreen(this) : NULL;
#endif
  games = display != NULL ? new GamesScreen(this) : NULL;
  sos = display != NULL ? new SosScreen(this) : NULL;
  charge_clock = (display != NULL && !display->isEink())
      ? new ChargeClockScreen(this, &rtc_clock, (MsgPreviewScreen*)msg_preview, node_prefs) : NULL;
#ifdef BOOK_READER
  reader = display != NULL ? new ReaderScreen(this, &book_store) : NULL;
#endif
  compose = display != NULL ? new ComposeScreen(this) : NULL;
  sent = display != NULL ? new SentScreen(this) : NULL;
  relays = display != NULL ? new RelayScreen(this) : NULL;
  _idle_since = millis();
  syncDisplayRefresh();
  setCurrScreen(splash);
}

void UITask::openCompose() {
  if (!compose) return;
  ((ComposeScreen*)compose)->enter();
  setCurrScreen(compose);
}

bool UITask::openReply(const ComposeTarget& to, const char* re) {
  if (!compose) return false;
  if (!((ComposeScreen*)compose)->enterTo(to, re)) return false;
  setCurrScreen(compose);
  return true;
}

bool UITask::openWriteTo(const ComposeTarget& to) {
  return openReply(to, NULL);
}

void UITask::openRelays() {
  if (!relays) return;
  ((RelayScreen*)relays)->enter();
  setCurrScreen(relays);
}

void UITask::openSent() {
  if (!sent) return;
  ((SentScreen*)sent)->enter();
  setCurrScreen(sent);
}

bool UITask::onUiAck(const uint8_t* ack, uint8_t* key_out) {
  SentEntry* e = _sent_log.ack(ack);
  if (e == NULL) return false;
  memcpy(key_out, e->to.key, 6);
  if (curr == sent) _next_refresh = 100;
  return true;
}

void UITask::onUiRetry(uint32_t old_ack, uint32_t new_ack, unsigned long until) {
  _sent_log.retry(old_ack, new_ack, until);
  if (curr == sent || curr == compose) _next_refresh = 100;
}

int UITask::getUnreadCount() const {
  if (!msg_preview) return 0;
  return ((MsgPreviewScreen *) msg_preview)->unreadCount();
}

void UITask::openReader() {
#ifdef BOOK_READER
  if (!reader) return;
  ((ReaderScreen*)reader)->enter();
  setCurrScreen(reader);
#endif
}

void UITask::showAlert(const char* text, int duration_millis) {
  // A short plaque appears and then disappears, so e-ink pays for two full refreshes.
  if (_display && _display->isEink() && duration_millis <= 2000) return;
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::keepDisplayAwake() {
#if AUTO_OFF_MILLIS > 0
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


int UITask::getDeviceStored() const {
  if (!msg_preview) return 0;
  return ((MsgPreviewScreen *) msg_preview)->storedCount();
}

void UITask::clearTakenMsg() {
  // The phone took a message. The copy on this device stays unread until it is
  // read or dismissed with the button: neither the phone nor the running line
  // marks it read here.
  if (!msg_preview) return;
  if (curr == msg_preview || curr == clock || curr == charge_clock || curr == home) {
    _next_refresh = 100;
  }
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  // The phone drains the offline queue as soon as a message arrives. That must
  // not dismiss the e-ink clock; the footer count already updated in newMsg.
  // an open message window stays open: only button presses read it
  if (msgcount == 0 && curr != clock && curr != charge_clock && curr != msg_preview && !inReader() && !inCompose() && !inSent() && !inRelays() && !inStopwatch() && !inGlance() && !inBadge() && !inGames() && !inSos()) {
    gotoHomeScreen();
  } else if (curr == home) {
    _next_refresh = 100;
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, bool group) {
  _msgcount = msgcount;

  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text, group);
  _idle_since = millis();   // the big clock waits before covering the new message
  if (curr == clock || inReader() || inCompose() || inSent() || inRelays() || inStopwatch() || inBadge() || inGames() || inSos()) {   // reader, writer and Sent only show an envelope
    _next_refresh = 100;
  } else {
    setCurrScreen(msg_preview);
  }

  if (_display != NULL) {
    // with the running line on, every incoming message wakes the screen
    if (!_display->isOn() && (tickerEnabled() || !hasConnection())) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

bool UITask::canSensePower() const {
#ifdef NRF52_PLATFORM
  return true;    // USB VBUS is read from the nRF52 power block
#else
  return false;   // ESP32 boards: isExternalPowered() is always false
#endif
}

// Big clock. Where USB power can be sensed: on the charger the clock stays
// lit; on battery the screen lights up for a moment at every new minute.
// Elsewhere the clock stays lit whenever the setting is on.
void UITask::updateChargeClock() {
#if AUTO_OFF_MILLIS > 0
  if (charge_clock == NULL || _display == NULL || _node_prefs == NULL) return;
  if (_node_prefs->charge_clock == 0) {
    if (curr == charge_clock) gotoHomeScreen();
    return;
  }
  bool steady = !canSensePower() || (_board != NULL && _board->isExternalPowered());
  bool idle = (long)(millis() - _idle_since) >= (long)AUTO_OFF_MILLIS;
  // never pull the user out of writing, reading or a settings page
  bool calm = curr == charge_clock || curr == home || curr == msg_preview;
  if (!calm) return;

  if (steady) {
    if (curr != charge_clock) {
      if (!idle) return;
      setCurrScreen(charge_clock);
    }
    if (!_display->isOn()) _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
    return;
  }

  // on battery: a short look once a minute while the screen is dark
  uint32_t now = rtc_clock.getCurrentTime();
  if (now < 1000000000UL) return;
  uint32_t minute = now / 60UL;
  if (_display->isOn() || (curr != charge_clock && !idle)) {
    _glance_minute = minute;
    return;
  }
  if (minute == _glance_minute) return;
  _glance_minute = minute;
  setCurrScreen(charge_clock);
  _display->turnOn();
  _auto_off = millis() + CHARGE_CLOCK_GLANCE_MILLIS;
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  if (curr == compose && c != compose && compose) ((ComposeScreen*)compose)->onLeave();
  curr = c;
  _next_refresh = 100;
  if (c == home) _idle_since = millis();
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    // Power off board including radio, display, GPS and components
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  {
    // the Write page types on a hold, so the hold is shorter there
    static int base_long = -1;
    if (base_long < 0) base_long = user_btn.getLongMillis();
    user_btn.setLongMillis(inCompose() && base_long > 0 ? COMPOSE_HOLD_MILLIS : base_long);
  }
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  } else if (ev == BUTTON_EVENT_QUAD_CLICK) {
    c = handleQuadClick(KEY_QUAD);
  } else if (ev == BUTTON_EVENT_PENTA_CLICK) {   // only the Write keyboard tells five from four
    c = handleQuadClick((inCompose() || inSent()) ? KEY_PENTA : KEY_QUAD);
  } else if (ev == BUTTON_EVENT_HEXA_CLICK) {    // six to nine: stopwatch (the keyboard keeps its five)
    c = handleManyClicks();
  } else if (ev == BUTTON_EVENT_MANY_CLICK) {    // ten or more: badge
    c = handleManyClicks(true);
  }
#endif
#if defined(UI_HAS_ROTARY_INPUT)
  RotaryInputEvent rotaryEv = rotary_input.poll();
  if (c == 0 && _display != NULL && _display->isOn()) {
    if (rotaryEv == RotaryInputEvent::Next) {
      c = KEY_NEXT;
    } else if (rotaryEv == RotaryInputEvent::Prev) {
      c = KEY_PREV;
    }
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    } else if (ev == BUTTON_EVENT_QUAD_CLICK) {
      c = handleQuadClick(KEY_QUAD);
    } else if (ev == BUTTON_EVENT_PENTA_CLICK) {
      c = handleQuadClick((inCompose() || inSent()) ? KEY_PENTA : KEY_QUAD);
    } else if (ev == BUTTON_EVENT_HEXA_CLICK) {
      c = handleManyClicks();
    } else if (ev == BUTTON_EVENT_MANY_CLICK) {
      c = handleManyClicks(true);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    _idle_since = millis();
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  updateChargeClock();

#ifdef PIN_USER_BTN
  {   // SOS: the button held 5 s on any screen but a game
    static unsigned long held_since = 0;
    static bool fired = false;
    if (isButtonPressed()) {
      if (held_since == 0) held_since = millis();
      else if (!fired && sos != NULL && !inGames() && !inSos() && (long)(millis() - held_since) >= 5000
               && !(curr == home && ((HomeScreen*)home)->shuttingDown())) {
        fired = true;
        if (_display != NULL && !_display->isOn()) _display->turnOn();
        if (!the_mesh.sosActive()) ((SosScreen*)sos)->arm();
        setCurrScreen(sos);
      }
    } else {
      held_since = 0;
      fired = false;
    }
  }
#endif
  if (curr == sos && _display != NULL) {
    _auto_off = millis() + AUTO_OFF_MILLIS;
    if (!_display->isOn()) _display->turnOn();
  }
  if ((stopwatch != NULL || games != NULL) && _display != NULL) {
    if (stopwatch != NULL && ((TimerScreen*)stopwatch)->due() && curr != stopwatch) {   // time is up while elsewhere
      setCurrScreen(stopwatch);
      if (!_display->isOn()) _display->turnOn();
    }
    if ((curr == stopwatch && ((TimerScreen*)stopwatch)->keepsAwake()) || curr == games || inBadge()) _auto_off = millis() + AUTO_OFF_MILLIS;   // counting / playing / badge stays in view
  }

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(UIColor::popup_bkg);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(UIColor::popup_txt);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
#endif
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }

  if (_display != NULL && _display->isEink() && clock != NULL && curr == home) {
    if (((HomeScreen*)home)->allowsClock() && (long)(millis() - _idle_since) >= 60000L) {
      setCurrScreen(clock);
    }
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
      if(!board.isExternalPowered()) {
        if (_display != NULL) {
          _display->startFrame();
          _display->setTextSize(2);
          _display->setColor(UIColor::warning_txt);
          _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
          _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
          _display->endFrame();
          if (_display->isEink() == false) { delay(3000); }
        }
        shutdown();
      }
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double-click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleQuadClick(char c) {
  c = checkDisplayOn(c);
  if (c == 0) return 0;
  if (curr == home || curr == msg_preview || inReader() || inCompose() || inSent() || inRelays() || inStopwatch() || inGlance() || inBadge() || inGames() || inSos()) return c;
  return 0;
}

// six clicks or more: the Write keyboard and Sent page take them as five,
// anywhere else six to nine open the stopwatch (or the running timer),
// ten or more the badge
char UITask::handleManyClicks(bool ten) {
  if (inCompose() || inSent()) return handleQuadClick(KEY_PENTA);
  if (inGames() || inSos()) return 0;   // fast tapping in a game (or SOS) is not a request for the stopwatch
  if (_display == NULL) return 0;
  if (ten && badge != NULL) {
    if (!_display->isOn()) _display->turnOn();
    openBadge();
    return 0;
  }
  if (stopwatch == NULL) return 0;
  if (!_display->isOn()) _display->turnOn();
  openStopwatch();
  return 0;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  c = checkDisplayOn(c);
  if (c == 0) return 0;
  if (curr == home || curr == msg_preview || inReader() || inCompose() || inSent() || inRelays() || inStopwatch() || inGlance() || inBadge() || inGames() || inSos()) return c;
  toggleBuzzer();
  return 0;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        if (_node_prefs->gps_enabled && _node_prefs->mcu_sleep) {
          showAlert("GPS needs CPU on", 1200);
        } else {
          showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        }
        _next_refresh = 0;
        break;
      }
    }
  }
}

bool UITask::isDisplayOn() const {
#if AUTO_OFF_MILLIS > 0
  return _display != NULL && _display->isOn();
#else
  return false;
#endif
}

void UITask::applyPowerProfile(bool rx_boost, bool fem_lna, bool cpu_sleep) {
  if (!radio_driver.setRxBoostedGainMode(rx_boost)) {
    showAlert("RX boost failed", 900);
    return;
  }
  _node_prefs->rx_boosted_gain = rx_boost ? 1 : 0;
  bool cpu_blocked = false;
  bool ble_blocked = bleBlocksCpuSleep();
  if (cpu_sleep && (gpsBlocksCpuSleep() || ble_blocked)) {
    cpu_sleep = false;
    cpu_blocked = true;
  }
  if (_board && _board->canControlLoRaFemLna()) {
    if (!_board->setLoRaFemLnaEnabled(fem_lna)) {
      the_mesh.savePrefs();
      showAlert("LNA failed", 900);
      _next_refresh = 0;
      return;
    }
    _node_prefs->radio_fem_rxgain = fem_lna ? 1 : 0;
  }
  _node_prefs->mcu_sleep = (cpu_sleep && canSelectMcuSleep()) ? 1 : 0;
  the_mesh.savePrefs();
  notify(UIEventType::ack);
  bool fem = canControlFemLna();
  bool cpu_ok = canSelectMcuSleep();
  PowerChoice choices[6];
  int n = buildPowerChoices(fem, cpu_ok, choices);
  bool boost = _node_prefs->rx_boosted_gain != 0;
  bool lna = fem && _node_prefs->radio_fem_rxgain != 0;
  bool cpu = _node_prefs->mcu_sleep != 0;
  const char* name = "Custom";
  for (int i = 0; i < n; i++) {
    if (choices[i].boost == boost && choices[i].lna == lna && choices[i].cpu == cpu) {
      name = choices[i].name;
      break;
    }
  }
  if (cpu_blocked) showAlert(ble_blocked ? "BLE needs CPU on" : "GPS needs CPU on", 1200);
  else showAlert(name, 800);
  _next_refresh = 0;
}

void UITask::cycleRadioProfile() {
  uint8_t next = radioNextProfile(the_mesh.radioProfile());
  the_mesh.applyRadioProfile(next);
  notify(UIEventType::ack);
  showAlert(radioProfileAlert(next), 900);
  _next_refresh = 0;
}

void UITask::toggleFemLna() {
  if (!_board || !_board->canControlLoRaFemLna()) {
    showAlert("LNA: n/a", 800);
    return;
  }
  bool enable = !_board->isLoRaFemLnaEnabled();
  if (!_board->setLoRaFemLnaEnabled(enable)) {
    showAlert("LNA: failed", 800);
    return;
  }
  _node_prefs->radio_fem_rxgain = enable ? 1 : 0;
  the_mesh.savePrefs();
  notify(UIEventType::ack);
  showAlert(enable ? "LNA: on" : "LNA: off", 800);
  _next_refresh = 0;
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
