// Coffee Pal for the Waveshare ESP32-S3-Touch-LCD-1.47.
// Hardware only: the UI lives in ui.cpp, the tray rules in config.h and tray.h.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <Wire.h>
#include "ui.h"

// Pins and panel setup come from Waveshare's demo code for this board.
#define PIN_LCD_DC 45
#define PIN_LCD_CS 21
#define PIN_LCD_SCK 38
#define PIN_LCD_MOSI 39
#define PIN_LCD_RST 40
#define PIN_LCD_BL 46
#define PIN_TOUCH_SDA 42
#define PIN_TOUCH_SCL 41
#define PIN_TOUCH_RST 47
#define TOUCH_ADDR 0x63
#define TOUCH_DATA_REG 0x01

#define PANEL_W 172
#define PANEL_H 320
#define LCD_SPI_HZ 40000000
#define FRAME_MS 33

static Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_MOSI);
static Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_LCD_RST, 0, false, PANEL_W, PANEL_H, 34, 0, 34, 0);

static uint16_t *frame;   // Landscape, what the UI draws into.
static uint16_t *panel;   // Portrait, big-endian, what the display currently shows.
static uint16_t *column;  // One panel row being prepared.
static CoffeePalUI ui;
static Preferences prefs;
static bool touchOk = false;

// JD9853 register setup, as supplied in Waveshare's demo.
static void panelInit() {
  static const uint8_t ops[] = {
      BEGIN_WRITE, WRITE_COMMAND_8, 0x11, END_WRITE, DELAY, 120,
      BEGIN_WRITE,
      WRITE_C8_D16, 0xDF, 0x98, 0x53,
      WRITE_C8_D8, 0xB2, 0x23,
      WRITE_COMMAND_8, 0xB7, WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,
      WRITE_COMMAND_8, 0xBB, WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
      WRITE_C8_D16, 0xC0, 0x44, 0xA4,
      WRITE_C8_D8, 0xC1, 0x16,
      WRITE_COMMAND_8, 0xC3, WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
      WRITE_COMMAND_8, 0xC4, WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
      WRITE_COMMAND_8, 0xC8, WRITE_BYTES, 32,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      WRITE_COMMAND_8, 0xD0, WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
      WRITE_C8_D16, 0xD7, 0x00, 0x30,
      WRITE_C8_D8, 0xE6, 0x14,
      WRITE_C8_D8, 0xDE, 0x01,
      WRITE_COMMAND_8, 0xB7, WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
      WRITE_COMMAND_8, 0xC1, WRITE_BYTES, 3, 0x14, 0x15, 0xC0,
      WRITE_C8_D16, 0xC2, 0x06, 0x3A,
      WRITE_C8_D16, 0xC4, 0x72, 0x12,
      WRITE_C8_D8, 0xBE, 0x00,
      WRITE_C8_D8, 0xDE, 0x02,
      WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x00, 0x02, 0x00,
      WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x01, 0x02, 0x00,
      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x35, 0x00,
      WRITE_C8_D8, 0x3A, 0x05,
      WRITE_COMMAND_8, 0x2A, WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,
      WRITE_COMMAND_8, 0x2B, WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,
      WRITE_C8_D8, 0xDE, 0x02,
      WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x00, 0x02, 0x00,
      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x36, 0x00,
      WRITE_COMMAND_8, 0x21,
      END_WRITE, DELAY, 10,
      BEGIN_WRITE, WRITE_COMMAND_8, 0x29, END_WRITE,
  };
  bus->batchOperation(ops, sizeof(ops));
}

static void touchInit() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(200);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(300);
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.beginTransmission(TOUCH_ADDR);
  touchOk = Wire.endTransmission() == 0;
}

// Returns the finger position in landscape UI coordinates.
static bool touchRead(int &x, int &y) {
  if (!touchOk) return false;
  uint8_t d[6];
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(TOUCH_DATA_REG);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(TOUCH_ADDR, 6) != 6) return false;
  Wire.readBytes(d, 6);
  if (d[1] == 0 || d[1] > 5) return false;
  int rawX = ((d[2] & 0x0F) << 8) | d[3];
  int rawY = ((d[4] & 0x0F) << 8) | d[5];
#if FLIP_SCREEN
  x = SCREEN_W - 1 - rawY;
  y = SCREEN_H - 1 - rawX;
#else
  x = rawY;
  y = rawX;
#endif
  return true;
}

// Rotates the landscape frame into the panel's portrait orientation and sends only
// the panel rows that changed. Each panel row is one landscape column.
static void flushFrame() {
  int runStart = -1;
  for (int py = 0; py <= PANEL_H; py++) {
    bool changed = false;
    if (py < PANEL_H) {
#if FLIP_SCREEN
      const int lx = SCREEN_W - 1 - py;
#else
      const int lx = py;
#endif
      for (int px = 0; px < PANEL_W; px++) {
#if FLIP_SCREEN
        uint16_t v = frame[px * SCREEN_W + lx];
#else
        uint16_t v = frame[(SCREEN_H - 1 - px) * SCREEN_W + lx];
#endif
        column[px] = (uint16_t)((v << 8) | (v >> 8));
      }
      uint16_t *row = panel + py * PANEL_W;
      changed = memcmp(row, column, PANEL_W * 2) != 0;
      if (changed) memcpy(row, column, PANEL_W * 2);
    }
    if (changed && runStart < 0) runStart = py;
    if (!changed && runStart >= 0) {
      gfx->draw16bitBeRGBBitmap(0, runStart, panel + runStart * PANEL_W, PANEL_W, py - runStart);
      runStart = -1;
    }
  }
}

static void setBacklight(int percent) {
  ledcWrite(PIN_LCD_BL, (1023 * percent) / 100);
}

// Debug console over USB: c/f/r press the buttons, i prints status, s dumps the frame.
static void handleSerial(uint32_t now, float fps, uint32_t drawUs, uint32_t flushUs) {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'c': ui.press(BTN_COFFEE, now); break;
      case 'f': ui.press(BTN_FROTH, now); break;
      case 'r': ui.press(BTN_RESET, now); break;
      case 'i':
        Serial.printf("coffees=%d froths=%d left=%d full=%d touch=%d fps=%.1f draw=%luus flush=%luus\n",
                      ui.tray().coffees, ui.tray().froths, ui.tray().remaining(), ui.tray().full(), touchOk, fps,
                      drawUs, flushUs);
        Serial.flush();
        break;
      case 's':
        Serial.printf("FRAME %d %d\n", SCREEN_W, SCREEN_H);
        Serial.write((const uint8_t *)frame, SCREEN_W * SCREEN_H * 2);
        Serial.flush();
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);

  frame = (uint16_t *)heap_caps_malloc(SCREEN_W * SCREEN_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  panel = (uint16_t *)heap_caps_calloc(PANEL_W * PANEL_H, 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  column = (uint16_t *)heap_caps_malloc(PANEL_W * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  ledcAttach(PIN_LCD_BL, 5000, 10);
  setBacklight(0);

  if (!gfx->begin(LCD_SPI_HZ)) Serial.println("Display init failed");
  panelInit();
  gfx->setRotation(0);
  gfx->fillScreen(0x0000);

  touchInit();
  Serial.printf("Touch %s\n", touchOk ? "ok" : "NOT FOUND");

  prefs.begin("coffeepal", false);
  Tray saved;
  saved.coffees = prefs.getUShort("coffees", 0);
  saved.froths = prefs.getUShort("froths", 0);
  ui.begin(saved, millis());
}

void loop() {
  static uint32_t lastFrame = 0, statStart = 0, drawSum = 0, flushSum = 0, drawUs = 0, flushUs = 0;
  static int statFrames = 0, shownBacklight = -1;
  static float fps = 0;
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) {
    delay(1);
    return;
  }
  lastFrame = now;

  handleSerial(now, fps, drawUs, flushUs);

  int x = 0, y = 0;
  bool touched = touchRead(x, y);
  ui.update(now, touched, x, y);

  // Saved on every count so a power cut never loses the tally.
  if (ui.takeChanged()) {
    prefs.putUShort("coffees", ui.tray().coffees);
    prefs.putUShort("froths", ui.tray().froths);
  }

  Canvas canvas(frame, SCREEN_W, SCREEN_H);
  uint32_t t0 = micros();
  ui.draw(canvas);
  uint32_t t1 = micros();
  flushFrame();
  drawSum += t1 - t0;
  flushSum += micros() - t1;

  int backlight = ui.backlightPercent();
  if (backlight != shownBacklight) {
    setBacklight(backlight);
    shownBacklight = backlight;
  }

  if (++statFrames == 100) {
    fps = 100000.f / (now - statStart);
    drawUs = drawSum / 100;
    flushUs = flushSum / 100;
    statFrames = 0;
    statStart = now;
    drawSum = flushSum = 0;
  }
}
