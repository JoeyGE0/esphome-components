# Pull Request: Crow panel diagnostics — `PANEL_STATUS` (0x10), `PANEL_INFO` (0x23), `LCD_CONTENT` (0x54)

**Source:** [JoeyGE0/esphome-components](https://github.com/JoeyGE0/esphome-components)  
**Target:** [jallier/esphome-components](https://github.com/jallier/esphome-components) (`main`)  
**Baseline compared:** [jallier `main` @ `components/crow_alarm_panel/`](https://github.com/jallier/esphome-components/tree/main/components/crow_alarm_panel) (fetched 2026-06-20)  
**Panel tested:** Crow **V908.3** (PCB sticker) / bus **v908** / firmware **v2.1** (Arrowhead/AAP ESLite-style bus)  
**ESPHome tested:** 2026.5.3  
**Device tested:** ESP32 on GPIO32 (clock) / GPIO33 (data)

**Opcode constants (this PR):** `PANEL_STATUS` (`0x10`), `PANEL_INFO` (`0x23`), `LCD_CONTENT` (`0x54`).

---

## Summary

Field-validated decoding for three opcodes jallier `main` does not handle today. Table is **jallier upstream BEFORE** → **this PR AFTER**:

| Opcode | jallier upstream **BEFORE** | This PR **AFTER** |
|--------|----------------------------|-------------------|
| **`0x10`** | Constant `UNKNOWN`; **no handler** — frames dropped | **`PANEL_STATUS`** — mains, battery, panel ready (binary sensors) |
| **`0x23`** | Constant `CURRENT_TEMP`; **not handled** | **`PANEL_INFO`** — panel hardware + firmware (text sensors) |
| **`0x54`** | Constant `CURRENT_TIME`; **log only** (wrong hour/minute in log text). **No HA entity** | **`LCD_CONTENT`** — **log only** (raw hex in verbose log). **No HA entity** |

Configuring **`crow_alarm_panel:`** **auto-creates six diagnostic entities** (hardware, firmware, ready, mains, battery, bus connected). Zones, armed state, ACP, buttons, and outputs stay optional YAML as before.

---

## Breaking changes

Read this first if you already run jallier `main` in production.

### 1. Six new Home Assistant entities appear automatically

**Before (jallier `main`):** Only entities you declared in YAML existed (`zone`, `bypass`, `armed_state`, ACP, etc.).

**After (this PR):** Every `crow_alarm_panel:` block registers **six extra entities** with no YAML:

| Entity | Type | Initial state |
|--------|------|---------------|
| Panel hardware | text | `unknown` |
| Panel firmware | text | `unknown` |
| Panel ready | binary | OFF |
| Mains power | binary (problem) | OFF |
| Battery state | binary (battery) | OFF |
| Alarm bus connected | binary (connectivity) | OFF |

**Impact:** Entity count grows; dashboards/automations that assumed a fixed entity set may need updating. Names are prefixed with `crow_alarm_panel:` `name:` when set (e.g. `"Crow Alarm Panel hardware"`).

**Migration:** Do not duplicate these in YAML (see §2). Hide unwanted entities in HA if needed — there is no flag to disable auto-create.

### 2. Duplicate YAML registration (do not double-declare auto entities)

Optional YAML types exist for the same C++ slots. Declaring them **in addition** to the auto-created ones registers **two sensors on one pointer** → compile error or undefined behaviour.

**Do not add YAML for:** `panel_hardware`, `panel_firmware`, `panel_ready`, `mains_power`, `battery_state`, `panel_bus_connected`.

**Safe optional YAML:** `armed_state`, `zone`, `bypass`, ACP, buttons, switches.

### 3. C++ opcode constant renames (fork / log grep only)

| jallier `main` | This PR |
|----------------|---------|
| `UNKNOWN` (`0x10`) | `PANEL_STATUS` |
| `CURRENT_TEMP` (`0x23`) | `PANEL_INFO` |
| `CURRENT_TIME` (`0x54`) | `LCD_CONTENT` |

No YAML keys changed on jallier `main`. Only affects C++ forks or debug builds.

### 4. `0x54` log text changes — still no HA entity

| | jallier **BEFORE** | This PR **AFTER** |
|---|-------------------|-------------------|
| HA entities | **None** | **None** |
| C++ behaviour | Log line with wrong clock math | `ESP_LOGV` raw hex only; wrong decode **removed** |

### 5. `0x10` now drives HA state (was silently ignored)

On jallier `main`, `0x10` frames hit `default:` and are discarded. This PR decodes them into **mains power**, **battery state**, and **panel ready**.

---

## Non-breaking / additive

| Change | Notes |
|--------|-------|
| `include_bypass_sensor: true` on `type: zone` | Opt-in; creates `"{name} Bypassed"` automatically |
| `type: bypass` | Still supported unchanged |
| Zone / armed state / ACP / buttons / switches / TX path | Same as jallier `main` (plus pre-existing upstream bugs — see AI review table) |

---

## Complete diff vs jallier GitHub `main`

Compared file-by-file against  
`https://github.com/jallier/esphome-components/tree/main/components/crow_alarm_panel`.

### `crow_alarm_panel.h`

| Topic | jallier `main` | This PR |
|-------|----------------|---------|
| `0x10` | `UNKNOWN` | `PANEL_STATUS` + field comments (B1 power, B2 ready) |
| `0x23` | `CURRENT_TEMP` | `PANEL_INFO` + Elite-S v908 notes |
| `0x54` | `CURRENT_TIME` | `LCD_CONTENT` |
| `PANEL_BUS_CONNECTED_TIMEOUT_US` | — | **Added** (2 s) on `CrowAlarmPanelStore` |
| Register API | zone, bypass, armed_state, outputs, ACP | **+** `panel_ready`, `mains_power`, `battery_state`, `panel_bus_connected`, `hardware_version`, `firmware_version` |
| State members | Minimal | **+** bus-connected publish flags, `last_*_state_` for publish-on-change text sensors |

### `crow_alarm_panel.cpp`

| Topic | jallier `main` | This PR |
|-------|----------------|---------|
| **`case PANEL_STATUS` (`0x10`)** | Missing | **New:** B2 → panel ready; B1 → mains (`& 0x02`), battery (`== 0x03`), ignore `0x01` |
| **`case PANEL_INFO` (`0x23`)** | Missing | **New:** tail validation `…23.03`, range checks, HW/FW text sensors |
| **`case LCD_CONTENT` (`0x54`)** | Wrong clock math in log | **Log only** — `ESP_LOGV` raw hex; wrong decode removed |
| Helpers | — | `panel_info_frame_valid_`, `panel_info_values_sane_`, `publish_text_sensor_if_changed_`, `update_panel_bus_connected_` |
| `setup()` | armed_state → disarmed; ACP disarmed | **+** boot OFF for ready/mains/battery/bus; `unknown` for HW/FW |
| `loop()` end | — | **+** `update_panel_bus_connected_()` each tick |
| All other opcodes | Same handlers | Unchanged (`0x11`–`0x20`, `0x50`, TX, zones, etc.) |

### `__init__.py`

| jallier `main` (70 lines) | This PR |
|---------------------------|---------|
| Pins, keypads, `on_message` only | Same **+** auto-register six diagnostics |
| No auto entities | Panel hardware, firmware, ready, mains, battery, alarm bus connected |

### `binary_sensor/__init__.py`

| jallier `main` | This PR |
|----------------|---------|
| `zone`, `bypass` only | **+** optional `panel_ready`, `mains_power`, `battery_state`, `panel_bus_connected` |
| — | **`include_bypass_sensor`** on zone → auto bypass child |

### `text_sensor/__init__.py`

| jallier `main` | This PR |
|----------------|---------|
| `armed_state` only | **+** optional `panel_hardware`, `panel_firmware` (avoid if using auto-register) |

### `README.md` (component)

| jallier `main` (~25 lines) | This PR |
|----------------------------|---------|
| Minimal `on_message` example | Full install, wiring, protocol table, entity docs |

### Root `README.md`

| jallier `main` | This PR |
|----------------|---------|
| **No root README** | Full project README |

### Unchanged vs jallier `main`

- `alarm_control_panel/`, `button/`, `switch/` modules
- ISR bus decode, keypress TX, arm/disarm flow
- Zone bitmask handling (`0x12`), armed state (`0x11`), outputs (`0x50`)
- Pre-existing bugs (zone→disarmed, `ZONE_STATE` `return`, incomplete bus idle) — **not introduced by this PR**

---

## Motivation

Users integrating Crow/Arrowhead panels via ESPHome need:

1. **Mains / AC fail** in Home Assistant (not only raw bus logging).
2. **Battery low** when the panel reports it — on tested hardware with AC fail (`B1 == 0x03`).
3. **Panel ready** from `0x10` byte B2 (`C1` = ready, `C0` = not ready), separate from zones.
4. **Panel hardware and firmware** from **`PANEL_INFO` (`0x23`)**.
5. **Bus connectivity** — clock alive within 2 s.

Decode rules validated on: normal AC, AC fail, battery low, mains loss/restore, ready/not-ready toggles.

---

## Auto-created entities

Appear as soon as you configure `crow_alarm_panel:` — boot OFF / disconnected / `unknown`, then update when the panel talks.

| Entity | Initial | Updates when |
|--------|---------|--------------|
| Panel hardware | `unknown` | First valid `0x23` |
| Panel firmware | `unknown` | First valid `0x23` |
| Panel ready | OFF | `0x10` byte2 (`C1`/`C0`) |
| Mains power | OFF | `0x10` byte1 (`0x02`/`0x03` = problem) |
| Battery state | OFF | `0x10` byte1 `0x03` |
| Alarm bus connected | OFF | Clock edges within 2 s |

---

## Repo layout note (important for merge)

| Repo | Component path | `external_components` YAML |
|------|----------------|----------------------------|
| **jallier** (target) | `components/crow_alarm_panel/` | `path: components` |
| **JoeyGE0** (this fork) | `crow_alarm_panel/` (repo root) | `path: .` |

When opening the PR against jallier, **place files under `components/crow_alarm_panel/`** (or merge jallier’s `44368e0` layout commit first, then apply diffs).

---

## Protocol findings (opcode `0x10` — `PANEL_STATUS`)

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
| `C2` / `C3` | Ready toggle during fault states; **ignore for fault typing** |

### Entity mapping (implemented)

```text
mains_power ON          when  (B1 & 0x02) != 0     # 0x02 or 0x03
mains_power OFF         when  B1 == 0x00
battery_state ON        when  B1 == 0x03
battery_state OFF       when  B1 == 0x02 or B1 == 0x00
ignore transition       when  B1 == 0x01
panel_ready ON          when  B2 == 0xC1  (C0/0x60 → OFF)
```

Boot defaults: `mains_power`, `battery_state`, and `panel_ready` publish **OFF** in `setup()`.

---

## Protocol findings (opcode `0x54` — `LCD_CONTENT`)

**Log only in this PR — no HA entity.**

jallier `main` logged `0x54` with wrong hour/minute math. This PR drops that and logs raw payload at verbose level.

Field-research notes (for logs / `on_message` — not published as entities):

| Raw bytes `[1][2]` | If BE16 minutes since midnight |
|--------------------|--------------------------------|
| `00.84` | 02:12 |
| `01.1D` | 04:45 |

---

## Protocol findings (opcode `0x23` — `PANEL_INFO`)

Observed stable payload: `00.03.8C.02.01.00.23.03` (8 bytes)

| Field | Bytes | Example |
|-------|-------|---------|
| Hardware | `[1][2]` BE16 | `0x038C` → **v908** |
| Firmware | `[3][4]` | `0x02.0x01` → **v2.1** |

Frames without tail `23.03` or with out-of-range values are ignored (guards corrupt post-OTA garbage).

---

## Migration checklist (jallier `main` → this PR)

1. Flash/update component.
2. Expect **six new entities** under your device — no YAML needed.
3. **Do not** duplicate auto entities in YAML.
4. Optionally add **`include_bypass_sensor: true`** on zone blocks to drop separate bypass YAML.
5. Re-test automations — mains/battery/ready now come from **`0x10`**.

---

## Field test evidence

| Scenario | B1 seen | Mains | Battery | Source |
|----------|---------|-------|---------|--------|
| AC on, normal | `0x00` only | OFF | OFF | Field logs |
| AC fail, batt OK | `0x02` only | ON | OFF | Field logs |
| AC fail + batt low | `0x03` only | ON | ON | Field logs |
| Unplug / restore | `0x01` → `0x03` → `0x02` → `0x00` | transitions | transitions | Field logs |
| Ready/not-ready | `0x00` + C0/C1 | OFF | OFF | Field logs |

**Live test (2026-06-17):** v908 / v2.1 stable; AC fail at `B1=0x02`; battery at `B1=0x03`; ready toggles on C0/C1.

### Known limitations

- **`0x10` is event-driven** — long idle gaps are normal.
- **Battery low not observed with mains ON** on v908/v2.1.
- **Panel-specific** — other Crow revisions may differ.
- **`B1=0x03`** may appear seconds after AC cut, before MEM logs low batt.

### Known code issues (AI review — not fixed in this PR)

| # | Severity | Issue | Where |
|---|----------|-------|-------|
| 1 | **Bug** | Short `ZONE_STATE` frame uses `return` from `loop()` not `break` | `crow_alarm_panel.cpp` ~L249 |
| 2 | **Bug** | Zone `triggered` forces HA **disarmed** while panel may still be armed | ~L265–273 |
| 3 | **Bug** | `last_clock_time_` / `BUS_IDLE_TIMEOUT_US` not used in `is_bus_idle_()` | `.h`, `is_bus_idle_()` |
| 4 | **Footgun** | `KEY_*` mix `KEYS[]` indices and raw bus bytes | `.h` ~L52–58 |
| 5 | **UX** | `battery_state` ON at `B1=0x03` may precede MEM “low batt” | `0x10` handler |
| 6 | **UX** | ACP publishes ARMING/DISARMING before panel confirms | `crow_alarm_control_panel.cpp` |
| 7 | **Stub** | `set_output()` empty — outputs read-only | upstream behaviour |

---

## Suggested PR title

```text
feat(crow_alarm_panel): decode 0x10/0x23 and auto diagnostic entities
```

---

## Suggested merge checklist

- [ ] Rebase onto jallier `main` (`components/crow_alarm_panel/` path).
- [ ] Copy/cherry-pick into `components/crow_alarm_panel/` if needed.
- [ ] Confirm compile on ESPHome ≥ 2025.11.
- [ ] Review **Breaking changes** with maintainer — auto entities are intentional.
- [ ] Optional follow-up: AI review bugs (`return`→`break`, bus idle, zone→disarmed).

---

## Test plan (for reviewer)

1. Flash with `external_components` pointing at PR branch.
2. With **only** `crow_alarm_panel:` (no extra diagnostic YAML), confirm **six auto entities** appear.
3. Confirm **Panel hardware** → `v908`, **Panel firmware** → `v2.1` within ~30 s.
4. Toggle not-ready → **Panel ready** OFF (`C0`); ready → ON (`C1`).
5. AC fail → **Mains power** ON at `B1=0x02`; restore → OFF.
6. AC out + panel low batt → **Battery state** ON at `B1=0x03`.
7. Disconnect clock wire → **Alarm bus connected** OFF within ~2 s.
8. Enable verbose logs — confirm `0x54` logged as raw hex (no wrong clock decode, no HA entity).
9. Zone with **`include_bypass_sensor: true`** — confirm bypass entity appears; separate `type: bypass` still works.

---

## Credits

Builds on [jesserockz/esphome-components](https://github.com/jesserockz/esphome-components) and [jallier/esphome-components](https://github.com/jallier/esphome-components). Field decode and validation by JoeyGE0 on an AAP Arrowhead installation.

---

## Commits to include (JoeyGE0 `main`, squashing optional)

Key commits (newest first):

- `ae73df2` — Remove redundant Panel year auto sensor
- `293125c` — Boot defaults for mains / battery / panel ready
- `8f3663c` — 0x10 decode; 0x54 log-only fix
- Earlier — panel_ready, mains_power, battery wiring, 0x23 publish, README

Full range: `0bcb1ae..ae73df2` on [JoeyGE0/esphome-components](https://github.com/JoeyGE0/esphome-components).
