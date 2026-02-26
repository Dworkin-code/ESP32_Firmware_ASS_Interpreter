# NFC recipe sufficiency for 6-cell Testbed 4.0 (analysis only)

**Scope:** Decide whether the current NFC recipe data model (on-tag + ESP32 reader firmware) supports a 6-cell deployment (ice crusher, shaker, alcohol storage, liquid storage, glass storage, sodamaker) using **only reader-side changes (profiles)**, or whether the NFC recipe format must change.

**Main question:** Can each reader decide, for each scanned tag, whether the current station should execute the next step, or the tag should wait / request transport — **without changing the tag format?**

---

# 1) Current NFC recipe model (facts)

## 1.1 TRecipeInfo layout

**Definition:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h` (lines 18–29).

Struct is `__attribute__((packed))`. Size: `TRecipeInfo_Size = sizeof(TRecipeInfo)` (used in NFC_reader.h:65). From field types: 1+2+1+1+2+1+1+1+2 = **12 bytes** (bool as 1 byte).

| Field | Type/Size | Offset | Meaning | Used where (file:function:line) |
|-------|-----------|--------|---------|----------------------------------|
| ID | uint8_t / 1 | 0 | Card/recipe identifier | NFC_recipes.c:ChangeID; app.c empty check (info->ID) |
| NumOfDrinks | uint16_t / 2 | 1 | Number of drinks (unknown semantics in logic) | app.c:NFC_IsRecipeEmpty (zeros check) |
| RecipeSteps | uint8_t / 1 | 3 | Number of recipe steps | app.c (MAX_RECIPE_STEPS, current step bounds); NFC_Handler, NFC_Reader (array size, load/write ranges) |
| ActualRecipeStep | uint8_t / 1 | 4 | Index of current step (0-based) | app.c state machine (curStep, advance, done); NFC_Recipes (AskForValidOffer, ReserveAll, etc.) |
| ActualBudget | uint16_t / 2 | 5 | Budget value (only recipe 1 sets 200 in firmware) | GetCardInfoByNumber; app.c (tempInfo.ActualBudget -= PriceForProcess/Transport) |
| Parameters | uint8_t / 1 | 7 | Unknown | app.c:NFC_IsRecipeEmpty (zeros check) |
| RightNumber | uint8_t / 1 | 8 | Redundancy check (255 - ID in ChangeID) | app.c:NFC_IsRecipeEmpty (zeros check) |
| RecipeDone | bool / 1 | 9 | Recipe completed flag | app.c (end-of-recipe, AAS failure path); NFC_IsRecipeEmpty (done state) |
| CheckSum | uint16_t / 2 | 10 | Checksum over step array only (see NFC_GetCheckSum) | NFC_reader.c:NFC_GetCheckSum, WriteStructRange; NFC_handler.c:IsSameData |

**On-tag layout:** First 12 bytes of tag payload = TRecipeInfo. Followed by `RecipeSteps` × `TRecipeStep_Size` bytes for steps.

---

## 1.2 TRecipeStep layout

**Definition:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h` (lines 30–50).

Struct is `__attribute__((packed))`. Size: `TRecipeStep_Size = sizeof(TRecipeStep)` (NFC_reader.h:66). UA_DateTime is `int64_t` (8 bytes) per `components/open62541lib/include/open62541.h:11803`.

| Field | Type/Size | Offset | Meaning | Used where (file:function:line) |
|-------|-----------|--------|---------|----------------------------------|
| ID | uint8_t / 1 | 0 | Step index | NFC_recipes.c (GetCardInfoByNumber, AddRecipe, GetMinule); app.c (NextID flow) |
| NextID | uint8_t / 1 | 1 | Next step index (chain) | app.c (State_Vyroba_Objeveni: tempInfo.ActualRecipeStep = tempStep.NextID) |
| TypeOfProcess | uint8_t / 1 | 2 | Process type (enum ProcessTypes) | app.c (state machine, GetCellInfoFromLDS, GetWinningCell, AAS msg5); NFC_recipes.c (GetRecipeStepByNumber, ChooseCell, ExistType); OPC_klient.c:Inquire |
| ParameterProcess1 | uint8_t / 1 | 3 | Param (alcohol/non-alcohol type, duration, etc.) | GetRecipeStepByNumber; app.c (msg5 for AAS); GetWinningCell |
| ParameterProcess2 | uint16_t / 2 | 4 | Param (volume ml, etc.) | GetRecipeStepByNumber; app.c (msg5); GetWinningCell |
| PriceForTransport | uint8_t / 1 | 6 | Cost for transport | app.c (ActualBudget -= tempStep.PriceForTransport) |
| TransportCellID | uint8_t / 1 | 7 | Cell ID that performs transport to next station | app.c (state branches, AddRecipe); NFC_recipes.c (AskForValidReservation, AskForValidOffer, ReserveAll, GetWinningCell) |
| TransportCellReservationID | uint16_t / 2 | 8 | Reservation ID for transport cell | NFC_recipes.c (reservation match); app.c (state conditions) |
| PriceForProcess | uint8_t / 1 | 10 | Cost for process | app.c (ActualBudget -= tempStep.PriceForProcess) |
| ProcessCellID | uint8_t / 1 | 11 | Cell ID that performs this process step | app.c (State_Vyroba_Objeveni: “wrong cell” → AddRecipe transport; DoReservation, IsDoneReservation; state conditions); NFC_recipes.c (AskForValidReservation, AskForValidOffer, ReserveAll) |
| ProcessCellReservationID | uint16_t / 2 | 12 | Reservation ID for process cell | app.c (state conditions); NFC_recipes.c (reservation APIs) |
| TimeOfProcess | UA_DateTime / 8 | 14 | Process start/completion time | app.c (state conditions: TimeOfProcess > 0 && MyCellInfo.IDofCell == ProcessCellID) |
| TimeOfTransport | UA_DateTime / 8 | 22 | Transport time | app.c (state conditions: TimeOfTransport, transport flow) |
| NeedForTransport | bool :1 | 30 | Step needs transport after process | app.c (State_Poptavka_Transporty, State_Transport branches) |
| IsTransport | bool :1 | 30 | Transport done for this step | app.c (state branches, write-back) |
| IsProcess | bool :1 | 30 | Process done for this step | app.c (state branches, write-back) |
| IsStepDone | bool :1 | 30 | Step fully done | app.c (AAS path, State_Vyroba_SpravneProvedeni); NFC_Handler write-back |

**Note:** Bitfields (NeedForTransport, IsTransport, IsProcess, IsStepDone) share one or more bytes at end of struct; exact byte layout is implementation-defined. Total size = `sizeof(TRecipeStep)` (used everywhere for index arithmetic).

---

## 1.3 Enums and constants

**ProcessTypes** — `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h` (lines 28–39):

| Value | Name |
|-------|------|
| 0 | ToStorageGlass |
| 1 | StorageAlcohol |
| 2 | StorageNonAlcohol |
| 3 | Shaker |
| 4 | Cleaner |
| 5 | SodaMake |
| 6 | ToCustomer |
| 7 | Transport |
| 8 | Buffer |

**AlcoholType** (NFC_recipes.h:41–45): Vodka=0, Rum=1, Goralka=2. Used in ParameterProcess1 for StorageAlcohol.

**NonAlcoholType** (NFC_recipes.h:48–51): Water=0, Cola=1. Used in ParameterProcess1 for StorageNonAlcohol.

**Constants:**
- `MAX_RECIPE_STEPS` = 64 — `main/app.c:75`. Tags with `RecipeSteps == 0` or `RecipeSteps > MAX_RECIPE_STEPS` are treated as empty.
- `TRecipeInfo_Size`, `TRecipeStep_Size` — `NFC_reader.h:65–66` (`sizeof(...)`).
- **StavovyAutomat** (state machine) — `NFC_recipes.h:59–78` (State_Mimo_Polozena, State_Inicializace_ZiskaniAdres, State_Poptavka_Vyroba, State_Poptavka_Transporty, State_Rezervace, State_Transport, State_Vyroba_Objeveni, etc.).

**GetRecipeStepByNumber** step-type numbers (1–10) map to ProcessTypes / parameters — `NFC_recipes.c:48–126`: 1=Vodka, 2=Rum, 3=Goralka, 4=Water, 5=Cola, 6=Shaker, 7=Cleaner, 8=ToStorageGlass, 9=ToCustomer, 10=Transport. Built-in steps are created with **ProcessCellID = 0** and **TransportCellID = 0**; they are filled at runtime when cells are chosen (GetWinningCell, AddRecipe, etc.).

---

# 2) How the firmware decides what to do (current behavior)

## State machine flow relevant to recipe execution

- **Current step:** `ActualRecipeStep` in `sRecipeInfo` (working copy). Read/written in `main/app.c` and `NFC_Handler`; advanced when a step is completed (e.g. app.c:410, AAS path 409–411).
- **Number of steps:** `RecipeSteps` from TRecipeInfo. Used to bound `ActualRecipeStep`, size step array, and compute tag layout (NFC_reader.c:277, 707, etc.).
- **Done / invalid:** Recipe is treated done when `ActualRecipeStep >= RecipeSteps` or `RecipeDone == true` (app.c:325–331, 427–429, 432–435). Empty/invalid tag: `NFC_IsRecipeEmpty` (app.c:81–133): RecipeSteps 0 or > MAX_RECIPE_STEPS, ActualRecipeStep out of bounds and not RecipeDone, or first step and info all zeros.

## How “current step” is used

- State machine branches on `sRecipeStep[ActualRecipeStep]` fields: `TypeOfProcess`, `ProcessCellID`, `TransportCellID`, `IsProcess`, `IsTransport`, `TimeOfProcess`, `TimeOfTransport`, `NeedForTransport`, `ProcessCellReservationID`, `TransportCellReservationID` (app.c:439–478, 516–531, 545–642, 701–734, 780–808, 863–870).
- **ProcessCellID** is used to decide “are we the right cell?” (e.g. app.c:453, 460, 808, 863, 870): e.g. `MyCellInfo.IDofCell == step.ProcessCellID` for process, or `!= ProcessCellID` for “need transport from other cell”.
- **TransportCellID** is used for transport reservation and “who does transport” (app.c:642; NFC_recipes.c:418–426, 494–502, 579–587, 627–631).
- **TypeOfProcess** drives which cells are considered: `GetCellInfoFromLDS(step.TypeOfProcess, ...)` (app.c:523); then `GetWinningCell(..., step.TypeOfProcess, ParameterProcess1, ParameterProcess2, ...)` (app.c:553, 620, 624). So TypeOfProcess is the primary key for “what kind of station,” and ProcessCellID/TransportCellID are the resolved “which concrete cell” (filled at runtime in current design).

## Mapping from TypeOfProcess to station

- Not stored on tag: cell list and “which cell supports which type” come from **GetCellInfoFromLDS** (NFC_recipes.c:222–236), which is currently **hardcoded** (array `Vsechny[]` with 3 cells and process type arrays). So “TypeOfProcess → cell” is defined in firmware/LDS, not on tag.
- AAS path (app.c:352–356): message to PLC is `sr_id/0/TypeOfProcess/ParameterProcess1/ParameterProcess2`; no ProcessCellID in the message. The reader’s `MyCellInfo` is fixed per device; the PLC/OPC side decides support.

## Validation rules (empty / invalid / done)

- **Empty:** `NFC_IsRecipeEmpty`: info NULL; RecipeSteps 0 or > 64; steps NULL with stepCount > 0; ActualRecipeStep >= stepCount and not RecipeDone; or first step and info all zeros (app.c:81–133).
- **Invalid/broken:** `ActualRecipeStep >= RecipeSteps` without RecipeDone → State_Mimo_NastaveniNaPresunDoSkladu (overwrite with “return to storage” recipe) (app.c:425–429).
- **Done:** RecipeDone set and/or ActualRecipeStep >= RecipeSteps; then State_KonecReceptu or State_Mimo_Polozena (app.c:330–341, 432–435).

---

# 3) Can we decide “this step belongs to this cell” with current data?

## 3.1 Strategy A: Use ProcessCellID / explicit target

- **Idea:** Each reader has a fixed `MyCellInfo.IDofCell` (profile). For the current step, if `step.ProcessCellID == MyCellInfo.IDofCell` → this reader executes the process; if `step.TransportCellID == MyCellInfo.IDofCell` → this reader does transport; otherwise → wait or request transport.
- **Feasibility:** Yes, with current fields. The firmware already branches on `ProcessCellID` and `TransportCellID` (app.c:453, 460, 808, 863, 870; NFC_recipes.c pass reservation by ProcessCellID/TransportCellID).
- **Rules:**  
  - Execute process here iff `step.ProcessCellID == MyCellID` (and step not yet done).  
  - Execute transport here iff `step.TransportCellID == MyCellID` (and transport needed).  
  - If `ProcessCellID == 0` and `TransportCellID == 0`: step has no cell assigned yet → need a fallback (e.g. Strategy B or “first reader assigns”).
- **Edge cases:**  
  - **Tags written by current firmware:** Built-in recipes and GetRecipeStepByNumber produce steps with ProcessCellID/TransportCellID = 0. They get filled only when the legacy path runs GetCellInfoFromLDS + GetWinningCell. So for “reader-only” 6-cell without that path, Strategy A alone fails for such tags unless something else pre-fills cell IDs (e.g. central authoring or first-contact resolution).  
  - **Pre-authored tags:** If recipes are written (e.g. by a tool or another system) with ProcessCellID/TransportCellID set to the 6-cell IDs, then Strategy A works: each reader matches on its ID.  
  - **Multiple cells same type:** ProcessCellID is a single byte → up to 256 cell IDs; enough for 6 cells. Uniqueness is by ID, not by type.

**Conclusion (A):** Strategy A is **possible** with current tag format **if** every step has ProcessCellID/TransportCellID set when the tag is written. It is **not** sufficient for tags that only have TypeOfProcess (and 0 in ProcessCellID/TransportCellID) unless we add a fallback (e.g. TypeOfProcess → cell mapping in reader profile).

---

## 3.2 Strategy B: Map TypeOfProcess → cell

- **Idea:** Each reader profile defines “I am cell type X.” For current step, if `TypeOfProcess` maps to this reader’s type → execute here; otherwise → wait / transport.
- **Feasibility:** Partially. The tag carries `TypeOfProcess` (byte). We can define a fixed mapping “TypeOfProcess value → cell role” in the reader profile.
- **Mapping table (6-cell Testbed 4.0):**

| Cell | ProcessTypes enum value | Note |
|------|--------------------------|------|
| Glass storage | 0 = ToStorageGlass | Direct |
| Alcohol storage | 1 = StorageAlcohol | Direct |
| Liquid storage | 2 = StorageNonAlcohol | Direct |
| Shaker | 3 = Shaker | Direct |
| (Cleaning station) | 4 = Cleaner | One cell |
| Sodamaker | 5 = SodaMake | Direct |
| (Delivery) | 6 = ToCustomer | Optional / special handling |
| (Transport) | 7 = Transport | Not a physical “station” in same sense |
| (Buffer) | 8 = Buffer | Optional |

- **Gap:** **Ice crusher** has **no** ProcessTypes value. Enum has: ToStorageGlass, StorageAlcohol, StorageNonAlcohol, Shaker, Cleaner, SodaMake, ToCustomer, Transport, Buffer. So we cannot represent “this step is for the ice crusher cell” in TypeOfProcess without reusing another value (ambiguous) or adding a new one.
- **Ambiguities:**  
  - If we reuse e.g. Cleaner for “ice crusher,” we cannot distinguish “cleaning” vs “ice crush” in TypeOfProcess. ParameterProcess1/2 could be used by convention (e.g. one value = ice crush), but that is convention only and not in the current enum.  
  - Multiple physical cells of the same type (e.g. two shakers): TypeOfProcess alone does not say *which* shaker; we’d need ProcessCellID or similar to distinguish. So Strategy B is unique only when “one cell per type” or when combined with Strategy A for disambiguation.
- **Default/fallback:** If TypeOfProcess is unknown or unmapped, reader should not execute (wait / reject / request transport). For ProcessTypes already in enum, mapping is clear except for ice crusher.

**Conclusion (B):** Strategy B is **possible** for 5 of 6 cells (glass, alcohol, liquid, shaker, sodamaker) with current **tag format**. **Ice crusher** cannot be represented without either (1) adding a new ProcessTypes value (e.g. IceCrusher = 9), or (2) overloading an existing value (e.g. Cleaner or ParameterProcess1) by convention — (1) is a **semantic/schema extension** (new value in same byte), not a struct layout change; (2) is ambiguous and fragile.

---

## 3.3 Result: Sufficient or not?

- **Verdict:** The **current tag layout (TRecipeInfo + TRecipeStep)** is **sufficient** to decide “this step belongs to this cell” **for 6-cell Testbed 4.0**, provided we accept **one** of the following:
  1. **Strategy A (explicit IDs):** Tags are authored (or runtime-filled) so that each step has ProcessCellID/TransportCellID set. Then each reader uses only `ProcessCellID`/`TransportCellID` vs `MyCellInfo.IDofCell`. **No change to NFC struct layout or to the tag format.**
  2. **Strategy B (type → cell):** Reader profile maps TypeOfProcess to “my cell.” This works for glass, alcohol, liquid, shaker, sodamaker, and cleaner **as defined today**. **Ice crusher** is not representable as a distinct process type without adding a new enum value (e.g. IceCrusher). Adding an enum value does **not** require changing the tag **format** (still one byte for TypeOfProcess); it only extends the allowed values and requires firmware/authoring to use it.

- **Recommendation:**  
  - Prefer **Strategy A** if tags can be written with cell IDs (central authoring or first-reader resolution). Then no tag format change and no new enum value.  
  - If tags must work with only TypeOfProcess (no cell IDs on tag), use **Strategy B** plus **one new ProcessTypes value for IceCrusher** (firmware + recipe authoring); tag format (struct layout) can stay as is.  
  - **Hybrid:** Use ProcessCellID when non-zero (“this step is for cell 3”); when zero, fall back to TypeOfProcess → cell mapping in reader profile. That covers both pre-authored and “type-only” tags.

- **What we do *not* need for sufficiency:**  
  - No new fields in TRecipeInfo or TRecipeStep for “target cell” — ProcessCellID/TransportCellID and TypeOfProcess already provide that.  
  - No schema version or CRC in the tag for *deciding* “is this step for this cell?” (CheckSum already exists over steps; optional schemaVersion could help future evolution but is not required for this decision.)

---

# 4) If insufficient: minimal required changes to tag format

**Conclusion from Section 3:** The current format is **sufficient** for the stated goal (decide per step whether this cell executes or not) using reader profiles and either explicit ProcessCellID/TransportCellID or TypeOfProcess mapping (with one new enum value for ice crusher if needed).

**If** a future requirement were to make the format “insufficient” (e.g. mandatory schema version, or stronger integrity), minimal extensions could be:

- **Schema version (optional):** Add e.g. `uint8_t schemaVersion` in TRecipeInfo (e.g. at offset 0, shifting ID and others, or reserve one of the existing bytes). Allows readers to reject unknown versions. **Backward compatibility:** readers that don’t know schemaVersion can treat 0 or “missing” as legacy.
- **CRC/checksum:** TRecipeInfo already has CheckSum (over steps). If a separate CRC over the whole payload were required, add e.g. `uint16_t Crc16` at end of TRecipeInfo (would increase header size by 2 bytes). **Backward compatibility:** if Crc16 == 0, skip verification.
- **Ice crusher:** No tag layout change. Add `IceCrusher` (e.g. value 9) to ProcessTypes in firmware and recipe authoring; TypeOfProcess remains 1 byte.

No change to tag format is **required** for the 6-cell “is this step for this cell?” decision.

---

# 5) Recommended next steps

1. **Reader profiles (no tag change)**  
   - Store per reader: `MyCellInfo.IDofCell` (1–6 or your ID range) and optionally “my process types” (e.g. [Shaker] or [StorageAlcohol]).  
   - Implement decision:  
     - If `step.ProcessCellID != 0` and `step.ProcessCellID == MyCellID` → execute process (or transport if TransportCellID matches).  
     - Else if `step.ProcessCellID == 0` and `step.TypeOfProcess` is in “my types” → execute (Strategy B fallback).  
     - Else → do not execute (wait / request transport / hand off).

2. **Recipe authoring**  
   - For Strategy A: ensure written tags have ProcessCellID/TransportCellID set for each step (tool or backend that knows the 6 cells).  
   - For Strategy B + ice crusher: add ProcessTypes value `IceCrusher` (e.g. 9) in firmware and use it in recipes for ice-crusher steps.

3. **Testing on real tags**  
   - Test with tags that have ProcessCellID/TransportCellID = 0: verify fallback (TypeOfProcess → cell) behaves as intended.  
   - Test with tags that have ProcessCellID/TransportCellID set: verify each of the 6 readers only acts when its ID matches.  
   - Test edge cases: ActualRecipeStep out of bounds, RecipeDone set, RecipeSteps 0 or > 64, and empty (all zeros) first step; confirm readers do not execute and treat as empty/invalid/done as in app.c.

4. **Documentation**  
   - Document the chosen strategy (A, B, or hybrid) and the reader profile format (cell ID, optional type list).  
   - Document ProcessTypes (including any new IceCrusher) and how ParameterProcess1/2 are used per type (for AAS/PLC and for authoring).

---

**References (definitions and key usage):**

- TRecipeInfo / TRecipeStep: `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
- ProcessTypes, CellInfo, GetRecipeStepByNumber, GetCardInfoByNumber: `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`, `NFC_recipes.c`
- State machine, empty check, AAS path: `ESP32_Firmware_ASS_Interpreter/main/app.c`
- Load/write/checksum: `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`, `NFC_Handler/NFC_handler.c`
