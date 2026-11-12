#include "ui.h"
#include <stdio.h>
#include "font_data.h"

static const float PI_F = 3.14159265f;

// Layout (landscape). The screen is only about 33 x 18 mm, so the bottom third is all buttons.
static const int BTN_Y = 114, BTN_H = 56, BTN_W = 100;
static const int BTN_X[BTN_COUNT] = {4, 110, 216};
static const int TOUCH_ZONE_Y = 106;  // Taps slightly above a button still count.
static const float TRAY_X = 8, TRAY_Y = 88, TRAY_W = 304, TRAY_H = 20;
static const float MUG_CX = 52, MUG_BASE = 86, MUG_W = 68, MUG_H = 52;
static const int COUNT_CX[2] = {169, 268};
static const int STATUS_CX = 218;

static const uint32_t TOUCH_RELEASE_MS = 90;  // Ignores brief touch-controller dropouts.

static const uint16_t BG = rgb(24, 16, 13);
static const uint16_t BG_ALARM_A = rgb(46, 10, 12);
static const uint16_t BG_ALARM_B = rgb(104, 18, 20);
static const uint16_t CREAM = rgb(250, 236, 214);
static const uint16_t CREAM_SHADE = rgb(222, 195, 162);
static const uint16_t ESPRESSO = rgb(58, 32, 20);
static const uint16_t INK = rgb(44, 26, 20);
static const uint16_t CARAMEL = rgb(214, 136, 62);
static const uint16_t CARAMEL_DIM = rgb(152, 88, 36);
static const uint16_t MILK = rgb(246, 240, 228);
static const uint16_t MILK_DIM = rgb(196, 186, 170);
static const uint16_t SLATE = rgb(62, 52, 48);
static const uint16_t WHITE = rgb(255, 255, 255);
static const uint16_t MUTED = rgb(190, 170, 150);
static const uint16_t WATER = rgb(70, 170, 235);
static const uint16_t WATER_LIGHT = rgb(150, 215, 250);
static const uint16_t AMBER = rgb(250, 180, 50);
static const uint16_t RED = rgb(240, 70, 60);
static const uint16_t MINT = rgb(70, 205, 160);
static const uint16_t BLUSH = rgb(244, 150, 130);
static const uint16_t TRAY_DARK = rgb(40, 32, 30);
static const uint16_t TRAY_EDGE = rgb(88, 76, 70);

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float approach(float value, float target, float rate, float dt) {
  return value + (target - value) * (1.f - expf(-rate * dt));
}
static inline float elapsed(uint32_t now, uint32_t start, float duration) {
  return clamp01((float)(now - start) / 1000.f / duration);
}

// ---- State ------------------------------------------------------------------

void CoffeePalUI::begin(const Tray &saved, uint32_t nowMs) {
  tray_ = saved;
  now_ = nowMs;
  lastActivityMs_ = nowMs;
  level_ = tray_.level();
  foam_ = tray_.froths > 0 ? 1.f : 0.f;
  nextBlink_ = nowMs + 1800;
  nextGlance_ = nowMs + 3000;
  nextHop_ = nowMs + 14000;
}

bool CoffeePalUI::takeChanged() {
  bool c = changed_;
  changed_ = false;
  return c;
}

float CoffeePalUI::rnd() {
  seed_ ^= seed_ << 13;
  seed_ ^= seed_ >> 17;
  seed_ ^= seed_ << 5;
  return (seed_ & 0xFFFF) / 65535.f;
}

void CoffeePalUI::react(Reaction r) {
  reaction_ = r;
  reactionStart_ = now_;
}

void CoffeePalUI::spawn(ParticleKind kind, float x, float y, float vx, float vy, float gravity, float life, float size) {
  if (particleCount_ >= MAX_PARTICLES) return;
  particles_[particleCount_++] = {x, y, vx, vy, gravity, life, 0.f, size, (uint8_t)kind};
}

void CoffeePalUI::press(Button b, uint32_t nowMs) {
  now_ = nowMs;
  lastActivityMs_ = nowMs;
  sleeping_ = false;
  buttonFlash_[b] = 1.f;
  bool wasFull = tray_.full();
  if (b == BTN_COFFEE || b == BTN_FROTH) lastPressMs_[b] = nowMs ? nowMs : 1;

  if (b == BTN_COFFEE) {
    tray_.coffees++;
    changed_ = true;
    coffeePop_ = 1.f;
    resetArmedUntil_ = 0;
    react(REACT_COFFEE);
    spawn(P_COFFEE, MUG_CX, -6, 0, 40, 620, 0.32f, 5);
    spawn(P_PLUS, COUNT_CX[0] + 37, 40, 0, -36, 0, 0.8f, 0);
  } else if (b == BTN_FROTH) {
    tray_.froths++;
    changed_ = true;
    frothPop_ = 1.f;
    resetArmedUntil_ = 0;
    react(REACT_FROTH);
    for (int i = 0; i < 7; i++)
      spawn(P_BUBBLE, MUG_CX - 22 + rnd() * 44, MUG_BASE - MUG_H, -8 + rnd() * 16, -30 - rnd() * 40, 0, 0.6f + rnd() * 0.5f, 2 + rnd() * 3.5f);
    spawn(P_PLUS, COUNT_CX[1] + 37, 40, 0, -36, 0, 0.8f, 0);
  } else if (b == BTN_RESET) {
    if (tray_.empty()) return;
    // A full tray resets on one tap. Otherwise the first tap only arms it, so a stray
    // touch can't lose the count.
    if (!tray_.full() && nowMs >= resetArmedUntil_) {
      resetArmedUntil_ = nowMs + RESET_CONFIRM_MS;
      return;
    }
    tray_ = Tray();
    changed_ = true;
    resetArmedUntil_ = 0;
    cleanUntil_ = nowMs + 2400;
    react(REACT_CLEAN);
    for (int i = 0; i < 12; i++) {
      float a = rnd() * 2.f * PI_F;
      float v = 40 + rnd() * 70;
      spawn(P_SPARKLE, MUG_CX, MUG_BASE - 34, cosf(a) * v, sinf(a) * v, 40, 0.6f + rnd() * 0.4f, 2 + rnd() * 2);
    }
  }

  if (!wasFull && tray_.full()) react(REACT_ALARM);
}

void CoffeePalUI::update(uint32_t nowMs, bool touched, int x, int y) {
  dt_ = (float)(nowMs - now_) / 1000.f;
  if (dt_ > 0.1f) dt_ = 0.1f;
  now_ = nowMs;
  t_ = (float)(nowMs % 3600000UL) / 1000.f;

  // A contact fires once, on landing. It must lift for a moment before the next one.
  if (touched) {
    lastTouchMs_ = nowMs;
    if (!wasTouched_) {
      wasTouched_ = true;
      lastActivityMs_ = nowMs;
      bool wasSleeping = sleeping_;
      sleeping_ = false;
      held_ = BTN_NONE;
      if (y >= TOUCH_ZONE_Y) {
        held_ = x < 107 ? BTN_COFFEE : (x < 213 ? BTN_FROTH : BTN_RESET);
        press(held_, nowMs);
      } else if (wasSleeping) {
        hopStart_ = nowMs;
      }
    }
  } else if (wasTouched_ && nowMs - lastTouchMs_ > TOUCH_RELEASE_MS) {
    wasTouched_ = false;
    held_ = BTN_NONE;
  }

  for (int i = 0; i < BTN_COUNT; i++) {
    buttonPress_[i] = approach(buttonPress_[i], held_ == i ? 1.f : 0.f, held_ == i ? 40.f : 14.f, dt_);
    buttonFlash_[i] = approach(buttonFlash_[i], 0.f, 6.f, dt_);
  }
  coffeePop_ = approach(coffeePop_, 0.f, 7.f, dt_);
  frothPop_ = approach(frothPop_, 0.f, 7.f, dt_);
  level_ = approach(level_, tray_.level(), reaction_ == REACT_CLEAN ? 3.f : 6.f, dt_);
  foam_ = approach(foam_, tray_.froths > 0 ? 1.f : 0.f, 6.f, dt_);

  if (reaction_ != REACT_NONE && reaction_ != REACT_ALARM && elapsed(now_, reactionStart_, 1.1f) >= 1.f)
    reaction_ = REACT_NONE;
  if (resetArmedUntil_ && nowMs >= resetArmedUntil_) resetArmedUntil_ = 0;

  bool full = tray_.full();
  if (!full && reaction_ == REACT_ALARM) reaction_ = REACT_NONE;
  if (!full && !sleeping_ && nowMs - lastActivityMs_ > SLEEP_AFTER_MS) sleeping_ = true;
  if (full) sleeping_ = false;

  // Small signs of life.
  if (nowMs >= nextBlink_) {
    blinkStart_ = nowMs;
    nextBlink_ = nowMs + 1800 + (uint32_t)(rnd() * 3200);
  }
  if (nowMs >= nextGlance_) {
    gazeTarget_ = rnd() < 0.4f ? 0.f : -1.f + rnd() * 2.f;
    nextGlance_ = nowMs + 1400 + (uint32_t)(rnd() * 3000);
  }
  gaze_ = approach(gaze_, gazeTarget_, 10.f, dt_);
  if (!sleeping_ && !full && !tray_.warn() && nowMs >= nextHop_) {
    hopStart_ = nowMs;
    nextHop_ = nowMs + 14000 + (uint32_t)(rnd() * 16000);
  }

  // Water spills over the tray lip while it's full.
  if (full && nowMs >= nextDrip_) {
    spawn(P_WATER, TRAY_X + 14 + rnd() * (TRAY_W - 28), TRAY_Y + TRAY_H - 2, 0, 12, 300, 0.45f, 2 + rnd() * 1.5f);
    nextDrip_ = nowMs + 180 + (uint32_t)(rnd() * 320);
  }
  if ((full || tray_.warn()) && !sleeping_ && nowMs >= nextSweat_) {
    spawn(P_SWEAT, MUG_CX + 30, MUG_BASE - MUG_H + 8, 24, -28, 200, 0.5f, 2.4f);
    nextSweat_ = nowMs + (full ? 450 : 1500);
  }

  stepParticles(dt_);
}

void CoffeePalUI::stepParticles(float dt) {
  int kept = 0;
  for (int i = 0; i < particleCount_; i++) {
    Particle p = particles_[i];
    p.age += dt;
    if (p.age >= p.life) continue;
    p.vy += p.gravity * dt;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    particles_[kept++] = p;
  }
  particleCount_ = kept;
}

float CoffeePalUI::recentPress(Button b) const {
  if (b != BTN_COFFEE && b != BTN_FROTH) return 0.f;
  if (!lastPressMs_[b]) return 0.f;
  uint32_t age = now_ - lastPressMs_[b];
  if (age >= RECENT_PRESS_MS) return 0.f;
  return 1.f - (float)age / RECENT_PRESS_MS;
}

int CoffeePalUI::backlightPercent() const {
  if (tray_.full()) return 72 + (int)(28.f * (0.5f + 0.5f * sinf(t_ * 4.f)));
  return sleeping_ ? BRIGHTNESS_SLEEP : BRIGHTNESS_NORMAL;
}

// ---- Drawing ----------------------------------------------------------------

void CoffeePalUI::draw(Canvas &c) {
  drawBackground(c);
  drawCounts(c);
  drawStatus(c);
  drawTrayBar(c);
  drawMug(c);
  drawButtons(c);
  drawParticles(c);
}

void CoffeePalUI::drawBackground(Canvas &c) {
  if (tray_.full()) {
    c.fill(mix(BG_ALARM_A, BG_ALARM_B, 0.5f + 0.5f * sinf(t_ * 4.f)));
  } else {
    c.fill(BG);
  }
}

void CoffeePalUI::drawTrayBar(Canvas &c) {
  c.rrect(TRAY_X - 2, TRAY_Y - 2, TRAY_W + 4, TRAY_H + 4, 11, TRAY_EDGE);
  c.rrect(TRAY_X, TRAY_Y, TRAY_W, TRAY_H, 9, TRAY_DARK);

  uint16_t water = tray_.full() ? RED : (tray_.warn() ? AMBER : WATER);
  uint16_t crest = tray_.full() ? rgb(255, 150, 130) : (tray_.warn() ? rgb(255, 220, 130) : WATER_LIGHT);
  float edge = TRAY_X + TRAY_W * level_;
  if (level_ > 0.002f) {
    c.clip((int)TRAY_X, (int)TRAY_Y, (int)(TRAY_X + TRAY_W), (int)(TRAY_Y + TRAY_H));
    c.rrect(TRAY_X, TRAY_Y, TRAY_W, TRAY_H, 9, water);
    // Cut the water back with a rippling edge so the level looks liquid.
    for (int y = (int)TRAY_Y; y < (int)(TRAY_Y + TRAY_H); y++) {
      float wave = sinf(y * 0.55f + t_ * 3.2f) * 2.2f + sinf(y * 0.21f - t_ * 1.7f) * 1.4f;
      float x0 = edge + wave;
      if (level_ >= 0.999f) break;
      if (x0 < TRAY_X + TRAY_W) {
        c.clip((int)x0, y, (int)(TRAY_X + TRAY_W), y + 1);
        c.rrect(TRAY_X, TRAY_Y, TRAY_W, TRAY_H, 9, TRAY_DARK);
      }
      c.clip((int)TRAY_X, (int)TRAY_Y, (int)(TRAY_X + TRAY_W), (int)(TRAY_Y + TRAY_H));
      if (x0 > TRAY_X + 8) c.circle(x0 - 0.5f, y + 0.5f, 1.1f, crest);
    }
    c.clearClip();
  }

  // One notch per unit (a coffee; a froth takes two): the remaining capacity can be counted at a glance.
  for (int i = 1; i < TRAY_CAPACITY_UNITS; i++) {
    float x = TRAY_X + TRAY_W * i / TRAY_CAPACITY_UNITS;
    c.line(x, TRAY_Y + 3, x, TRAY_Y + TRAY_H - 3, 0.6f, TRAY_EDGE, 0.75f);
  }
}

void CoffeePalUI::drawMug(Canvas &c) {
  bool full = tray_.full();
  float sx = 1.f, sy = 1.f, dx = 0.f, dy = 0.f;

  // Calm breathing, or slow deep breaths while asleep.
  float breathe = sinf(t_ * (sleeping_ ? 1.1f : 2.1f)) * (sleeping_ ? 0.028f : 0.016f);
  sy += breathe;
  sx -= breathe * 0.7f;

  // A hop: crouch, jump, land with a squash.
  float hopAge = (float)(now_ - hopStart_) / 1000.f;
  float reactAge = (float)(now_ - reactionStart_) / 1000.f;
  float jump = 0.f;
  if (reaction_ == REACT_COFFEE) jump = reactAge - 0.20f;  // Hop as the drop lands.
  else if (reaction_ == REACT_CLEAN) jump = reactAge;
  else if (hopStart_ && hopAge < 0.7f) jump = hopAge;
  else jump = -1.f;
  if (jump >= 0.f && jump < 0.7f) {
    if (jump < 0.08f) {
      float k = jump / 0.08f;
      sy -= 0.14f * k;
      sx += 0.12f * k;
    } else if (jump < 0.42f) {
      float k = (jump - 0.08f) / 0.34f;
      dy = -18.f * sinf(k * PI_F);
      sy += 0.10f * sinf(k * PI_F);
      sx -= 0.08f * sinf(k * PI_F);
    } else {
      float k = (jump - 0.42f) / 0.28f;
      float bounce = sinf(k * PI_F) * (1.f - k);
      sy -= 0.16f * bounce;
      sx += 0.14f * bounce;
    }
  }
  if (reaction_ == REACT_FROTH && reactAge < 0.8f) {
    float k = 1.f - reactAge / 0.8f;
    dx += sinf(reactAge * 30.f) * 3.5f * k;
    sy += sinf(reactAge * 22.f) * 0.05f * k;
  }
  if (full) {
    // Shakes in bursts, not constantly, so the face stays readable.
    float burst = sinf(t_ * 2.f);
    if (burst > 0.2f) dx += sinf(t_ * 42.f) * 2.2f * burst;
  } else if (tray_.warn()) {
    dx += sinf(t_ * 2.4f) * 0.8f;
  }

  float w = MUG_W * sx, h = MUG_H * sy;
  float cx = MUG_CX + dx;
  float top = MUG_BASE - h + dy;
  float left = cx - w * 0.5f;

  // Ground shadow shrinks as the mug leaves the tray.
  float air = clamp01(-dy / 18.f);
  c.rrect(cx - (w * 0.45f) * (1.f - air * 0.4f), MUG_BASE - 1, w * 0.9f * (1.f - air * 0.4f), 5, 3, rgb(14, 10, 9), 0.75f);

  // Handle.
  c.arc(left + w - 1, top + h * 0.5f, h * 0.27f, 3.6f, -1.45f, 1.45f, CREAM_SHADE);

  // Body with a soft shaded base.
  c.rrect(left, top, w, h, 15, CREAM_SHADE);
  c.rrect(left, top, w, h - 5, 15, CREAM);
  c.rrect(left + 5, top + 4, 5, h * 0.42f, 2.5f, WHITE, 0.75f);

  // Coffee surface.
  c.rrect(left + 6, top + 3, w - 12, 10, 5, ESPRESSO);
  c.rrect(left + 13, top + 4.5f, w * 0.36f, 2.4f, 1.2f, rgb(112, 70, 44));

  // Foam crown once milk has been frothed; it swells on each froth.
  if (foam_ > 0.02f) {
    float swell = 1.f;
    if (reaction_ == REACT_FROTH && reactAge < 0.5f) swell += 0.5f * sinf(reactAge / 0.5f * PI_F);
    static const float BX[] = {-21, -10, 2, 13, 22};
    static const float BR[] = {6, 8, 9, 7.5f, 5.5f};
    for (int i = 0; i < 5; i++) {
      float wobble = sinf(t_ * 2.f + i * 1.7f) * 0.5f;
      c.circle(cx + BX[i], top + 5 - BR[i] * 0.3f * foam_ + wobble, BR[i] * foam_ * swell, MILK);
    }
    for (int i = 0; i < 2; i++)
      c.circle(cx + BX[i * 2 + 1] - 1.5f, top + 1.5f, 1.8f * foam_, MILK_DIM, 0.6f);
  }

  // Steam, or Zs while napping.
  if (sleeping_) {
    for (int i = 0; i < 3; i++) {
      float k = fmodf(t_ * 0.32f + i / 3.f, 1.f);
      float zx = cx + 16 + k * 22 + sinf(k * 6.f) * 3.f;
      float zy = top - 2 - k * 28;
      float s = 3.f + k * 3.5f;
      float a = sinf(k * PI_F);
      c.line(zx - s, zy - s, zx + s, zy - s, 1.0f, MUTED, a);
      c.line(zx + s, zy - s, zx - s, zy + s, 1.0f, MUTED, a);
      c.line(zx - s, zy + s, zx + s, zy + s, 1.0f, MUTED, a);
    }
  } else if (!full) {
    for (int w_i = 0; w_i < 3; w_i++) {
      float base = cx - 15 + w_i * 15;
      float phase = t_ * 0.55f + w_i * 0.37f;
      for (int i = 0; i < 6; i++) {
        float k = fmodf(phase + i / 6.f, 1.f);
        float sy_ = top - 3 - k * 18;
        if (foam_ > 0.5f) sy_ -= 6;
        float sx_ = base + sinf(k * 7.f + t_ * 1.4f + w_i) * (1.5f + k * 3.5f);
        c.circle(sx_, sy_, 2.6f - k * 0.9f, rgb(200, 186, 172), sinf(k * PI_F) * 0.26f);
      }
    }
  }

  drawFace(c, cx, top, w);
}

void CoffeePalUI::drawFace(Canvas &c, float cx, float top, float w) {
  float eyeY = top + 26, mouthY = top + 37;
  float gap = w * 0.235f;
  bool full = tray_.full();
  bool warn = tray_.warn();
  bool happy = reaction_ == REACT_COFFEE || reaction_ == REACT_FROTH || reaction_ == REACT_CLEAN || now_ < cleanUntil_;

  // Cheeks.
  c.rrect(cx - gap - 12, eyeY + 6, 10, 5, 2.5f, BLUSH, happy ? 0.85f : 0.5f);
  c.rrect(cx + gap + 2, eyeY + 6, 10, 5, 2.5f, BLUSH, happy ? 0.85f : 0.5f);

  if (sleeping_) {
    c.arc(cx - gap, eyeY - 2, 5, 1.4f, 0.5f, PI_F - 0.5f, INK);
    c.arc(cx + gap, eyeY - 2, 5, 1.4f, 0.5f, PI_F - 0.5f, INK);
    c.circle(cx, mouthY, 2.2f, INK);
    return;
  }

  if (happy && !full) {
    // Joyful closed eyes: ^ ^
    c.arc(cx - gap, eyeY + 3, 5.5f, 1.8f, PI_F + 0.45f, 2 * PI_F - 0.45f, INK);
    c.arc(cx + gap, eyeY + 3, 5.5f, 1.8f, PI_F + 0.45f, 2 * PI_F - 0.45f, INK);
    c.rrect(cx - 8, mouthY - 2, 16, 8, 4, INK);
    c.rrect(cx - 4, mouthY + 2, 8, 3.5f, 1.7f, BLUSH);
    return;
  }

  float blink = elapsed(now_, blinkStart_, 0.14f);
  float open = (blinkStart_ && blink < 1.f) ? fabsf(blink * 2.f - 1.f) : 1.f;
  float eyeW = full ? 13.f : 11.f;
  float eyeH = (full ? 15.f : 13.f) * open;
  if (eyeH < 2.4f) eyeH = 2.4f;

  float look = gaze_ * 1.8f;
  float lookY = 0;
  if (full) { look = sinf(t_ * 7.f) * 1.5f; }
  else if (warn) { look = 0; lookY = 2.0f; }  // Eyeing the tray below.

  for (int side = -1; side <= 1; side += 2) {
    float ex = cx + side * gap;
    c.rrect(ex - eyeW * 0.5f + look * 0.4f, eyeY - eyeH * 0.5f, eyeW, eyeH, eyeW * 0.5f, INK);
    if (open > 0.6f) {
      c.circle(ex + look - 1.8f, eyeY - 2.5f + lookY, 2.1f, WHITE);
      c.circle(ex + look + 2.0f, eyeY + 1.8f + lookY, 1.0f, WHITE, 0.85f);
    }
    if (full || warn) {
      // Worried brows: the inner ends lift towards the middle of the face.
      float browY = eyeY - (full ? 11.5f : 10.f);
      float tilt = full ? 2.4f : 1.8f;
      c.line(ex + side * 5.f, browY + tilt, ex - side * 4.f, browY - tilt, 1.2f, INK);
    }
  }

  if (full) {
    // Open "O" of alarm.
    float o = 1.f + sinf(t_ * 8.f) * 0.12f;
    c.rrect(cx - 5.f * o, mouthY - 3, 10.f * o, 11.f * o, 5, INK);
  } else if (warn) {
    // Nervous wavy line.
    for (int i = 0; i < 3; i++) {
      float x = cx - 7.5f + i * 5.f;
      float a = (i % 2 == 0 ? 1.f : -1.f) * 1.6f;
      c.line(x, mouthY + 2 + a, x + 5.f, mouthY + 2 - a, 1.2f, INK);
    }
  } else {
    c.arc(cx, mouthY - 3.5f, 7.f, 1.5f, 0.45f, PI_F - 0.45f, INK);
  }
}

// Small drink icons shared by the counters and the buttons.
static void drawCupIcon(Canvas &c, float x, float y, float s, uint16_t col, uint16_t steam) {
  c.arc(x + s * 0.62f, y + s * 0.55f, s * 0.20f, s * 0.07f, -1.5f, 1.5f, col);
  c.rrect(x, y + s * 0.26f, s * 0.70f, s * 0.68f, s * 0.2f, col);
  for (int i = 0; i < 2; i++) {
    float sx = x + s * (0.2f + i * 0.28f);
    c.line(sx, y + s * 0.03f, sx + s * 0.05f, y + s * 0.13f, s * 0.045f, steam);
    c.line(sx + s * 0.05f, y + s * 0.13f, sx, y + s * 0.20f, s * 0.045f, steam);
  }
}

static void drawFoamIcon(Canvas &c, float x, float y, float s, uint16_t col) {
  c.circle(x + s * 0.26f, y + s * 0.62f, s * 0.24f, col);
  c.circle(x + s * 0.74f, y + s * 0.62f, s * 0.24f, col);
  c.circle(x + s * 0.50f, y + s * 0.42f, s * 0.30f, col);
  c.rrect(x + s * 0.06f, y + s * 0.62f, s * 0.88f, s * 0.24f, s * 0.1f, col);
  c.circle(x + s * 0.88f, y + s * 0.18f, s * 0.09f, col);
  c.circle(x + s * 0.13f, y + s * 0.24f, s * 0.06f, col);
}

void CoffeePalUI::drawCounts(Canvas &c) {
  const int baseline = 48;
  int values[2] = {tray_.coffees, tray_.froths};
  float pops[2] = {coffeePop_, frothPop_};
  uint16_t tint[2] = {CARAMEL, MILK};
  bool alarm = tray_.full();

  for (int i = 0; i < 2; i++) {
    char buf[8];
    snprintf(buf, sizeof buf, "%d", values[i] > 999 ? 999 : values[i]);
    int width = Canvas::textWidth(fontDigits, buf);
    bool roomForIcon = values[i] < 100;
    int iconSize = 22;
    int total = width + (roomForIcon ? iconSize + 4 : 0);
    int x = COUNT_CX[i] - total / 2;
    int lift = (int)(pops[i] * pops[i] * 7.f);

    if (roomForIcon) {
      uint16_t iconColor = alarm ? WHITE : tint[i];
      if (i == 0) drawCupIcon(c, x, baseline - 32 - lift, iconSize, iconColor, iconColor);
      else drawFoamIcon(c, x, baseline - 32 - lift, iconSize, iconColor);
      x += iconSize + 4;
    }
    uint16_t digit = mix(alarm ? WHITE : CREAM, tint[i], clamp01(pops[i]) * 0.9f);
    c.text(fontDigits, buf, x, baseline - lift, digit);
  }
}

void CoffeePalUI::drawStatus(Canvas &c) {
  char buf[28];
  uint16_t color = MUTED;
  int left = tray_.remaining();

  if (tray_.full()) {
    snprintf(buf, sizeof buf, "EMPTY THE TRAY!");
    color = WHITE;
  } else if (resetArmedUntil_) {
    snprintf(buf, sizeof buf, "Tap again to reset");
    color = AMBER;
  } else if (now_ < cleanUntil_) {
    snprintf(buf, sizeof buf, "All clean, thanks!");
    color = MINT;
  } else if (tray_.warn()) {
    snprintf(buf, sizeof buf, "Almost full: %d left", left);
    color = AMBER;
  } else {
    snprintf(buf, sizeof buf, left == 1 ? "%d drink left" : "%d drinks left", left);
  }
  c.textCentered(fontText, buf, STATUS_CX, 75, color);
}

void CoffeePalUI::drawButtons(Canvas &c) {
  for (int i = 0; i < BTN_COUNT; i++) drawButton(c, (Button)i);
}

void CoffeePalUI::drawButton(Canvas &c, Button b) {
  float p = buttonPress_[b];
  float shrink = p * 3.f;
  float x = BTN_X[b] + shrink, y = BTN_Y + shrink, w = BTN_W - shrink * 2, h = BTN_H - shrink * 2;
  bool full = tray_.full();
  bool armed = resetArmedUntil_ != 0;

  uint16_t face, depth, ink;
  const char *label;
  if (b == BTN_COFFEE) {
    face = CARAMEL; depth = CARAMEL_DIM; ink = rgb(44, 22, 8); label = "+ Coffee";
  } else if (b == BTN_FROTH) {
    face = MILK; depth = MILK_DIM; ink = INK; label = "+ Froth";
  } else if (full) {
    // The one thing to do now: this button glows.
    face = mix(MINT, WHITE, 0.25f + 0.25f * sinf(t_ * 4.f));
    depth = rgb(30, 130, 100); ink = rgb(8, 44, 34); label = "Emptied";
  } else if (armed) {
    face = AMBER; depth = rgb(170, 110, 20); ink = rgb(50, 30, 0); label = "Sure?";
  } else {
    face = SLATE; depth = rgb(36, 30, 28); ink = CREAM; label = "Reset";
  }
  face = mix(face, WHITE, buttonFlash_[b] * 0.55f);

  c.rrect(x, y + 2 * (1.f - p), w, h, 13, depth);
  c.rrect(x, y + p * 2.f - 1.f * (1.f - p), w, h - 1, 13, face);

  // Small dot in the corner while this drink was counted in the last few minutes.
  float recent = recentPress(b);
  if (recent > 0.f) c.circle(x + w - 11, y + 9 + p, 2.f + 1.2f * recent, ink, 0.35f + 0.5f * recent);

  float mid = x + w * 0.5f;
  float iconY = y + 5 + p;
  float iconSize = 24;
  if (b == BTN_COFFEE) {
    drawCupIcon(c, mid - iconSize * 0.42f, iconY, iconSize, ink, ink);
  } else if (b == BTN_FROTH) {
    drawFoamIcon(c, mid - iconSize * 0.5f, iconY, iconSize, ink);
  } else if (full) {
    // Check mark.
    c.line(mid - 9, iconY + 12, mid - 3, iconY + 18, 2.2f, ink);
    c.line(mid - 3, iconY + 18, mid + 9, iconY + 5, 2.2f, ink);
  } else {
    // Circular arrow, with a head at the leading end.
    float cy = iconY + 12, r = 8.5f;
    c.arc(mid, cy, r, 1.5f, -PI_F * 0.35f, PI_F * 1.3f, ink);
    float ax = mid + cosf(-PI_F * 0.35f) * r, ay = cy + sinf(-PI_F * 0.35f) * r;
    c.line(ax - 5, ay - 2.5f, ax + 1, ay, 1.5f, ink);
    c.line(ax + 1, ay, ax - 1.5f, ay + 5.5f, 1.5f, ink);
  }
  c.textCentered(fontText, label, (int)mid, (int)(y + h - 8 + p), ink);
}

void CoffeePalUI::drawParticles(Canvas &c) {
  for (int i = 0; i < particleCount_; i++) {
    const Particle &p = particles_[i];
    float k = p.age / p.life;
    switch (p.kind) {
      case P_COFFEE:
        c.circle(p.x, p.y, p.size, rgb(120, 70, 36));
        c.circle(p.x - 1.2f, p.y - 1.4f, 1.2f, rgb(200, 150, 110));
        break;
      case P_BUBBLE:
        c.circle(p.x + sinf(p.age * 12.f + i) * 2.f, p.y, p.size, MILK, (1.f - k) * 0.9f);
        break;
      case P_SPARKLE: {
        float s = p.size * (1.f - k);
        c.line(p.x - s * 1.6f, p.y, p.x + s * 1.6f, p.y, 0.8f, MINT, 1.f - k);
        c.line(p.x, p.y - s * 1.6f, p.x, p.y + s * 1.6f, 0.8f, WATER_LIGHT, 1.f - k);
        break;
      }
      case P_PLUS:
        c.textCentered(fontText, "+1", (int)p.x, (int)p.y, WHITE, 1.f - k * k);
        break;
      case P_WATER:
        c.circle(p.x, p.y, p.size, WATER, 1.f - k * 0.5f);
        break;
      case P_SWEAT:
        c.circle(p.x, p.y, p.size, WATER_LIGHT, 1.f - k);
        break;
    }
  }
}
