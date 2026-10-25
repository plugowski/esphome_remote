#pragma once
#include <string>
#include <cstring>
#include <cmath>
#include "ac_entities.h"
#include "room_entities.h"
#include "room_filter.h"
#include "display_utils.h"
#include "remote_core.h"
#include "context_strip.h"

// ── ACController ──────────────────────────────────────────────────────────────
// All AC mode logic: button actions, state management, and display rendering.
// State remains in ESPHome globals; methods take references so they can mutate them.
//
// Typical YAML usage (one-liners):
//   ACController::prevUnit(id(selected_ac_index));
//   ACController::nextUnit(id(selected_ac_index));
//   ACController::indexForEntity(id(selected_ac));   // → int index
//   ACController::selectUnit(id(selected_ac_index), id(selected_ac));
//   ACController::cycleMode(id(selected_ac_index), id(selected_ac_mode_index));
//   ACController::applyMode(id(selected_ac_index), id(selected_ac_mode_index),
//                           id(selected_ac_mode), id(updated_ui));
//   ACController::cycleFan(id(selected_ac_index), id(selected_fan_index),
//                          id(selected_fan_mode), id(updated_ui));
//   ACController::tempUp(id(selected_temp), id(updated_ui));
//   ACController::tempDown(id(selected_temp), id(updated_ui));
//   ACController::applyCurrentStates(temp, mode, fan,
//                   id(current_ac_mode), id(current_fan_mode), id(current_temp), id(is_booting));
//   ACController::syncSelections(
//                   id(current_ac_mode), id(current_fan_mode), id(current_temp),
//                   id(selected_ac_mode), id(selected_ac_mode_index),
//                   id(selected_fan_mode), id(selected_fan_index),
//                   id(selected_temp), id(selected_ac_index), id(updated_ui));
//   ACController::draw(id(oled_display),
//                   id(ac_symbols), id(ac_symbols_big), id(font_big), id(font_bigger),
//                   id(font_base), id(font_small),
//                   id(selected_ac_mode), id(selected_fan_mode),
//                   id(selected_ac_index), id(selected_temp), id(updated_ui));

class ACController {
public:

  // ── button actions ─────────────────────────────────────────────────────────

  // Both skip units outside the room filter (see room_filter.h).
  static void prevUnit(int& idx) {
    idx = RoomFilter::step(idx, AC_LIST_COUNT, -1, acRoom);
  }

  static void nextUnit(int& idx) {
    idx = RoomFilter::step(idx, AC_LIST_COUNT, +1, acRoom);
  }

  // Reverse-lookup: find the index for a given entity_id string (fallback 0)
  static int indexForEntity(const std::string& entity_id) {
    for (int i = 0; i < AC_LIST_COUNT; i++)
      if (entity_id == AC_LIST[i].entity_id) return i;
    return 0;
  }

  // Update entity_id global to match the selected unit index
  static void selectUnit(int idx, std::string& entity) {
    entity = AC_LIST[idx].entity_id;
  }

  // Reset mode/fan/temp to the unit's own array defaults (index 0 of each list).
  // Must be called after selectUnit so that cycleFan/cycleMode start from a
  // consistent position — avoids the "first press does nothing" bug that occurs
  // when a hardcoded initial value does not match fan_modes[0] for that unit.
  static void resetUnit(int ac_idx,
                        int& mode_idx, std::string& mode,
                        int& fan_idx,  std::string& fan_mode,
                        int& temp) {
    mode_idx = 0;
    mode     = AC_LIST[ac_idx].modes[0];
    fan_idx  = 0;
    fan_mode = AC_LIST[ac_idx].fan_modes[0];
    temp     = 22;
  }

  // Advance to next HVAC mode in the unit's supported list
  static void cycleMode(int ac_idx, int& mode_idx) {
    mode_idx = wrap_index(mode_idx, AC_LIST[ac_idx].modes_count, +1);
  }

  // Write mode_idx into the selected_ac_mode string and mark UI dirty
  static void applyMode(int ac_idx, int mode_idx,
                        std::string& ac_mode, bool& updated_ui) {
    if (mode_idx >= 0 && mode_idx < AC_LIST[ac_idx].modes_count)
      ac_mode = AC_LIST[ac_idx].modes[mode_idx];
    updated_ui = true;
  }

  // Toggle power: off → restore last mode (mode_idx preserved); on → "off"
  // mode_idx is intentionally kept when turning off so the next power-on
  // restores the same mode automatically.
  static void turnOn(int ac_idx, int& mode_idx,
                     std::string& ac_mode, bool& updated_ui) {
    if (ac_mode != "off") return;
    if (mode_idx >= 0 && mode_idx < AC_LIST[ac_idx].modes_count)
      ac_mode = AC_LIST[ac_idx].modes[mode_idx];
    else { mode_idx = 0; ac_mode = AC_LIST[ac_idx].modes[0]; }
    updated_ui = true;
  }

  static void turnOff(std::string& ac_mode, bool& updated_ui) {
    if (ac_mode == "off") return;
    ac_mode    = "off";
    updated_ui = true;
  }

  // Advance to next fan speed in the unit's fan_modes list.
  // Re-syncs fan_idx from the current fan_mode string before advancing,
  // so any mismatch between index and string (boot state, unit switch, HA sync)
  // is corrected automatically and the first press always produces a visible change.
  static void cycleFan(int ac_idx, int& fan_idx,
                       std::string& fan_mode, bool& updated_ui) {
    const int count = AC_LIST[ac_idx].fan_modes_count;
    for (int i = 0; i < count; i++) {
      if (fan_mode == AC_LIST[ac_idx].fan_modes[i]) { fan_idx = i; break; }
    }
    fan_idx  = wrap_index(fan_idx, count, +1);
    fan_mode = AC_LIST[ac_idx].fan_modes[fan_idx];
    updated_ui = true;
  }

  static void tempUp(int& temp, bool& updated_ui) {
    if (temp < 30) temp++;
    updated_ui = true;
  }

  static void tempDown(int& temp, bool& updated_ui) {
    if (temp > 16) temp--;
    updated_ui = true;
  }

  // ── state management ───────────────────────────────────────────────────────

  // Called inside ac_set_current_states after reading sensor values for the
  // selected unit.  Populates current_* globals and clears the boot flag.
  static void applyCurrentStates(float temp_f, const std::string& mode,
                                  const std::string& fan,
                                  std::string& cur_mode, std::string& cur_fan,
                                  int& cur_temp, bool& is_booting) {
    if (mode.empty()) {
      cur_mode = "unknown";
      cur_fan  = "unknown";
      cur_temp = -1;
    } else {
      cur_mode   = mode;
      cur_fan    = fan;
      cur_temp   = std::isnan(temp_f) ? -1 : static_cast<int>(temp_f);
      is_booting = false;
    }
  }

  // Syncs selected_* from current_* when HA pushes an external state change.
  // Also finds the matching index in the per-unit mode/fan arrays.
  static void syncSelections(
      const std::string& cur_mode,  const std::string& cur_fan,  int cur_temp,
      std::string& sel_mode,        int& sel_mode_idx,
      std::string& sel_fan,         int& sel_fan_idx,
      int& sel_temp, int ac_idx, bool& updated_ui)
  {
    if (cur_mode == "unknown") return;
    bool changed = false;

    if (sel_mode != cur_mode) {
      sel_mode = cur_mode;
      for (int i = 0; i < AC_LIST[ac_idx].modes_count; i++) {
        if (strcmp(AC_LIST[ac_idx].modes[i], cur_mode.c_str()) == 0) {
          sel_mode_idx = i;
          break;
        }
      }
      changed = true;
    }

    if (sel_fan != cur_fan) {
      sel_fan = cur_fan;
      for (int i = 0; i < AC_LIST[ac_idx].fan_modes_count; i++) {
        if (strcmp(AC_LIST[ac_idx].fan_modes[i], cur_fan.c_str()) == 0) {
          sel_fan_idx = i;
          break;
        }
      }
      changed = true;
    }

    if (sel_temp != cur_temp) {
      sel_temp = cur_temp;
      changed  = true;
    }

    if (changed) updated_ui = true;
  }

  // ── display helpers ────────────────────────────────────────────────────────

  // HVAC modes that show the setpoint screen: mode icon, target temperature and
  // a fan indicator. Anything not listed here falls through to the "..." screen,
  // which is meant for "no state from Home Assistant yet" - not for a mode we
  // simply forgot to draw. Keep this in sync with AC_HVAC_MODES in the
  // configurator (multi.js), which is what the user can actually pick.
  static bool isSetpointMode(const std::string& mode) {
    return mode == "cool" || mode == "heat" || mode == "heat_cool" || mode == "auto";
  }

  // Material Symbols codepoints; every one of these must also be listed under
  // the ac_symbols font in packages/mode_ac.yaml or it renders as blank.
  static const char* modeIcon(const std::string& mode) {
    if (mode == "heat")      return "\uef55";   // local_fire_department
    if (mode == "heat_cool") return "\uf16b";   // mode_heat_cool
    if (mode == "auto")      return "\uf077";   // thermostat_auto
    return "\ueb3b";                            // ac_unit (cool, and fallback)
  }

  // Layout below the context strip (rows 0..8: "CLIMATE" chip, one dot per
  // unit, the unit's room - or its name, without a room - in the badge). The free area is rows 10..52; everything in the hero row
  // is centred on HERO_CY = 29:
  //   x=5          24 px mode icon, top-left (5, 14) → rows 19..38
  //   x=64         40 px temperature centred at (64, 30) → digits rows 15..43
  //   x=85         "C" in font_base, top-left (85, 12) → caps start on row 15
  //   x=108..127   fan indicator (below)
  //   y=53..63     MODE | ••• | FAN
  static constexpr int HERO_CY = 29;

  // Fan speed on the right of the hero row. Ordered speeds are stacked bars;
  // the named speeds with no place on that scale get an icon, the same way
  // "auto" has always used a letter.
  //
  // All three share one slot. "A" is printed top-left-anchored at (108, 13)
  // with font_bigger (Arial Bold 28), which inks the box x=108..127,
  // y=19..38. The icons are centred on that same box — (118, 28) — rather
  // than sharing the top-left anchor: these are 24 px glyphs of differing
  // widths, so a common top-left origin would both overrun the 128 px panel
  // on the right and make each glyph sit in a slightly different place.
  // Centring keeps them locked to the "A" position whatever the glyph. The
  // three bars (7 px, 3 px apart) span rows 16..42.
  static constexpr int FAN_ICON_CX = 118;
  static constexpr int FAN_ICON_CY = 28;

  template<class D, class F>
  static void drawFanIndicator(D* it, F* symbols, F* font_bigger,
                               const std::string& fan_mode) {
    if (fan_mode == "turbo") {
      it->print(FAN_ICON_CX, FAN_ICON_CY, symbols, COLOR_ON,
                display::TextAlign::CENTER, "\ueb9b");   // rocket_launch
      return;
    }
    if (fan_mode == "quiet") {
      it->print(FAN_ICON_CX, FAN_ICON_CY, symbols, COLOR_ON,
                display::TextAlign::CENTER, "\ue04f");   // volume_off
      return;
    }
    if (fan_mode == "auto") {
      it->print(108, 13, font_bigger, "A");
      return;
    }
    if (fan_mode == "high")
      it->filled_rectangle(110, 16, 20, 7);
    if (fan_mode == "high" || fan_mode == "medium")
      it->filled_rectangle(110, 26, 20, 7);
    if (fan_mode == "high" || fan_mode == "medium" || fan_mode == "low")
      it->filled_rectangle(110, 36, 20, 7);
  }

  // ── display ────────────────────────────────────────────────────────────────

  // Renders the AC screen.  No-ops if updated_ui is false.
  // Clears updated_ui after drawing so the caller does not need to.
  //
  // Font parameters (all ESPHome font::Font*):
  //   symbols      — 24 px Material Symbols (fan, ac_unit, heat, dry, power_off)
  //   symbols_big  — 36 px Material Symbols bold (power_off hero icon)
  //   font_big     — 40 px Arial Bold (temperature number)
  //   font_bigger  — 28 px Arial Bold (fan label / DRY label)
  //   font_base    — 14 px Arial Bold (unit name / misc labels)
  //   font_small   — 10 px Arial Bold (button bar)
  template<class D, class F>
  static void draw(D* it,
                   F* symbols, F* symbols_big,
                   F* font_big, F* font_bigger, F* font_base, F* font_small,
                   const std::string& mode, const std::string& fan_mode,
                   int ac_idx, int temp,
                   bool& updated_ui, int conn_status)
  {
    if (!updated_ui) return;
    updated_ui = false;

    // Nothing in the chosen room: empty state (the keys are gated off too)
    if (AC_LIST_COUNT > 0 && !RoomFilter::any(AC_LIST_COUNT, acRoom)) {
      drawEmptyRoom(it, font_base, font_small, "CLIMATE", "climate", conn_status);
      return;
    }

    it->clear();

    // ── context strip: "CLIMATE" chip, one dot per unit in the room filter;
    //    the badge names the unit's room, or the unit itself without one ─────
    const char* room = roomName(AC_LIST[ac_idx].room);
    ContextStrip::draw(it, "CLIMATE",
                       RoomFilter::position(ac_idx, AC_LIST_COUNT, acRoom),
                       RoomFilter::visibleCount(AC_LIST_COUNT, acRoom), conn_status,
                       room ? room : AC_LIST[ac_idx].name);

    // ── mode-specific hero row ────────────────────────────────────────────────
    if (mode == "off") {

      it->print(64, HERO_CY + 1, symbols_big, COLOR_ON, display::TextAlign::CENTER, "\ue8ac");

    } else if (isSetpointMode(mode)) {

      // Setpoint modes share one screen: mode icon + big temperature + fan bars.
      it->print(5,  HERO_CY - 15, symbols, modeIcon(mode));
      it->print(64, HERO_CY + 1, font_big, COLOR_ON, display::TextAlign::CENTER,
                std::to_string(temp).c_str());
      it->print(85, HERO_CY - 17, font_base, "C");

      drawFanIndicator(it, symbols, font_bigger, fan_mode);

    } else if (mode == "fan_only") {

      it->print(5, HERO_CY - 15, symbols, "\uf168");    // fan icon
      std::string lbl = (fan_mode == "medium") ? std::string("MID") : fan_mode;
      for (auto& c : lbl) c = toupper(static_cast<unsigned char>(c));
      it->print(80, HERO_CY, font_bigger, COLOR_ON, display::TextAlign::CENTER, lbl.c_str());

    } else if (mode == "dry") {

      it->print(5, HERO_CY - 15, symbols, "\uf164");    // dry icon
      it->print(80, HERO_CY, font_bigger, COLOR_ON, display::TextAlign::CENTER, "DRY");

    } else {

      // unknown / loading — HA hasn't reported a mode for this unit yet
      // (freshly booted or just switched to it); also reachable if HA ever
      // sends a genuinely unrecognized mode string. A refresh icon in the
      // hero slot reads as "still fetching" rather than looking broken, and
      // the chip above already says which unit is loading.
      it->print(64, HERO_CY, symbols_big, COLOR_ON, display::TextAlign::CENTER, "");

    }

    // ── button bar — FAN only where the fan speed means something; ON while
    //    the unit is off (the right key turns it on, see ac_btn_fan) ─────────
    bool show_fan = isSetpointMode(mode) || mode == "fan_only";
    draw_bottom_menu(it, font_small, "MODE", nullptr,
                     mode == "off" ? "ON" : show_fan ? "FAN" : "");

    Toast::present(it);
  }
};
