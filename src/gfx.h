#pragma once
// Small anti-aliased RGB565 renderer. Has no hardware dependencies, so the same
// drawing code runs on the device and in the desktop preview (test/preview.cpp).
#include <math.h>
#include <stdint.h>
#include <string.h>

struct Glyph {
  uint16_t offset;
  uint8_t w, h;
  int8_t xo, yo;
  uint8_t advance;
};

struct Font {
  const uint8_t *bitmap;
  const Glyph *glyphs;
  uint8_t first, last;
};

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline uint16_t mix(uint16_t a, uint16_t b, float t) {
  uint32_t x = (a | ((uint32_t)a << 16)) & 0x07E0F81F;
  uint32_t y = (b | ((uint32_t)b << 16)) & 0x07E0F81F;
  uint32_t k = (uint32_t)(t * 32.f + 0.5f);
  uint32_t r = (x + (((y - x) * k) >> 5)) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}

struct Canvas {
  uint16_t *px;
  int w, h;
  int cx0, cy0, cx1, cy1;

  Canvas(uint16_t *buffer, int width, int height) : px(buffer), w(width), h(height) { clearClip(); }

  void clearClip() { cx0 = 0; cy0 = 0; cx1 = w; cy1 = h; }
  void clip(int x0, int y0, int x1, int y1) {
    cx0 = x0 < 0 ? 0 : x0;
    cy0 = y0 < 0 ? 0 : y0;
    cx1 = x1 > w ? w : x1;
    cy1 = y1 > h ? h : y1;
  }

  void fill(uint16_t c) {
    for (int i = 0, n = w * h; i < n; i++) px[i] = c;
  }

  inline void put(int x, int y, uint16_t c, uint32_t a32) {
    uint16_t &d = px[y * w + x];
    if (a32 >= 32) { d = c; return; }
    uint32_t x0 = (d | ((uint32_t)d << 16)) & 0x07E0F81F;
    uint32_t y0 = (c | ((uint32_t)c << 16)) & 0x07E0F81F;
    uint32_t r = (x0 + (((y0 - x0) * a32) >> 5)) & 0x07E0F81F;
    d = (uint16_t)(r | (r >> 16));
  }

  // Coverage from a signed distance (negative inside), scaled by alpha.
  inline void shade(int x, int y, float sd, uint16_t c, float alpha) {
    if (sd >= 0.5f) return;
    float cov = sd <= -0.5f ? 1.f : 0.5f - sd;
    uint32_t a = (uint32_t)(cov * alpha * 32.f + 0.5f);
    if (a) put(x, y, c, a);
  }

  bool bounds(float x0, float y0, float x1, float y1, int &ix0, int &iy0, int &ix1, int &iy1) const {
    ix0 = (int)floorf(x0) - 1;
    iy0 = (int)floorf(y0) - 1;
    ix1 = (int)ceilf(x1) + 1;
    iy1 = (int)ceilf(y1) + 1;
    if (ix0 < cx0) ix0 = cx0;
    if (iy0 < cy0) iy0 = cy0;
    if (ix1 > cx1) ix1 = cx1;
    if (iy1 > cy1) iy1 = cy1;
    return ix0 < ix1 && iy0 < iy1;
  }

  // Fills pixels known to be fully inside a shape, skipping the distance maths.
  inline void span(int x0, int x1, int y, uint16_t c, float alpha) {
    uint32_t a = (uint32_t)(alpha * 32.f + 0.5f);
    if (a >= 32) {
      uint16_t *d = px + y * w;
      for (int x = x0; x < x1; x++) d[x] = c;
    } else if (a) {
      for (int x = x0; x < x1; x++) put(x, y, c, a);
    }
  }

  void circle(float cx, float cy, float r, uint16_t c, float alpha = 1.f) {
    int x0, y0, x1, y1;
    if (!bounds(cx - r, cy - r, cx + r, cy + r, x0, y0, x1, y1)) return;
    float inner = r - 0.75f, outer = r + 0.75f;
    for (int y = y0; y < y1; y++) {
      float dy = y + 0.5f - cy;
      float o2 = outer * outer - dy * dy;
      if (o2 <= 0) continue;
      float oh = sqrtf(o2);
      int ex0 = (int)floorf(cx - oh - 0.5f), ex1 = (int)ceilf(cx + oh - 0.5f) + 1;
      if (ex0 < x0) ex0 = x0;
      if (ex1 > x1) ex1 = x1;
      int sl = ex1, sr = ex1;
      if (inner > 0 && fabsf(dy) < inner) {
        float h = sqrtf(inner * inner - dy * dy);
        sl = (int)ceilf(cx - h - 0.5f);
        sr = (int)floorf(cx + h - 0.5f) + 1;
        if (sl < ex0) sl = ex0;
        if (sr > ex1) sr = ex1;
        if (sr < sl) sr = sl;
      }
      for (int x = ex0; x < sl; x++) {
        float dx = x + 0.5f - cx;
        shade(x, y, sqrtf(dx * dx + dy * dy) - r, c, alpha);
      }
      span(sl, sr, y, c, alpha);
      for (int x = sr; x < ex1; x++) {
        float dx = x + 0.5f - cx;
        shade(x, y, sqrtf(dx * dx + dy * dy) - r, c, alpha);
      }
    }
  }

  void rrect(float x, float y, float rw, float rh, float r, uint16_t c, float alpha = 1.f) {
    int x0, y0, x1, y1;
    if (!bounds(x, y, x + rw, y + rh, x0, y0, x1, y1)) return;
    float hw = rw * 0.5f, hh = rh * 0.5f, mx = x + hw, my = y + hh;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    float inner = r - 0.75f;
    for (int py = y0; py < y1; py++) {
      float qy = fabsf(py + 0.5f - my) - hh + r;
      int sl = x1, sr = x1;
      if (inner > 0 && qy < inner) {
        float h = qy > 0 ? sqrtf(inner * inner - qy * qy) : inner;
        sl = (int)ceilf(mx - (hw - r) - h - 0.5f);
        sr = (int)floorf(mx + (hw - r) + h - 0.5f) + 1;
        if (sl < x0) sl = x0;
        if (sr > x1) sr = x1;
        if (sr < sl) sr = sl;
      }
      for (int px_ = x0; px_ < sl; px_++) {
        float qx = fabsf(px_ + 0.5f - mx) - hw + r;
        float sd = (qx > 0 && qy > 0) ? sqrtf(qx * qx + qy * qy) : (qx > qy ? qx : qy);
        shade(px_, py, sd - r, c, alpha);
      }
      span(sl, sr, py, c, alpha);
      for (int px_ = sr; px_ < x1; px_++) {
        float qx = fabsf(px_ + 0.5f - mx) - hw + r;
        float sd = (qx > 0 && qy > 0) ? sqrtf(qx * qx + qy * qy) : (qx > qy ? qx : qy);
        shade(px_, py, sd - r, c, alpha);
      }
    }
  }

  void line(float ax, float ay, float bx, float by, float r, uint16_t c, float alpha = 1.f) {
    int x0, y0, x1, y1;
    float lx = ax < bx ? ax : bx, hx = ax < bx ? bx : ax;
    float ly = ay < by ? ay : by, hy = ay < by ? by : ay;
    if (!bounds(lx - r, ly - r, hx + r, hy + r, x0, y0, x1, y1)) return;
    float vx = bx - ax, vy = by - ay;
    float len2 = vx * vx + vy * vy;
    for (int y = y0; y < y1; y++) {
      for (int x = x0; x < x1; x++) {
        float wx = x + 0.5f - ax, wy = y + 0.5f - ay;
        float t = len2 > 0 ? (wx * vx + wy * vy) / len2 : 0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        float dx = wx - vx * t, dy = wy - vy * t;
        shade(x, y, sqrtf(dx * dx + dy * dy) - r, c, alpha);
      }
    }
  }

  // Thick arc; angles in radians, measured clockwise on screen from +x.
  void arc(float cx, float cy, float radius, float thick, float a0, float a1, uint16_t c) {
    int x0, y0, x1, y1;
    float outer = radius + thick;
    if (!bounds(cx - outer, cy - outer, cx + outer, cy + outer, x0, y0, x1, y1)) return;
    float mid = (a0 + a1) * 0.5f, half = (a1 - a0) * 0.5f;
    float ex0 = cx + cosf(a0) * radius, ey0 = cy + sinf(a0) * radius;
    float ex1 = cx + cosf(a1) * radius, ey1 = cy + sinf(a1) * radius;
    float mc = cosf(mid), ms = sinf(mid), hc = cosf(half);
    float reject = (outer + 1.f) * (outer + 1.f);
    for (int y = y0; y < y1; y++) {
      float dy = y + 0.5f - cy;
      for (int x = x0; x < x1; x++) {
        float dx = x + 0.5f - cx;
        float d2 = dx * dx + dy * dy;
        if (d2 > reject) continue;
        float d = sqrtf(d2);
        float sd;
        // Inside the wedge: distance to the ring. Outside: distance to a round end cap.
        if (d > 0 && (dx * mc + dy * ms) / d >= hc) {
          sd = fabsf(d - radius) - thick;
        } else {
          float e0 = (x + 0.5f - ex0) * (x + 0.5f - ex0) + (y + 0.5f - ey0) * (y + 0.5f - ey0);
          float e1 = (x + 0.5f - ex1) * (x + 0.5f - ex1) + (y + 0.5f - ey1) * (y + 0.5f - ey1);
          sd = sqrtf(e0 < e1 ? e0 : e1) - thick;
        }
        shade(x, y, sd, c, 1.f);
      }
    }
  }

  // Draws one line of text with its baseline at y.
  void text(const Font &f, const char *s, int x, int y, uint16_t c, float alpha = 1.f) {
    for (; *s; s++) {
      uint8_t ch = (uint8_t)*s;
      if (ch < f.first || ch > f.last) continue;
      const Glyph &g = f.glyphs[ch - f.first];
      const uint8_t *src = f.bitmap + g.offset;
      for (int j = 0; j < g.h; j++) {
        int py = y + g.yo + j;
        if (py < cy0 || py >= cy1) continue;
        for (int i = 0; i < g.w; i++) {
          int px_ = x + g.xo + i;
          if (px_ < cx0 || px_ >= cx1) continue;
          uint8_t v = src[j * g.w + i];
          if (!v) continue;
          uint32_t a = (uint32_t)(v * alpha * 32.f / 255.f + 0.5f);
          if (a) put(px_, py, c, a);
        }
      }
      x += g.advance;
    }
  }

  static int textWidth(const Font &f, const char *s) {
    int w = 0;
    for (; *s; s++) {
      uint8_t ch = (uint8_t)*s;
      if (ch >= f.first && ch <= f.last) w += f.glyphs[ch - f.first].advance;
    }
    return w;
  }

  void textCentered(const Font &f, const char *s, int cx, int y, uint16_t c, float alpha = 1.f) {
    text(f, s, cx - textWidth(f, s) / 2, y, c, alpha);
  }
};
