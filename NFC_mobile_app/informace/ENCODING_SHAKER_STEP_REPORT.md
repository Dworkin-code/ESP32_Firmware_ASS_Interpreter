# Encoding Report: NFC Tag Data for Shaker Step (Protrepani)

**Goal:** Determine exactly what the mobile NFC app must write so the reader firmware executes a **Shaker** step with a given duration (the same semantics as `case 6` in `GetRecipeStepByNumber`).

**Sources:** NFC_mobile_app folder (tag_format_spec.md, reverse engineering report, codec, real dumps), ESP32 NFC_Reader/NFC_Recipes/NFC_Handler firmware.

---

## 1. Relevant files found

| File | Role |
|------|------|
| `NFC_mobile_app/tag_format_spec.md` | Recipe stream layout, TRecipeInfo/TRecipeStep byte map, checksum, page mapping |
| `NFC_mobile_app/NFC_TAG_DATA_FORMAT_REVERSE_ENGINEERING_REPORT.md` | Firmware-based struct layout, NFC memory access, validation rules |
| `NFC_mobile_app/app_plan.md` | Data flow, codec responsibility |
| `NFC_mobile_app/android-app/.../core/codec/RecipeCodec.kt` | decodeHeader, decodeStep, encodeStep, encodeRecipe, checksum |
| `NFC_mobile_app/android-app/.../core/tagmodel/RecipeStep.kt` | RecipeStep data class (typeOfProcess, parameterProcess1, …) |
| `NFC_mobile_app/android-app/.../core/tagmodel/RecipeHeader.kt` | RecipeHeader, isValidIntegrity() |
| `NFC_mobile_app/real_tag_info/04-C7-88-62-6F-71-80_*.txt` | NTAG213 dump (pages 8+ = user data) |
| `ESP32_.../components/NFC_Reader/NFC_reader.h` | **Authoritative** TRecipeInfo / TRecipeStep structs, TRecipeInfo_Size, TRecipeStep_Size |
| `ESP32_.../components/NFC_Reader/NFC_reader.c` | NFC_LoadTRecipeInfoStructure, NFC_LoadTRecipeSteps, NFC_GetCheckSum |
| `ESP32_.../components/NFC_Recipes/NFC_recipes.h` | **ProcessTypes** enum (Shaker = 3), GetRecipeStepByNumber declaration |
| `ESP32_.../components/NFC_Recipes/NFC_recipes.c` | GetRecipeStepByNumber (case 6 → Shaker, ParameterProcess1 = aParam) |
| `ESP32_.../main/app.c` | Uses step->TypeOfProcess, step->ParameterProcess1/2 from tag (no GetRecipeStepByNumber for tag data) |

---

## 2. Decoding chain from raw tag data to “case 6” behaviour

**Critical finding:** The firmware does **not** read a “step number” (1–10) from the tag and then call `GetRecipeStepByNumber`. It reads **raw TRecipeStep** bytes and uses them **directly**.

- **Tag → bytes:** Ultralight: pages 8, 9, … concatenated into a stream. Classic: logical blocks from block 2, 16 bytes each.
- **Bytes 0..11:** Copied into `TRecipeInfo` (header). `RecipeSteps` is read from byte 3.
- **Bytes 12 .. 12 + RecipeSteps×TRecipeStep_Size - 1:** Copied into `sRecipeStep[]` (raw structs).
- **Execution:** `app.c` uses `step->TypeOfProcess`, `step->ParameterProcess1`, `step->ParameterProcess2` from this in-memory struct. There is no switch on “step number” from the tag.

So the “case 6” behaviour (Shaker with duration) is achieved when the **on-tag** step has:

- **TypeOfProcess** = value of enum **Shaker** in firmware.
- **ParameterProcess1** = duration in seconds (the `aParam` used in the debug message “Protrepani o dobe trvani %d s”).

`GetRecipeStepByNumber(6, aParam)` is only used in **hardcoded** recipes (`GetCardInfoByNumber`); it is **not** the path for tag-driven steps. The tag stores the **ProcessTypes** enum value, not the “recipe step number” 6.

---

## 3. Field(s) responsible for process type selection

- **Single field:** `TRecipeStep.TypeOfProcess` (uint8).
- **Byte offset in step:** **2** (first byte of step = offset 0: ID; 1: NextID; 2: TypeOfProcess).
- **Stream offset for step index `s`:** `12 + s * TRecipeStep_Size`; then byte at `+2` is TypeOfProcess.

**ProcessTypes enum** (NFC_recipes.h):

```c
ToStorageGlass = 0, StorageAlcohol = 1, StorageNonAlcohol = 2,
Shaker = 3, Cleaner = 4, SodaMake = 5, ToCustomer = 6, Transport = 7, Buffer = 8
```

So for Shaker the tag must contain **TypeOfProcess = 3**, not 6. The “6” in the source is the **recipe step number** in the comment list (1=Vodka … 6=Shaker); the **on-tag value** is the enum, i.e. **3**.

---

## 4. Field(s) responsible for aParam (shake duration)

- **Field:** `TRecipeStep.ParameterProcess1` (uint8 in firmware struct).
- **Byte offset in step:** **3**.
- **Semantics for Shaker:** Duration in **seconds** (as in the debug string “Protrepani o dobe trvani %d s”).

So for a 5-second shake, the tag must have **ParameterProcess1 = 5** at step byte offset 3. Type is **uint8** (0–255 seconds). ParameterProcess2 is not used for Shaker in the firmware.

---

## 5. Constraints for a valid recipe step

- **Header:** Bytes 0–11 must form a valid `TRecipeInfo`: `RecipeSteps` (byte 3) must be ≥ 1 for at least one step; `ID + RightNumber == 255` (bytes 0 and 8).
- **Step count:** `RecipeSteps` determines how many steps are read; the step that should be Shaker must be within `[0, RecipeSteps-1]`.
- **Step layout:** Each step is `TRecipeStep_Size` bytes (firmware uses `sizeof`; app uses STEP_SIZE = 32). Offsets 0, 1, 2, 3 in that step are ID, NextID, TypeOfProcess, ParameterProcess1.
- **Checksum:** `TRecipeInfo.CheckSum` (bytes 10–11, uint16 LE) must equal the firmware checksum over **all** TRecipeStep bytes only (see below).
- **Other step fields:** For the reader to accept the card and run the step, no other field is required to have a specific value for “Shaker” except TypeOfProcess and ParameterProcess1; the rest can be zero. For execution logic (e.g. transport, cells), other fields may matter in practice but are not required for triggering “Shaker with duration”.

---

## 6. Whether checksum / length / header also matter

- **Checksum:** Yes. `NFC_GetCheckSum` is computed over `TRecipeStep[]` only:  
  `sum of (step_byte[i] * ((i % 4) + 1))` as uint16. Stored in header bytes 10–11 (LE). If the app changes any step byte, it must recompute and write this checksum.
- **Length:** Yes. The firmware reads exactly `TRecipeInfo_Size + RecipeSteps * TRecipeStep_Size` bytes for the recipe. So `RecipeSteps` (header byte 3) must match the number of steps actually present.
- **Header:** Yes. `ID + RightNumber == 255` is checked; otherwise the card is rejected (e.g. return 5 in handler). So when encoding, set `RightNumber = 255 - ID`.

---

## 7. Most likely encoding needed for “Shaker”

| What | Value / encoding |
|------|-------------------|
| Process type (Shaker) | **TypeOfProcess = 3** (enum value in NFC_recipes.h) |
| Duration (seconds) | **ParameterProcess1 = duration** (uint8, 0–255) |
| Byte in step | Offset 2: `0x03`; offset 3: duration byte (e.g. `0x05` for 5 s) |
| Endianness | Single bytes; no endianness issue for these two fields. |

So for one step that means “Shaker, 5 seconds”:

- At step byte 2: `0x03`
- At step byte 3: `0x05`

All other step bytes can be zero (or preserved from previous decode); ID/NextID can be 0,0 or set for a single-step recipe (e.g. ID=0, NextID=0).

---

## 8. Concrete write example for duration value

**Desired:** Shaker, duration **5 seconds**, as the only step.

- **Header (12 bytes):** e.g. ID=1, NumOfDrinks=0, RecipeSteps=1, ActualRecipeStep=0, ActualBudget=0, Parameters=0, RightNumber=254, RecipeDone=0, CheckSum=recomputed after steps.
- **Step 0 (32 bytes in app; firmware uses sizeof(TRecipeStep)):**  
  - Byte 0: ID = 0  
  - Byte 1: NextID = 0  
  - Byte 2: TypeOfProcess = **3** (Shaker)  
  - Byte 3: ParameterProcess1 = **5** (duration 5 s)  
  - Bytes 4–31: 0 (or as in tag_format_spec: ParameterProcess2, transport/process IDs, timestamps, flags).  

**Affected pages (Ultralight):** Stream bytes 0–43 (header + one step).  
- Bytes 0–15: pages 8–11  
- Bytes 16–31: pages 12–15  
- Bytes 32–43: first 12 bytes of pages 16–17  

So pages 8–17 (inclusive) contain the header and the full first step. After encoding, the app must recompute the checksum over the 32 step bytes and write it into header bytes 10–11, then write the full stream to the tag starting at page 8.

**Example step bytes (hex) for Shaker 5 s (first 8 bytes):**  
`00 00 03 05 00 00 00 00 ...` (rest zeros or preserved).

---

## 9. Mobile app implementation guidance

- **Model:** Use existing `RecipeStep`: set `typeOfProcess = 3`, `parameterProcess1 = durationSeconds` (0–255). Other fields can be 0 or kept from existing decode.
- **Encoding:** Use existing `encodeStep()`; it already writes byte 2 from `typeOfProcess` and byte 3 from `parameterProcess1`. So creating a step with `typeOfProcess = 3` and `parameterProcess1 = 5` and passing it to `encodeRecipe()` is sufficient for those two bytes.
- **Header:** Ensure `recipeSteps` matches the list length, `rightNumber = 255 - id` (done in `encodeRecipe()`), and `checksum` is recomputed from step bytes (also done in `encodeRecipe()`).
- **Validation:** Before write: `header.recipeSteps == steps.size`, `header.id + header.rightNumber == 255` (or use `header.isValidIntegrity()`), and after encoding run `computeStepChecksum(stepBytes)` and set header.checksum (handled by `encodeRecipe()`).
- **Preserve tail:** If the tag has bytes after the last step, pass `unknownTail` into `encodeRecipe()` so they are not overwritten.

---

## 10. Confidence level

| Item | Level | Note |
|------|--------|------|
| Tag holds TRecipeStep struct, not “step number” 1–10 | **Confirmed from code** | NFC_LoadTRecipeSteps copies raw bytes into sRecipeStep; app.c uses step->TypeOfProcess. |
| Process type is enum value; Shaker = 3 | **Confirmed from code** | NFC_recipes.h enum and GetRecipeStepByNumber(6,…) sets TypeOfProcess = Shaker. |
| ParameterProcess1 = duration for Shaker | **Confirmed from code** | GetRecipeStepByNumber case 6 sets ParameterProcess1 = aParam; debug says “dobe trvani %d s”. |
| Byte offsets 2 and 3 in step | **Confirmed from code** | NFC_reader.h packed struct: TypeOfProcess at 2, ParameterProcess1 at 3. |
| Checksum and ID+RightNumber required | **Confirmed from code** | NFC_GetCheckSum, NFC_Handler checks ID+RightNumber. |
| STEP_SIZE = 32 vs firmware sizeof(TRecipeStep) | **Strongly inferred** | Spec and app use 32; firmware uses sizeof; if firmware is 31, last byte might be padding; recommend one verification read of a known-good tag. |

---

# How to implement this in the mobile app

## Exact field names to create/use

- **Process type (Shaker):** `RecipeStep.typeOfProcess` = **3**.
- **Duration (seconds):** `RecipeStep.parameterProcess1` = **duration** (Int 0–255).

No separate “step number” or “case 6” field; the firmware only sees `TypeOfProcess` and `ParameterProcess1`.

## Encoding rule for the Shaker step

1. Create a `RecipeStep` with:
   - `typeOfProcess = 3` (Shaker),
   - `parameterProcess1 = durationInSeconds`,
   - other fields as needed: e.g. `id = stepIndex`, `nextId = stepIndex` (or next step index), remaining numeric fields 0, booleans false.
2. Pass the list of steps (e.g. `listOf(thatStep)`) and the header (with `recipeSteps = 1`) to `encodeRecipe(header, steps, unknownTail)`.
3. Do **not** set `typeOfProcess = 6`; 6 is ToCustomer in the enum. Use **3** for Shaker.

## Encoding rule for duration

- Set `parameterProcess1` to the desired duration in seconds (0–255). It is written as a single byte at step byte offset 3 by `encodeStep()`.

## Validation checklist before write

1. `header.recipeSteps == steps.size`.
2. `header.id + header.rightNumber == 255` (or use `header.isValidIntegrity()`).
3. For each step that should be Shaker: `step.typeOfProcess == 3`, `step.parameterProcess1 in 0..255`.
4. After building the byte stream, checksum is recomputed in `encodeRecipe()`; no extra step needed if you use that function.
5. For Ultralight: write only user pages (e.g. 8–39 for NTAG213); do not write lock/config pages.

## Test procedure using a real tag and the reader firmware

1. **Create recipe:** One step: Shaker, 5 seconds. Header: e.g. ID=1, RecipeSteps=1, ActualRecipeStep=0; RightNumber and CheckSum set by `encodeRecipe()`.
2. **Write:** Use the app’s Write to tag flow (encodeRecipe → write Ultralight pages starting at page 8).
3. **Verify on reader:** Place tag on ESP32 reader; confirm in logs the message equivalent to “Generuji krok receptu Protrepani o dobe trvani 5 s” and that the executed step has TypeOfProcess = Shaker and ParameterProcess1 = 5.
4. **Optional:** Read the tag again in the app and decode; decoded step should show `typeOfProcess == 3`, `parameterProcess1 == 5`, and checksum/integrity OK.

---

**Summary:** Write **TypeOfProcess = 3** (Shaker) and **ParameterProcess1 = duration in seconds** in the step’s bytes 2 and 3, with a valid header (ID+RightNumber=255, RecipeSteps matching step count) and correct checksum over the step bytes. The mobile app already has the right data model and codec; it only needs to use the value **3** for Shaker and put the duration in **parameterProcess1**.
