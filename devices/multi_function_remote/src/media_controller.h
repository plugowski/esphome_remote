#pragma once
#include <string>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include "display_utils.h"
#include "context_strip.h"
#include "toast.h"

// ── MediaController ───────────────────────────────────────────────────────────
// One Home Assistant media player (MEDIA_ENTITY in remote.yaml) on a card:
// play-state icon on the left, the volume large with the title or source
// under it, and a volume slider on the right edge.
//
// Button mapping (routing in buttons.yaml / the layouts):
//   ↑ / ↓            volume ± VOLUME_STEP, sent 300 ms after the last press
//   ●                play / pause
//   ■ / round R      mute on / off
//   ← / →            previous / next track
//   round L          select MEDIA_SOURCE (e.g. "TV" on a Sonos soundbar)
//
// HA sends a boolean attribute as "on" / "off", and sends nothing for an
// attribute the entity doesn't have, so the title can be stale after the
// player switches to MEDIA_SOURCE - the card shows the source name then.

class MediaController {
public:
  static const int VOLUME_STEP = 2;
  static const int CARD_CY     = 31;

  struct State {
    std::string player;        // playing / paused / idle / off / unavailable
    std::string source;
    std::string title;
    int  volume      = -1;     // 0..100 as HA reports it, -1 = not known yet
    int  target      = -1;     // shown and sent; leads `volume` while pending
    bool muted       = false;
    bool mute_target = false;
    bool pending     = false;  // a volume step not yet confirmed by HA
  };

  static State& state() { static State st; return st; }

  // ── HA → remote ───────────────────────────────────────────────────────────

  static void syncPlayer(const std::string& x, bool& ui) { state().player = x; ui = true; }
  static void syncSource(const std::string& x, bool& ui) { state().source = x; ui = true; }
  static void syncTitle(const std::string& x, bool& ui)  { state().title  = x; ui = true; }

  static void syncMuted(const std::string& x, bool& ui) {
    state().muted = (x == "on" || x == "True" || x == "true");
    ui = true;
  }

  static void syncVolume(float level, bool& ui) {
    if (std::isnan(level)) return;
    State& s = state();
    s.volume = static_cast<int>(lroundf(level * 100.0f));
    if (s.pending && s.volume == s.target) s.pending = false;
    if (!s.pending) s.target = s.volume;
    ui = true;
  }

  // ── remote → HA ───────────────────────────────────────────────────────────

  // false while HA hasn't reported a volume yet (nothing to step from)
  static bool volumeStep(int dir, bool& ui) {
    State& s = state();
    if (s.target < 0) return false;
    int v = s.target + dir * VOLUME_STEP;
    s.target  = v < 0 ? 0 : v > 100 ? 100 : v;
    s.pending = true;
    ui = true;
    return true;
  }

  static bool volumeChanged() { return state().target != state().volume; }

  // Some integrations (Sonos) truncate volume_level * 100 (0.29 * 100 is
  // 28.999...), so aim at the middle of the step.
  static std::string volumeToSend() {
    char buf[12];
    snprintf(buf, sizeof(buf), "%.3f", (state().target + 0.4f) / 100.0f);
    return buf;
  }

  // HA never confirmed the step: fall back to what it last reported.
  static void volumeSettled(bool& ui) {
    State& s = state();
    s.pending = false;
    if (s.volume >= 0 && s.target != s.volume) { s.target = s.volume; ui = true; }
  }

  // Shown at once; HA's own report follows.
  static void toggleMute(bool& ui) {
    State& s = state();
    s.mute_target = !s.muted;
    s.muted       = s.mute_target;
    ui = true;
  }

  static std::string muteToSend() { return state().mute_target ? "true" : "false"; }

  // ── display ───────────────────────────────────────────────────────────────

  template<class D, class F>
  static void draw(D* it, F* icons, F* font_bigger, F* font_base, F* font_small,
                   const char* label, const char* source_name,
                   bool& updated_ui, int conn_status) {
    if (!updated_ui) return;
    updated_ui = false;
    const State& s = state();

    it->clear();
    ContextStrip::draw(it, label, 0, 1, conn_status, nullptr);

    it->print(18, CARD_CY, icons, COLOR_ON, display::TextAlign::CENTER, icon(s, source_name));

    // volume, large, centred between the icon and the slider
    const int cx = (36 + VSLIDER_TEXT_R) / 2;
    if (s.target >= 0) {
      char vbuf[12];
      snprintf(vbuf, sizeof(vbuf), "%d", s.target);
      it->print(cx, CARD_CY - 6, font_bigger, COLOR_ON, display::TextAlign::CENTER, vbuf);
    } else {
      it->print(cx, CARD_CY - 6, font_base, COLOR_ON, display::TextAlign::CENTER, "--");
    }

    // title / source / state under it, trimmed to the space left of the slider
    char line[40];
    toSmallFont(subtitle(s, source_name), line, sizeof(line));
    int tx, ty, tw, th;
    it->get_text_bounds(cx, 0, line, font_small, display::TextAlign::TOP_CENTER, &tx, &ty, &tw, &th);
    while (tw > VSLIDER_TEXT_R - 34 && strlen(line) > 1) {
      line[strlen(line) - 1] = '\0';
      it->get_text_bounds(cx, 0, line, font_small, display::TextAlign::TOP_CENTER, &tx, &ty, &tw, &th);
    }
    it->print(cx, 44, font_small, COLOR_ON, display::TextAlign::CENTER, line);

    // slider: the level while sound is on, a tick at it while muted
    const int level = s.target < 0 ? 0 : s.target;
    draw_vslider(it, s.muted ? -1 : level, s.muted ? level : -1);

    char src_key[8];
    toSmallFont(source_name, src_key, sizeof(src_key));
    draw_bottom_menu(it, font_small, src_key, nullptr, s.muted ? "SOUND" : "MUTE");
    Toast::present(it);
  }

private:
  // Material Symbols codepoints; each one is in the media_icon font glyphs
  // (packages/mode_media.yaml).
  static const char* icon(const State& s, const char* source_name) {
    if (s.muted)                 return "";   // volume_off
    if (s.player == "playing")   return s.source == source_name ? "" : "";   // tv / play_arrow
    if (s.player == "paused")    return "";   // pause
    return "";                                 // speaker
  }

  static std::string subtitle(const State& s, const char* source_name) {
    if (s.muted) return "MUTED";
    if (s.player == "playing" || s.player == "paused") {
      if (s.source == source_name) return s.source;
      return !s.title.empty() ? s.title : s.source;
    }
    return s.player.empty() ? std::string("...") : s.player;
  }

  // upper case, and only characters font_small has glyphs for
  static void toSmallFont(const std::string& src, char* out, size_t n) {
    static const char* const OK = "0123456789.%ABCDEFGHIJKLMNOPQRSTUVWXYZ _:-/'&+(),!?#@";
    size_t k = 0;
    for (char c : src) {
      if (k + 1 >= n) break;
      const char u = static_cast<char>(toupper(static_cast<unsigned char>(c)));
      if (u != '\0' && strchr(OK, u)) out[k++] = u;
    }
    out[k] = '\0';
  }
};
