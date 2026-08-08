#pragma once
// GENERATED from packs/internal/warning-takeover.yat-pack.json -- do not hand-edit.
// Regenerate with the command in firmware/README.md's "Warning takeover" section.
//
// Firmware-internal pack (never on LittleFS, never user-installed) for the
// warning-takeover feature (PRD §4.2/§5.1, docs/UX-FLOWS.md G15) -- see
// firmware/README.md "Warning takeover" for the policy and design rationale.

static const char WARNING_TAKEOVER_PACK[] =
    R"yat(
{
  "yat": 1,
  "id": "warning-takeover",
  "version": "1.0.1",
  "name": {
    "en": "Warning",
    "zh-Hant": "警告"
  },
  "description": {
    "en": "Firmware-internal takeover page for active HKO typhoon (T1/T3/T8/T9/T10) and rainstorm (amber/red/black) warnings. Not user-installed; exempt from the warning-language rule — see firmware/README.md.",
    "zh-Hant": "firmware 內置嘅全螢幕頁面，顯示現正生效嘅天文台熱帶氣旋警告信號（一/三/八/九/十號）同暴雨警告（黃/紅/黑色）。內嵌喺韌體入面，由 firmware 政策直接觸發（用戶唔會安裝，亦唔會出現喺 pack 目錄），因為呢個係firmware 自己嘅警告畫面，所以豁免「pack 唔可以用警告用語」呢條規則；透過標準 engine 渲染，所以一樣可以用 yat-preview 預覽。"
  },
  "aliases": {
    "en": ["warning", "typhoon"],
    "zh-Hant": ["警告", "打風"],
    "jyutping": ["ging2 gou3", "daa2 fung1"]
  },
  "params": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  },
  "data": {
    "sources": [
      {
        "id": "warnsum",
        "type": "https",
        "url": "https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=warnsum&lang=tc",
        "format": "json",
        "max_bytes": 8192,
        "extract": {
          "tc_code": "WTCSGNL.code",
          "tc_issued": "WTCSGNL.issueTime",
          "rain_code": "WRAIN.code",
          "rain_issued": "WRAIN.issueTime"
        }
      }
    ]
  },
  "render": {
    "chrome": "standard",
    "widgets": [
      {
        "type": "column",
        "flex": 1,
        "padding": [20, 32, 12, 32],
        "gap": 14,
        "children": [
          {
            "type": "spacer",
            "flex": 1
          },
          {
            "type": "column",
            "when": "data.tc_code == 'TC1'",
            "gap": 10,
            "children": [
              { "type": "text", "value": "熱帶氣旋警告信號", "size": "medium", "weight": "bold" },
              { "type": "text", "value": "Tropical Cyclone Warning Signal", "size": "small" },
              {
                "type": "row", "gap": 24, "align": "center",
                "children": [
                  { "type": "icon", "name": "wind", "size": "medium", "color": "black" },
                  { "type": "bignum", "value": "1", "size": "xlarge", "color": "black" },
                  {
                    "type": "column", "flex": 1, "gap": 4,
                    "children": [
                      { "type": "text", "value": "一號戒備信號", "size": "large", "weight": "bold", "max_lines": 2 },
                      { "type": "text", "value": "No. 1 Standby Signal", "size": "medium", "max_lines": 2 }
                    ]
                  }
                ]
              },
              { "type": "text", "value": "發出時間 issued {{data.tc_issued|time_hhmm}}", "size": "small" }
            ]
          },
          {
            "type": "column",
            "when": "data.tc_code == 'TC3'",
            "gap": 10,
            "children": [
              { "type": "text", "value": "熱帶氣旋警告信號", "size": "medium", "weight": "bold" },
              { "type": "text", "value": "Tropical Cyclone Warning Signal", "size": "small" },
              {
                "type": "row", "gap": 24, "align": "center",
                "children": [
                  { "type": "icon", "name": "wind", "size": "large", "color": "black" },
                  { "type": "bignum", "value": "3", "size": "xlarge", "color": "black" },
                  {
                    "type": "column", "flex": 1, "gap": 4,
                    "children": [
                      { "type": "text", "value": "三號強風信號", "size": "large", "weight": "bold", "max_lines": 2 },
                      { "type": "text", "value": "No. 3 Strong Wind Signal", "size": "medium", "max_lines": 2 }
                    ]
                  }
                ]
              },
              { "type": "text", "value": "發出時間 issued {{data.tc_issued|time_hhmm}}", "size": "small" }
            ]
          },
          {
            "type": "column",
            "when": { "any": ["data.tc_code == 'TC8NE'", "data.tc_code == 'TC8SE'", "data.tc_code == 'TC8SW'", "data.tc_code == 'TC8NW'"] },
            "gap": 10,
            "children": [
              { "type": "text", "value": "熱帶氣旋警告信號", "size": "medium", "weight": "bold" },
              { "type": "text", "value": "Tropical Cyclone Warning Signal", "size": "small" },
              {
                "type": "row", "gap": 24, "align": "center",
                "children": [
                  { "type": "icon", "name": "alert", "size": "large", "color": "red" },
                  { "type": "bignum", "value": "8", "size": "xlarge", "color": "red" },
                  {
                    "type": "column", "flex": 1, "gap": 4,
                    "children": [
                      { "type": "text", "value": "八號東北烈風或暴風信號", "size": "large", "weight": "bold", "max_lines": 2, "when": "data.tc_code == 'TC8NE'" },
                      { "type": "text", "value": "No. 8 Northeast Gale or Storm Signal", "size": "medium", "max_lines": 2, "when": "data.tc_code == 'TC8NE'" },
                      { "type": "text", "value": "八號東南烈風或暴風信號", "size": "large", "weight": "bold", "max_lines": 2, "when": "data.tc_code == 'TC8SE'" },
                      { "type": "text", "value": "No. 8 Southeast Gale or Storm Signal", "size": "medium", "max_lines": 2, "when": "data.tc_code == 'TC8SE'" },
                      { "type": "text", "value": "八號西南烈風或暴風信號", "size": "large", "weight": "bold", "max_lines": 2, "when": "data.tc_code == 'TC8SW'" },
                      { "type": "text", "value": "No. 8 Southwest Gale or Storm Signal", "size": "medium", "max_lines": 2, "when": "data.tc_code == 'TC8SW'" },
                      { "type": "text", "value": "八號西北烈風或暴風信號", "size": "large", "weight": "bold", "max_lines": 2, "when": "data.tc_code == 'TC8NW'" },
                      { "type": "text", "value": "No. 8 Northwest Gale or Storm Signal", "size": "medium", "max_lines": 2, "when": "data.tc_code == 'TC8NW'" }
                    ]
                  }
                ]
              },
              { "type": "text", "value": "發出時間 issued {{data.tc_issued|time_hhmm}}", "size": "small" },
              { "type": "text", "value": "學校及法院停課（按標準安排）", "size": "medium", "weight": "bold", "color": "red" },
              { "type": "text", "value": "Schools and courts closed (standard arrangements)", "size": "small", "color": "red" }
            ]
          },
          {
            "type": "column",
            "when": "data.tc_code == 'TC9'",
            "gap": 10,
            "children": [
              { "type": "text", "value": "熱帶氣旋警告信號", "size": "medium", "weight": "bold" },
              { "type": "text", "value": "Tropical Cyclone Warning Signal", "size": "small" },
              {
                "type": "row", "gap": 24, "align": "center",
                "children": [
                  { "type": "icon", "name": "alert", "size": "large", "color": "red" },
                  { "type": "bignum", "value": "9", "size": "xlarge", "color": "red" },
                  {
                    "type": "column", "flex": 1, "gap": 4,
                    "children": [
                      { "type": "text", "value": "九號烈風或暴風風力增強信號", "size": "large", "weight": "bold", "max_lines": 2 },
                      { "type": "text", "value": "No. 9 Increasing Gale or Storm Signal", "size": "medium", "max_lines": 2 }
                    ]
                  }
                ]
              },
              { "type": "text", "value": "發出時間 issued {{data.tc_issued|time_hhmm}}", "size": "small" },
              { "type": "text", "value": "學校及法院停課（按標準安排）", "size": "medium", "weight": "bold", "color": "red" },
              { "type": "text", "value": "Schools and courts closed (standard arrangements)", "size": "small", "color": "red" }
            ]
          },
          {
            "type": "column",
            "when": "data.tc_code == 'TC10'",
            "gap": 10,
            "children": [
              { "type": "text", "value": "熱帶氣旋警告信號", "size": "medium", "weight": "bold" },
              { "type": "text", "value": "Tropical Cyclone Warning Signal", "size": "small" },
              {
                "type": "row", "gap": 24, "align": "center",
                "children": [
                  { "type": "icon", "name": "alert", "size": "large", "color": "red" },
                  { "type": "bignum", "value": "10", "size": "xlarge", "color": "red" },
                  {
                    "type": "column", "flex": 1, "gap": 4,
                    "children": [
                      { "type": "text", "value": "十號颶風信號", "size": "large", "weight": "bold", "max_lines": 2 },
                      { "type": "text", "value": "No. 10 Hurricane Signal", "size": "medium", "max_lines": 2 }
                    ]
                  }
                ]
              },
              { "type": "text", "value": "發出時間 issued {{data.tc_issued|time_hhmm}}", "size": "small" },
              { "type": "text", "value": "學校及法院停課（按標準安排）", "size": "medium", "weight": "bold", "color": "red" },
              { "type": "text", "value": "Schools and courts closed (standard arrangements)", "size": "small", "color": "red" }
            ]
          },
          {
            "type": "column",
            "when": { "all": ["data.tc_code != 'TC1'", "data.tc_code != 'TC3'", "data.tc_code != 'TC8NE'", "data.tc_code != 'TC8SE'", "data.tc_code != 'TC8SW'", "data.tc_code != 'TC8NW'", "data.tc_code != 'TC9'", "data.tc_code != 'TC10'"] },
            "gap": 10,
            "children": [
              {
                "type": "column",
                "when": "data.rain_code == 'WRAINA'",
                "gap": 10,
                "children": [
                  { "type": "text", "value": "暴雨警告信號", "size": "medium", "weight": "bold" },
                  { "type": "text", "value": "Rainstorm Warning Signal", "size": "small" },
                  {
                    "type": "row", "gap": 24, "align": "center",
                    "children": [
                      { "type": "icon", "name": "rain", "size": "large", "color": "yellow" },
                      { "type": "bignum", "value": "黃", "size": "xlarge", "color": "yellow" },
                      {
                        "type": "column", "flex": 1, "gap": 4,
                        "children": [
                          { "type": "text", "value": "黃色暴雨警告信號", "size": "large", "weight": "bold", "max_lines": 2 },
                          { "type": "text", "value": "Amber Rainstorm Warning Signal", "size": "medium", "max_lines": 2 }
                        ]
                      }
                    ]
                  },
                  { "type": "text", "value": "發出時間 issued {{data.rain_issued|time_hhmm}}", "size": "small" }
                ]
              },
              {
                "type": "column",
                "when": "data.rain_code == 'WRAINR'",
                "gap": 10,
                "children": [
                  { "type": "text", "value": "暴雨警告信號", "size": "medium", "weight": "bold" },
                  { "type": "text", "value": "Rainstorm Warning Signal", "size": "small" },
                  {
                    "type": "row", "gap": 24, "align": "center",
                    "children": [
                      { "type": "icon", "name": "rain_heavy", "size": "large", "color": "red" },
                      { "type": "bignum", "value": "紅", "size": "xlarge", "color": "red" },
                      {
                        "type": "column", "flex": 1, "gap": 4,
                        "children": [
                          { "type": "text", "value": "紅色暴雨警告信號", "size": "large", "weight": "bold", "max_lines": 2 },
                          { "type": "text", "value": "Red Rainstorm Warning Signal", "size": "medium", "max_lines": 2 }
                        ]
                      }
                    ]
                  },
                  { "type": "text", "value": "發出時間 issued {{data.rain_issued|time_hhmm}}", "size": "small" }
                ]
              },
              {
                "type": "column",
                "when": "data.rain_code == 'WRAINB'",
                "gap": 10,
                "children": [
                  { "type": "text", "value": "暴雨警告信號", "size": "medium", "weight": "bold" },
                  { "type": "text", "value": "Rainstorm Warning Signal", "size": "small" },
                  {
                    "type": "row", "gap": 24, "align": "center",
                    "children": [
                      { "type": "icon", "name": "alert", "size": "large", "color": "red" },
                      { "type": "bignum", "value": "黑", "size": "xlarge", "color": "black" },
                      {
                        "type": "column", "flex": 1, "gap": 4,
                        "children": [
                          { "type": "text", "value": "黑色暴雨警告信號", "size": "large", "weight": "bold", "max_lines": 2 },
                          { "type": "text", "value": "Black Rainstorm Warning Signal", "size": "medium", "max_lines": 2 }
                        ]
                      }
                    ]
                  },
                  { "type": "text", "value": "發出時間 issued {{data.rain_issued|time_hhmm}}", "size": "small" },
                  { "type": "text", "value": "學校及法院停課（按標準安排）", "size": "medium", "weight": "bold", "color": "red" },
                  { "type": "text", "value": "Schools and courts closed (standard arrangements)", "size": "small", "color": "red" }
                ]
              },
              {
                "type": "column",
                "when": { "all": ["data.rain_code != 'WRAINA'", "data.rain_code != 'WRAINR'", "data.rain_code != 'WRAINB'"] },
                "gap": 10,
                "children": [
                  { "type": "icon", "name": "check", "size": "large", "color": "good" },
                  { "type": "text", "value": "現時沒有生效警告", "size": "large", "weight": "bold" },
                  { "type": "text", "value": "No warning currently in force", "size": "medium" }
                ]
              }
            ]
          },
          {
            "type": "spacer",
            "flex": 1
          }
        ]
      }
    ]
  },
  "schedule": {
    "default": { "every_min": 15 }
  }
})yat";
