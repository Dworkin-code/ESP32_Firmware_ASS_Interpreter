# LED Signalization Map — ESP32 Reader Firmware

**Analysis-only report.** No code was modified. All conclusions cite file paths and function names.

---

# 1) Hardware overview

| Item | Finding |
|------|--------|
| **LED count** | **2 logical LEDs** implemented as **2 pixels** on a single **WS2812B Neopixel strip** (4 pixels total, 2 used). No separate discrete LED GPIO is driven in code. |
| **GPIO pins** | **One data pin:** Neopixel strip data line is **GPIO 3** (hardcoded in `main/app.c`: `neopixel_Init(4, 3)`). **BLINK_GPIO** is defined in `main/Kconfig.projbuild` (default **2**) and stored in `sdkconfig` / `sdkconfig.old` as `CONFIG_BLINK_GPIO=2`, but **no C source references CONFIG_BLINK_GPIO or drives that pin** — the “blink” LED is unused. |
| **LED type** | **RGB** (WS2812B Neopixel). Pixel 0 and pixel 3 are used; pixels 1 and 2 are never set. |
| **PWM or digital** | **Digital** from the MCU’s perspective. The Neopixel driver (`managed_components/zorxx__neopixel/`) uses a dedicated task and timing to generate the WS2812B protocol; no ESP32 `ledc` or `gpio_set_level` is used for user-visible LEDs. |

**Conclusion:** The “two LEDs” are **pixel index 0** (NFC read/write activity) and **pixel index 3** (card presence) on the same Neopixel strip on GPIO 3.

---

# 2) LED control implementation

## 2.1 Neopixel context and pin

- **File:** `main/app.c`  
- **Init:** `Parametry.Svetelka = neopixel_Init(4, 3);` — 4 pixels, data on GPIO 3.  
- **Registration:** `setLight(&Parametry.Svetelka);` so the NFC_Reader component uses the same strip via a static pointer.

```c
// main/app.c (excerpt)
Parametry.Svetelka = neopixel_Init(4, 3);
setLight(&Parametry.Svetelka);
```

## 2.2 NFC_Reader component (pixel 0)

- **File:** `components/NFC_Reader/NFC_reader.c`  
- **Global:** `static tNeopixelContext* Light;` set by `setLight()` (see `NFC_reader.h` and `NFC_reader.c` line 1780–1782).  
- **Macros:** `Pozice` = 0, `SvetloR` = 255, `SvetloG` = 0, `SvetloB` = 0.  
- **Red:** `NP_RGB(SvetloR, SvetloG, SvetloB)` → (255, 0, 0).  
- **Green:** `NP_RGB(SvetloG, SvetloR, SvetloB)` → (0, 255, 0).  
- **Off:** `NP_RGB(0,0,0)` or `Svetlo.rgb = 0`.  
- **API:** `neopixel_SetPixel(*Light, &Svetlo, 1);` (single pixel update).

**Red — write start** (`NFC_WriteStructRange`):

```c
// NFC_reader.c, NFC_WriteStructRange(), ~line 201
tNeopixel Svetlo = { Pozice, NP_RGB(SvetloR, SvetloG,  SvetloB) };
neopixel_SetPixel(*Light,&Svetlo,1);
```

**Green — read start** (e.g. `NFC_LoadTRecipeInfoStructure`):

```c
// NFC_reader.c, NFC_LoadTRecipeInfoStructure(), ~line 553
tNeopixel Svetlo = { Pozice, NP_RGB(SvetloG, SvetloR,  SvetloB) };
neopixel_SetPixel(*Light,&Svetlo,1);
```

**Off** (example from same file, on error/success):

```c
Svetlo.rgb = NP_RGB(0, 0,  0);
neopixel_SetPixel(*Light,&Svetlo,1);
```

## 2.3 Main app — card presence (pixel 3)

- **File:** `main/app.c`  
- **Task:** `Is_Card_On_Reader()` (created in `app_main()`).  
- **Pixel index:** 3 (hardcoded in `tNeopixel Svetlo = {3, NP_RGB(0, 0, 0)};`).

**Card just placed (green):**

```c
// app.c, Is_Card_On_Reader(), ~line 1003
if (minulyStav) {
  Svetlo.rgb = NP_RGB(0, 150, 0);
  neopixel_SetPixel(Parametry->Svetelka, &Svetlo, 1);
}
```

**Card just removed (reddish):**

```c
// app.c, Is_Card_On_Reader(), ~line 1008
else {
  Svetlo.rgb = NP_RGB(100, 0, 30);
  neopixel_SetPixel(Parametry->Svetelka, &Svetlo, 1);
}
```

## 2.4 Neopixel driver

- **Files:** `managed_components/zorxx__neopixel/neopixel.c`, `neopixel.h`  
- **Functions:** `neopixel_Init()`, `neopixel_SetPixel()`, `neopixel_Deinit()`, `neopixel_GetRefreshRate()`.  
- **No blinking:** No timers or delays for LED patterns; only immediate color updates.

---

# 3) LED state mapping table

| LED (pixel) | Color | Pattern | Trigger function | System state | Meaning |
|-------------|--------|----------|-------------------|---------------|---------|
| **0** | **Red** (255,0,0) | Solid | `NFC_WriteStructRange()` (start) | NFC write to tag in progress | Writing recipe/card data to tag |
| **0** | **Green** (0,255,0) | Solid | `NFC_LoadTRecipeInfoStructure()`, `NFC_LoadTRecipeSteps()`, `NFC_LoadTRecipeStep()` (start) | NFC read in progress | Reading recipe/card data from tag |
| **0** | **Off** (0,0,0) | Solid | Same NFC_Reader functions on return | After read/write | Success or error (read/write finished or failed) |
| **3** | **Green** (0,150,0) | Solid | `Is_Card_On_Reader()` when `minulyStav` becomes true | Card just detected on reader | Tag/card present |
| **3** | **Reddish** (100,0,30) | Solid | `Is_Card_On_Reader()` when `minulyStav` becomes false | Card just removed | Tag/card removed |

**NFC pixel 0 “Off” is used in:**

- `NFC_WriteStructRange`: invalid range (return 4), index out of range (return 1), auth failure (return 3), card not present (return 2), success (return 0).  
- `NFC_LoadTRecipeInfoStructure`, `NFC_LoadTRecipeSteps`, `NFC_LoadTRecipeStep`: read failure, auth failure, card not present, or success — all turn pixel 0 off before return.

**No LED changes** are performed in:

- State machine (`State_Machine()` in `app.c`).  
- OPC_Klient (no LED or GPIO for status).  
- NFC_Handler (only calls NFC_Reader; no direct LED code).  
- Empty-tag handling or AAS flow (ReportProduct, ReserveAction, etc.) — they do not set LEDs.

---

# 4) State machine relation

- **State machine:** `State_Machine()` in `main/app.c` drives RAF states (e.g. `State_Mimo_Polozena`, `State_WaitUntilRemoved`, `State_Inicializace_ZiskaniAdres`, etc.) and calls `NFC_Handler_LoadData()` in `State_Mimo_Polozena`.  
- **LED coupling:** The state machine does **not** set any LED. LED changes happen only when:
  1. **Pixel 0:** Any call into `NFC_Reader` that performs read/write (`NFC_LoadTRecipeInfoStructure`, `NFC_LoadTRecipeSteps`, `NFC_LoadTRecipeStep`, `NFC_WriteStructRange`, and any code path that calls these, e.g. `NFC_Handler_LoadData` → `NFC_LoadTRecipeInfoStructure` / `NFC_LoadTRecipeSteps`).  
  2. **Pixel 3:** The `Is_Card_On_Reader` task, which only reacts to `NFC_isCardReady()` and updates pixel 3 when card presence **changes** (placed → green, removed → reddish).

**Transition logic (concise):**

- **Card removed** (State_Machine): `!Parametry->CardOnReader` → `RAF = State_Mimo_Polozena`; no LED write there. The “card removed” **LED** is set by `Is_Card_On_Reader` when it sees the transition to no card (pixel 3 reddish).  
- **Card placed:** State_Machine stays in or enters `State_Mimo_Polozena`, takes NFC semaphore, calls `NFC_Handler_LoadData()` → load functions run and set pixel 0 green during read, then off. `Is_Card_On_Reader` sets pixel 3 green when it first sees card present.  
- **AAS / OPC / empty-tag:** No direct LED logic; only the same NFC read/write paths above drive pixel 0.

So LED transitions are **not** explicitly tied to RAF state names; they are tied to **NFC operations** (read/write) and **card presence transitions** (pixel 3).

---

# 5) Conflicts / potential issues

| Issue | Description |
|-------|-------------|
| **Multiple writers** | Two execution contexts write to the same Neopixel context: (1) **State_Machine** (via NFC_Handler → NFC_Reader) updates **pixel 0**; (2) **Is_Card_On_Reader** task updates **pixel 3**. They use different pixel indices, so both values persist in the strip buffer until overwritten. There is **no mutex or lock** around `neopixel_SetPixel`. |
| **BLINK_GPIO unused** | `CONFIG_BLINK_GPIO` (default 2) is never used in the codebase. If hardware has a second LED on GPIO 2, it will never turn on. |
| **Pixel 3 not reset on boot** | Pixel 3 is set only when card presence **changes** in `Is_Card_On_Reader`. If the task starts with `CardOnReader == false`, the first transition to “card present” sets green; the first transition to “no card” sets reddish. There is no explicit “idle” or “off” for pixel 3 when no card has been seen yet (initial state is effectively undefined until first transition). |
| **Pixel 0 left on during long ops** | If an NFC read/write runs for a long time, pixel 0 stays green or red until the function returns and sets it off. No timeout or “still busy” pattern. |
| **No LED on error from State_Machine** | When `NFC_Handler_LoadData` returns non‑zero (e.g. invalid recipe → `State_Mimo_NastaveniNaPresunDoSkladu`) or on empty-tag path, the NFC_Reader layer has already turned pixel 0 off. There is no distinct “error” color; “off” means “done or error.” |

---

# 6) Recommendations (optional)

1. **BLINK_GPIO:** Either use `CONFIG_BLINK_GPIO` in code for a second status LED (e.g. OPC/network or general error) or remove the option from Kconfig to avoid confusion.  
2. **Pixel 3 initial state:** Consider setting pixel 3 to a defined state (e.g. off or a dim idle color) in `app_main()` after `neopixel_Init` and before starting `Is_Card_On_Reader`, so “no card yet” is explicit.  
3. **Documentation:** If the device is described as “two LEDs,” document that they are pixel 0 (NFC activity) and pixel 3 (card present/removed) on the Neopixel strip (GPIO 3), and that BLINK_GPIO is currently unused.

---

**References (files):**

- `main/app.c` — Neopixel init, `Is_Card_On_Reader`, `State_Machine`, `app_main`.  
- `main/Kconfig.projbuild` — BLINK_GPIO config.  
- `components/NFC_Reader/NFC_reader.c` — All pixel 0 updates; `setLight()`.  
- `components/NFC_Reader/NFC_reader.h` — `setLight()`, `Light` declaration.  
- `managed_components/zorxx__neopixel/neopixel.c`, `neopixel.h` — Driver API and behavior.
