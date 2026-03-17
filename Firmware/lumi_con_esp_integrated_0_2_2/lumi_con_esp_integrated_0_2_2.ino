/*
  ============================================================================
  Lumi-Con ESP8266 Pet Firmware
  ============================================================================
  Lumia-only pet control, chat mode, and noPet debug mode

  Boot key behavior
  ---------------------------------------------------------------------------
  - Hold Pico key 0 at boot -> reset Wi-Fi settings and restart
  - Hold Pico key 1 at boot -> Chat mode (no pet, Lumia messages only)
  - Hold Pico key 2 at boot -> noPet Debug mode

  HTTP endpoints
  ---------------------------------------------------------------------------
    GET  /msg?t=Hello
    GET  /status?t=OK
    GET  /clear
    POST /ui {"channel":"chat|status|clear","text":"..."}
    GET  /health
    GET  /pet?action=status|sync|feed|play|clean|sleep|med|toggleSleep|discipline|reset
  ============================================================================
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <EEPROM.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// ============================================================================
// 1. CONFIGURATION
// ============================================================================

const char* PLUGIN_HOST = "192.168.1.87";
constexpr uint16_t PLUGIN_PORT = 8787;
const char* PLUGIN_SECRET = "";

constexpr uint32_t PICO_BAUD = 115200;
constexpr uint8_t KEY_COUNT = 36;
constexpr uint32_t LONG_PRESS_MS = 600;

constexpr uint8_t FACTORY_RESET_KEY = 0;
constexpr uint8_t CHAT_MODE_BOOT_KEY = 1;
constexpr uint8_t NOPET_DEBUG_BOOT_KEY = 2;
constexpr uint32_t BOOT_HOLD_MS = 1200;
constexpr uint32_t BOOT_HOLD_WINDOW_MS = 2500;

#ifndef D0
#define D0 16
#endif
#ifndef D1
#define D1 5
#endif
#ifndef D2
#define D2 4
#endif
#ifndef D3
#define D3 0
#endif
#ifndef D4
#define D4 2
#endif
#ifndef D5
#define D5 14
#endif
#ifndef D6
#define D6 12
#endif
#ifndef D7
#define D7 13
#endif
#ifndef D8
#define D8 15
#endif

#define TFT_CS   D2
#define TFT_DC   D1
#define TFT_RST  -1
#define ST7735_TAB INITR_GREENTAB

constexpr uint16_t EEPROM_BYTES = 256;
constexpr uint32_t PET_MAGIC = 0x50455431UL; // PET1
constexpr uint16_t PET_VERSION = 4;

constexpr uint16_t SCREEN_W = 160;
constexpr uint16_t SCREEN_H = 128;
constexpr uint8_t TEXT_SIZE = 1;
constexpr uint8_t CHAR_W = 6;
constexpr uint8_t LINE_H = 8;
constexpr uint8_t MAX_COLS = SCREEN_W / CHAR_W; // 26

constexpr uint32_t HEADER_TOAST_MS = 2000;

// Dedicated message page hold time in pet mode only.
// Edit this value if you want Lumia messages/status text to stay visible longer/shorter.
constexpr uint32_t MESSAGE_PAGE_HOLD_MS = 6000;

constexpr uint8_t TEXT_PAGE_HISTORY_LINES = 15;
constexpr uint32_t GAME_TICK_MS = 1000;
constexpr uint32_t ANIM_TICK_MS = 220;
constexpr uint32_t STATUS_ROTATE_MS = 3500;
constexpr uint32_t SAVE_DEFER_MS = 4000;
constexpr uint32_t WIFI_RETRY_MS = 15000;

constexpr uint8_t PET_ALERT_BASE = 72;
constexpr uint8_t PET_CHANGE_EVENT_COUNT = 22;
constexpr uint8_t PET_CHANGE_QUEUE_SIZE = 24;
constexpr uint32_t PET_EVENT_MIN_GAP_MS = 70;

constexpr uint16_t COLOR_BG = ST77XX_BLACK;
constexpr uint16_t COLOR_FG = ST77XX_WHITE;
constexpr uint16_t COLOR_DIM = 0x7BEF;
constexpr uint16_t COLOR_ACCENT = ST77XX_CYAN;
constexpr uint16_t COLOR_WARN = ST77XX_YELLOW;
constexpr uint16_t COLOR_BAD = 0x001F;;
constexpr uint16_t COLOR_GOOD = ST77XX_GREEN;
constexpr uint16_t COLOR_BROWN = 0xA145;

constexpr uint8_t HEADER_LINES = 2;
constexpr uint8_t HEADER_H = HEADER_LINES * LINE_H;
constexpr uint8_t GAME_Y = HEADER_H;
constexpr uint8_t GAME_H = SCREEN_H - HEADER_H;

constexpr int PET_AREA_X = 40;
constexpr int PET_AREA_Y = GAME_Y + 28;
constexpr int PET_AREA_W = 80;
constexpr int PET_AREA_H = 56;
constexpr int PET_CX = PET_AREA_X + PET_AREA_W / 2;
constexpr int PET_CY = PET_AREA_Y + PET_AREA_H / 2 + 2;
constexpr int FOOTER_Y = SCREEN_H - 10;

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
ESP8266WebServer server(80);
String deviceId;

// ============================================================================
// 2. LOW-OVERHEAD HELPER FUNCTIONS
// ============================================================================

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static inline uint32_t elapsedMs(uint32_t now, uint32_t since) {
  return now - since;
}

static inline uint8_t clampU8(int v, uint8_t lo, uint8_t hi) {
  if (v < (int)lo) return lo;
  if (v > (int)hi) return hi;
  return (uint8_t)v;
}

String truncateCols(const String& s) {
  if ((int)s.length() <= MAX_COLS) return s;
  return s.substring(0, MAX_COLS);
}

String urlDecode(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < in.length()) {
      auto hexVal = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return 10 + (h - 'a');
        if (h >= 'A' && h <= 'F') return 10 + (h - 'A');
        return 0;
      };
      char decoded = (hexVal(in[i + 1]) << 4) | hexVal(in[i + 2]);
      out += decoded;
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

String jsonFindString(const String& body, const String& key) {
  int k = body.indexOf("\"" + key + "\"");
  if (k < 0) return "";
  int colon = body.indexOf(':', k);
  if (colon < 0) return "";
  int q1 = body.indexOf('"', colon + 1);
  if (q1 < 0) return "";
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 < 0) return "";
  return body.substring(q1 + 1, q2);
}

// ============================================================================
// 3. PET DATA TYPES / GLOBALS
// ============================================================================

struct PetState {
  uint8_t stage;
  uint8_t hunger;
  uint8_t happiness;
  uint8_t energy;
  uint8_t hygiene;
  uint8_t health;
  uint8_t discipline;
  uint8_t poop;
  uint8_t sick;
  uint8_t sleeping;
  uint8_t alive;
  uint8_t reserved0;
  uint16_t ageMinutes;
  uint16_t actionsTaken;
  uint32_t lastSimMs;
  uint32_t bornMs;
};

struct PetSnapshot {
  uint8_t stage;
  uint8_t hunger;
  uint8_t happiness;
  uint8_t energy;
  uint8_t hygiene;
  uint8_t health;
  uint8_t discipline;
  uint8_t poop;
  uint8_t sick;
  uint8_t sleeping;
  uint8_t alive;
};

PetState pet;
PetSnapshot lastBroadcastPet = {};

enum PetAction : uint8_t {
  ACT_FEED = 0,
  ACT_PLAY,
  ACT_CLEAN,
  ACT_SLEEP,
  ACT_MED,
  ACT_DISCIPLINE
};

enum PetTelemetryField : uint8_t {
  PET_FIELD_HUNGER = 0,
  PET_FIELD_HAPPINESS,
  PET_FIELD_ENERGY,
  PET_FIELD_HYGIENE,
  PET_FIELD_HEALTH,
  PET_FIELD_DISCIPLINE,
  PET_FIELD_POOP,
  PET_FIELD_STAGE,
  PET_FIELD_SLEEPING,
  PET_FIELD_SICK,
  PET_FIELD_ALIVE
};

struct FuturePetTelemetryPacket {
  uint8_t changeCode;
  uint8_t fieldId;
  uint8_t fromValue;
  uint8_t toValue;
  int8_t delta;
  uint16_t ageMinutes;
};

enum PetChangeCode : uint8_t {
  PET_CHG_HUNGER_UP = 0, PET_CHG_HUNGER_DOWN,
  PET_CHG_HAPPINESS_UP, PET_CHG_HAPPINESS_DOWN,
  PET_CHG_ENERGY_UP, PET_CHG_ENERGY_DOWN,
  PET_CHG_HYGIENE_UP, PET_CHG_HYGIENE_DOWN,
  PET_CHG_HEALTH_UP, PET_CHG_HEALTH_DOWN,
  PET_CHG_DISCIPLINE_UP, PET_CHG_DISCIPLINE_DOWN,
  PET_CHG_POOP_UP, PET_CHG_POOP_DOWN,
  PET_CHG_STAGE_UP, PET_CHG_STAGE_DOWN,
  PET_CHG_SLEEP_ON, PET_CHG_SLEEP_OFF,
  PET_CHG_SICK_ON, PET_CHG_SICK_OFF,
  PET_CHG_ALIVE_ON, PET_CHG_ALIVE_OFF
};

struct PetTransitionEvent {
  uint8_t code;
  uint8_t fromValue;
  uint8_t toValue;
};

enum BootMode : uint8_t {
  BOOT_MODE_NORMAL = 0,
  BOOT_MODE_WIFI_RESET,
  BOOT_MODE_CHAT,
  BOOT_MODE_NOPET_DEBUG
};

enum UiMode : uint8_t {
  UI_MODE_PET = 0,
  UI_MODE_CHAT,
  UI_MODE_NOPET_DEBUG
};

UiMode uiMode = UI_MODE_PET;

PetTransitionEvent petEventQueue[PET_CHANGE_QUEUE_SIZE];
uint8_t petEventHead = 0;
uint8_t petEventTail = 0;
uint32_t lastPetEventPostMs = 0;
uint8_t lastPetEventCode = 255;

bool saveDirty = false;
uint32_t saveDirtyAt = 0;

// ============================================================================
// 4. FORWARD DECLARATIONS
// ============================================================================

PetTelemetryField telemetryFieldForChange(uint8_t changeCode);
static inline uint8_t petEventNumber(uint8_t code);
const char* petChangeName(uint8_t code);
FuturePetTelemetryPacket buildFutureTelemetryPacket(uint8_t code, uint8_t fromValue, uint8_t toValue, uint16_t ageMinutes);

bool readPicoPacket(uint8_t &type, uint8_t &key);
bool parseAckSeq(const String& body, uint32_t &outSeq);
bool postEventToPlugin(uint8_t eventNumber, uint8_t keyIndex, const char* pressKind, uint32_t heldMs, uint32_t seq, const char* extraJson);
bool servicePetTransitionQueue(uint32_t now);
BootMode detectBootMode();

uint16_t crc16_ccitt(const uint8_t* data, size_t len);
void markSaveDirty();
void resetPet(bool firstBoot);
bool loadPet();
void savePetNow();
void serviceSave(uint32_t now);

void snapshotPet(PetSnapshot &out);
void resetPetEventQueue();
bool enqueuePetTransition(uint8_t code, uint8_t fromValue, uint8_t toValue);
void queueDeltaPair(uint8_t upCode, uint8_t downCode, uint8_t before, uint8_t after);
void detectAndQueuePetTransitions(const PetSnapshot &before, const PetSnapshot &after);
void syncPetTransitionBaseline();
void commitPetTransitions();

uint8_t stageFromAgeMinutes(uint16_t ageMinutes);
const char* stageName(uint8_t s);
void applyBounds();
void agePetByMinutes(uint16_t mins);
void simulatePetToNow(uint32_t now);
void performPetAction(PetAction action);

uint16_t moodColor(uint8_t v);
void drawBar(int x, int y, int w, uint8_t value, uint16_t color, uint8_t index);
void drawHudLine(int row, const String& text, String& cache);
void clearPetArea();
void drawPoop(int x, int y);
void drawPetSprite();
void drawFooter();
void drawHudAndBars();
void renderPetUi(bool force);

void appendTextPageMessage(const String& msgRaw);
void refreshNoPetPage(bool resetHistory);
void showTextPage(uint32_t ttlMs);
void renderTextPage(bool force);
void serviceTextPage(uint32_t now);

void handleRoot();
void handleMsg();
void handleStatus();
void handleClear();
void handleUi();
String petStatusJson();
void handlePet();
void handleHealth();

void ensureWiFi(uint32_t now);
void drawBootSplash(const char* msg);
void fullUiInit();
void servicePetTiming(uint32_t now);
void servicePicoEvents();

inline bool petModeEnabled() {
  return uiMode == UI_MODE_PET;
}
inline bool noPetModeActive() {
  return uiMode != UI_MODE_PET;
}

inline bool postEventToPlugin(uint8_t eventNumber, uint8_t keyIndex, const char* pressKind, uint32_t heldMs, uint32_t seq) {
  return postEventToPlugin(eventNumber, keyIndex, pressKind, heldMs, seq, nullptr);
}
inline void resetPet() {
  resetPet(false);
}
inline void renderPetUi() {
  renderPetUi(false);
}

// ============================================================================
// 5. ONBOARD LED FEEDBACK
// ============================================================================

bool ledPulseActive = false;
uint32_t ledPulseUntil = 0;

void ledOn()  { digitalWrite(LED_BUILTIN, LOW); }
void ledOff() { digitalWrite(LED_BUILTIN, HIGH); }

void pulseLed(uint16_t ms) {
  ledOn();
  ledPulseActive = true;
  ledPulseUntil = millis() + ms;
}

void serviceLed(uint32_t now) {
  if (ledPulseActive && (int32_t)(now - ledPulseUntil) >= 0) {
    ledOff();
    ledPulseActive = false;
  }
}

// ============================================================================
// 6. UI STATE / MESSAGE PAGE
// ============================================================================

String headerStatusBase = "Boot...";
String lastKeyText = "K:--";
String transientStatus = "";
uint32_t transientUntilMs = 0;
String lastDrawLine1 = "";
String lastDrawLine2 = "";

String textPageLines[TEXT_PAGE_HISTORY_LINES];
uint8_t textPageLineCount = 0;
bool textPageActive = false;
uint32_t textPageUntilMs = 0;
uint32_t lastTextPageDrawToken = 0;

enum AckState : uint8_t {
  ACK_UNKNOWN = 0,
  ACK_OK,
  ACK_FAIL
};

AckState ackState = ACK_UNKNOWN;
String lastRenderedIp = "";

void drawLineArea(int y, const String& text, uint16_t fg = COLOR_FG) {
  tft.fillRect(0, y, SCREEN_W, LINE_H, COLOR_BG);
  tft.setCursor(0, y);
  tft.setTextColor(fg, COLOR_BG);
  tft.print(truncateCols(text));
}

String computeLine1() {
  if (transientStatus.length()) return transientStatus;
  return "";
}

String computeLine2() {
  String line2 = headerStatusBase;
  if (line2.length()) line2 += " ";
  line2 += lastKeyText;
  return line2;
}

void updateHeaderLine1(bool force = false) {
  String line1 = computeLine1();
  if (!force && line1 == lastDrawLine1) return;
  lastDrawLine1 = line1;
  drawLineArea(0, line1, COLOR_FG);
}

void updateHeaderLine2(bool force = false) {
  String line2 = computeLine2();
  if (!force && line2 == lastDrawLine2) return;
  lastDrawLine2 = line2;
  drawLineArea(LINE_H, line2, COLOR_FG);
}

void setToast(const String& msg, uint32_t ttlMs) {
  transientStatus = truncateCols(msg);
  transientUntilMs = millis() + ttlMs;
  if (!textPageActive && petModeEnabled()) updateHeaderLine1(true);
}

void updateTransientStatus(uint32_t now) {
  if (transientStatus.length() && (int32_t)(now - transientUntilMs) >= 0) {
    transientStatus = "";
    if (!textPageActive && petModeEnabled()) updateHeaderLine1(true);
  }
}

void clearUiMessages() {
  transientStatus = "";
  transientUntilMs = 0;
  lastDrawLine1 = "";
  textPageLineCount = 0;
  textPageActive = false;
  textPageUntilMs = 0;
  lastTextPageDrawToken = 0;

  if (petModeEnabled()) {
    updateHeaderLine1(true);
  } else {
    refreshNoPetPage(true);
  }
}

void appendTextPageMessage(const String& msgRaw) {
  String clean = msgRaw;
  clean.replace("\r", " ");
  clean.replace("\n", " ");
  clean.trim();
  if (!clean.length()) return;

  int start = 0;
  while (start < (int)clean.length()) {
    int remaining = (int)clean.length() - start;
    int take = remaining > MAX_COLS ? MAX_COLS : remaining;

    if (remaining > MAX_COLS) {
      int split = -1;
      for (int i = start + take - 1; i > start; --i) {
        if (clean[i] == ' ') {
          split = i;
          break;
        }
      }
      if (split > start) take = split - start;
    }

    String line = clean.substring(start, start + take);
    line.trim();
    if (!line.length()) {
      start += take + 1;
      continue;
    }

    if (textPageLineCount < TEXT_PAGE_HISTORY_LINES) {
      textPageLines[textPageLineCount++] = line;
    } else {
      for (uint8_t i = 1; i < TEXT_PAGE_HISTORY_LINES; ++i) {
        textPageLines[i - 1] = textPageLines[i];
      }
      textPageLines[TEXT_PAGE_HISTORY_LINES - 1] = line;
    }

    start += take;
    while (start < (int)clean.length() && clean[start] == ' ') start++;
  }
}

void refreshNoPetPage(bool resetHistory) {
  if (resetHistory) {
    textPageLineCount = 0;
    if (uiMode == UI_MODE_NOPET_DEBUG) {
      appendTextPageMessage("noPet Debug");
      appendTextPageMessage("Lumia + key debug");
    }
  }

  textPageActive = true;
  textPageUntilMs = 0;
  lastTextPageDrawToken = 0;
  renderTextPage(true);
}

void renderTextPage(bool force) {
  if (!textPageActive && !force) return;

  uint32_t token = (uint32_t)textPageLineCount ^ (textPageUntilMs << 1) ^ (uint32_t)textPageActive ^
                   ((uint32_t)uiMode << 24) ^ ((uint32_t)ackState << 16) ^ (uint32_t)WiFi.localIP();
  if (!force && token == lastTextPageDrawToken) return;
  lastTextPageDrawToken = token;

  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);
  tft.setTextSize(TEXT_SIZE);

  if (uiMode == UI_MODE_PET) {
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setCursor(0, 0);
    tft.print("MESSAGES");
    tft.drawFastHLine(0, LINE_H, SCREEN_W, COLOR_DIM);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    for (uint8_t i = 0; i < textPageLineCount && i < TEXT_PAGE_HISTORY_LINES; ++i) {
      tft.setCursor(0, (i + 1) * LINE_H);
      tft.print(textPageLines[i]);
    }
    return;
  }

  // Chat mode and noPet Debug mode share the same two-line status header.
  uint16_t ackColor = COLOR_WARN;
  const char* ackText = "ACK: --";
  if (ackState == ACK_OK) {
    ackText = "ACK: OK";
    ackColor = COLOR_GOOD;
  } else if (ackState == ACK_FAIL) {
    ackText = "ACK: FAIL";
    ackColor = COLOR_BAD;
  }

  drawLineArea(0, ackText, ackColor);

  String ipLine = "IP:";
  if (WiFi.status() == WL_CONNECTED) ipLine += WiFi.localIP().toString();
  else ipLine += "DOWN";
  drawLineArea(LINE_H, ipLine, COLOR_FG);

  tft.drawFastHLine(0, 2 * LINE_H, SCREEN_W, COLOR_DIM);

  uint8_t visibleLines = (SCREEN_H / LINE_H) - 3;
  tft.setTextColor(COLOR_FG, COLOR_BG);
  for (uint8_t i = 0; i < textPageLineCount && i < visibleLines; ++i) {
    tft.setCursor(0, (i + 3) * LINE_H);
    tft.print(textPageLines[i]);
  }
}

void showTextPage(uint32_t ttlMs) {
  textPageActive = true;
  textPageUntilMs = petModeEnabled() ? (millis() + ttlMs) : 0;
  renderTextPage(true);
}

void serviceTextPage(uint32_t now) {
  if (noPetModeActive()) {
    if (!textPageActive) {
      refreshNoPetPage(false);
    } else {
      renderTextPage(false);
    }
    return;
  }

  if (!textPageActive) return;
  if ((int32_t)(now - textPageUntilMs) >= 0) {
    textPageActive = false;
    lastTextPageDrawToken = 0;
    renderPetUi(true);
    updateHeaderLine1(true);
    updateHeaderLine2(true);
  }
}

// ============================================================================
// 7. PICO SERIAL INPUT + ACK EVENT PATH
// ============================================================================

uint32_t pressStart[KEY_COUNT] = {};
bool isDown[KEY_COUNT] = {};
bool picoSeen = false;
uint32_t seqCounter = 0;
uint32_t lastSeqSent = 0;
uint32_t lastAckSeq = 0;
bool lastPostOk = false;

PetTelemetryField telemetryFieldForChange(uint8_t changeCode) {
  switch (changeCode) {
    case 0: case 1:   return PET_FIELD_HUNGER;
    case 2: case 3:   return PET_FIELD_HAPPINESS;
    case 4: case 5:   return PET_FIELD_ENERGY;
    case 6: case 7:   return PET_FIELD_HYGIENE;
    case 8: case 9:   return PET_FIELD_HEALTH;
    case 10: case 11: return PET_FIELD_DISCIPLINE;
    case 12: case 13: return PET_FIELD_POOP;
    case 14: case 15: return PET_FIELD_STAGE;
    case 16: case 17: return PET_FIELD_SLEEPING;
    case 18: case 19: return PET_FIELD_SICK;
    case 20: case 21: return PET_FIELD_ALIVE;
    default:          return PET_FIELD_HUNGER;
  }
}

static inline uint8_t petEventNumber(uint8_t code) {
  return (uint8_t)(PET_ALERT_BASE + code);
}

const char* petChangeName(uint8_t code) {
  static const char* const NAMES[PET_CHANGE_EVENT_COUNT] = {
    "hunger_up", "hunger_down",
    "happiness_up", "happiness_down",
    "energy_up", "energy_down",
    "hygiene_up", "hygiene_down",
    "health_up", "health_down",
    "discipline_up", "discipline_down",
    "poop_up", "poop_down",
    "stage_up", "stage_down",
    "sleep_on", "sleep_off",
    "sick_on", "sick_off",
    "alive_on", "alive_off"
  };
  return (code < PET_CHANGE_EVENT_COUNT) ? NAMES[code] : "unknown";
}

FuturePetTelemetryPacket buildFutureTelemetryPacket(uint8_t code, uint8_t fromValue, uint8_t toValue, uint16_t ageMinutes) {
  FuturePetTelemetryPacket pkt;
  pkt.changeCode = code;
  pkt.fieldId = (uint8_t)telemetryFieldForChange(code);
  pkt.fromValue = fromValue;
  pkt.toValue = toValue;
  pkt.delta = (int8_t)toValue - (int8_t)fromValue;
  pkt.ageMinutes = ageMinutes;
  return pkt;
}

bool readPicoPacket(uint8_t &type, uint8_t &key) {
  static uint8_t state = 0;
  static uint8_t b1 = 0, b2 = 0;

  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();
    switch (state) {
      case 0:
        if (b == 0xA5) state = 1;
        break;
      case 1:
        b1 = b;
        state = 2;
        break;
      case 2:
        b2 = b;
        state = 3;
        break;
      case 3: {
        uint8_t chk = (uint8_t)(0xA5 ^ b1 ^ b2);
        state = 0;
        if (b == chk) {
          type = b1;
          key = b2;
          return true;
        }
        break;
      }
    }
  }
  return false;
}

bool parseAckSeq(const String& body, uint32_t &outSeq) {
  int i = body.indexOf("\"seq\"");
  if (i < 0) return false;
  int colon = body.indexOf(':', i);
  if (colon < 0) return false;
  int j = colon + 1;
  while (j < (int)body.length() && (body[j] == ' ' || body[j] == '\t')) j++;
  String num;
  while (j < (int)body.length() && isDigit(body[j])) {
    num += body[j++];
  }
  if (!num.length()) return false;
  outSeq = (uint32_t)num.toInt();
  return true;
}

bool postEventToPlugin(uint8_t eventNumber, uint8_t keyIndex, const char* pressKind, uint32_t heldMs, uint32_t seq, const char* extraJson) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  String url = String("http://") + PLUGIN_HOST + ":" + String(PLUGIN_PORT) + "/event";

  String body;
  body.reserve(512);
  body = "{";
  body += "\"event\":" + String(eventNumber);
  body += ",\"seq\":" + String(seq);
  body += ",\"deviceId\":\"" + deviceId + "\"";
  body += ",\"key\":" + String(keyIndex);
  body += ",\"press\":\"" + String(pressKind) + "\"";
  body += ",\"heldMs\":" + String(heldMs);
  body += ",\"uptimeMs\":" + String(millis());
  if (WiFi.status() == WL_CONNECTED) body += ",\"rssi\":" + String(WiFi.RSSI());
  if (extraJson && extraJson[0]) {
    body += ",";
    body += extraJson;
  }
  body += "}";

  uint32_t backoff = 120;
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    http.setTimeout(900);
    if (!http.begin(client, url)) {
      http.end();
      delay(backoff);
      backoff <<= 1;
      continue;
    }

    http.addHeader("Content-Type", "application/json");
    if (PLUGIN_SECRET && PLUGIN_SECRET[0] != '\0') {
      http.addHeader("X-Matrix-Secret", PLUGIN_SECRET);
    }

    int code = http.POST((uint8_t*)body.c_str(), body.length());
    String resp = http.getString();
    http.end();

    if (code >= 200 && code < 300) {
      uint32_t ack = 0;
      if (parseAckSeq(resp, ack) && ack == seq) {
        lastAckSeq = ack;
        return true;
      }
    }

    delay(backoff);
    backoff <<= 1;
    yield();
  }

  return false;
}

bool servicePetTransitionQueue(uint32_t now) {
  if (!petModeEnabled()) return false;
  if (petEventHead == petEventTail) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (elapsedMs(now, lastPetEventPostMs) < PET_EVENT_MIN_GAP_MS) return false;

  const PetTransitionEvent &ev = petEventQueue[petEventHead];
  FuturePetTelemetryPacket pkt = buildFutureTelemetryPacket(ev.code, ev.fromValue, ev.toValue, pet.ageMinutes);
  int16_t delta = (int16_t)ev.toValue - (int16_t)ev.fromValue;

  char extra[384];
  snprintf(
    extra,
    sizeof(extra),
    "\"petChange\":true,"
    "\"variation\":%u,"
    "\"changeCode\":%u,"
    "\"changeName\":\"%s\","
    "\"fieldId\":%u,"
    "\"from\":%u,"
    "\"to\":%u,"
    "\"delta\":%d,"
    "\"alive\":%u,"
    "\"stage\":%u,"
    "\"stageName\":\"%s\","
    "\"ageMinutes\":%u,"
    "\"hunger\":%u,"
    "\"happiness\":%u,"
    "\"energy\":%u,"
    "\"hygiene\":%u,"
    "\"health\":%u,"
    "\"discipline\":%u,"
    "\"poop\":%u,"
    "\"sick\":%u,"
    "\"sleeping\":%u",
    (unsigned)ev.code,
    (unsigned)ev.code,
    petChangeName(ev.code),
    (unsigned)pkt.fieldId,
    (unsigned)ev.fromValue,
    (unsigned)ev.toValue,
    (int)delta,
    (unsigned)pet.alive,
    (unsigned)pet.stage,
    stageName(pet.stage),
    (unsigned)pet.ageMinutes,
    (unsigned)pet.hunger,
    (unsigned)pet.happiness,
    (unsigned)pet.energy,
    (unsigned)pet.hygiene,
    (unsigned)pet.health,
    (unsigned)pet.discipline,
    (unsigned)pet.poop,
    (unsigned)pet.sick,
    (unsigned)pet.sleeping
  );

  seqCounter++;
  uint32_t seq = seqCounter;
  lastSeqSent = seq;

  bool ok = postEventToPlugin(petEventNumber(ev.code), 255, "pet", 0, seq, extra);
  lastPostOk = ok;
  if (!ok) return false;

  petEventHead = (uint8_t)((petEventHead + 1) % PET_CHANGE_QUEUE_SIZE);
  lastPetEventPostMs = now;
  lastPetEventCode = ev.code;
  return true;
}

BootMode detectBootMode() {
  uint32_t start = millis();

  bool k0Down = false;
  bool k1Down = false;
  bool k2Down = false;
  uint32_t k0DownAt = 0;
  uint32_t k1DownAt = 0;
  uint32_t k2DownAt = 0;

  while (elapsedMs(millis(), start) < BOOT_HOLD_WINDOW_MS) {
    uint8_t type, key;
    if (readPicoPacket(type, key)) {
      picoSeen = true;

      if (key == FACTORY_RESET_KEY) {
        if (type == 1) { if (!k0Down) { k0Down = true; k0DownAt = millis(); } }
        else k0Down = false;
      } else if (key == CHAT_MODE_BOOT_KEY) {
        if (type == 1) { if (!k1Down) { k1Down = true; k1DownAt = millis(); } }
        else k1Down = false;
      } else if (key == NOPET_DEBUG_BOOT_KEY) {
        if (type == 1) { if (!k2Down) { k2Down = true; k2DownAt = millis(); } }
        else k2Down = false;
      }
    }

    uint32_t now = millis();
    if (k0Down && elapsedMs(now, k0DownAt) >= BOOT_HOLD_MS) return BOOT_MODE_WIFI_RESET;
    if (k1Down && elapsedMs(now, k1DownAt) >= BOOT_HOLD_MS) return BOOT_MODE_CHAT;
    if (k2Down && elapsedMs(now, k2DownAt) >= BOOT_HOLD_MS) return BOOT_MODE_NOPET_DEBUG;
    yield();
  }

  return BOOT_MODE_NORMAL;
}

// ============================================================================
// 8. PET SAVE DATA + CRC PROTECTION
// ============================================================================

struct SaveBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  PetState pet;
  uint16_t crc;
};

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
  }
  return crc;
}

void markSaveDirty() {
  saveDirty = true;
  saveDirtyAt = millis();
}

void snapshotPet(PetSnapshot &out) {
  out.stage = pet.stage;
  out.hunger = pet.hunger;
  out.happiness = pet.happiness;
  out.energy = pet.energy;
  out.hygiene = pet.hygiene;
  out.health = pet.health;
  out.discipline = pet.discipline;
  out.poop = pet.poop;
  out.sick = pet.sick;
  out.sleeping = pet.sleeping;
  out.alive = pet.alive;
}

void resetPetEventQueue() {
  petEventHead = 0;
  petEventTail = 0;
  lastPetEventCode = 255;
}

void syncPetTransitionBaseline() {
  snapshotPet(lastBroadcastPet);
}

bool enqueuePetTransition(uint8_t code, uint8_t fromValue, uint8_t toValue) {
  if (code >= PET_CHANGE_EVENT_COUNT || fromValue == toValue) return false;

  for (uint8_t i = petEventHead; i != petEventTail; i = (uint8_t)((i + 1) % PET_CHANGE_QUEUE_SIZE)) {
    if (petEventQueue[i].code == code) {
      petEventQueue[i].toValue = toValue;
      return true;
    }
  }

  uint8_t nextTail = (uint8_t)((petEventTail + 1) % PET_CHANGE_QUEUE_SIZE);
  if (nextTail == petEventHead) {
    petEventHead = (uint8_t)((petEventHead + 1) % PET_CHANGE_QUEUE_SIZE);
  }

  petEventQueue[petEventTail].code = code;
  petEventQueue[petEventTail].fromValue = fromValue;
  petEventQueue[petEventTail].toValue = toValue;
  petEventTail = nextTail;
  return true;
}

void queueDeltaPair(uint8_t upCode, uint8_t downCode, uint8_t before, uint8_t after) {
  if (after > before) enqueuePetTransition(upCode, before, after);
  else if (after < before) enqueuePetTransition(downCode, before, after);
}

void detectAndQueuePetTransitions(const PetSnapshot &before, const PetSnapshot &after) {
  queueDeltaPair(PET_CHG_HUNGER_UP, PET_CHG_HUNGER_DOWN, before.hunger, after.hunger);
  queueDeltaPair(PET_CHG_HAPPINESS_UP, PET_CHG_HAPPINESS_DOWN, before.happiness, after.happiness);
  queueDeltaPair(PET_CHG_ENERGY_UP, PET_CHG_ENERGY_DOWN, before.energy, after.energy);
  queueDeltaPair(PET_CHG_HYGIENE_UP, PET_CHG_HYGIENE_DOWN, before.hygiene, after.hygiene);
  queueDeltaPair(PET_CHG_HEALTH_UP, PET_CHG_HEALTH_DOWN, before.health, after.health);
  queueDeltaPair(PET_CHG_DISCIPLINE_UP, PET_CHG_DISCIPLINE_DOWN, before.discipline, after.discipline);
  queueDeltaPair(PET_CHG_POOP_UP, PET_CHG_POOP_DOWN, before.poop, after.poop);
  queueDeltaPair(PET_CHG_STAGE_UP, PET_CHG_STAGE_DOWN, before.stage, after.stage);

  if (before.sleeping != after.sleeping) {
    enqueuePetTransition(after.sleeping ? PET_CHG_SLEEP_ON : PET_CHG_SLEEP_OFF, before.sleeping, after.sleeping);
  }
  if (before.sick != after.sick) {
    enqueuePetTransition(after.sick ? PET_CHG_SICK_ON : PET_CHG_SICK_OFF, before.sick, after.sick);
  }
  if (before.alive != after.alive) {
    enqueuePetTransition(after.alive ? PET_CHG_ALIVE_ON : PET_CHG_ALIVE_OFF, before.alive, after.alive);
  }
}

void commitPetTransitions() {
  PetSnapshot nowSnap;
  snapshotPet(nowSnap);
  detectAndQueuePetTransitions(lastBroadcastPet, nowSnap);
  lastBroadcastPet = nowSnap;
}

void resetPet(bool firstBoot) {
  memset(&pet, 0, sizeof(pet));
  pet.stage = 0;
  pet.hunger = 72;
  pet.happiness = 78;
  pet.energy = 84;
  pet.hygiene = 80;
  pet.health = 92;
  pet.discipline = 45;
  pet.poop = 0;
  pet.sick = 0;
  pet.sleeping = 0;
  pet.alive = 1;
  pet.ageMinutes = 0;
  uint32_t now = millis();
  pet.lastSimMs = now;
  pet.bornMs = now;
  pet.actionsTaken = 0;

  if (!firstBoot) {
    markSaveDirty();
  }

  if (firstBoot) {
    resetPetEventQueue();
    syncPetTransitionBaseline();
  }
}

bool loadPet() {
  SaveBlob blob;
  EEPROM.get(0, blob);
  if (blob.magic != PET_MAGIC) return false;
  if (blob.version != PET_VERSION) return false;
  if (blob.length != sizeof(PetState)) return false;
  uint16_t crc = crc16_ccitt((const uint8_t*)&blob.pet, sizeof(PetState));
  if (crc != blob.crc) return false;
  pet = blob.pet;
  resetPetEventQueue();
  syncPetTransitionBaseline();
  return true;
}

void savePetNow() {
  SaveBlob blob;
  blob.magic = PET_MAGIC;
  blob.version = PET_VERSION;
  blob.length = sizeof(PetState);
  blob.pet = pet;
  blob.crc = crc16_ccitt((const uint8_t*)&blob.pet, sizeof(PetState));
  EEPROM.put(0, blob);
  EEPROM.commit();
  saveDirty = false;
}

void serviceSave(uint32_t now) {
  if (saveDirty && elapsedMs(now, saveDirtyAt) >= SAVE_DEFER_MS) {
    savePetNow();
  }
}

// ============================================================================
// 9. PET GAME MODEL
// ============================================================================

uint8_t animFrame = 0;
uint32_t lastGameTickMs = 0;
uint32_t lastAnimTickMs = 0;
uint32_t lastStatusRotateMs = 0;
uint8_t statusPage = 0;

uint8_t stageFromAgeMinutes(uint16_t ageMinutes) {
  if (ageMinutes < 6) return 0;
  if (ageMinutes < 24) return 1;
  if (ageMinutes < 90) return 2;
  if (ageMinutes < 240) return 3;
  if (ageMinutes < 800) return 4;
  return 5;
}

const char* stageName(uint8_t s) {
  switch (s) {
    case 0: return "EGG";
    case 1: return "BABY";
    case 2: return "CHILD";
    case 3: return "TEEN";
    case 4: return "ADULT";
    default: return "ELDER";
  }
}

void applyBounds() {
  pet.hunger = clampU8(pet.hunger, 0, 100);
  pet.happiness = clampU8(pet.happiness, 0, 100);
  pet.energy = clampU8(pet.energy, 0, 100);
  pet.hygiene = clampU8(pet.hygiene, 0, 100);
  pet.health = clampU8(pet.health, 0, 100);
  pet.discipline = clampU8(pet.discipline, 0, 100);
  pet.poop = clampU8(pet.poop, 0, 3);
  pet.stage = stageFromAgeMinutes(pet.ageMinutes);
  if (pet.health == 0) pet.alive = 0;
}

void agePetByMinutes(uint16_t mins) {
  if (!pet.alive || mins == 0) return;

  for (uint16_t i = 0; i < mins; ++i) {
    pet.ageMinutes++;

    if (pet.sleeping) {
      if (pet.energy < 100) pet.energy++;
      if ((pet.ageMinutes % 5) == 0 && pet.hunger > 0) pet.hunger--;
      if ((pet.ageMinutes % 6) == 0 && pet.hygiene > 0) pet.hygiene--;
    } else {
      if ((pet.ageMinutes % 2) == 0 && pet.hunger > 0) pet.hunger--;
      if ((pet.ageMinutes % 3) == 0 && pet.energy > 0) pet.energy--;
      if ((pet.ageMinutes % 4) == 0 && pet.hygiene > 0) pet.hygiene--;
      if ((pet.ageMinutes % 5) == 0 && pet.happiness > 0) pet.happiness--;
    }

    bool harsh = (pet.hunger < 18) || (pet.hygiene < 20) || (pet.energy < 10);
    if (harsh && (pet.ageMinutes % 3) == 0 && pet.health > 0) pet.health--;

    if (pet.poop < 3 && !pet.sleeping && (pet.ageMinutes % 13) == 0) {
      pet.poop++;
      if (pet.hygiene > 5) pet.hygiene -= 5;
    }

    if (pet.poop >= 2 && (pet.ageMinutes % 4) == 0 && pet.health > 0) pet.health--;
    if (pet.discipline < 20 && (pet.ageMinutes % 8) == 0 && pet.happiness > 0) pet.happiness--;

    if (!pet.sick) {
      if ((pet.hygiene < 15 || pet.hunger < 12) && (pet.ageMinutes % 11) == 0) {
        pet.sick = 1;
      }
    } else {
      if ((pet.ageMinutes % 2) == 0 && pet.health > 0) pet.health--;
      if (pet.happiness > 0 && (pet.ageMinutes % 4) == 0) pet.happiness--;
    }

    if (pet.energy == 0 && !pet.sleeping) pet.sleeping = 1;
  }

  applyBounds();
}

void simulatePetToNow(uint32_t now) {
  if (!petModeEnabled()) return;
  if (!pet.alive) return;

  uint32_t delta = elapsedMs(now, pet.lastSimMs);
  if (delta < 60000UL) return;

  uint16_t mins = (uint16_t)(delta / 60000UL);
  agePetByMinutes(mins);
  pet.lastSimMs += (uint32_t)mins * 60000UL;
  commitPetTransitions();
  markSaveDirty();
}

void performPetAction(PetAction action) {
  if (!petModeEnabled()) return;
  if (!pet.alive && action != ACT_MED) return;

  switch (action) {
    case ACT_FEED:
      if (!pet.sleeping) {
        pet.hunger = clampU8((int)pet.hunger + 18, 0, 100);
        if (pet.happiness < 95) pet.happiness += 4;
        if (pet.hygiene > 1) pet.hygiene -= 1;
        if (pet.poop < 3 && pet.actionsTaken % 3 == 2) pet.poop++;
        setToast("Fed", 1200);
      } else {
        setToast("Sleeping", 1200);
      }
      break;

    case ACT_PLAY:
      if (!pet.sleeping) {
        pet.happiness = clampU8((int)pet.happiness + 15, 0, 100);
        if (pet.energy > 7) pet.energy -= 7;
        if (pet.hunger > 4) pet.hunger -= 4;
        if (pet.discipline < 95) pet.discipline += 2;
        setToast("Played", 1200);
      } else {
        setToast("Too sleepy", 1200);
      }
      break;

    case ACT_CLEAN:
      pet.hygiene = 100;
      pet.poop = 0;
      if (pet.health < 96) pet.health += 4;
      setToast("Cleaned", 1200);
      break;

    case ACT_SLEEP:
      pet.sleeping = !pet.sleeping;
      setToast(pet.sleeping ? "Sleep on" : "Awake", 1200);
      break;

    case ACT_MED:
      if (!pet.alive) {
        pet.alive = 1;
        pet.health = 45;
        pet.energy = 40;
        pet.hunger = 35;
        pet.happiness = 35;
        pet.hygiene = 50;
        pet.sick = 0;
        pet.sleeping = 0;
        setToast("Revived", 1500);
      } else {
        if (pet.sick) {
          pet.sick = 0;
          if (pet.health < 88) pet.health += 12;
          setToast("Healed", 1200);
        } else {
          if (pet.health < 98) pet.health += 4;
          setToast("Boosted", 1200);
        }
      }
      break;

    case ACT_DISCIPLINE:
      pet.discipline = clampU8((int)pet.discipline + 12, 0, 100);
      if (pet.happiness > 3) pet.happiness -= 3;
      setToast("Taught", 1200);
      break;
  }

  pet.actionsTaken++;
  applyBounds();
  commitPetTransitions();
  markSaveDirty();
}

// ============================================================================
// 10. PET RENDERING
// ============================================================================

static const uint8_t PROGMEM SPR_EGG0[72] = {0,0,0,0,0,0,0,0,0,15,255,0,63,255,192,127,255,224,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,127,255,224,63,255,192,15,255,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_EGG1[72] = {0,0,0,0,0,0,0,0,0,7,254,0,31,255,128,63,255,192,127,255,224,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,127,255,224,63,255,192,31,255,128,7,254,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_BABY0[72] = {0,0,0,0,0,0,3,48,192,7,176,224,63,255,192,127,255,224,127,255,224,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,127,255,224,127,255,224,63,255,192,31,255,128,3,48,192,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_BABY1[72] = {0,0,0,0,0,0,1,128,96,3,192,48,127,255,224,127,255,224,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,255,255,240,127,255,224,127,255,224,63,255,192,31,255,128,3,48,192,1,128,96,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_CHILD0[72] = {0,0,0,1,129,128,3,195,192,127,255,224,255,255,240,255,255,240,255,255,248,255,255,248,255,255,248,255,255,248,255,255,248,255,255,248,255,255,248,255,255,240,255,255,240,127,255,224,63,195,192,48,0,192,24,0,96,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_CHILD1[72] = {0,0,0,3,0,192,7,129,224,255,255,240,255,255,248,255,255,248,255,255,248,255,255,248,255,255,248,255,255,248,255,255,248,255,255,248,255,255,240,255,255,240,127,255,224,63,255,192,56,0,224,24,0,96,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_TEEN0[72] = {0,0,0,3,128,112,7,193,248,255,255,240,255,255,248,255,255,248,255,255,252,255,255,252,255,255,252,255,255,252,255,255,252,255,255,252,255,255,252,255,255,248,255,255,248,127,255,240,120,96,240,48,0,112,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_TEEN1[72] = {0,0,0,14,1,192,31,3,224,255,255,240,255,255,248,255,255,252,255,255,252,255,255,252,255,255,252,255,255,252,255,255,252,255,255,252,127,255,248,127,255,248,63,255,240,31,255,224,120,15,240,56,0,112,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_ADULT0[72] = {0,0,0,15,0,240,31,129,248,127,255,248,255,255,252,255,255,252,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,252,255,255,252,127,255,248,127,63,248,120,15,240,56,7,112,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_ADULT1[72] = {0,0,0,30,0,120,63,0,252,255,255,252,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,252,255,255,252,127,255,248,127,255,248,63,224,248,60,15,240,24,3,96,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_ELDER0[72] = {0,0,0,14,0,112,31,0,248,127,255,248,255,255,252,255,255,252,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,252,255,255,252,127,255,248,127,255,248,31,224,248,60,15,240,48,3,48,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t PROGMEM SPR_ELDER1[72] = {0,0,0,28,0,56,62,0,124,255,255,252,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,254,255,255,252,255,255,252,127,255,248,127,255,248,31,255,240,62,15,248,28,7,112,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

struct FastStageSpriteSet {
  const uint8_t* frames[2];
  uint8_t width;
  uint8_t height;
};

static const FastStageSpriteSet FAST_STAGE_SPRITES[6] = {
  { { SPR_EGG0,   SPR_EGG1   }, 24, 24 },
  { { SPR_BABY0,  SPR_BABY1  }, 24, 24 },
  { { SPR_CHILD0, SPR_CHILD1 }, 24, 24 },
  { { SPR_TEEN0,  SPR_TEEN1  }, 24, 24 },
  { { SPR_ADULT0, SPR_ADULT1 }, 24, 24 },
  { { SPR_ELDER0, SPR_ELDER1 }, 24, 24 }
};

inline uint16_t spriteColorForStage(uint8_t stage) {
  if (stage == 0) return COLOR_FG;
  if (stage == 1) return rgb565(255, 220, 120);
  if (stage == 2) return COLOR_ACCENT;
  if (stage == 3) return rgb565(255, 160, 220);
  if (stage == 4) return rgb565(180, 255, 180);
  return rgb565(190, 220, 255);
}

String lastHud0 = "";
String lastHud1 = "";
String lastHud2 = "";
String lastHud3 = "";
String lastFooter = "";

uint8_t lastBarValues[6] = {255, 255, 255, 255, 255, 255};
uint16_t lastBarColors[6] = {0, 0, 0, 0, 0, 0};
uint8_t lastSpriteBaseHash = 255;
uint8_t lastSpriteFaceHash = 255;
uint8_t lastSpritePoop = 255;

uint16_t moodColor(uint8_t v) {
  if (v > 66) return COLOR_GOOD;
  if (v > 33) return COLOR_WARN;
  return COLOR_BAD;
}

void drawBar(int x, int y, int w, uint8_t value, uint16_t color, uint8_t index) {
  if (index < 6 && lastBarValues[index] == value && lastBarColors[index] == color) return;

  if (index < 6) {
    lastBarValues[index] = value;
    lastBarColors[index] = color;
  }

  tft.drawRect(x, y, w, 6, COLOR_DIM);
  int fill = ((w - 2) * value) / 100;
  tft.fillRect(x + 1, y + 1, w - 2, 4, COLOR_BG);
  if (fill > 0) tft.fillRect(x + 1, y + 1, fill, 4, color);
}

void drawHudLine(int row, const String& text, String& cache) {
  if (text == cache) return;
  cache = text;
  int y = GAME_Y + row * LINE_H;
  tft.fillRect(0, y, SCREEN_W, LINE_H, COLOR_BG);
  tft.setCursor(0, y);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.print(truncateCols(text));
}

void clearPetArea() {
  tft.fillRect(0, GAME_Y, SCREEN_W, GAME_H, COLOR_BG);
  lastHud0 = lastHud1 = lastHud2 = lastHud3 = "";
  lastFooter = "";
  lastSpriteBaseHash = 255;
  lastSpriteFaceHash = 255;
  lastSpritePoop = 255;
  for (uint8_t i = 0; i < 6; ++i) {
    lastBarValues[i] = 255;
    lastBarColors[i] = 0;
  }
}

void drawPoop(int x, int y) {
  tft.fillRoundRect(x, y, 8, 6, 2, COLOR_BROWN);
  tft.fillRoundRect(x + 2, y - 3, 5, 4, 2, COLOR_BROWN);
}

void drawPetSprite() {
  const int areaX = PET_AREA_X;
  const int areaY = PET_AREA_Y;
  const int areaW = PET_AREA_W;
  const int areaH = PET_AREA_H;

  const int groundY = areaY + areaH - 8;
  const int spriteW = 24;
  const int spriteH = 24;
  const int spriteX = PET_CX - (spriteW / 2);
  const int spriteY = PET_CY - (spriteH / 2) - 2;

  const uint8_t stageIndex = (pet.stage <= 5) ? pet.stage : 5;
  const uint8_t frameIndex = pet.sleeping ? 0 : (animFrame & 0x01);
  const uint8_t spriteHash =
      (pet.alive ? 0x80 : 0x00) |
      ((stageIndex & 0x07) << 3) |
      ((pet.sleeping ? 1 : 0) << 2) |
      ((pet.sick ? 1 : 0) << 1) |
      (frameIndex & 0x01);

  const bool redrawSprite = (spriteHash != lastSpriteFaceHash);
  const bool redrawPoop = (pet.poop != lastSpritePoop) || (lastSpriteBaseHash == 255);
  const bool redrawGround = (lastSpriteBaseHash == 255);

  if (!redrawSprite && !redrawPoop && !redrawGround) return;

  if (redrawGround) {
    tft.fillRect(areaX, areaY, areaW, areaH, COLOR_BG);
    tft.drawFastHLine(areaX, groundY, areaW, COLOR_DIM);
    tft.fillRect(areaX, groundY + 1, areaW, 7, rgb565(0, 18, 0));
    lastSpriteBaseHash = 1;
  }

  if (redrawSprite) {
    lastSpriteFaceHash = spriteHash;

    tft.fillRect(spriteX - 1, spriteY - 1, spriteW + 2, spriteH + 2, COLOR_BG);
    if (spriteY + spriteH >= groundY) {
      const int overlapY = groundY;
      const int overlapH = (spriteY + spriteH + 1) - groundY;
      if (overlapH > 0) {
        tft.drawFastHLine(spriteX - 1, overlapY, spriteW + 2, COLOR_DIM);
        if (overlapH > 1) {
          tft.fillRect(spriteX - 1, overlapY + 1, spriteW + 2, overlapH - 1, rgb565(0, 18, 0));
        }
      }
    }

    if (!pet.alive) {
      tft.drawRoundRect(spriteX + 4, spriteY + 2, 16, 20, 5, COLOR_DIM);
      tft.drawLine(spriteX + 7, spriteY + 8,  spriteX + 10, spriteY + 11, COLOR_FG);
      tft.drawLine(spriteX + 10, spriteY + 8, spriteX + 7,  spriteY + 11, COLOR_FG);
      tft.drawLine(spriteX + 14, spriteY + 8, spriteX + 17, spriteY + 11, COLOR_FG);
      tft.drawLine(spriteX + 17, spriteY + 8, spriteX + 14, spriteY + 11, COLOR_FG);
      tft.drawFastHLine(spriteX + 8, spriteY + 16, 8, COLOR_FG);
    } else {
      const FastStageSpriteSet &set = FAST_STAGE_SPRITES[stageIndex];
      const uint16_t bodyColor = spriteColorForStage(stageIndex);

      tft.drawBitmap(spriteX, spriteY, set.frames[frameIndex], set.width, set.height, bodyColor);

      const int exL = spriteX + 8;
      const int exR = spriteX + 15;
      const int ey  = spriteY + 9 + ((frameIndex && !pet.sleeping) ? 1 : 0);
      const int my  = spriteY + 15 + ((frameIndex && !pet.sleeping) ? 1 : 0);

      if (pet.sleeping) {
        tft.drawFastHLine(exL - 1, ey, 4, COLOR_BG);
        tft.drawFastHLine(exR - 1, ey, 4, COLOR_BG);
        tft.setCursor(spriteX + 18, spriteY + 1);
        tft.setTextColor(COLOR_ACCENT, COLOR_BG);
        tft.print("Z");
      } else if (pet.sick) {
        tft.drawLine(exL - 1, ey - 1, exL + 2, ey + 2, COLOR_BG);
        tft.drawLine(exL + 2, ey - 1, exL - 1, ey + 2, COLOR_BG);
        tft.drawLine(exR - 1, ey - 1, exR + 2, ey + 2, COLOR_BG);
        tft.drawLine(exR + 2, ey - 1, exR - 1, ey + 2, COLOR_BG);
      } else {
        tft.fillRect(exL, ey, 2, 2, COLOR_BG);
        tft.fillRect(exR, ey, 2, 2, COLOR_BG);
      }

      if (pet.happiness > 55) {
        tft.drawLine(spriteX + 9,  my, spriteX + 12, my + 2, COLOR_BG);
        tft.drawLine(spriteX + 12, my + 2, spriteX + 15, my, COLOR_BG);
      } else if (pet.happiness > 25) {
        tft.drawFastHLine(spriteX + 10, my + 1, 5, COLOR_BG);
      } else {
        tft.drawLine(spriteX + 9,  my + 2, spriteX + 12, my, COLOR_BG);
        tft.drawLine(spriteX + 12, my,     spriteX + 15, my + 2, COLOR_BG);
      }
    }
  }

  if (redrawPoop) {
    lastSpritePoop = pet.poop;
    const int poopY = areaY + areaH - 12;
    tft.fillRect(areaX + 4, poopY - 1, 40, 10, COLOR_BG);
    tft.drawFastHLine(areaX, groundY, areaW, COLOR_DIM);
    tft.fillRect(areaX, groundY + 1, areaW, 7, rgb565(0, 18, 0));
    for (uint8_t i = 0; i < pet.poop; ++i) {
      drawPoop(areaX + 6 + (i * 12), areaY + areaH - 11);
    }
  }
}

void drawFooter() {
  String text = "Lumia /pet control";
  if (text == lastFooter) return;
  lastFooter = text;
  tft.fillRect(0, FOOTER_Y, SCREEN_W, 10, COLOR_BG);
  tft.drawFastHLine(0, FOOTER_Y - 1, SCREEN_W, COLOR_DIM);
  tft.setCursor(4, FOOTER_Y);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.print(text);
}

void drawHudAndBars() {
  String line0 = String(stageName(pet.stage)) + " A:" + String(pet.ageMinutes) + "m";
  if (pet.sick) line0 += " SICK";
  else if (pet.sleeping) line0 += " SLEEP";

  String line1;
  switch (statusPage % 3) {
    case 0:
      line1 = "HGR " + String(pet.hunger) + " HAP " + String(pet.happiness);
      break;
    case 1:
      line1 = "ENG " + String(pet.energy) + " HYG " + String(pet.hygiene);
      break;
    default:
      line1 = "HLT " + String(pet.health) + " DSP " + String(pet.discipline);
      break;
  }

  String line2 = String("Poop:") + String(pet.poop) + (pet.alive ? " Alive" : " Down");
  String line3 = String("WiFi:") + (WiFi.status() == WL_CONNECTED ? "UP" : "DOWN");

  drawHudLine(0, line0, lastHud0);
  drawHudLine(1, line1, lastHud1);
  drawHudLine(2, line2, lastHud2);
  drawHudLine(3, line3, lastHud3);

  const int leftLabelX = 2;
  const int leftBarX = 20;
  const int rightLabelX = 122;
  const int rightBarX = 134;
  const int by = GAME_Y + 34;
  const int bw = 22;

  tft.setTextColor(COLOR_DIM, COLOR_BG);

  tft.setCursor(leftLabelX, by - 1);      tft.print("H");
  tft.setCursor(leftLabelX, by + 9);      tft.print("P");
  tft.setCursor(leftLabelX, by + 19);     tft.print("E");
  tft.setCursor(leftLabelX, by + 29);     tft.print("Y");
  tft.setCursor(rightLabelX, by - 1);     tft.print("L");
  tft.setCursor(rightLabelX, by + 9);     tft.print("D");

  drawBar(leftBarX,  by,      bw, pet.hunger,     moodColor(pet.hunger),     0);
  drawBar(leftBarX,  by + 10, bw, pet.happiness,  moodColor(pet.happiness),  1);
  drawBar(leftBarX,  by + 20, bw, pet.energy,     moodColor(pet.energy),     2);
  drawBar(leftBarX,  by + 30, bw, pet.hygiene,    moodColor(pet.hygiene),    3);
  drawBar(rightBarX, by,      bw, pet.health,     moodColor(pet.health),     4);
  drawBar(rightBarX, by + 10, bw, pet.discipline, moodColor(pet.discipline), 5);
}

void renderPetUi(bool force) {
  if (!petModeEnabled()) {
    refreshNoPetPage(false);
    return;
  }
  if (force) clearPetArea();
  drawHudAndBars();
  drawPetSprite();
  drawFooter();
}

// ============================================================================
// 11. HTTP API
// ============================================================================

void pushWrappedChat(const String& msgRaw) {
  appendTextPageMessage(msgRaw);
  showTextPage(MESSAGE_PAGE_HOLD_MS);
}

void handleRoot() {
  server.send(200, "text/plain",
    "Lumi-Con ESP Pet ACK OK\n"
    "GET /msg?t=Hello\n"
    "GET /status?t=OK\n"
    "GET /clear\n"
    "POST /ui {\"channel\":\"chat|status|clear\",\"text\":\"...\"}\n"
    "GET /health\n"
    "GET /pet?action=status|sync|feed|play|clean|sleep|med|toggleSleep|discipline|reset\n"
  );
}

void handleMsg() {
  if (!server.hasArg("t")) return server.send(400, "text/plain", "Missing 't'");
  String text = urlDecode(server.arg("t"));
  if (!text.length()) return server.send(400, "text/plain", "Empty");
  pushWrappedChat(text);
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  if (!server.hasArg("t")) return server.send(400, "text/plain", "Missing 't'");
  String text = urlDecode(server.arg("t"));
  if (!text.length()) return server.send(400, "text/plain", "Empty");
  appendTextPageMessage(String("STATUS: ") + text);
  showTextPage(MESSAGE_PAGE_HOLD_MS);
  server.send(200, "text/plain", "OK");
}

void handleClear() {
  clearUiMessages();
  if (petModeEnabled()) renderPetUi(true);
  else renderTextPage(true);
  server.send(200, "text/plain", "CLEARED");
}

void handleUi() {
  if (server.method() != HTTP_POST) return server.send(405, "text/plain", "POST only");
  String body = server.arg("plain");
  body.trim();
  if (!body.length()) return server.send(400, "text/plain", "Empty body");

  String channel = jsonFindString(body, "channel");
  String text = jsonFindString(body, "text");

  if (channel == "clear") {
    clearUiMessages();
    if (petModeEnabled()) renderPetUi(true);
    else renderTextPage(true);
    return server.send(200, "text/plain", "OK");
  }

  if (!text.length()) return server.send(400, "text/plain", "Missing text");

  if (channel == "status") {
    appendTextPageMessage(String("STATUS: ") + text);
    showTextPage(MESSAGE_PAGE_HOLD_MS);
    return server.send(200, "text/plain", "OK");
  }

  pushWrappedChat(text);
  server.send(200, "text/plain", "OK");
}

String petStatusJson() {
  String json;
  json.reserve(460);
  json = "{";
  json += "\"uiMode\":\"";
  if (uiMode == UI_MODE_PET) json += "pet";
  else if (uiMode == UI_MODE_CHAT) json += "chat";
  else json += "noPetDebug";
  json += "\"";
  json += ",\"petModeEnabled\":" + String(petModeEnabled() ? "true" : "false");
  json += ",\"alive\":" + String(pet.alive ? "true" : "false");
  json += ",\"stage\":" + String(pet.stage);
  json += ",\"stageName\":\"" + String(stageName(pet.stage)) + "\"";
  json += ",\"ageMinutes\":" + String(pet.ageMinutes);
  json += ",\"hunger\":" + String(pet.hunger);
  json += ",\"happiness\":" + String(pet.happiness);
  json += ",\"energy\":" + String(pet.energy);
  json += ",\"hygiene\":" + String(pet.hygiene);
  json += ",\"health\":" + String(pet.health);
  json += ",\"discipline\":" + String(pet.discipline);
  json += ",\"poop\":" + String(pet.poop);
  json += ",\"sick\":" + String(pet.sick ? "true" : "false");
  json += ",\"sleeping\":" + String(pet.sleeping ? "true" : "false");
  uint8_t queued = (petEventTail >= petEventHead) ? (petEventTail - petEventHead) : (PET_CHANGE_QUEUE_SIZE - petEventHead + petEventTail);
  json += ",\"petEventBase\":" + String(PET_ALERT_BASE);
  json += ",\"petEventCount\":" + String(PET_CHANGE_EVENT_COUNT);
  json += ",\"petQueueDepth\":" + String(queued);
  json += ",\"lastPetEventCode\":" + String(lastPetEventCode == 255 ? -1 : (int)lastPetEventCode);
  json += "}";
  return json;
}

void handlePet() {
  if (!server.hasArg("action")) return server.send(400, "text/plain", "Missing action");
  String action = server.arg("action");

  if (action == "status") {
    return server.send(200, "application/json", petStatusJson());
  }

  if (!petModeEnabled()) {
    return server.send(503, "application/json", "{\"ok\":false,\"error\":\"pet_disabled_in_current_ui_mode\",\"petModeEnabled\":false}");
  }

  if (action == "sync") {
    resetPetEventQueue();
    syncPetTransitionBaseline();
    return server.send(200, "application/json", petStatusJson());
  }

  if (action == "feed") performPetAction(ACT_FEED);
  else if (action == "play") performPetAction(ACT_PLAY);
  else if (action == "clean") performPetAction(ACT_CLEAN);
  else if (action == "sleep" || action == "toggleSleep") performPetAction(ACT_SLEEP);
  else if (action == "med") performPetAction(ACT_MED);
  else if (action == "discipline") performPetAction(ACT_DISCIPLINE);
  else if (action == "reset") {
    resetPet();
    commitPetTransitions();
    setToast("Pet reset", 1500);
    markSaveDirty();
  } else {
    return server.send(400, "text/plain", "Unknown action");
  }

  renderPetUi(false);
  server.send(200, "application/json", petStatusJson());
}

void handleHealth() {
  String ip = WiFi.localIP().toString();
  long rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  String json;
  json.reserve(340);
  json = "{";
  json += "\"ok\":true";
  json += ",\"deviceId\":\"" + deviceId + "\"";
  json += ",\"mode\":\"ack\"";
  json += ",\"ip\":\"" + ip + "\"";
  json += ",\"rssi\":" + String(rssi);
  json += ",\"uptimeMs\":" + String(millis());
  json += ",\"lastKey\":\"" + lastKeyText + "\"";
  json += ",\"lastSeq\":" + String(lastSeqSent);
  json += ",\"lastAck\":" + String(lastAckSeq);
  json += ",\"lastPostOk\":" + String(lastPostOk ? "true" : "false");
  json += ",\"uiMode\":\"";
  if (uiMode == UI_MODE_PET) json += "pet";
  else if (uiMode == UI_MODE_CHAT) json += "chat";
  else json += "noPetDebug";
  json += "\"";
  json += ",\"petModeEnabled\":" + String(petModeEnabled() ? "true" : "false");
  json += ",\"petAlive\":" + String(pet.alive ? "true" : "false");
  json += ",\"petStage\":" + String(pet.stage);
  json += ",\"petStageName\":\"" + String(stageName(pet.stage)) + "\"";
  json += ",\"petEventBase\":" + String(PET_ALERT_BASE);
  json += ",\"petEventCount\":" + String(PET_CHANGE_EVENT_COUNT);
  json += "}";
  server.send(200, "application/json", json);
}

// ============================================================================
// 12. WIFI MAINTENANCE
// ============================================================================

uint32_t lastWifiAttemptMs = 0;

void ensureWiFi(uint32_t now) {
  int st = WiFi.status();
  if (st == WL_CONNECTED) return;
  if (elapsedMs(now, lastWifiAttemptMs) < WIFI_RETRY_MS) return;
  lastWifiAttemptMs = now;
  WiFi.reconnect();
}

// ============================================================================
// 13. BOOT UI / STARTUP HELPERS
// ============================================================================

void drawBootSplash(const char* msg) {
  tft.fillScreen(COLOR_BG);
  tft.fillRect(12, 16, 136, 28, COLOR_ACCENT);
  tft.setCursor(20, 24);
  tft.setTextColor(COLOR_BG, COLOR_ACCENT);
  tft.print("LUMI-CON PET");
  tft.setCursor(18, 56);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.print(msg);
  tft.drawRoundRect(20, 78, 120, 26, 6, COLOR_DIM);
  tft.setCursor(34, 87);
  tft.print("LUMIA CONTROL");
}

void fullUiInit() {
  tft.fillScreen(COLOR_BG);
  lastDrawLine1 = "__force__";
  lastDrawLine2 = "__force__";

  if (petModeEnabled()) {
    updateHeaderLine1(true);
    updateHeaderLine2(true);
    renderPetUi(true);
  } else {
    refreshNoPetPage(true);
  }
}

// ============================================================================
// 14. SETUP AND MAIN LOOP
// ============================================================================

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  ledOff();

  Serial.begin(PICO_BAUD);
  Serial.setDebugOutput(false);
  delay(120);

  char idBuf[24];
  snprintf(idBuf, sizeof(idBuf), "lumicon-%lx", (unsigned long)ESP.getChipId());
  deviceId = idBuf;

  SPI.begin();
  tft.initR(ST7735_TAB);
  tft.setRotation(3);
  tft.setTextWrap(false);
  tft.setTextSize(TEXT_SIZE);
  tft.setTextColor(COLOR_FG, COLOR_BG);

  drawBootSplash("Init...");

  EEPROM.begin(EEPROM_BYTES);
  if (!loadPet()) {
    resetPet(true);
    savePetNow();
  }
  pet.lastSimMs = millis();

  headerStatusBase = "Boot...";
  updateHeaderLine2(true);

  setToast("K0 WiFi K1 Chat K2 Dbg", 1800);
  BootMode bootMode = detectBootMode();

  if (bootMode == BOOT_MODE_WIFI_RESET) {
    drawBootSplash("WiFi reset");
    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.resetSettings();
    delay(200);
    ESP.restart();
  }

  if (bootMode == BOOT_MODE_CHAT) {
    uiMode = UI_MODE_CHAT;
  } else if (bootMode == BOOT_MODE_NOPET_DEBUG) {
    uiMode = UI_MODE_NOPET_DEBUG;
  } else {
    uiMode = UI_MODE_PET;
  }

  drawBootSplash("WiFi setup");
  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(180);
  bool ok = wm.autoConnect("Lumi-Con-Setup");
  if (!ok) {
    setToast("Portal timeout", 2000);
  }

  if (WiFi.status() == WL_CONNECTED) {
    headerStatusBase = "IP:" + WiFi.localIP().toString();
  } else {
    headerStatusBase = "WiFi:DOWN";
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/msg", HTTP_GET, handleMsg);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/clear", HTTP_GET, handleClear);
  server.on("/ui", HTTP_POST, handleUi);
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/pet", HTTP_GET, handlePet);
  server.begin();

  lastGameTickMs = millis();
  lastAnimTickMs = millis();
  lastStatusRotateMs = millis();

  fullUiInit();

  if (petModeEnabled()) {
    setToast("Ready", 1400);
  } else {
    renderTextPage(true);
  }
}

void servicePetTiming(uint32_t now) {
  if (!petModeEnabled()) return;

  if (elapsedMs(now, lastGameTickMs) >= GAME_TICK_MS) {
    lastGameTickMs += GAME_TICK_MS;
    simulatePetToNow(now);
    if (!textPageActive) renderPetUi(false);
  }

  if (elapsedMs(now, lastAnimTickMs) >= ANIM_TICK_MS) {
    lastAnimTickMs += ANIM_TICK_MS;
    animFrame = (animFrame + 1) & 0x03;
    if (!textPageActive) drawPetSprite();
  }

  if (elapsedMs(now, lastStatusRotateMs) >= STATUS_ROTATE_MS) {
    lastStatusRotateMs += STATUS_ROTATE_MS;
    statusPage = (statusPage + 1) % 3;
    if (!textPageActive) drawHudAndBars();
  }
}

void servicePicoEvents() {
  uint8_t type, key;
  while (readPicoPacket(type, key)) {
    picoSeen = true;
    if (key >= KEY_COUNT) continue;

    uint32_t now = millis();
    if (type == 1) {
      pulseLed(14);
      if (!isDown[key]) {
        isDown[key] = true;
        pressStart[key] = now;
      }
      continue;
    }

    if (!isDown[key]) continue;
    isDown[key] = false;

    uint32_t held = elapsedMs(now, pressStart[key]);
    bool isLongPress = held >= LONG_PRESS_MS;
    uint8_t eventOut = isLongPress ? (uint8_t)(key + 36) : key;
    const char* pressKind = isLongPress ? "long" : "short";

    lastKeyText = String("K:") + String(key) + (isLongPress ? "L" : "S");
    if (petModeEnabled()) updateHeaderLine2(true);

    seqCounter++;
    uint32_t seq = seqCounter;
    lastSeqSent = seq;

    bool ok = postEventToPlugin(eventOut, key, pressKind, held, seq);
    lastPostOk = ok;
    ackState = ok ? ACK_OK : ACK_FAIL;

    if (ok) {
      pulseLed(34);
      setToast("ACK OK", HEADER_TOAST_MS);
    } else {
      pulseLed(120);
      setToast("ACK FAIL", 2600);
    }

    if (uiMode == UI_MODE_NOPET_DEBUG) {
      appendTextPageMessage(String("KEY ") + String(key) + (isLongPress ? " long" : " short") + (ok ? " ok" : " fail"));
      renderTextPage(true);
    } else if (uiMode == UI_MODE_CHAT) {
      // Chat mode shows only Lumia-sent messages below the two status lines.
      renderTextPage(true);
    }
  }
}

void loop() {
  uint32_t now = millis();

  server.handleClient();
  serviceLed(now);
  updateTransientStatus(now);
  serviceTextPage(now);
  ensureWiFi(now);
  servicePicoEvents();
  servicePetTiming(now);
  servicePetTransitionQueue(now);
  serviceSave(now);

  yield();
}