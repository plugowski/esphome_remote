#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "quick_action_entities.h"

// ── QuickActionState ──────────────────────────────────────────────────────────
// On / off state of the toggle-type quick actions (service *.toggle, e.g.
// switch.toggle, input_boolean.toggle), shown on the quick-action card as an
// icon tile (filled when on) and an ON / OFF badge in the strip
// (MenuController). Everything else (scenes, scripts, ...) has no state to
// show.
//
// Fed by Home Assistant: on the WiFi remote one text_sensor per toggle entity
// (generated into packages/mode_automations.yaml) calls sync(); on the
// ESP-NOW remote the hub relays it in STATE_QUICK packets (setRaw()).
// Until the first report a toggle is UNKNOWN.

class QuickActionState {
public:
  static const uint8_t OFF = 0, ON = 1, UNKNOWN = 2;   // same values as rnp's light state

  static bool isToggle(const char* service) {
    static const char SUFFIX[] = ".toggle";
    const size_t n = service ? strlen(service) : 0, k = sizeof(SUFFIX) - 1;
    return n > k && strcmp(service + n - k, SUFFIX) == 0;
  }

  static bool isToggle(int i) {
    return i >= 0 && i < QUICK_ACTION_COUNT && isToggle(QUICK_ACTION_LIST[i].service);
  }

  static uint8_t get(int i) {
    return (i >= 0 && i < QUICK_ACTION_COUNT) ? at(i) : UNKNOWN;
  }

  static void setRaw(int i, uint8_t v) {
    if (i >= 0 && i < QUICK_ACTION_COUNT) at(i) = v > UNKNOWN ? UNKNOWN : v;
  }

  // Home Assistant state string for `entity`: every toggle action on it follows.
  static void sync(const char* entity, const std::string& s) {
    const uint8_t v = (s == "on") ? ON : (s == "off") ? OFF : UNKNOWN;
    for (int i = 0; i < QUICK_ACTION_COUNT; i++)
      if (isToggle(i) && strcmp(QUICK_ACTION_LIST[i].entity_id, entity) == 0) at(i) = v;
  }

  // Shown straight after RUN, before Home Assistant reports back (it corrects
  // this if the toggle did not happen). Unknown stays unknown.
  static void flip(int i) {
    if (!isToggle(i) || at(i) == UNKNOWN) return;
    const char* entity = QUICK_ACTION_LIST[i].entity_id;
    const uint8_t v = at(i) == ON ? OFF : ON;
    for (int j = 0; j < QUICK_ACTION_COUNT; j++)
      if (isToggle(j) && strcmp(QUICK_ACTION_LIST[j].entity_id, entity) == 0) at(j) = v;
  }

  // Left soft-button label for action i: what RUN will do.
  static const char* runLabel(int i) {
    if (!isToggle(i)) return "RUN";
    const uint8_t v = at(i);
    return v == ON ? "OFF" : v == OFF ? "ON" : "TOGGLE";
  }

private:
  static uint8_t& at(int i) {
    static uint8_t s[QUICK_ACTION_CAPACITY];
    static bool init = false;
    if (!init) { init = true; for (auto& v : s) v = UNKNOWN; }
    return s[i];
  }
};
