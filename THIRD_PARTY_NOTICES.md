# Third-party components

| Component | Location | License |
|---|---|---|
| Noto Sans (Latin/Greek/Cyrillic) | source TTF in `tools/fonts/src/`; rasterized into `engine/src/fonts_data_*.cpp` | SIL Open Font License 1.1 — [`tools/fonts/src/OFL-NotoSans.txt`](tools/fonts/src/OFL-NotoSans.txt) |
| Noto Sans TC (Traditional Chinese) | source TTF in `tools/fonts/src/`; rasterized into `engine/src/fonts_data_*.cpp` | SIL Open Font License 1.1 — [`tools/fonts/src/OFL-NotoSansTC.txt`](tools/fonts/src/OFL-NotoSansTC.txt) |
| efont Unicode bitmap fonts | `engine/third_party/efont/` (vendored; no longer built into any target as of v0.3 — superseded by the Noto rasterizations above) | BSD-style permissive; full notices in [`engine/third_party/efont-licence/`](engine/third_party/efont-licence/) |
| ArduinoJson (Benoit Blanchon) | `engine/third_party/ArduinoJson.h` | MIT |
| stb_image_write (Sean Barrett) | `tools/preview/stb_image_write.h` | Public domain / MIT |
| stb_image (Sean Barrett) | `tools/preview/stb_image.h` | Public domain / MIT |
| qrcodegen (Project Nayuki) | `engine/third_party/qrcodegen/` | MIT |
| uf2conv (Microsoft UF2) | `tools/release/` | MIT — see `tools/release/LICENSE-uf2.txt` |
| s2t table (OpenCC-derived) | `engine/third_party/s2t_table.h` | Apache-2.0 — [`engine/third_party/LICENSE-APACHE-2.0.txt`](engine/third_party/LICENSE-APACHE-2.0.txt) |
| Hacker News story data (test fixtures) | `tools/preview/fixtures/hacker-news-highlights.stories*.json` | Fetched from the [Algolia HN Search API](https://hn.algolia.com/api) (`hn.algolia.com`). Story titles and submitter usernames are public forum metadata, retained here only as fixed input for the render pipeline's tests |

Firmware build dependencies fetched by PlatformIO (Seeed_GFX, arduino-esp32)
carry their own licenses and are not distributed in this repository.

## Test fixtures: what's real and what's synthetic

- `tools/preview/fixtures/headlines-simple.rthk*.xml` and
  `tools/preview/fixtures/news-sections.feeds*.xml` shape themselves like a
  real RTHK RSS feed (CDATA titles, RFC 822 `+0800` `pubDate`, per-item
  `guid`/`link`/`description` — verified against the live endpoints) but every
  headline and article body is invented placeholder civic-news copy. None of
  it reports a real event, and no byline names a real person.
- `tools/preview/fixtures/stock-ticker.quotes.*.json`,
  `tools/preview/fixtures/hsi.quote*.json` and
  `tools/preview/fixtures/fx-hkd.rate.json` are all synthetic numeric
  snapshots — shaped like the Finnhub `/quote`, Yahoo Finance chart, and
  open.er-api.com responses these packs read at runtime, but with invented
  prices/rates rather than a captured real market moment.
- `tools/preview/fixtures/hacker-news-highlights.stories*.json` is the one
  exception: real (but ordinary, public) HN front-page metadata, kept because
  fabricating story titles would defeat the point of testing against
  Algolia's actual response shape.

## Runtime data sources (not redistributed)

At runtime, a flashed device fetches live data directly from each pack's
configured public API — for example HKO (weather, warnings), EPD/AQHI (air
quality), RTHK (news), DATA.GOV.HK transit feeds (MTR/bus/ferry), Open-Meteo,
and whichever market/news APIs a given pack is configured to call. This
project does not proxy, cache, or redistribute any of that data — the device
talks to each provider directly, and use of the fetched data is governed by
that provider's own terms, which is the user's responsibility to observe.
