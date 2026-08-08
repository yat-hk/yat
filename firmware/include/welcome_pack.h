#pragma once
// GENERATED from packs/internal/welcome.yat-pack.json — do not hand-edit.
//
// The static first-run "welcome" page: the content rendered while no real
// pages are configured yet. Its copy points at the one setup gesture the
// device has (hold the green button until it beeps -> the on-device setup
// portal); it deliberately mentions no cable, serial console or "connect on
// the website" step, because none of those are part of content setup any more.
//
// The hold is never given as a fixed "5 秒": the threshold is counted from the
// press that woke the device, not from the moment sampling starts, so a real
// hold is 5-10 seconds of somebody's thumb and the beep is the only honest cue
// for when to let go. The headline is deliberately beep-shaped rather than
// 「嘀」-shaped — 嘀 (U+5600) is absent from the 32px bold face this line draws
// with, and prov::drawText/the engine draw nothing at all for a codepoint they
// cannot find. The 24px body line below, whose face does carry it, says 「嘀」.

inline const char WELCOME_PACK[] =
    R"yat(
{
  "yat": 1,
  "id": "welcome",
  "version": "1.2.0",
  "name": {
    "en": "Welcome",
    "zh-Hant": "歡迎"
  },
  "description": {
    "en": "First-run guide, shown until pages are chosen through the device's own setup portal.",
    "zh-Hant": "開始指引：喺揀好內容之前會顯示。"
  },
  "aliases": {
    "en": ["welcome", "home"],
    "zh-Hant": ["歡迎"],
    "jyutping": ["fun1 jing4"]
  },
  "params": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  },
  "data": {
    "sources": []
  },
  "render": {
    "chrome": "standard",
    "widgets": [
      {
        "type": "column",
        "padding": [24, 48, 20, 48],
        "gap": 8,
        "children": [
          {
            "type": "text",
            "value": "仲未揀內容 · Nothing chosen yet",
            "size": "large",
            "weight": "bold",
            "color": "info"
          },
          { "type": "divider" },
          {
            "type": "text",
            "value": "撳住綠色掣，聽到一聲響先放手",
            "size": "large",
            "weight": "bold"
          },
          {
            "type": "text",
            "value": "通常撳住 5 至 10 秒就會「嘀」一聲。跟住呢個畫面會出一個 QR code，用手機掃咗佢，就可以由天氣、潮汐、巴士、新聞等等，揀出想喺度顯示嘅版面。",
            "size": "medium",
            "max_lines": 4
          },
          {
            "type": "text",
            "value": "Hold the green button until you hear the beep — usually 5 to 10 seconds. This screen then shows a QR code — scan it with your phone to pick weather, tides, buses, news and more.",
            "size": "medium",
            "max_lines": 4
          },
          { "type": "spacer", "flex": 1 },
          {
            "type": "text",
            "value": "揀好之後，呢個畫面就會自動換成你嘅內容。 · Once you choose, this screen becomes your content.",
            "size": "small",
            "color": "info",
            "max_lines": 2
          }
        ]
      }
    ]
  },
  "schedule": {
    "default": {
      "every_min": 180
    }
  }
}
)yat";
