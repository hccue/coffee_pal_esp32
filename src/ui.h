#pragma once
#include <stdint.h>
#include "gfx.h"
#include "tray.h"

#define SCREEN_W 320
#define SCREEN_H 172

enum Button { BTN_COFFEE, BTN_FROTH, BTN_RESET, BTN_COUNT, BTN_NONE = -1 };

class CoffeePalUI {
 public:
  void begin(const Tray &saved, uint32_t nowMs);
  // Call once per frame with the current finger position (landscape coordinates).
  void update(uint32_t nowMs, bool touched, int x, int y);
  void draw(Canvas &c);
  // Same as a finger landing on the button.
  void press(Button b, uint32_t nowMs);

  const Tray &tray() const { return tray_; }
  // True once after each count change, so the caller can persist it.
  bool takeChanged();
  int backlightPercent() const;
  bool sleeping() const { return sleeping_; }
  // 1 right after + Coffee / + Froth is pressed, fading to 0 over RECENT_PRESS_MS.
  float recentPress(Button b) const;

 private:
  enum Reaction { REACT_NONE, REACT_COFFEE, REACT_FROTH, REACT_CLEAN, REACT_ALARM };
  enum ParticleKind { P_COFFEE, P_BUBBLE, P_SPARKLE, P_PLUS, P_WATER, P_SWEAT };
  struct Particle {
    float x, y, vx, vy, gravity, life, age, size;
    uint8_t kind;
  };
  static const int MAX_PARTICLES = 24;

  void react(Reaction r);
  void spawn(ParticleKind kind, float x, float y, float vx, float vy, float gravity, float life, float size);
  void stepParticles(float dt);
  float rnd();

  void drawBackground(Canvas &c);
  void drawTrayBar(Canvas &c);
  void drawMug(Canvas &c);
  void drawFace(Canvas &c, float cx, float top, float w);
  void drawCounts(Canvas &c);
  void drawStatus(Canvas &c);
  void drawButtons(Canvas &c);
  void drawButton(Canvas &c, Button b);
  void drawParticles(Canvas &c);

  Tray tray_;
  bool changed_ = false;
  uint32_t now_ = 0;
  float t_ = 0;
  float dt_ = 0;

  bool wasTouched_ = false;
  uint32_t lastTouchMs_ = 0;
  uint32_t lastActivityMs_ = 0;
  bool sleeping_ = false;

  Button held_ = BTN_NONE;
  float buttonPress_[BTN_COUNT] = {0, 0, 0};
  float buttonFlash_[BTN_COUNT] = {0, 0, 0};
  uint32_t resetArmedUntil_ = 0;
  uint32_t lastPressMs_[2] = {0, 0};  // Last coffee / froth press; 0 means none yet.

  Reaction reaction_ = REACT_NONE;
  uint32_t reactionStart_ = 0;
  float coffeePop_ = 0;
  float frothPop_ = 0;
  float level_ = 0;
  float foam_ = 0;
  uint32_t cleanUntil_ = 0;

  uint32_t nextBlink_ = 0;
  uint32_t blinkStart_ = 0;
  uint32_t nextGlance_ = 0;
  float gaze_ = 0;
  float gazeTarget_ = 0;
  uint32_t nextDrip_ = 0;
  uint32_t nextSweat_ = 0;
  uint32_t nextHop_ = 0;
  uint32_t hopStart_ = 0;

  Particle particles_[MAX_PARTICLES];
  int particleCount_ = 0;
  uint32_t seed_ = 0x13579BDF;
};
