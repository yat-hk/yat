#pragma once
// Frozen firmware-embedded fallback pack — do not hand-edit. Generated long
// ago from an early hko-now.yat-pack.json (v0.1.0), back when that pack
// still lived in this repo's packs/examples/; the real hko-now has since
// moved to yat-packs' official/hko-now.yat-pack.json and evolved well past
// this snapshot (HKO warnsum strip, colour-coded temperature, ...). This
// copy is intentionally not kept in sync — main.cpp reaches for it only as
// the boot-safe fallback when the configured pack fails to load.

static const char HKO_NOW_PACK[] =
    R"yat(
{
  "yat": 1,
  "id": "hko-now",
  "version": "0.1.0",
  "name": {
    "en": "Weather Now",
    "zh-Hant": "現時天氣"
  },
  "description": {
    "en": "The v0.1 walking-skeleton page: current temperature and humidity for one district, straight from the Hong Kong Observatory.",
    "zh-Hant": "v0.1 骨架頁：顯示所選地區嘅現時氣溫同濕度，數據直接嚟自香港天文台。"
  },
  "aliases": {
    "en": [
      "weather"
    ],
    "zh-Hant": [
      "天氣"
    ],
    "jyutping": [
      "tin1 hei3"
    ]
  },
  "params": {
    "type": "object",
    "properties": {
      "district": {
        "type": "string",
        "title": "District",
        "description": "HKO temperature station, Chinese name as used by the tc feed, e.g. 沙田, 香港天文台, 屯門.",
        "default": "沙田",
        "maxLength": 40
      }
    },
    "required": [
      "district"
    ],
    "additionalProperties": false
  },
  "data": {
    "sources": [
      {
        "id": "current",
        "type": "https",
        "url": "https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=rhrread&lang=tc",
        "format": "json",
        "extract": {
          "temp": "temperature.data[?place=='{{params.district}}'].value|[0]",
          "humidity": "humidity.data[0].value",
          "updated": "updateTime"
        }
      }
    ]
  },
  "render": {
    "chrome": "standard",
    "widgets": [
      {
        "type": "column",
        "padding": [
          24,
          32,
          16,
          32
        ],
        "gap": 12,
        "children": [
          {
            "type": "row",
            "gap": 24,
            "align": "center",
            "children": [
              {
                "type": "bignum",
                "value": "{{data.temp}}°",
                "size": "xlarge"
              },
              {
                "type": "column",
                "flex": 1,
                "gap": 8,
                "children": [
                  {
                    "type": "text",
                    "value": "{{params.district}}",
                    "size": "large",
                    "weight": "bold"
                  },
                  {
                    "type": "text",
                    "value": "濕度 {{data.humidity}}%",
                    "size": "medium"
                  },
                  {
                    "type": "text",
                    "value": "高溫注意",
                    "size": "medium",
                    "color": "red",
                    "when": "data.temp >= '33'"
                  }
                ]
              }
            ]
          },
          {
            "type": "spacer",
            "flex": 1
          },
          {
            "type": "divider"
          },
          {
            "type": "text",
            "value": "天文台 {{data.updated|time_hhmm}} 更新",
            "size": "small",
            "align": "right"
          }
        ]
      }
    ]
  },
  "schedule": {
    "default": {
      "every_min": 60
    }
  }
})yat";
