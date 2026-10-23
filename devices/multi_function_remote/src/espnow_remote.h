#pragma once
#include <string>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include "espnow_protocol.h"
#include "ac_entities.h"
#include "light_entities.h"
#include "lights_controller.h"
#include "cover_entities.h"
#include "cover_controller.h"
#include "automation_entities.h"
#include "quick_action_entities.h"

// ── EspNowRemote ──────────────────────────────────────────────────────────────
// Remote-side glue between the ESP-NOW protocol and the shared controllers.
//
//   • Parses STATE_* packets from the hub and pushes values into the
//     Lights / Cover controllers directly; AC values are cached here and read
//     by ac_set_current_states (packages/espnow/mode_ac.yaml).
//   • Builds CMD packets for the YAML send scripts.
//
// YAML usage:
//   uint8_t t = EspNowRemote::handle(data, size, KEY, id(updated_ui), done);
//   EspNowRemote::acTemp(idx) / acMode(idx) / acFan(idx)
//   EspNowRemote::acModeValue(idx, mode)   → value for rnp::AC_SET_MODE
//   EspNowRemote::acFanValue(idx, fan)     → value for rnp::AC_SET_FAN
//   EspNowRemote::resolveAction(entity, service, domain, idx) → bool

class EspNowRemote {
public:

  // ── AC cache ───────────────────────────────────────────────────────────────

  // True when the hub snapshot has filled in every AC unit (an empty mode means no state yet).
  static bool allAcKnown() {
    for (int i = 0; i < AC_LIST_COUNT; i++)
      if (acModeRef(i).empty()) return false;
    return true;
  }

  static float acTemp(int idx) {
    if (idx < 0 || idx >= AC_LIST_COUNT) return NAN;
    int8_t t = acTempRaw(idx);
    return t < 0 ? NAN : static_cast<float>(t);
  }

  static const std::string& acMode(int idx) {
    static const std::string empty;
    if (idx < 0 || idx >= AC_LIST_COUNT) return empty;
    return acModeRef(idx);
  }

  static const std::string& acFan(int idx) {
    static const std::string empty;
    if (idx < 0 || idx >= AC_LIST_COUNT) return empty;
    return acFanRef(idx);
  }

  // Encode the selected mode string as the CMD value expected by the hub.
  static int16_t acModeValue(int ac_idx, const std::string& mode) {
    if (mode == "off") return rnp::AC_MODE_OFF;
    for (int i = 0; i < AC_LIST[ac_idx].modes_count; i++)
      if (mode == AC_LIST[ac_idx].modes[i]) return static_cast<int16_t>(i);
    return -1;   // unknown → hub ignores
  }

  static int16_t acFanValue(int ac_idx, const std::string& fan) {
    for (int i = 0; i < AC_LIST[ac_idx].fan_modes_count; i++)
      if (fan == AC_LIST[ac_idx].fan_modes[i]) return static_cast<int16_t>(i);
    return -1;
  }

  // ── automations / quick actions ────────────────────────────────────────────

  // Finds the (domain, idx) pair for an entity + service as used by the shared
  // menu.yaml / mode_automations flow.  Searches AUTOMATION_LIST first, then
  // QUICK_ACTION_LIST.  Returns false when nothing matches.
  static bool resolveAction(const std::string& entity, const std::string& service,
                            uint8_t& domain, uint8_t& idx) {
    for (int i = 0; i < AUTOMATION_LIST_COUNT; i++) {
      if (entity == AUTOMATION_LIST[i].entity_id && service == AUTOMATION_LIST[i].service) {
        domain = rnp::DOM_AUTOMATIONS; idx = static_cast<uint8_t>(i); return true;
      }
    }
    for (int i = 0; i < QUICK_ACTION_COUNT; i++) {
      if (entity == QUICK_ACTION_LIST[i].entity_id && service == QUICK_ACTION_LIST[i].service) {
        domain = rnp::DOM_QUICK; idx = static_cast<uint8_t>(i); return true;
      }
    }
    return false;
  }

  // ── packet handling ────────────────────────────────────────────────────────

  // Parses one packet from the hub.  Returns the packet type (0 if the packet
  // was invalid / not for us).  Sets state_done when a STATE_DONE arrives and
  // ac_changed when AC values were updated.
  static uint8_t handle(const uint8_t* data, size_t size, uint32_t key,
                        bool& updated_ui, bool& state_done, bool& ac_changed) {
    uint8_t type = rnp::Reader::check(data, size, key);
    if (type == 0) return 0;
    rnp::Reader r(data, size);

    switch (type) {

      case rnp::STATE_AC: {
        uint8_t count;
        if (!r.u8(count)) return 0;
        for (uint8_t i = 0; i < count; i++) {
          uint8_t idx; int8_t temp; std::string mode, fan;
          if (!r.u8(idx) || !r.i8(temp) || !r.str(mode) || !r.str(fan)) break;
          if (idx >= AC_LIST_COUNT) continue;
          if (acTempRaw(idx) != temp || acModeRef(idx) != mode || acFanRef(idx) != fan) {
            acTempRaw(idx) = temp;
            acModeRef(idx) = mode;
            acFanRef(idx)  = fan;
            ac_changed = true;
          }
        }
        break;
      }

      case rnp::STATE_LIGHTS: {
        uint8_t count;
        if (!r.u8(count)) return 0;
        for (uint8_t i = 0; i < count; i++) {
          uint8_t idx, on, bri;
          if (!r.u8(idx) || !r.u8(on) || !r.u8(bri)) break;
          if (idx >= LIGHTS_LIST_COUNT) continue;
          std::string state = (on == 1) ? "on" : (on == 0) ? "off" : "unknown";
          float b = (bri == rnp::VALUE_UNKNOWN) ? NAN : static_cast<float>(bri);
          LightsController::syncLight(LIGHTS_LIST[idx].entity_id, state, b, updated_ui);
        }
        break;
      }

      case rnp::STATE_COVERS: {
        uint8_t count;
        if (!r.u8(count)) return 0;
        for (uint8_t i = 0; i < count; i++) {
          uint8_t idx, pos; std::string state;
          if (!r.u8(idx) || !r.str(state) || !r.u8(pos)) break;
          if (idx >= COVER_LIST_COUNT) continue;
          CoverController::syncCoverState(idx, state, updated_ui);
          CoverController::syncCoverPosition(idx, pos == rnp::VALUE_UNKNOWN ? NAN : static_cast<float>(pos), updated_ui);
        }
        break;
      }

      case rnp::STATE_DONE:
        state_done = true;
        break;

      default:
        break;
    }
    return type;
  }

  // Formats a MAC for logging / settings screen
  static std::string macToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
  }

private:
  static int8_t& acTempRaw(int idx) {
    static int8_t t[AC_LIST_COUNT > 0 ? AC_LIST_COUNT : 1];
    static bool init = false;
    if (!init) { init = true; for (auto& v : t) v = -1; }
    return t[idx];
  }
  static std::string& acModeRef(int idx) {
    static std::string m[AC_LIST_COUNT > 0 ? AC_LIST_COUNT : 1];
    return m[idx];
  }
  static std::string& acFanRef(int idx) {
    static std::string f[AC_LIST_COUNT > 0 ? AC_LIST_COUNT : 1];
    return f[idx];
  }
};
