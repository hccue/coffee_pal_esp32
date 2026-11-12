// Desktop check of the tray rules and the UI, with no hardware.
//   clang++ -std=c++17 -O2 -Isrc test/preview.cpp src/ui.cpp -o <out>/preview && <out>/preview <out>
// Runs the assertions, then writes one RGB .ppm per scene into <out>.
#include <assert.h>
#include <stdio.h>
#include <string>
#include "ui.h"

static uint16_t frame[SCREEN_W * SCREEN_H];

struct Sim {
  CoffeePalUI ui;
  uint32_t now = 1000;
  void run(uint32_t ms, bool touched = false, int x = 0, int y = 0) {
    for (uint32_t end = now + ms; now < end; now += 33) ui.update(now, touched, x, y);
  }
  // A real finger: lands, stays for 100 ms, lifts, and the UI sees it release.
  void tap(int x, int y) {
    run(99, true, x, y);
    run(198);
  }
  void tapButton(Button b) {
    static const int centers[] = {54, 160, 266};
    tap(centers[b], 140);
  }
};

static void save(Sim &s, const std::string &dir, const char *name) {
  Canvas c(frame, SCREEN_W, SCREEN_H);
  s.ui.draw(c);
  std::string path = dir + "/" + name + ".ppm";
  FILE *f = fopen(path.c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t v = frame[i];
    uint8_t rgb8[3] = {(uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)};
    fwrite(rgb8, 1, 3, f);
  }
  fclose(f);
}

static void testRules() {
  // 12 coffees fill the tray.
  Tray a;
  a.coffees = 11;
  assert(!a.full());
  a.coffees = 12;
  assert(a.full());
  // A froth counts twice.
  Tray b;
  b.coffees = 4;
  b.froths = 3;
  assert(!b.full() && b.remaining() == 2);
  b.froths = 4;
  assert(b.full());
  Tray f;
  f.froths = 5;
  assert(!f.full());
  f.froths = 6;
  assert(f.full());
  Tray w;
  w.coffees = 9;
  assert(!w.warn());
  w.coffees = 10;
  assert(w.warn() && w.remaining() == 2);
  Tray over;
  over.coffees = 20;
  assert(over.full() && over.remaining() == 0 && over.level() == 1.f);

  // Each button counts once per contact, however long the finger stays.
  Sim s;
  s.ui.begin(Tray(), s.now);
  s.run(100);
  s.run(1000, true, 54, 140);
  s.run(200);
  assert(s.ui.tray().coffees == 1);
  s.tapButton(BTN_FROTH);
  assert(s.ui.tray().froths == 1);
  assert(s.ui.takeChanged() && !s.ui.takeChanged());

  // Recent presses are flagged, then fade out after RECENT_PRESS_MS.
  assert(s.ui.recentPress(BTN_COFFEE) > 0.f && s.ui.recentPress(BTN_FROTH) > 0.f);
  assert(s.ui.recentPress(BTN_RESET) == 0.f);
  s.run(RECENT_PRESS_MS + 100);
  assert(s.ui.recentPress(BTN_COFFEE) == 0.f && s.ui.recentPress(BTN_FROTH) == 0.f);
  {
    Sim r;
    r.ui.begin(Tray(), r.now);
    r.run(100);
    assert(r.ui.recentPress(BTN_COFFEE) == 0.f);
    r.tapButton(BTN_COFFEE);
    assert(r.ui.recentPress(BTN_COFFEE) > 0.9f && r.ui.recentPress(BTN_FROTH) == 0.f);
  }

  // A touch dropout mid-press is not a second tap.
  s.run(66, true, 54, 140);
  s.run(66);
  s.run(66, true, 54, 140);
  s.run(200);
  assert(s.ui.tray().coffees == 2);

  // Touching above the buttons counts nothing.
  s.tap(160, 40);
  assert(s.ui.tray().coffees == 2 && s.ui.tray().froths == 1);

  // Reset needs a second tap, and the first one expires.
  s.tapButton(BTN_RESET);
  assert(s.ui.tray().coffees == 2);
  s.run(RESET_CONFIRM_MS + 100);
  s.tapButton(BTN_RESET);
  assert(s.ui.tray().coffees == 2);
  s.tapButton(BTN_RESET);
  assert(s.ui.tray().empty());

  // A full tray resets on a single tap.
  for (int i = 0; i < 12; i++) s.tapButton(BTN_COFFEE);
  assert(s.ui.tray().full());
  s.tapButton(BTN_RESET);
  assert(s.ui.tray().empty());

  // Naps when idle, but a tap on a button still counts while waking.
  s.run(SLEEP_AFTER_MS + 1000);
  assert(s.ui.sleeping() && s.ui.backlightPercent() == BRIGHTNESS_SLEEP);
  s.tapButton(BTN_COFFEE);
  assert(!s.ui.sleeping() && s.ui.tray().coffees == 1);

  // Never naps on a full tray.
  for (int i = 0; i < 11; i++) s.tapButton(BTN_COFFEE);
  s.run(SLEEP_AFTER_MS + 1000);
  assert(s.ui.tray().full() && !s.ui.sleeping());
  puts("rules: all passed");
}

static Sim *fresh(int coffees, int froths) {
  Sim *s = new Sim();
  Tray t;
  t.coffees = coffees;
  t.froths = froths;
  s->ui.begin(t, s->now);
  s->run(700);
  return s;
}

int main(int argc, char **argv) {
  testRules();
  std::string dir = argc > 1 ? argv[1] : ".";

  save(*fresh(0, 0), dir, "1_empty");
  save(*fresh(4, 2), dir, "2_normal");

  Sim *s = fresh(4, 2);
  s->run(99, true, 54, 140);
  s->run(200);
  save(*s, dir, "3_coffee_hop");

  s = fresh(4, 2);
  s->run(99, true, 160, 140);
  s->run(66);
  save(*s, dir, "4_froth_bubbles");

  s = fresh(4, 2);
  s->tapButton(BTN_RESET);
  save(*s, dir, "5_reset_confirm");

  save(*fresh(6, 2), dir, "6_warn");

  s = fresh(4, 4);
  s->run(1500);
  save(*s, dir, "7_full_alert");

  s = fresh(4, 2);
  s->tapButton(BTN_RESET);
  s->tapButton(BTN_RESET);
  save(*s, dir, "8_clean");

  s = fresh(4, 2);
  s->run(SLEEP_AFTER_MS + 1000);
  save(*s, dir, "9_sleep");

  s = fresh(4, 2);
  s->tapButton(BTN_COFFEE);
  s->run(150000);
  save(*s, dir, "10_recent_press");
  return 0;
}
