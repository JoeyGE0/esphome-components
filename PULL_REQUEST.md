# Pull Request: Crow panel status decode (0x10), clock fix (0x54), panel info (0x23)

**Source:** [JoeyGE0/esphome-components](https://github.com/JoeyGE0/esphome-components)  
**Target:** [jallier/esphome-components](https://github.com/jallier/esphome-components) (`main`)  
**Panel tested:** Crow v908 / firmware v2.1 (Arrowhead/AAP ESLite-style bus)  
**ESPHome tested:** 2026.5.3  
**Device tested:** ESP32 on GPIO32 (clock) / GPIO33 (data)

---

## Summary

This PR adds field-validated decoding for opcode **`0x10`** (panel ready, mains fault, battery low), fixes **`0x54`** panel clock/date handling, and publishes **`0x23`** panel hardware/firmware to Home Assistant. It replaces earlier experimental heuristics (0x54 prefix guessing, C2/C3-paired mains rules) with rules derived from long captured bus sessions on a live installation.

Upstream [jallier/esphome-components](https://github.com/jallier/esphome-components/tree/main/components) currently **ignores `0x10` entirely** and only logs `0x54` time with an incorrect byte layout. This fork implements the missing handlers and exposes them as optional binary sensors plus auto-created diagnostic text sensors.

---

## Motivation

Users integrating Crow/Arrowhead panels via ESPHome need:

1. **Mains / AC fail** exposed in Home Assistant (not only raw bus logging in YAML).
2. **Battery low** when the panel reports it on the bus — on tested hardware this only appears together with AC fail (`B1 == 0x03`).
3. **Panel ready** as its own signal from `0x10` byte B2 (`C1` = ready, `C0` = not ready), separate from zone binary sensors.
4. **Panel clock and date** from `0x54` published correctly — upstream treated bytes `[1][2]` as hour/minute; they are big-endian minutes since midnight.
5. **Panel hardware and firmware version** text sensors from `0x23` (e.g. `v908`, `v2.1`), registered automatically when the component is configured.

Decode rules were checked against multiple captured bus sessions: normal AC, AC fail, battery low, mains loss/restore, and ready/not-ready toggles on `C0`/`C1`.

---

## Repo layout note (important for merge)

| Repo | Component path | `external_components` YAML |
|------|----------------|----------------------------|
| **jallier** (target) | `components/crow_alarm_panel/` | `path: components` |
| **JoeyGE0** (this fork) | `crow_alarm_panel/` (repo root) | `path: .` |

When opening the PR against jallier, **place files under `components/crow_alarm_panel/`** (or merge jallier’s `44368e0` layout commit first, then apply diffs). Functionality is identical; only directory structure differs.

---

## Protocol findings (opcode `0x10`)

Frame shape: `10.00.B1.B2.00.00`

### Byte B1 (fault flags)

| B1 hex | Binary   | Mains fail | Battery low | Notes |
|--------|----------|------------|-------------|-------|
| `0x00` | `00000000` | OFF | OFF | Normal; ready bit is in B2 |
| `0x01` | `00000001` | — | — | **Transition** — do not latch |
| `0x02` | `00000010` | ON | OFF | AC fail, battery OK |
| `0x03` | `00000011` | ON | ON | AC fail + battery low (never seen without AC fail) |

### Byte B2 (ready bit — not battery)

| B2 | Meaning |
|----|---------|
| `C1` | Panel **ready** |
| `C0` | Panel **not ready** |
| `C2` / `C3` | Same ready toggle during fault states; **ignore for fault typing** |

### Entity mapping (implemented)

```text
mains_power ON          when  (B1 & 0x02) != 0     # 0x02 or 0x03
mains_power OFF         when  B1 == 0x00
battery_state ON        when  B1 == 0x03
battery_state OFF       when  B1 == 0x02 or B1 == 0x00
ignore transition       when  B1 == 0x01
panel_ready ON          when  B1 == 0x00 and B2 == 0xC1
panel_ready OFF         when  B1 == 0x00 and B2 == 0xC0
```

Boot defaults: `mains_power`, `battery_state`, and `panel_ready` publish **OFF** in `setup()` so Home Assistant never shows `unknown` before the first `0x10` frame (which may be minutes apart when idle).

---

## Protocol findings (opcode `0x54` — panel clock)

**Fix:** bytes `[1][2]` are **big-endian minutes since midnight**, not hour + minute when byte1 ≠ 0.

Examples from field logs:

| Raw bytes `[1][2]` | Decoded time |
|--------------------|--------------|
| `00.84` | 02:12 |
| `01.1D` | 04:45 |
| `07.05.5F` | 22:39 (minutes = 0x075F) |

**Date bytes** are only trusted on phase bytes `0x1E` and `0x2D` (not on `0x00` top-of-minute). Phase `0x0F` is time-only.

Published format:

- **Panel time:** `HH:MM:SS` (15-second phases)
- **Panel date:** `YYYY-MM-DD` (year included — separate year sensor removed as redundant)

---

## Protocol findings (opcode `0x23` — panel info)

Observed stable payload: `00.03.8C.02.01.00.23.03` (8 bytes)

| Field | Bytes | Example |
|-------|-------|---------|
| Hardware | `[1][2]` BE16 | `0x038C` → **v908** |
| Firmware | `[3][4]` | `0x02.0x01` → **v2.1** |

**Recommendation (follow-up, not blocking):** validate tail `23.03` and/or prefix `00.03.8C` before publishing to ignore corrupt frames seen briefly after OTA reboot (e.g. spurious v836 / v129.0).

---

## What changed (files)

### `crow_alarm_panel.cpp` / `crow_alarm_panel.h`

- Add **`PANEL_READY` (`0x10`)** handler: ready, mains, battery decode per table above.
- Add **`PANEL_INFO` (`0x23`)** handler: hardware + firmware text sensors.
- Fix **`CURRENT_TIME` (`0x54`)** handler: `decode_panel_clock_time()`, trusted date phases.
- Remove **`apply_battery_low_heuristic_()`** (incorrect — matched clock minute bytes in `0x54`, false positives).
- Remove unused `last_time_*` / `mains_fault_active_` state used only by that heuristic.
- Publish startup defaults for diagnostic text sensors and power binary sensors.
- Rename/clarify constants: `UNKNOWN` → `PANEL_READY`, `CURRENT_TEMP` → `PANEL_INFO`.

### `crow_alarm_panel/__init__.py`

- Auto-register diagnostic text sensors (no YAML required):
  - Panel hardware
  - Panel firmware
  - Panel time
  - Panel date
- **Removed** auto **Panel year** (duplicate of date’s `YYYY-MM-DD`).

### `crow_alarm_panel/binary_sensor/__init__.py`

- Add optional YAML types:
  - `panel_ready`
  - `mains_power` (`device_class: problem`, diagnostic)
  - `battery_state_experimental` (`device_class: battery`, diagnostic)

### `README.md`

- Document new entities, protocol table updates, YAML examples.

### Unchanged (same as upstream)

- Zone / bypass / armed state / keypad / output / arm-disarm buttons
- `crow_alarm_control_panel.cpp`, switch/button modules
- ISR bus decode and transmission path

---

## Breaking / behavior changes

| Area | Before (jallier) | After (this PR) |
|------|------------------|-----------------|
| `0x10` | Ignored | Decoded; optional entities |
| `0x54` | Log-only, wrong time math | HA text sensors, corrected decode |
| `0x23` | Ignored | HW/FW text sensors |
| Battery low | N/A | `B1 == 0x03` only (not 0x54 heuristic) |
| Mains | N/A | `B1 & 0x02` (not strictly tied to B2 C2/C3) |
| Panel year sensor | N/A | **Not added** (date includes year) |

**YAML migration for existing users of experimental rules:**

```yaml
binary_sensor:
  - platform: crow_alarm_panel
    crow_alarm_panel_id: alarm_panel
    type: panel_ready
    name: "Panel Ready"

  - platform: crow_alarm_panel
    crow_alarm_panel_id: alarm_panel
    type: mains_power
    name: "Mains power"

  - platform: crow_alarm_panel
    crow_alarm_panel_id: alarm_panel
    type: battery_state_experimental
    name: "Battery state"
```

Remove any YAML-only `0x10` / `0x54` template heuristics — they are superseded by the component.

---

## Field test evidence

### Scenarios logged

| Scenario | B1 seen | Mains | Battery | Source file |
|----------|---------|-------|---------|-------------|
| AC on, normal | `0x00` only | OFF | OFF | `AC ON. .txt` |
| AC fail, batt OK (~20 min) | `0x02` only | ON | OFF | `morelogs.txt` raw |
| AC fail + batt low | `0x03` only | ON | ON | `NO AC LOGS WITH BAT LOW.txt` |
| Unplug / restore | `0x01` → `0x03` → `0x02` → `0x00` | transitions | transitions | `logs.text` |
| Ready/not-ready toggle | `0x00` + C0/C1 | OFF | OFF | Normal AC logs |

### Live test (2026-06-17, post-OTA)

- **Panel time** matched the keypad LCD within one 15 s phase update.
- **AC fail test:** `00.02.C2` → Mains power **problem detected**; cleared on mains restore.
- **Ready bit:** `00.00.C0` (not ready) ↔ `00.00.C1` (ready) toggles matched panel state.
- **HW/FW stable:** v908 / v2.1 after boot (ignore brief corrupt reads on reboot).

### Known limitations

- **`0x10` is event-driven** — not periodic. Long idle periods without frames are normal.
- **Battery low not observed with mains ON** — only `0x03` with AC fail on this panel.
- **Panel-specific:** rules validated on v908/v2.1; other Crow revisions may differ.
- **`battery_state_experimental` name** kept for compatibility; consider renaming to `battery_state` in a follow-up.

---

## Suggested PR title

```text
feat(crow_alarm_panel): decode 0x10 power/ready, fix 0x54 clock, add 0x23 panel info
```

---

## Suggested merge checklist

- [ ] Rebase onto jallier `main` (includes `components/` path move `44368e0`).
- [ ] Copy/cherry-pick into `components/crow_alarm_panel/` if needed.
- [ ] Update root + component README (remove Panel year references).
- [ ] Confirm compile on ESPHome ≥ 2025.11.
- [ ] Optional follow-up: `0x23` frame validation; rename `battery_state_experimental`.
- [ ] Optional follow-up: make HW/FW/time/date YAML-opt-in instead of auto-created (if upstream prefers minimal entities).

---

## Test plan (for reviewer)

1. Flash with `external_components` pointing at PR branch.
2. Confirm **Panel hardware** → `v908`, **Panel firmware** → `v2.1` within ~30 s.
3. Confirm **Panel time** matches the keypad within one phase (15 s).
4. Trigger not-ready → **Panel Ready** OFF (`C0`); back to ready → ON (`C1`).
5. Remove mains / trigger AC fail → **Mains power** ON at `B1=0x02`; restore → OFF.
6. With AC out and panel reporting battery low → **Battery state** ON at `B1=0x03`.
7. Verify no spurious battery triggers from clock (`0x54`) traffic alone.

---

## Credits

Builds on [jesserockz/esphome-components](https://github.com/jesserockz/esphome-components) and [jallier/esphome-components](https://github.com/jallier/esphome-components). Field decode and validation by JoeyGE0 on an AAP Arrowhead installation.

---

## Commits to include (JoeyGE0 `main`, squashing optional)

Key commits (newest first):

- `ae73df2` — Remove redundant Panel year auto sensor
- `293125c` — Boot defaults for mains / battery / panel ready
- `8f3663c` — 0x10 decode + 0x54 clock fix; remove 0x54 battery heuristic
- Earlier commits — panel_ready, mains_power, battery wiring, 0x23/0x54 publish, README

Full range: `0bcb1ae..ae73df2` on [JoeyGE0/esphome-components](https://github.com/JoeyGE0/esphome-components).
