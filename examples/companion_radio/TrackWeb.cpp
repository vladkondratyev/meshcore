#include "TrackWeb.h"

#if defined(ESP32) && defined(WIFI_SSID) && ENV_INCLUDE_GPS == 1

#include <WiFi.h>
#include <WebServer.h>
#include "MyMesh.h"
#include "TrackPage.h"

static WebServer* web = NULL;

// streams console output into the HTTP reply in chunks
class ChunkPrint : public Print {
  WebServer& _s;
  char _b[512];
  size_t _n = 0;
public:
  ChunkPrint(WebServer& s) : _s(s) {}
  size_t write(uint8_t c) override {
    _b[_n++] = (char)c;
    if (_n == sizeof(_b)) done();
    return 1;
  }
  void done() {
    if (_n) { _s.sendContent(_b, _n); _n = 0; }
  }
};

static void sendText(const char* cmd) {
  web->sendHeader("Cache-Control", "no-store");
  web->setContentLength(CONTENT_LENGTH_UNKNOWN);
  web->send(200, "text/plain; charset=utf-8", "");
  ChunkPrint out(*web);
  char c[40];
  snprintf(c, sizeof(c), "%s", cmd);
  the_mesh.consoleCommand(c, out);
  out.done();
  web->sendContent("");
}

void trackWebBegin() {
  if (web) return;
  web = new WebServer(80);
  web->on("/", HTTP_GET, []() {
    web->send_P(200, "text/html; charset=utf-8", TRACK_PAGE);
  });
  web->on("/api/raw", HTTP_GET, []() { sendText("track raw"); });
  web->on("/api/get", HTTP_GET, []() {
    char c[24];
    snprintf(c, sizeof(c), "track get %d", web->arg("n").toInt());
    sendText(c);
  });
  web->on("/api/del", HTTP_GET, []() {
    char c[24];
    snprintf(c, sizeof(c), "track del %d", web->arg("n").toInt());
    sendText(c);
  });
  web->on("/api/cmd", HTTP_GET, []() {
    String a = web->arg("c");
    if (a == "start" || a == "pause" || a == "resume" || a == "stop") {
      char c[24];
      snprintf(c, sizeof(c), "track %s", a.c_str());
      sendText(c);
    } else {
      web->send(400, "text/plain", "ERR,command");
    }
  });
  web->onNotFound([]() { web->send(404, "text/plain", "not found"); });
  web->begin();
}

void trackWebLoop() {
  if (web) web->handleClient();
}

#else

void trackWebBegin() {}
void trackWebLoop() {}

#endif
