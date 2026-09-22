#include "ui.h"

#include <math.h>
#include <stdio.h>

#include "config.h"
#include "layout.h"
#include "painter.h"
#include "reorder_ui.h"
#include "rng.h"
#include "settings_ui.h"
#include "shapes.h"
#include "sprite_table.h"
#include "theme.h"

namespace {

// Fonts chosen to sit where render.py's TrueType text sits. These and
// MINUTES_DIGIT_H are the only numbers here expected to need tuning by eye.
const Font FONT_SMALL = {nullptr, 1};               // GLCD 6x8
const Font FONT_BADGE = {nullptr, 2};               // 16 px proportional
const Font FONT_MINUTES = {&FreeMonoBold12pt7b, 0};
constexpr int MINUTES_DIGIT_H = 15;                 // for the cancelled strike

// The bob is sin(5t): period 2*pi/5 s = 1256.64 ms. Folding time into 100
// whole periods (125,664 ms) keeps the float argument small, so it never
// loses precision however long the board has been up. The fold is 0.3 ms
// off a whole number of periods: an invisible 0.0015 rad jump every ~2 min.
constexpr uint32_t BOB_FOLD_MS = 125664;

void status_bar(Painter& p, const Board& b, const Theme& th) {
  p.rect(0, 0, W, STATUS_H, th.colours[C_PANEL]);
  const int mid = STATUS_H / 2;
  p.text(b.location, 6, mid, ML_DATUM, FONT_SMALL, th.colours[C_DIM]);
  p.text(b.clock, W - 6, mid, MR_DATUM, FONT_SMALL, th.colours[C_TEXT]);

  char label[16];
  Rgb dot;
  if (b.is_stale()) {
    snprintf(label, sizeof label, "stale %ldm", static_cast<long>(b.stale_s / 60));
    dot = th.colours[C_WARN];
  } else {
    snprintf(label, sizeof label, "live");
    dot = th.colours[C_LIVE];
  }
  p.text(label, W - 54, mid, MR_DATUM, FONT_SMALL, th.colours[C_DIM]);
  const int dx = W - 46;
  p.ellipse(dx - 3, mid - 3, dx + 3, mid + 3, dot);
}

// A small up/down chevron pair, side by side - always visible, toggles
// reorder mode.
void reorder_toggle_icon(Painter& p, const Theme& th) {
  const Rect r = reorder_toggle_rect();
  const int mid = (r.x0 + r.x1) / 2;
  const Rgb colour = reorder_ui_active() ? th.colours[C_LIVE] : th.colours[C_DIM];
  p.triangle(r.x0, r.y1, mid - 1, r.y1, (r.x0 + mid - 1) / 2, r.y0, colour);      // up, left
  p.triangle(mid + 1, r.y0, r.x1, r.y0, (mid + 1 + r.x1) / 2, r.y1, colour);      // down, right
}

// A cog, left of the reorder toggle: a ring of teeth around a hollow centre,
// at this size really just a suggestion of one.
void settings_cog_icon(Painter& p, const Theme& th) {
  const Rect r = settings_cog_rect();
  const int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
  const Rgb colour = settings_ui_active() ? th.colours[C_LIVE] : th.colours[C_DIM];
  // Kept inside r: the rect is the hit zone, and an icon drawn past it would
  // have edges that look tappable and aren't.
  p.ellipse(cx - 4, cy - 4, cx + 4, cy + 4, colour);
  p.rect(cx - 6, cy - 1, cx + 6, cy + 1, colour);  // teeth, left and right
  p.rect(cx - 1, cy - 6, cx + 1, cy + 6, colour);  // teeth, top and bottom
  p.ellipse(cx - 1, cy - 1, cx + 1, cy + 1, th.colours[C_PANEL]);  // the hole
}

// One chevron, filled if it exists at this slot (top has no up, bottom no
// down - see reorder_lane_chevron).
void reorder_lane_chevron_icon(Painter& p, int slot, int n, bool up, const Theme& th) {
  const Rect r = reorder_lane_chevron(slot, n, up);
  if (r.x1 <= r.x0) return;  // doesn't exist at this slot
  const Rgb colour = th.colours[C_LIVE];
  const int mid_x = (r.x0 + r.x1) / 2;
  if (up) p.triangle(r.x0, r.y1, r.x1, r.y1, mid_x, r.y0, colour);
  else p.triangle(r.x0, r.y0, r.x1, r.y0, mid_x, r.y1, colour);
}

// A settings row: a card with a label on the left, drawn the same way for
// every row so the page reads as one list.
void settings_row(Painter& p, Rect r, const char* label, const Theme& th) {
  p.rrect(r.x0, r.y0, r.x1, r.y1, 4, th.colours[C_PANEL]);
  p.text(label, r.x0 + 10, (r.y0 + r.y1) / 2, ML_DATUM, FONT_SMALL, th.colours[C_DIM]);
}

// The whole settings page, drawn over the board rather than beside it: at
// 320x240 there is no room for both, and the board is still there when the
// page closes.
void settings_page(Painter& p, const Board& b, const Theme& th) {
  p.text("Settings", 10, 12, ML_DATUM, FONT_BADGE, th.colours[C_TEXT]);
  const Rect x = settings_close_rect();
  p.rrect(x.x0, x.y0, x.x1, x.y1, 4, th.colours[C_PANEL]);
  p.text("X", (x.x0 + x.x1) / 2, (x.y0 + x.y1) / 2, MC_DATUM, FONT_SMALL, th.colours[C_TEXT]);

  // Theme on the left of the top row, brightness on the right.
  const Rect top = settings_top_rect(), tr = settings_theme_rect();
  p.rrect(top.x0, top.y0, top.x1, top.y1, 4, th.colours[C_PANEL]);
  p.text("Theme", tr.x0 + 10, (tr.y0 + tr.y1) / 2, ML_DATUM, FONT_SMALL, th.colours[C_DIM]);
  p.text(theme(config_theme()).name, tr.x1 - 8, (tr.y0 + tr.y1) / 2, MR_DATUM, FONT_BADGE,
         th.colours[C_LIVE]);

  const Rect minus = settings_bright_minus_rect(), plus = settings_bright_plus_rect();
  p.rrect(minus.x0, minus.y0, minus.x1, minus.y1, 4, th.colours[C_PANEL_HI]);
  p.rrect(plus.x0, plus.y0, plus.x1, plus.y1, 4, th.colours[C_PANEL_HI]);
  p.text("-", (minus.x0 + minus.x1) / 2, (minus.y0 + minus.y1) / 2, MC_DATUM, FONT_BADGE,
         th.colours[C_TEXT]);
  p.text("+", (plus.x0 + plus.x1) / 2, (plus.y0 + plus.y1) / 2, MC_DATUM, FONT_BADGE,
         th.colours[C_TEXT]);
  char pct[8];
  snprintf(pct, sizeof pct, "%d%%", (config_brightness() * 100 + 127) / 255);
  p.text(pct, (minus.x1 + plus.x0) / 2, (minus.y0 + minus.y1) / 2, MC_DATUM, FONT_SMALL,
         th.colours[C_TEXT]);

  // Groups, as chips across one row: the lane rows below need the height.
  const int n_groups = config_n_groups();
  if (n_groups > 0) {
    p.text("Group", 10, 68, ML_DATUM, FONT_SMALL, th.colours[C_DIM]);
    const uint8_t active = config_active_group();
    for (int i = 0; i < n_groups; i++) {
      const Rect r = settings_group_rect(i, n_groups);
      if (r.x1 <= r.x0) break;
      const bool on = i == active;
      p.rrect(r.x0, r.y0, r.x1, r.y1, 4, on ? th.colours[C_LIVE] : th.colours[C_PANEL]);
      // Names run to 23 characters and a chip is ~12 wide at this font, so
      // the draw clips rather than wraps - the full name is on the setup page.
      p.text(config_group_name(i), (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2, MC_DATUM, FONT_SMALL,
             on ? th.colours[C_DARK] : th.colours[C_TEXT]);
    }
  }

  int n = config_n_watches();
  if (n > b.n_watches) n = b.n_watches;  // the board is what has labels to show
  if (n == 0) {
    // DEMO_MODE, where config_begin() never runs and there are no watches to
    // switch. Theme and brightness above still work.
    p.text("No watches configured", 10, 124, TL_DATUM, FONT_SMALL, th.colours[C_DIM]);
    return;
  }

  p.text("Lanes", 10, 112, ML_DATUM, FONT_SMALL, th.colours[C_DIM]);
  const bool* visible = config_lane_visible();
  for (int i = 0; i < n && i < MAX_WATCHES; i++) {
    const Rect r = settings_lane_rect(i);
    if (r.x1 <= r.x0) break;
    char label[48];
    snprintf(label, sizeof label, "%s %s", b.watches[i].badge, b.watches[i].headsign);
    settings_row(p, r, label, th);

    // The state, as a word rather than a tick: at 6x8 a tick is a smudge.
    const bool on = visible[i];
    const Rect box = {r.x1 - 44, r.y0 + 4, r.x1 - 6, r.y1 - 4};
    p.rrect(box.x0, box.y0, box.x1, box.y1, 4, on ? th.colours[C_LIVE] : th.colours[C_PANEL_HI]);
    p.text(on ? "on" : "off", (box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2, MC_DATUM, FONT_SMALL,
           on ? th.colours[C_DARK] : th.colours[C_DIM]);
  }
}

void times(Painter& p, const Lane& ln, const Watch& w, const Theme& th) {
  const Rgb dim = th.colours[C_DIM];
  const Departure* nxt = w.message[0] != '\0' ? nullptr : w.next();
  if (nxt == nullptr) {
    p.text("--", ln.minutes_x, ln.minutes_y, TR_DATUM, FONT_MINUTES, dim);
    const bool has_message = w.message[0] != '\0';
    p.text(has_message ? w.message : "none tonight", ln.following_x, ln.following_y,
           TR_DATUM, FONT_SMALL, has_message ? th.colours[C_WARN] : dim);
    return;
  }

  char buf[16];
  snprintf(buf, sizeof buf, "%d", display_minutes(nxt->eta_s));
  p.text(buf, ln.minutes_x, ln.minutes_y, TR_DATUM, FONT_MINUTES,
         nxt->cancelled ? dim : th.colours[C_TEXT]);

  if (nxt->cancelled) {
    const int tw = p.text_width(buf, FONT_MINUTES);
    const int y = ln.minutes_y + MINUTES_DIGIT_H / 2;
    p.rect(ln.minutes_x - tw - 1, y, ln.minutes_x + 1, y + 1, th.colours[C_WARN]);
    // A 2px strike is invisible from across the room, which is the distance
    // this board is read from. The word is what carries it.
    p.text("cancelled", ln.following_x, ln.following_y, TR_DATUM, FONT_SMALL, th.colours[C_WARN]);
    return;
  }

  const Departure* f = w.following();
  if (f != nullptr) snprintf(buf, sizeof buf, "then %d", display_minutes(f->eta_s));
  else snprintf(buf, sizeof buf, "then --");
  p.text(buf, ln.following_x, ln.following_y, TR_DATUM, FONT_SMALL, dim);
}

// Port of sprites.blit. Origin is the BOTTOM-left: vehicles sit on the track
// line, and that is the edge that must stay put. Runs of one role become one
// horizontal line; role 0 is transparent.
void blit(Painter& p, const SpriteRef& sp, int x, int y, const Rgb roles[ROLE_SLOTS]) {
  const int top = y - sp.h;
  for (int ry = 0; ry < sp.h; ry++) {
    const int py = top + ry;
    if (py < p.oy || py >= p.oy + BAND_H) continue;
    int run_start = 0;
    uint8_t run_role = sprite_role(sp, 0, ry);
    for (int rx = 1; rx <= sp.w; rx++) {
      const uint8_t role = rx < sp.w ? sprite_role(sp, rx, ry) : 0xFF;
      if (role == run_role) continue;
      if (run_role != 0) p.hline(x + run_start, x + rx - 1, py, roles[run_role]);
      run_start = rx;
      run_role = role;
    }
  }
}

// Port of scenery.transit: low skyline for the road, overhead wires for rail.
// Ink derives from the card colour because lanes alternate panel shades.
void scenery_transit(Painter& p, int x0, int y0, int x1, int y1, Kind kind, uint32_t seed,
                     Rgb card) {
  Rng rnd(seed);
  const Rgb ink = bright(card, 1.0, 22);
  const Rgb lit = bright(card, 1.3, 52);
  const int base = y1 - 14;
  if (kind == Kind::Bus) {
    int x = x0 + 6;
    while (x < x1 - 6) {
      const int bw = rnd.between(9, 20);
      const int bh = rnd.between(6, 18);
      p.rect(x, base - bh, x + bw, base, ink);
      for (int wy = base - bh + 3; wy < base - 2; wy += 5)
        for (int wx = x + 2; wx < x + bw - 2; wx += 5)
          if (rnd.chance(35)) p.point(wx, wy, lit);
      x += bw + rnd.between(2, 6);
    }
  } else {
    p.hline(x0 + 6, x1 - 6, y0 + 30, ink);
    for (int mx = x0 + 20; mx < x1 - 10; mx += 46) p.vline(mx, y0 + 30, base, ink);
  }
}

// Port of scenery.ghibli: stars over a hill for the road, over water for rail.
// Draw order matters for the RNG: x, then y, then the star colour.
void scenery_ghibli(Painter& p, int x0, int y0, int x1, int y1, Kind kind, uint32_t seed,
                    Rgb card) {
  Rng rnd(seed);
  const Rgb stars[3] = {bright(card, 1.2, 58), bright(card, 1.4, 92), bright(card, 1.6, 140)};
  for (int i = 0; i < 26; i++) {
    const int sx = rnd.between(x0 + 6, x1 - 6);
    const int sy = rnd.between(y0 + 26, y1 - 34);
    p.point(sx, sy, stars[rnd.below(3)]);
  }
  const Rgb land = shade(card, 0.72);  // hills sit BELOW the ground
  const int base = y1 - 14;
  if (kind == Kind::Bus) {
    for (int x = x0 + 6; x < x1 - 6; x++) p.vline(x, base - hill_at(x - x0), base, land);
  } else {
    for (int x = x0 + 6; x < x1 - 6; x++) p.vline(x, base - shore_at(x - x0), base, land);
    for (int k = 0; k < 6; k++)  // moon path on the water
      p.hline(x0 + 40 + k * 30, x0 + 52 + k * 30, base - 2, bright(card, 1.0, 26));
  }
}

void scenery(Painter& p, SceneryKind kind, int x0, int y0, int x1, int y1, Kind vehicle,
             uint32_t seed, Rgb card) {
  if (kind == SCENERY_TRANSIT) scenery_transit(p, x0, y0, x1, y1, vehicle, seed, card);
  else if (kind == SCENERY_GHIBLI) scenery_ghibli(p, x0, y0, x1, y1, vehicle, seed, card);
}

void vehicle(Painter& p, const Lane& ln, const Watch& w, float t, int index, const Theme& th,
             const SpriteRef& sp, Rgb colour) {
  Rgb roles[ROLE_SLOTS];
  const Departure* nxt = w.message[0] != '\0' ? nullptr : w.next();
  if (nxt == nullptr) {
    // Parked at the left, lights off.
    role_colours(th, shade(colour, 0.4), roles);
    blit(p, sp, ln.track.x0, ln.sprite_baseline, roles);
    return;
  }
  const int x = vehicle_x(nxt->eta_s, ln, sp.w);
  const float bob = nxt->eta_s > 30 ? sinf(t * 5 + index * 1.7f) : 0.0f;
  role_colours(th, nxt->cancelled ? shade(colour, 0.4) : colour, roles);
  // The cast truncates toward zero, as sprites.blit's int(y) does.
  blit(p, sp, x, static_cast<int>(ln.sprite_baseline + bob), roles);
}

void draw_lane(Painter& p, const Lane& ln, const Watch& w, float t, int index, const Theme& th,
               uint8_t theme_index, SizeClass size) {
  const Rgb card = th.colours[index % 2 == 0 ? C_PANEL : C_PANEL_HI];
  p.rrect(ln.rect.x0 + MARGIN, ln.rect.y0 + 3, ln.rect.x1 - MARGIN, ln.rect.y1 - 3, 5, card);

  if (size == SizeClass::Large && th.scenery != SCENERY_NONE)
    scenery(p, th.scenery, ln.rect.x0 + 8, ln.rect.y0, ln.rect.x1 - 8, ln.rect.y1, w.kind,
            7 + index, card);

  const Rgb colour = badge_colour(th, w);

  // route badge
  const int bw = p.text_width(w.badge, FONT_BADGE) + 14;
  const Rect& b = ln.badge;
  p.rrect(b.x0, b.y0, b.x0 + bw, b.y1, 4, colour);
  p.text(w.badge, (b.x0 + b.x0 + bw) / 2, (b.y0 + b.y1) / 2, MC_DATUM, FONT_BADGE,
         th.colours[C_DARK]);
  p.text(w.headsign, b.x0 + bw + 8, ln.headsign_y, TL_DATUM, FONT_SMALL, th.colours[C_DIM]);

  times(p, ln, w, th);

  // track + stop marker
  p.rect(ln.track.x0, ln.track.y0, ln.track.x1, ln.track.y1,
         th.colours[w.kind == Kind::Bus ? C_ROAD : C_RAIL]);
  p.rect(ln.marker_x, ln.track.y0 - 12, ln.marker_x + 2, ln.track.y1, th.colours[C_DIM]);
  p.ellipse(ln.marker_x - 3, ln.track.y0 - 17, ln.marker_x + 5, ln.track.y0 - 9, colour);

  vehicle(p, ln, w, t, index, th, sprite_for(theme_index, size, w.kind), colour);
}

}  // namespace

Ui::Ui(TFT_eSPI& tft) : band_(&tft) {}

bool Ui::begin() {
  band_.setColorDepth(16);
  return band_.createSprite(W, BAND_H) != nullptr;
}

void Ui::draw(const Board& b, uint32_t ms, bool touch_down_edge, int touch_x, int touch_y) {
  const Theme& th = theme(b.theme);
  const float t = (ms % BOB_FOLD_MS) / 1000.0f;
  const int n = b.n_watches < MAX_WATCHES ? b.n_watches : MAX_WATCHES;

  // Which watch each on-screen lane shows, and where in order() it came from.
  // order() is a permutation of every published watch; hidden ones (config.h)
  // are skipped here rather than removed from it, so hiding and re-showing a
  // lane can't lose the arrangement.
  const uint8_t* order = reorder_ui_order();
  const bool* visible = config_lane_visible();
  uint8_t slot_watch[MAX_WATCHES], slot_to_order[MAX_WATCHES];
  int n_slots = 0;
  for (int i = 0; i < MAX_WATCHES && n_slots < MAX_WATCHES; i++) {
    const uint8_t w = order[i];
    if (w >= n) continue;       // order() can outlive a shrunken watch list
    if (!visible[w]) continue;  // hidden: still fetched, just not drawn
    slot_watch[n_slots] = w;
    slot_to_order[n_slots] = static_cast<uint8_t>(i);
    n_slots++;
  }
  if (n_slots == 0) {  // nothing visible (shouldn't happen - config.cpp refuses)
    for (int i = 0; i < n; i++) {
      slot_watch[i] = static_cast<uint8_t>(i);
      slot_to_order[i] = static_cast<uint8_t>(i);
    }
    n_slots = n;
  }
  const SizeClass size = size_class(n_slots);

  // Settings first: it can close reorder mode, and it swallows the tap that
  // opened it so nothing underneath sees it too.
  settings_ui_touch(touch_down_edge, touch_x, touch_y, config_n_watches(), ms);
  settings_ui_tick(ms);
  const bool in_settings = settings_ui_active();
  if (!in_settings) {
    reorder_ui_touch(touch_down_edge, touch_x, touch_y, slot_to_order, n_slots, ms);
    reorder_ui_tick(ms);
  }
  const bool reordering = reorder_ui_active();

  for (int oy = 0; oy < H; oy += BAND_H) {
    Painter p{band_, oy, b.dimmed};
    band_.fillSprite(p.c(th.colours[C_BG]));

    if (in_settings) {
      settings_page(p, b, th);
      band_.pushSprite(0, oy);
      continue;
    }

    // Status bar (and its icons) before the lanes: it sits entirely within
    // y:0-18, above where any lane's card starts (y0+3=21 at the nearest),
    // so draw order between the two doesn't matter here - this is just the
    // more natural header-first order.
    if (oy <= STATUS_H) {
      status_bar(p, b, th);
      settings_cog_icon(p, th);
      reorder_toggle_icon(p, th);
    }
    for (int i = 0; i < n_slots; i++) {
      const Lane ln = lane(i, n_slots);
      if (ln.rect.y1 < oy || ln.rect.y0 >= oy + BAND_H) continue;  // not in this band
      draw_lane(p, ln, b.watches[slot_watch[i]], t, i, th, b.theme, size);
      if (reordering) {
        reorder_lane_chevron_icon(p, i, n_slots, true, th);
        reorder_lane_chevron_icon(p, i, n_slots, false, th);
      }
    }
    band_.pushSprite(0, oy);
  }
}
