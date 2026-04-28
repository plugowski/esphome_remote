#pragma once
#include <string>
#include <cstring>
#include <cmath>
#include "ac_entities.h"
#include "display_utils.h"

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

  static void prevUnit(int& idx) {
    idx = (idx - 1 + AC_LIST_COUNT) % AC_LIST_COUNT;
  }

  static void nextUnit(int& idx) {
    idx = (idx + 1) % AC_LIST_COUNT;
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
    mode_idx = (mode_idx + 1) % AC_LIST[ac_idx].modes_count;
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
    fan_idx  = (fan_idx + 1) % count;
    fan_mode = AC_LIST[ac_idx].fan_modes[fan_idx];
    updated_ui = true;
  }

  static void tempUp(int& temp, bool& updated_ui) {
    if (temp < 30) temp++;
    updated_ui = true;
  }

  static void tempDown(int& temp, bool& updated_ui) {
    if (temp > 20) temp--;
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

  // ── display ────────────────────────────────────────────────────────────────

  // Renders the AC screen.  No-ops if updated_ui is false.
  // Clears updated_ui after drawing so the caller does not need to.
  //
  // Font parameters (all ESPHome font::Font*):
  //   symbols      — 24 px Material Symbols (fan, ac_unit, dry, power_off)
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
                   bool& updated_ui)
  {
    if (!updated_ui) return;
    updated_ui = false;

    it->clear();

    std::string label = AC_LIST[ac_idx].name;
    for (auto& c : label) c = toupper(static_cast<unsigned char>(c));

    // ── mode-specific upper area ──────────────────────────────────────────────
    if (mode == "off") {

      it->print(64, 22, symbols_big, COLOR_ON, display::TextAlign::CENTER, "\ue8ac");
      it->line(0, 44, 127, 44);
      it->print(64, 56, font_base, COLOR_ON, display::TextAlign::CENTER, label.c_str());

    } else if (mode == "cool") {

      it->print(5,  8, symbols, "\ueb3b");   // ac_unit icon
      it->print(64, 22, font_big, COLOR_ON, display::TextAlign::CENTER,
                std::to_string(temp).c_str());
      it->print(85, 4, font_base, "C");

      // Fan-speed bars in top-right corner (stacked, bottom-anchored)
      if (fan_mode == "high")
        it->filled_rectangle(110, 8, 20, 7);
      if (fan_mode == "high" || fan_mode == "medium")
        it->filled_rectangle(110, 18, 20, 7);
      if (fan_mode == "high" || fan_mode == "medium" || fan_mode == "low")
        it->filled_rectangle(110, 28, 20, 7);
      if (fan_mode == "auto")
        it->print(108, 8, font_bigger, "A");

    } else if (mode == "fan_only") {

      it->print(5, 8, symbols, "\uf168");    // fan icon
      std::string lbl = (fan_mode == "medium") ? std::string("MID") : fan_mode;
      for (auto& c : lbl) c = toupper(static_cast<unsigned char>(c));
      it->print(80, 22, font_bigger, COLOR_ON, display::TextAlign::CENTER, lbl.c_str());

    } else if (mode == "dry") {

      it->print(5, 8, symbols, "\uf164");    // dry icon
      it->print(80, 22, font_bigger, COLOR_ON, display::TextAlign::CENTER, "DRY");

    } else {

      // unknown / loading — waiting for HA state
      it->print(64, 22, font_bigger, COLOR_ON, display::TextAlign::CENTER, "...");

    }

    // ── lower bar (unit name + button labels) — shown for all non-off modes ──
    if (mode != "off") {
      if (fan_mode == "auto") {
        it->filled_rectangle(0, 38, 128, 14, COLOR_ON);
        it->print(64, 45, font_base, COLOR_OFF, display::TextAlign::CENTER, label.c_str());
      } else {
        it->line(0, 38, 127, 38);
        it->print(64, 46, font_base, COLOR_ON, display::TextAlign::CENTER, label.c_str());
      }

      bool show_fan = (mode == "cool" || mode == "fan_only");
      draw_bottom_menu(it, font_small, "MODE", "", show_fan ? "FAN" : nullptr);
    }

    it->display();
  }
};
