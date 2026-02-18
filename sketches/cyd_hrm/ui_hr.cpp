#include "ui_hr.h"

namespace ui_hr {
namespace {
BleHrClient* g_client = nullptr;
lv_obj_t* g_bpm_label = nullptr;
lv_obj_t* g_minmax_label = nullptr;
lv_obj_t* g_status_label = nullptr;

const char* stateText(HrConnState state) {
  switch (state) {
    case HrConnState::DISCONNECTED: return "disconnected";
    case HrConnState::SCANNING: return "scanning";
    case HrConnState::CONNECTING: return "connecting";
    case HrConnState::SUBSCRIBED: return "connected";
    case HrConnState::ERROR: return "error";
    default: return "unknown";
  }
}

void formatRuntime(char* out, size_t out_len, uint32_t elapsed_ms) {
  const uint32_t sec = elapsed_ms / 1000U;
  const uint32_t hh = sec / 3600U;
  const uint32_t mm = (sec % 3600U) / 60U;
  const uint32_t ss = sec % 60U;
  snprintf(out, out_len, "%02lu:%02lu:%02lu", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
}

void onResetClicked(lv_event_t* e) {
  (void)e;
  if (g_client != nullptr) {
    g_client->resetMetrics();
  }
}

}  // namespace

void init(BleHrClient* client) {
  g_client = client;

  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_text_color(scr, lv_color_white(), 0);

  g_bpm_label = lv_label_create(scr);
  lv_obj_set_style_text_font(g_bpm_label, &lv_font_montserrat_48, 0);
  lv_label_set_text(g_bpm_label, "--");
  lv_obj_align(g_bpm_label, LV_ALIGN_CENTER, 0, -42);

  g_minmax_label = lv_label_create(scr);
  lv_obj_set_style_text_font(g_minmax_label, &lv_font_montserrat_16, 0);
  lv_label_set_text(g_minmax_label, "MIN --   MAX --");
  lv_obj_align(g_minmax_label, LV_ALIGN_CENTER, 0, 18);

  g_status_label = lv_label_create(scr);
  lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_14, 0);
  lv_label_set_text(g_status_label, "TIME 00:00:00   disconnected");
  lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -16);

  lv_obj_t* btn = lv_button_create(scr);
  lv_obj_set_size(btn, 86, 34);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
  lv_obj_add_event_cb(btn, onResetClicked, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* btn_lbl = lv_label_create(btn);
  lv_label_set_text(btn_lbl, "Reset");
  lv_obj_center(btn_lbl);
}

void refresh(const HrSnapshot& snap, uint32_t now_ms) {
  if (g_bpm_label == nullptr) {
    return;
  }

  if (snap.has_hr) {
    lv_label_set_text_fmt(g_bpm_label, "%u", snap.current_bpm);
    lv_label_set_text_fmt(g_minmax_label, "MIN %u   MAX %u", snap.min_bpm, snap.max_bpm);
  } else {
    lv_label_set_text(g_bpm_label, "--");
    lv_label_set_text(g_minmax_label, "MIN --   MAX --");
  }

  char runtime[16];
  const uint32_t elapsed = now_ms - snap.start_ms;
  formatRuntime(runtime, sizeof(runtime), elapsed);

  lv_label_set_text_fmt(g_status_label, "TIME %s   %s", runtime, stateText(snap.state));
}

}  // namespace ui_hr

