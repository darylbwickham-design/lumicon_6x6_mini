/*
  ============================================================================
  Lumi-Con ESP8266 Pet Firmware
  ============================================================================
  Tutorial Edition / Readability Revision

  Hardware target
  ---------------------------------------------------------------------------
  - ESP8266 board
  - ST7735 SPI TFT display
  - Raspberry Pi Pico feeding key events over serial
  - 2 extra SPST buttons wired to the ESP for direct pet gameplay
  - Lumia plugin on the network for ACK events and pet-state change events

  What this firmware is trying to be
  ---------------------------------------------------------------------------
  This is not just “code that works”.
  This revision is written so you can OPEN the sketch and actually learn from it.

  The design goal is:
  1. Keep the firmware stable and non-blocking
  2. Keep RAM usage sensible for ESP8266
  3. Keep the display responsive by redrawing only what changes
  4. Keep save data resilient with validation and CRC
  5. Keep Lumia integration compact and easy to extend later

  Big architecture, in plain English
  ---------------------------------------------------------------------------
  There are five main jobs running side-by-side:

  1) Network/UI layer
     - Hosts a tiny web server
     - Lets Lumia or tools push messages/status to the header
     - Exposes /pet and /health endpoints

  2) Pico key event bridge
     - Reads packets from the Pico
     - Detects short vs long presses
     - Sends ACK events to the Lumia-side plugin

  3) Pet simulation
     - Tracks hunger, happiness, energy, hygiene, health, discipline
     - Advances age and stage over time
     - Handles sickness, sleep, poop, death, revive, discipline

  4) Pet rendering
     - Draws a small animated creature
     - Rotates stat views
     - Updates only changed areas when possible

  5) Persistence + fault tolerance
     - Saves to EEPROM with magic/version/CRC
     - Defers writes to avoid EEPROM wear
     - Uses wrap-safe millis() timing everywhere practical

  Lumia event model in this build
  ---------------------------------------------------------------------------
  - Key releases still generate the normal ACK/plugin events.
  - Pet state changes also generate a compact event packet.
  - That second channel is meant for a future Lumia plugin variable layer.
    Example: hunger went down, sleep turned on, sickness turned off, etc.

  Controls
  ---------------------------------------------------------------------------
  - Button A (D5): cycle current pet action
  - Button B (D6): perform current pet action
  - Hold A+B: reset the pet
  - Pico keys still operate as the main Lumia input source

  HTTP endpoints
  ---------------------------------------------------------------------------
    GET  /msg?t=Hello
    GET  /status?t=OK
    GET  /clear
    POST /ui {"channel":"chat|status|clear","text":"..."}
    GET  /health
    GET  /pet?action=status|sync|feed|play|clean|sleep|med|toggleSleep|discipline|reset

  Notes for future you
  ---------------------------------------------------------------------------
  - Buttons use INPUT_PULLUP, so pressed = LOW.
  - The sketch avoids full-screen redraw spam on the TFT.
  - The pet uses minute-scale simulation, but the UI animates every few hundred ms.
  - The code is split into tutorial-style sections so you can navigate fast.

  This revision also deliberately anticipates the next three likely expansions:
  1) richer Lumia-side pet telemetry and variable syncing
  2) a proper sprite/content pipeline instead of one hardcoded creature
  3) deeper gameplay balancing, personalities, and branching evolution

  Those systems are scaffolded below with tutorial comments and placeholder
  structures, even where the current runtime still uses the simpler build.

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
// Everything in this block is the “change me on purpose” zone.
// If you want to retarget pins, timings, plugin host details, or display setup,
// this is the first place to look.
// ============================================================================

// Lumia/plugin host that receives event JSON.
const char* PLUGIN_HOST = "192.168.1.87";
constexpr uint16_t PLUGIN_PORT = 8787;
const char* PLUGIN_SECRET = "";

// Serial speed used between the Pico and the ESP.
constexpr uint32_t PICO_BAUD = 115200;
// Number of keys coming from the Pico matrix firmware.
constexpr uint8_t KEY_COUNT = 36;
constexpr uint32_t LONG_PRESS_MS = 600;

constexpr uint8_t FACTORY_RESET_KEY = 0;
constexpr uint32_t FACTORY_RESET_HOLD_MS = 1200;
constexpr uint32_t FACTORY_RESET_WINDOW_MS = 2500;

// Some ESP8266 board packages provide friendly D-pin aliases (D1, D2, etc.)
// and some do not. These fallbacks make the sketch compile on both styles.
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

// ST7735 SPI pin assignment.
#define TFT_CS   D2
#define TFT_DC   D1
#define TFT_RST  -1
#define ST7735_TAB INITR_GREENTAB

// Two local gameplay buttons connected directly to the ESP.
constexpr uint8_t BTN_A_PIN = D5;
constexpr uint8_t BTN_B_PIN = D6;
constexpr bool BTN_ACTIVE = LOW;
constexpr uint16_t BUTTON_DEBOUNCE_MS = 28;
constexpr uint16_t BUTTON_COMBO_HOLD_MS = 900;

// EEPROM budget reserved for this sketch.
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
constexpr uint32_t TEXT_PAGE_SHOW_MS = 2000;
constexpr uint8_t TEXT_PAGE_HISTORY_LINES = 15;
// Time constants for UI, simulation, save, and Wi-Fi servicing.
constexpr uint32_t GAME_TICK_MS = 1000;
constexpr uint32_t ANIM_TICK_MS = 220;
constexpr uint32_t STATUS_ROTATE_MS = 3500;
constexpr uint32_t SAVE_DEFER_MS = 4000;
constexpr uint32_t WIFI_RETRY_MS = 15000;

// Lumia pet-change events start here.
constexpr uint8_t PET_ALERT_BASE = 72;
constexpr uint8_t PET_CHANGE_EVENT_COUNT = 22;
constexpr uint8_t PET_CHANGE_QUEUE_SIZE = 24;
constexpr uint32_t PET_EVENT_MIN_GAP_MS = 70;

constexpr uint16_t COLOR_BG = ST77XX_BLACK;
constexpr uint16_t COLOR_FG = ST77XX_WHITE;
constexpr uint16_t COLOR_DIM = 0x7BEF;
constexpr uint16_t COLOR_ACCENT = ST77XX_CYAN;
constexpr uint16_t COLOR_WARN = ST77XX_YELLOW;
constexpr uint16_t COLOR_BAD = ST77XX_RED;
constexpr uint16_t COLOR_GOOD = ST77XX_GREEN;
constexpr uint16_t COLOR_PINK = 0xF81F;
constexpr uint16_t COLOR_BROWN = 0xA145;
constexpr uint16_t COLOR_SKY = 0x04FF;

constexpr uint8_t HEADER_LINES = 2;
constexpr uint8_t HEADER_H = HEADER_LINES * LINE_H;
constexpr uint8_t GAME_Y = HEADER_H;
constexpr uint8_t GAME_H = SCREEN_H - HEADER_H;

// Sprite layout constants. Keeping these in one place makes redraw logic
// easier to understand and tune.
constexpr int PET_AREA_X = 36;
constexpr int PET_AREA_Y = GAME_Y + 34;
constexpr int PET_AREA_W = 88;
constexpr int PET_AREA_H = 54;
constexpr int PET_CX = PET_AREA_X + PET_AREA_W / 2;
constexpr int PET_CY = PET_AREA_Y + PET_AREA_H / 2 + 2;
constexpr int ACTION_STRIP_Y = SCREEN_H - 10;

// Global device objects.
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
ESP8266WebServer server(80);
String deviceId;


// Forward declarations are written out manually so the Arduino auto-prototype
// generator does not guess and produce broken declarations for tutorial code
// that uses custom structs, enums, and references.
struct DebouncedButton;
struct PetSnapshot;
struct FuturePetTelemetryPacket;
enum PetAction : uint8_t;
enum PetTelemetryField : uint8_t;

PetTelemetryField telemetryFieldForChange(uint8_t changeCode);
static inline uint8_t petEventNumber(uint8_t code);
const char* petChangeName(uint8_t code);
FuturePetTelemetryPacket buildFutureTelemetryPacket(uint8_t code, uint8_t fromValue, uint8_t toValue, uint16_t ageMinutes);
bool readPicoPacket(uint8_t &type, uint8_t &key);
bool parseAckSeq(const String& body, uint32_t &outSeq);
bool postEventToPlugin(uint8_t eventNumber, uint8_t keyIndex, const char* pressKind, uint32_t heldMs, uint32_t seq, const char* extraJson);
bool servicePetTransitionQueue(uint32_t now);
bool checkFactoryResetHold();
void initButton(DebouncedButton& b);
void updateButton(DebouncedButton& b, uint32_t now);
bool isPressed(const DebouncedButton& b);
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
void drawActionStrip();
void drawHudAndBars();
void renderPetUi(bool force);
void pushWrappedChat(const String& msgRaw);
void appendTextPageMessage(const String& msgRaw);
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
void serviceButtons(uint32_t now);
void servicePetTiming(uint32_t now);
void servicePicoEvents();

// Small wrappers keep call sites tidy while avoiding duplicate default arguments
// in both prototypes and definitions, which older Arduino toolchains dislike.
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
// 2. LOW-OVERHEAD HELPER FUNCTIONS
// ============================================================================
// Small helpers live here.
// They are intentionally tiny because this firmware calls them a lot.
// On an ESP8266, little utility functions like these help keep the code tidy
// without paying a big runtime penalty.
// ============================================================================

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static inline uint32_t elapsedMs(uint32_t now, uint32_t since) {
  return now - since;
}

static inline int clampInt(int v, int lo, int hi) {
  return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t clampU8(int v, uint8_t lo, uint8_t hi) {
  if (v < (int)lo) return lo;
  if (v > (int)hi) return hi;
  return (uint8_t)v;
}

static inline uint16_t clampU16(int v, uint16_t lo, uint16_t hi) {
  if (v < (int)lo) return lo;
  if (v > (int)hi) return hi;
  return (uint16_t)v;
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
// 3. ONBOARD LED FEEDBACK
// ============================================================================
// The builtin LED is our tiny hardware heartbeat.
// We pulse it for button taps, ACK success/failure hints, and activity.
// The LED service is non-blocking: we set an expiry timestamp, then turn it
// back off later inside the main loop.
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
// 4. HEADER / MESSAGE LAYER
// ============================================================================
// The TFT has a two-line header at the top.
//
// Line 1 priority:
//   transient toast > latest chat line > blank
//
// Line 2:
//   base status + last key event
//
// We cache what was previously drawn, so we do not redraw identical text.
// That cuts flicker and saves time.
// ============================================================================

String statusBase = "Boot...";
String lastKeyText = "K:--";
String transientStatus = "";
uint32_t transientUntilMs = 0;
String lastDrawLine1 = "";
String lastDrawLine2 = "";
String latestChat = "";

// ---------------------------------------------------------------------------
// Dedicated text page
// ---------------------------------------------------------------------------
// The pet UI is great for game data, but terrible for longer messages.
// So incoming text gets its own temporary full-screen page.
//
// Behavior:
// - Every new message is wrapped into multiple 26-character lines
// - New wrapped lines are appended to a small rolling history
// - The text page becomes visible for 2 seconds on new text
// - After the timeout, the display returns to the pet scene automatically
//
// This gives us a readable message view without permanently stealing the TFT.
String textPageLines[TEXT_PAGE_HISTORY_LINES];
uint8_t textPageLineCount = 0;
bool textPageActive = false;
uint32_t textPageUntilMs = 0;
uint32_t lastTextPageDrawToken = 0;

void drawLineArea(int y, const String& text) {
  tft.fillRect(0, y, SCREEN_W, LINE_H, COLOR_BG);
  tft.setCursor(0, y);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.print(truncateCols(text));
}

String computeLine1() {
  if (transientStatus.length()) return transientStatus;
  if (latestChat.length()) return latestChat;
  return "";
}

String computeLine2() {
  String line2 = statusBase;
  if (line2.length()) line2 += " ";
  line2 += lastKeyText;
  return line2;
}

void updateHeaderLine1(bool force = false) {
  String line1 = computeLine1();
  if (!force && line1 == lastDrawLine1) return;
  lastDrawLine1 = line1;
  drawLineArea(0, line1);
}

void updateHeaderLine2(bool force = false) {
  String line2 = computeLine2();
  if (!force && line2 == lastDrawLine2) return;
  lastDrawLine2 = line2;
  drawLineArea(LINE_H, line2);
}

void setToast(const String& msg, uint32_t ttlMs) {
  transientStatus = truncateCols(msg);
  transientUntilMs = millis() + ttlMs;
  if (!textPageActive) updateHeaderLine1(true);
}

void updateTransientStatus(uint32_t now) {
  if (transientStatus.length() && (int32_t)(now - transientUntilMs) >= 0) {
    transientStatus = "";
    if (!textPageActive) updateHeaderLine1(true);
  }
}

void clearUiMessages() {
  latestChat = "";
  transientStatus = "";
  transientUntilMs = 0;
  lastDrawLine1 = "";
  textPageLineCount = 0;
  textPageActive = false;
  textPageUntilMs = 0;
  lastTextPageDrawToken = 0;
  updateHeaderLine1(true);
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
      for (uint8_t i = 1; i < TEXT_PAGE_HISTORY_LINES; ++i) textPageLines[i - 1] = textPageLines[i];
      textPageLines[TEXT_PAGE_HISTORY_LINES - 1] = line;
    }

    start += take;
    while (start < (int)clean.length() && clean[start] == ' ') start++;
  }
}

void renderTextPage(bool force) {
  if (!textPageActive && !force) return;

  uint32_t token = (uint32_t)textPageLineCount ^ (textPageUntilMs << 1) ^ (uint32_t)textPageActive;
  if (!force && token == lastTextPageDrawToken) return;
  lastTextPageDrawToken = token;

  tft.fillScreen(COLOR_BG);
  tft.setTextWrap(false);
  tft.setTextSize(TEXT_SIZE);
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.setCursor(0, 0);
  tft.print("MESSAGES");
  tft.drawFastHLine(0, LINE_H, SCREEN_W, COLOR_DIM);

  tft.setTextColor(COLOR_FG, COLOR_BG);
  for (uint8_t i = 0; i < textPageLineCount && i < TEXT_PAGE_HISTORY_LINES; ++i) {
    tft.setCursor(0, (i + 1) * LINE_H);
    tft.print(textPageLines[i]);
  }
}

void showTextPage(uint32_t ttlMs) {
  textPageActive = true;
  textPageUntilMs = millis() + ttlMs;
  renderTextPage(true);
}

void serviceTextPage(uint32_t now) {
  if (!textPageActive) return;
  if ((int32_t)(now - textPageUntilMs) >= 0) {
    textPageActive = false;
    lastTextPageDrawToken = 0;
    renderPetUi(true);
  }
}
// ============================================================================
// 5. PICO SERIAL INPUT + ACK EVENT PATH
// ============================================================================
// The Pico speaks to the ESP using a tiny packet:
//   0xA5, type, key, checksum
//
// type == 1  -> key down
// type != 1  -> key up (release)
//
// Why this matters:
// - We want short vs long press detection
// - We want a compact serial format
// - We want corrupted packets to be ignored cleanly
//
// This section also contains the Lumia event sender.
// ============================================================================

uint32_t pressStart[KEY_COUNT] = {};
bool isDown[KEY_COUNT] = {};
bool picoSeen = false;
uint32_t seqCounter = 0;
uint32_t lastSeqSent = 0;
uint32_t lastAckSeq = 0;
bool lastPostOk = false;

// ----------------------------------------------------------------------------
// FUTURE REVISION SCAFFOLD 1: PET TELEMETRY / LUMIA VARIABLE SYNC
// ----------------------------------------------------------------------------
// Right now the firmware sends compact pet-change events as JSON posts.
//
// The next likely revision is a more formal telemetry contract between the ESP
// and a Lumia-side plugin. To make that future change painless, we define the
// ideas here in tutorial form:
//
// - Stable change IDs that should never shift once released
// - A compact packet shape for plugin-side variables
// - One obvious place to extend if we later add batching or checksums
//
// The current build still uses the existing JSON sender below, but these
// scaffolds explain where that future protocol should grow.
// ----------------------------------------------------------------------------

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
  uint8_t changeCode;      // Stable event meaning, for example hunger_down
  uint8_t fieldId;         // Which tracked field changed
  uint8_t fromValue;       // Previous value
  uint8_t toValue;         // New value
  int8_t delta;            // Signed difference, handy for plugin variables
  uint16_t ageMinutes;     // Snapshot context for later Lumia-side logic
};

// This helper is not strictly required in the current firmware, but it makes
// the code read like a tutorial: a pet change is not just an alert number, it
// is also a semantic field update.
PetTelemetryField telemetryFieldForChange(uint8_t changeCode) {
  switch (changeCode) {
    case 0: case 1:  return PET_FIELD_HUNGER;
    case 2: case 3:  return PET_FIELD_HAPPINESS;
    case 4: case 5:  return PET_FIELD_ENERGY;
    case 6: case 7:  return PET_FIELD_HYGIENE;
    case 8: case 9:  return PET_FIELD_HEALTH;
    case 10: case 11:return PET_FIELD_DISCIPLINE;
    case 12: case 13:return PET_FIELD_POOP;
    case 14: case 15:return PET_FIELD_STAGE;
    case 16: case 17:return PET_FIELD_SLEEPING;
    case 18: case 19:return PET_FIELD_SICK;
    case 20: case 21:return PET_FIELD_ALIVE;
    default:         return PET_FIELD_HUNGER;
  }
}

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

PetTransitionEvent petEventQueue[PET_CHANGE_QUEUE_SIZE];
uint8_t petEventHead = 0;
uint8_t petEventTail = 0;
uint32_t lastPetEventPostMs = 0;
uint8_t lastPetEventCode = 255;

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

// Read one complete Pico packet if available.
// Returns true only when a full packet with a valid checksum arrives.
// If bytes are corrupted or incomplete, the state machine safely recovers.
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

// Send one event to the Lumia-side plugin.
// This function is shared by both:
//   1) normal key ACK events
//   2) pet transition events
//
// The payload is JSON because it is easy to inspect during development.
// We keep it compact, but readable.
bool postEventToPlugin(uint8_t eventNumber, uint8_t keyIndex, const char* pressKind, uint32_t heldMs, uint32_t seq, const char* extraJson) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  String url = String("http://") + PLUGIN_HOST + ":" + String(PLUGIN_PORT) + "/event";

  String body;
  body.reserve(320);
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

// Try to send one queued pet transition to Lumia.
// Why queue them?
// Because simulation can generate multiple changes in one burst.
// Queuing avoids blocking the pet logic and lets network delivery happen
// gradually and safely.
bool servicePetTransitionQueue(uint32_t now) {
  if (petEventHead == petEventTail) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (elapsedMs(now, lastPetEventPostMs) < PET_EVENT_MIN_GAP_MS) return false;

  const PetTransitionEvent &ev = petEventQueue[petEventHead];
  int16_t delta = (int16_t)ev.toValue - (int16_t)ev.fromValue;
  char extra[176];
  snprintf(extra, sizeof(extra),
           "\"petChange\":true,\"variation\":%u,\"changeCode\":%u,\"changeName\":\"%s\",\"from\":%u,\"to\":%u,\"delta\":%d",
           (unsigned)ev.code, (unsigned)ev.code, petChangeName(ev.code),
           (unsigned)ev.fromValue, (unsigned)ev.toValue, (int)delta);

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

bool checkFactoryResetHold() {
  uint32_t start = millis();
  bool keyDown = false;
  uint32_t downAt = 0;

  while (elapsedMs(millis(), start) < FACTORY_RESET_WINDOW_MS) {
    uint8_t type, key;
    if (readPicoPacket(type, key)) {
      picoSeen = true;
      if (key == FACTORY_RESET_KEY) {
        if (type == 1) {
          keyDown = true;
          downAt = millis();
        } else {
          keyDown = false;
        }
      }
    }
    if (keyDown && elapsedMs(millis(), downAt) >= FACTORY_RESET_HOLD_MS) return true;
    yield();
  }
  return false;
}

// ============================================================================
// 6. DIRECT ESP BUTTON INPUT
// ============================================================================
// These are the two extra SPST buttons mounted directly on the ESP side.
//
// We do not trust raw digitalRead() values.
// Mechanical buttons bounce, so a single press can look like a burst of taps.
// The small debouncer below turns noisy input into clean pressed/released events.
// ============================================================================

struct DebouncedButton {
  uint8_t pin;
  bool stableState;
  bool rawState;
  uint32_t lastChangeMs;
  bool pressedEvent;
  bool releasedEvent;
  uint32_t pressedAt;
};

DebouncedButton btnA { BTN_A_PIN, HIGH, HIGH, 0, false, false, 0 };
DebouncedButton btnB { BTN_B_PIN, HIGH, HIGH, 0, false, false, 0 };
uint32_t bothHeldStart = 0;
bool comboConsumed = false;

void initButton(DebouncedButton& b) {
  pinMode(b.pin, INPUT_PULLUP);
  bool s = digitalRead(b.pin);
  b.stableState = s;
  b.rawState = s;
  b.lastChangeMs = millis();
  b.pressedEvent = false;
  b.releasedEvent = false;
  b.pressedAt = 0;
}

// Debounce one button.
// The logic is:
//   raw input changes -> wait a short time -> accept as stable change
// This prevents false multiple triggers from mechanical bounce.
void updateButton(DebouncedButton& b, uint32_t now) {
  bool reading = digitalRead(b.pin);
  b.pressedEvent = false;
  b.releasedEvent = false;

  if (reading != b.rawState) {
    b.rawState = reading;
    b.lastChangeMs = now;
  }

  if (reading != b.stableState && elapsedMs(now, b.lastChangeMs) >= BUTTON_DEBOUNCE_MS) {
    b.stableState = reading;
    if (b.stableState == BTN_ACTIVE) {
      b.pressedEvent = true;
      b.pressedAt = now;
    } else {
      b.releasedEvent = true;
    }
  }
}

bool isPressed(const DebouncedButton& b) {
  return b.stableState == BTN_ACTIVE;
}

// ============================================================================
// 7. PET SAVE DATA + CRC PROTECTION
// ============================================================================
// EEPROM is tiny and not immortal.
//
// To keep save data trustworthy, we store:
// - a magic number   -> “is this even our save file?”
// - a version number -> “does this match the current structure?”
// - a length field   -> “did the struct layout change?”
// - a CRC            -> “did the bytes get corrupted?”
//
// We also defer writes instead of committing every tiny stat change.
// That reduces EEPROM wear.
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

// ----------------------------------------------------------------------------
// FUTURE REVISION SCAFFOLD 2: GAMEPLAY DEPTH / BALANCE TABLES
// ----------------------------------------------------------------------------
// The current pet model is intentionally simple. That keeps the firmware stable
// and easy to tune.
//
// But the next likely evolution is richer gameplay, where the pet can develop
// tendencies or personalities and where stage-specific balance values live in
// data tables instead of being buried in conditionals.
//
// These structs are tutorial placeholders for that future. They let you see
// where deeper balancing can plug in without forcing that complexity today.
// ----------------------------------------------------------------------------

enum PetTraitFlags : uint8_t {
  PET_TRAIT_NONE        = 0x00,
  PET_TRAIT_GLUTTONOUS  = 0x01,
  PET_TRAIT_PLAYFUL     = 0x02,
  PET_TRAIT_STUBBORN    = 0x04,
  PET_TRAIT_SLEEPY      = 0x08,
  PET_TRAIT_RESILIENT   = 0x10
};

struct PetBalanceProfile {
  uint8_t hungerDecayPerTick;
  uint8_t happinessDecayPerTick;
  uint8_t energyDecayPerTick;
  uint8_t hygieneDecayPerTick;
  uint8_t sicknessChancePct;
};

struct FutureGameplayState {
  uint8_t traitFlags;
  uint8_t weight;
  uint8_t evolutionPath;
};

// Default profiles are included as teaching examples.
// The current runtime does not fully consume them yet, but they show the clean
// direction for future balancing work.
const PetBalanceProfile FUTURE_STAGE_BALANCE[] = {
  {1, 0, 0, 0, 0},   // egg
  {2, 1, 1, 1, 3},   // baby
  {3, 2, 2, 2, 5},   // child
  {4, 3, 2, 3, 7},   // teen
  {4, 3, 3, 3, 9},   // adult
  {5, 4, 4, 4, 12}   // elder
};

struct SaveBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  PetState pet;
  uint16_t crc;
};

PetState pet;
bool saveDirty = false;
uint32_t saveDirtyAt = 0;

// Standard CRC16-CCITT.
// This is our save-data integrity check.
// If the EEPROM bytes no longer match the CRC, we treat the save as invalid.
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

// Reset the pet to a known-good starting state.
// firstBoot=true is used when creating a fresh pet during initial startup.
// In that case we also reset the pet-event baseline cleanly.
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
  if (!firstBoot) markSaveDirty();
  if (firstBoot) {
    resetPetEventQueue();
    syncPetTransitionBaseline();
  }
}

// Load the pet from EEPROM.
// This will only succeed if:
// - magic matches
// - version matches
// - struct length matches
// - CRC matches
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

// Commit the save only after the pet has been dirty for a while.
// This reduces EEPROM wear by collapsing lots of tiny changes into one write.
void serviceSave(uint32_t now) {
  if (saveDirty && elapsedMs(now, saveDirtyAt) >= SAVE_DEFER_MS) {
    savePetNow();
  }
}

// ============================================================================
// 8. PET GAME MODEL
// ============================================================================
// This section is the creature’s brain and biology.
//
// It decides:
// - how aging works
// - when stats decay
// - when sickness starts
// - what each action changes
// - how stages evolve
//
// The model is intentionally simple and deterministic.
// That makes it easier to tune and easier to debug.
// ============================================================================

enum PetAction : uint8_t {
  ACT_FEED = 0,
  ACT_PLAY,
  ACT_CLEAN,
  ACT_SLEEP,
  ACT_MED,
  ACT_DISCIPLINE,
  ACT_COUNT
};

const char* ACTION_NAMES[ACT_COUNT] = {
  "FEED", "PLAY", "CLEAN", "SLEEP", "MED", "TEACH"
};

uint8_t selectedAction = ACT_FEED;
uint8_t animFrame = 0;
uint32_t lastGameTickMs = 0;
uint32_t lastAnimTickMs = 0;
uint32_t lastStatusRotateMs = 0;
uint8_t statusPage = 0;
PetSnapshot lastBroadcastPet = {};

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

// Compare an old pet snapshot against a new one.
// Any meaningful change becomes a Lumia-side transition event.
// Example: hunger 72 -> 70 becomes hunger_down.
void detectAndQueuePetTransitions(const PetSnapshot &before, const PetSnapshot &after) {
  queueDeltaPair(PET_CHG_HUNGER_UP, PET_CHG_HUNGER_DOWN, before.hunger, after.hunger);
  queueDeltaPair(PET_CHG_HAPPINESS_UP, PET_CHG_HAPPINESS_DOWN, before.happiness, after.happiness);
  queueDeltaPair(PET_CHG_ENERGY_UP, PET_CHG_ENERGY_DOWN, before.energy, after.energy);
  queueDeltaPair(PET_CHG_HYGIENE_UP, PET_CHG_HYGIENE_DOWN, before.hygiene, after.hygiene);
  queueDeltaPair(PET_CHG_HEALTH_UP, PET_CHG_HEALTH_DOWN, before.health, after.health);
  queueDeltaPair(PET_CHG_DISCIPLINE_UP, PET_CHG_DISCIPLINE_DOWN, before.discipline, after.discipline);
  queueDeltaPair(PET_CHG_POOP_UP, PET_CHG_POOP_DOWN, before.poop, after.poop);
  queueDeltaPair(PET_CHG_STAGE_UP, PET_CHG_STAGE_DOWN, before.stage, after.stage);
  if (before.sleeping != after.sleeping) enqueuePetTransition(after.sleeping ? PET_CHG_SLEEP_ON : PET_CHG_SLEEP_OFF, before.sleeping, after.sleeping);
  if (before.sick != after.sick) enqueuePetTransition(after.sick ? PET_CHG_SICK_ON : PET_CHG_SICK_OFF, before.sick, after.sick);
  if (before.alive != after.alive) enqueuePetTransition(after.alive ? PET_CHG_ALIVE_ON : PET_CHG_ALIVE_OFF, before.alive, after.alive);
}

void syncPetTransitionBaseline() {
  snapshotPet(lastBroadcastPet);
}

void commitPetTransitions() {
  PetSnapshot nowSnap;
  snapshotPet(nowSnap);
  detectAndQueuePetTransitions(lastBroadcastPet, nowSnap);
  lastBroadcastPet = nowSnap;
}


uint8_t stageFromAgeMinutes(uint16_t ageMinutes) {
  if (ageMinutes < 6) return 0;      // egg
  if (ageMinutes < 24) return 1;     // baby
  if (ageMinutes < 90) return 2;     // child
  if (ageMinutes < 240) return 3;    // teen
  if (ageMinutes < 800) return 4;    // adult
  return 5;                          // elder
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

// Clamp every stat to sane limits.
// This is a defensive “no weird numbers survive” function.
// After major updates, we funnel values through here.
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

// Advance the pet by N minutes.
// This is minute-scale simulation, not per-frame simulation.
// That makes the pet tolerant of lag, Wi-Fi hiccups, and temporary stalls.
// If the device was busy for a while, the pet can catch up in chunks.
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

// Catch the pet up to the current time using elapsed minutes.
// We do not simulate every loop pass.
// Instead, we only apply minute-sized steps when enough real time has passed.
void simulatePetToNow(uint32_t now) {
  if (!pet.alive) return;
  uint32_t delta = elapsedMs(now, pet.lastSimMs);
  if (delta < 60000UL) return;
  uint16_t mins = (uint16_t)(delta / 60000UL);
  agePetByMinutes(mins);
  pet.lastSimMs += (uint32_t)mins * 60000UL;
  markSaveDirty();
}

// Apply one user action to the pet.
// This is the “gameplay verb” layer.
// Actions change stats, set toast messages, and mark the save dirty.
void performPetAction(PetAction action) {
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

    default:
      break;
  }

  pet.actionsTaken++;
  applyBounds();
  markSaveDirty();
}

// ============================================================================
// 9. PET RENDERING
// ============================================================================
// Rendering is split from simulation on purpose.
//
// The pet “lives” in the model section.
// This section only draws what the model currently says.
//
// Important efficiency idea:
// We cache previously drawn text/action state and only redraw changed regions.
// The sprite area is also limited to a fixed box instead of repainting the
// whole display every frame.
// ============================================================================

// ----------------------------------------------------------------------------
// FUTURE REVISION SCAFFOLD 3: MODULAR SPRITE / VISUAL PIPELINE
// ----------------------------------------------------------------------------
// The current build draws the pet from primitive shapes. That is efficient and
// easy to edit by hand, which is great for a first stable release.
//
// The next likely visual upgrade is a real asset pipeline:
// - stage-specific sprite sets
// - mood overlays such as sick, sleepy, dirty, happy
// - simple frame animation controlled by tables rather than hardcoded drawing
//
// These placeholder structs are here so the file already teaches that future
// direction. You can expand them later without reorganising the whole sketch.
// ----------------------------------------------------------------------------

enum FutureSpriteLayer : uint8_t {
  SPR_LAYER_BODY = 0,
  SPR_LAYER_FACE,
  SPR_LAYER_EFFECT
};

struct FutureSpriteFrame {
  const uint8_t* bitmap;
  uint8_t width;
  uint8_t height;
  int8_t offsetX;
  int8_t offsetY;
};

struct FuturePetVisualSet {
  const FutureSpriteFrame* bodyFrames;
  uint8_t bodyFrameCount;
  const FutureSpriteFrame* faceFrames;
  uint8_t faceFrameCount;
};


// -----------------------------------------------------------------------------
// Fast sprite assets
// -----------------------------------------------------------------------------
// For the highest possible refresh rate on an ESP8266 + ST7735 pairing, we keep
// the pet body as tiny 1-bit bitmaps in PROGMEM and only redraw the sprite box
// when the frame or visible mood changes.
//
// Each growth stage has 2 lightweight animation frames. The frames are tiny on
// purpose: pushing a 24x24 bitmap over SPI is much cheaper than rebuilding the
// creature from lots of primitive shapes every tick.
// -----------------------------------------------------------------------------

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
uint8_t lastDrawnAction = 255;
uint8_t lastAnimStateHash = 255;

// Dirty-state caches for the stat bars and sprite layers.
// The TFT is much happier when we redraw only the exact areas that changed.
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
  // Each bar remembers its last value and color.
  // If neither changed, there is nothing to redraw.
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
  lastDrawnAction = 255;
  lastAnimStateHash = 255;
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

// Draw the creature itself.
// The sprite is deliberately hand-drawn from primitives instead of using
// a large bitmap. That saves space and keeps the sketch easy to tinker with.

void drawPetSprite() {
  // This version is aggressively optimized for refresh rate.
  //
  // Instead of drawing the creature from many TFT primitives every time the
  // animation advances, we keep the pet body as a tiny 1-bit bitmap and redraw
  // only one small sprite rectangle when something visible changes.
  //
  // Dirty regions:
  // - sprite box: only when frame / stage / alive / mood changes
  // - poop strip: only when poop count changes
  //
  // This keeps the SPI workload small enough for the ST7735 to feel much more
  // lively on an ESP8266.

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

    // Clear only the sprite box, then restore the ground slice under it.
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
      // A very cheap memorial marker when the pet is down.
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

      // Tiny mood overlay. This is kept intentionally small so the sprite still
      // animates fast even while the face changes.
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

void drawActionStrip() {
  if (selectedAction == lastDrawnAction) return;
  lastDrawnAction = selectedAction;
  const int y = ACTION_STRIP_Y;
  tft.fillRect(0, y, SCREEN_W, 10, COLOR_BG);
  tft.setCursor(0, y);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.print("A:");
  tft.setTextColor(COLOR_ACCENT, COLOR_BG);
  tft.print(ACTION_NAMES[selectedAction]);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.print(" B:GO");
}

// Draw the text HUD and side stat bars.
// The status page rotates so we can show more information without cramming
// everything into one unreadable line.
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

  const int bx = 4;
  const int by = GAME_Y + 32;
  const int bw = 26;
  drawBar(bx, by, bw, pet.hunger, moodColor(pet.hunger), 0);
  drawBar(bx, by + 10, bw, pet.happiness, moodColor(pet.happiness), 1);
  drawBar(bx, by + 20, bw, pet.energy, moodColor(pet.energy), 2);
  drawBar(bx, by + 30, bw, pet.hygiene, moodColor(pet.hygiene), 3);
  drawBar(130, by, bw, pet.health, moodColor(pet.health), 4);
  drawBar(130, by + 10, bw, pet.discipline, moodColor(pet.discipline), 5);
}

void renderPetUi(bool force) {
  if (force) clearPetArea();
  drawHudAndBars();
  drawPetSprite();
  drawActionStrip();
}

// ============================================================================
// 10. HTTP API
// ============================================================================
// The built-in web server is small but very useful.
// It allows:
// - pushing chat/status text to the display
// - clearing the UI
// - reading health info
// - controlling the pet remotely
//
// This makes the firmware easy to test from a browser or Lumia-side tooling.
// ============================================================================

void pushWrappedChat(const String& msgRaw) {
  latestChat = truncateCols(msgRaw);
  appendTextPageMessage(msgRaw);
  showTextPage(TEXT_PAGE_SHOW_MS);
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
  statusBase = truncateCols(text);
  appendTextPageMessage(String("STATUS: ") + text);
  showTextPage(TEXT_PAGE_SHOW_MS);
  server.send(200, "text/plain", "OK");
}

void handleClear() {
  clearUiMessages();
  renderPetUi(true);
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
    renderPetUi(true);
    return server.send(200, "text/plain", "OK");
  }

  if (channel == "status") {
    if (!text.length()) return server.send(400, "text/plain", "Missing text");
    statusBase = truncateCols(text);
    appendTextPageMessage(String("STATUS: ") + text);
    showTextPage(TEXT_PAGE_SHOW_MS);
    return server.send(200, "text/plain", "OK");
  }

  if (!text.length()) return server.send(400, "text/plain", "Missing text");
  pushWrappedChat(text);
  server.send(200, "text/plain", "OK");
}

String petStatusJson() {
  String json;
  json.reserve(360);
  json = "{";
  json += "\"alive\":" + String(pet.alive ? "true" : "false");
  json += ",\"stage\":\"" + String(stageName(pet.stage)) + "\"";
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

// Main pet HTTP endpoint.
// Example:
//   /pet?action=feed
//   /pet?action=status
//   /pet?action=reset
//
// This is great for debugging because you can drive the game without pressing
// physical buttons.
void handlePet() {
  if (!server.hasArg("action")) return server.send(400, "text/plain", "Missing action");
  String action = server.arg("action");

  if (action == "status") return server.send(200, "application/json", petStatusJson());
  if (action == "sync") { resetPetEventQueue(); syncPetTransitionBaseline(); return server.send(200, "application/json", petStatusJson()); }
  if (action == "feed") performPetAction(ACT_FEED);
  else if (action == "play") performPetAction(ACT_PLAY);
  else if (action == "clean") performPetAction(ACT_CLEAN);
  else if (action == "sleep" || action == "toggleSleep") performPetAction(ACT_SLEEP);
  else if (action == "med") performPetAction(ACT_MED);
  else if (action == "discipline") performPetAction(ACT_DISCIPLINE);
  else if (action == "reset") { resetPet(); commitPetTransitions(); setToast("Pet reset", 1500); }
  else return server.send(400, "text/plain", "Unknown action");

  renderPetUi(false);
  server.send(200, "application/json", petStatusJson());
}

void handleHealth() {
  String ip = WiFi.localIP().toString();
  long rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  String json;
  json.reserve(260);
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
  json += ",\"petAlive\":" + String(pet.alive ? "true" : "false");
  json += ",\"petStage\":\"" + String(stageName(pet.stage)) + "\"";
  json += ",\"petEventBase\":" + String(PET_ALERT_BASE);
  json += ",\"petEventCount\":" + String(PET_CHANGE_EVENT_COUNT);
  json += "}";
  server.send(200, "application/json", json);
}

// ============================================================================
// 11. WIFI MAINTENANCE
// ============================================================================
// Wi-Fi can disappear.
// We do not want the whole sketch to panic when that happens.
//
// This helper performs lightweight reconnect attempts on a timer, rather than
// hammering the radio continuously.
// ============================================================================

uint32_t lastWifiAttemptMs = 0;

// Lightweight Wi-Fi reconnect helper.
// We do not sit here blocking until Wi-Fi returns.
// We just nudge reconnect attempts at a controlled interval.
void ensureWiFi(uint32_t now) {
  int st = WiFi.status();
  if (st == WL_CONNECTED) return;
  if (elapsedMs(now, lastWifiAttemptMs) < WIFI_RETRY_MS) return;
  lastWifiAttemptMs = now;
  WiFi.reconnect();
}

// ============================================================================
// 12. BOOT UI / STARTUP HELPERS
// ============================================================================
// These functions handle the “power-on theatre”.
// The boot splash is also useful for debugging because it shows how far the
// firmware got before a problem.
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
  tft.drawRoundRect(26, 78, 108, 26, 6, COLOR_DIM);
  tft.setCursor(40, 87);
  tft.print("ACK MODE");
}

void fullUiInit() {
  tft.fillScreen(COLOR_BG);
  lastDrawLine1 = "__force__";
  lastDrawLine2 = "__force__";
  updateHeaderLine1(true);
  updateHeaderLine2(true);
  renderPetUi(true);
}

// ============================================================================
// 13. SETUP AND MAIN LOOP
// ============================================================================
// setup() runs once.
// loop() runs forever.
//
// The main loop is intentionally organised as a service chain.
// Each subsystem gets a quick turn, then we yield.
// This keeps the firmware responsive without threads or RTOS complexity.
// ============================================================================

// Arduino setup()
// Think of this as firmware boot choreography:
// - prepare hardware
// - prepare display
// - restore or create pet
// - configure Wi-Fi
// - start web endpoints
// - draw the first UI
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  ledOff();

  Serial.begin(PICO_BAUD);
  Serial.setDebugOutput(false);
  delay(120);

  initButton(btnA);
  initButton(btnB);

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

  statusBase = "Boot...";
  updateHeaderLine2(true);

  setToast("Hold K0=Reset", 1800);
  if (checkFactoryResetHold()) {
    drawBootSplash("WiFi reset");
    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.resetSettings();
    delay(200);
    ESP.restart();
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
    statusBase = "IP:" + WiFi.localIP().toString();
  } else {
    statusBase = "WiFi:DOWN";
  }

  server.on("/", handleRoot);
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
  setToast("Ready", 1200);
}

// Process the two ESP-side gameplay buttons.
// A = cycle action
// B = perform action
// A+B hold = reset pet
void serviceButtons(uint32_t now) {
  updateButton(btnA, now);
  updateButton(btnB, now);

  if (btnA.pressedEvent) pulseLed(20);
  if (btnB.pressedEvent) pulseLed(20);

  bool both = isPressed(btnA) && isPressed(btnB);
  if (both) {
    if (!bothHeldStart) bothHeldStart = now;
    if (!comboConsumed && elapsedMs(now, bothHeldStart) >= BUTTON_COMBO_HOLD_MS) {
      comboConsumed = true;
      resetPet();
      commitPetTransitions();
      renderPetUi(true);
      setToast("Pet reset", 1500);
    }
  } else {
    bothHeldStart = 0;
    comboConsumed = false;
  }

  if (btnA.pressedEvent && !both) {
    selectedAction = (selectedAction + 1) % ACT_COUNT;
    drawActionStrip();
  }

  if (btnB.pressedEvent && !both) {
    performPetAction((PetAction)selectedAction);
    renderPetUi(false);
  }
}

// Time-based pet servicing.
// This splits work into three rhythms:
// - GAME_TICK_MS   -> simulation catch-up / UI heartbeat
// - ANIM_TICK_MS   -> sprite bob/blink animation
// - STATUS_ROTATE  -> cycle which stats are shown in the HUD
void servicePetTiming(uint32_t now) {
  if (elapsedMs(now, lastGameTickMs) >= GAME_TICK_MS) {
    lastGameTickMs += GAME_TICK_MS;
    // 1 second UI heartbeat, real simulation done by minute delta.
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

// Process key events coming from the Pico.
// The Pico tells us when keys go down and up.
// We use the duration between those two moments to classify short vs long press.
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
    updateHeaderLine2(true);

    seqCounter++;
    uint32_t seq = seqCounter;
    lastSeqSent = seq;

    bool ok = postEventToPlugin(eventOut, key, pressKind, held, seq);
    lastPostOk = ok;

    if (ok) {
      pulseLed(34);
      setToast("ACK OK", HEADER_TOAST_MS);
    } else {
      pulseLed(120);
      setToast("ACK FAIL", 2600);
    }
  }
}

// Main loop
// Every pass:
// - service web requests
// - keep LED pulse timing alive
// - expire toast messages
// - recover Wi-Fi if needed
// - read direct buttons
// - read Pico events
// - advance pet timers
// - flush one queued pet transition
// - save EEPROM if the defer window elapsed
//
// The order is chosen to keep input/UI snappy while network and save work remain
// opportunistic rather than dominant.
void loop() {
  uint32_t now = millis();

  server.handleClient();
  serviceLed(now);
  updateTransientStatus(now);
  serviceTextPage(now);
  ensureWiFi(now);
  serviceButtons(now);
  servicePicoEvents();
  servicePetTiming(now);
  servicePetTransitionQueue(now);
  serviceSave(now);

  yield();
}
