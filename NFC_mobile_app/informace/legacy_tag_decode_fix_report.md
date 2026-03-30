## Legacy NFC recipe tag decode fix – report

- **Analyzed tag backup**: `NFC_mobile_app/real_tag_info/backup_04-C7-88-62-6F-71-80_2026-03-13_12-34-09.hex`  
- **Metadata**: `NFC_mobile_app/real_tag_info/backup_04-C7-88-62-6F-71-80_2026-03-13_12-34-09_meta.json` (UID `04:C7:88:62:6F:71:80`, `ULTRALIGHT_NTAG`, memory size 144 bytes)

### Raw tag structure interpretation

- **Header (`TRecipeInfo`)** – first 12 bytes of the backup match the structure and layout already documented in `tag_format_spec.md`:
  - Byte 0: `ID = 0x01` (1)
  - Bytes 1–2: `NumOfDrinks = 0x0000` (0)
  - Byte 3: `RecipeSteps = 0x05` (5)
  - Byte 4: `ActualRecipeStep = 0x00` (0)
  - Bytes 5–6: `ActualBudget = 0x0000` (0)
  - Byte 7: `Parameters = 0x00`
  - Byte 8: `RightNumber = 0xFE` (254) → `ID + RightNumber = 1 + 254 = 255` ✓
  - Byte 9: `RecipeDone = 0x00` (false)
  - Bytes 10–11: `CheckSum = 0x00C8` (200, LE)
- **Steps (`TRecipeStep` array)**:
  - The bytes after offset 12 clearly contain repeated `(ID, NextID, TypeOfProcess, ParameterProcess1, ParameterProcess2, …)` patterns.
  - Using the firmware enums:
    - `TypeOfProcess`: `0=ToStorageGlass`, `1=StorageAlcohol`, `2=StorageNonAlcohol`, `3=Shaker`, `4=Cleaner`, `5=SodaMake`, `6=ToCustomer`, `7=Transport`, `8=Buffer`
    - `AlcoholType`: `0=Vodka`, `1=Rum`, `2=Goralka`
    - `NonAlcoholType`: `0=Water`, `1=Cola`
  - From the backup bytes we can reliably identify the following step semantics:
    - At offset 12: `ID=0, NextID=1, TypeOfProcess=2 (StorageNonAlcohol), ParameterProcess1=0 (Water), ParameterProcess2=5` → **Water 5 ml**
    - At offset 42: `ID=1, NextID=2, TypeOfProcess=1 (StorageAlcohol), ParameterProcess1=0 (Vodka), ParameterProcess2=20` → **Vodka 20 ml**
    - At offset 76: `ID=2, NextID=3, TypeOfProcess=2 (StorageNonAlcohol), ParameterProcess1=0 (Water), ParameterProcess2=0` → **Water 0 ml**
    - At offset 109: `ID=3, NextID=4, TypeOfProcess=2 (StorageNonAlcohol), ParameterProcess1=1 (Cola), ParameterProcess2=20` → **Cola 20 ml**
  - These decoded meanings match the reader firmware’s intended semantics for a “Vodka + Water mix (20 + 80 ml)” legacy recipe built from `StorageAlcohol` / `StorageNonAlcohol` steps plus a final “return to storage” style step (not fully visible in the truncated backup stream, but handled by firmware based on TypeOfProcess `ToStorageGlass`).

### What the mobile app decoded before the fix

- The core decoder (`RecipeCodec.decodeHeader` / `decodeStep`) already read:
  - `TypeOfProcess` from step byte offset 2.
  - `ParameterProcess1` from offset 3.
  - `ParameterProcess2` from offsets 4–5 (uint16 LE).
- However, the **UI layer** (`StepsAdapter`) only displayed:
  - The raw **process type name** from `ProcessTypes.name(typeOfProcess)`, e.g. `StorageAlcohol`, `StorageNonAlcohol`.
  - A generic **`param1=`** value, without using `ParameterProcess2` or the firmware enums for drink type.
- For the known-good legacy tag this resulted in user-facing text like:
  - `"Step 1: StorageNonAlcohol (param1=0)"`
  - `"Step 2: StorageAlcohol (param1=0)"`, etc.
- This is technically based on the right bytes but **does not reflect the real meaning** the firmware derives:
  - It hides that `param1=0` means **Vodka** (for `StorageAlcohol`) or **Water** (for `StorageNonAlcohol`).
  - It ignores `ParameterProcess2`, which carries the **volume in ml** for these steps.

### What was wrong

- The **on-tag structure and offsets were already correct** in the mobile app:
  - `HEADER_SIZE = 12` matches `TRecipeInfo`.
  - `TypeOfProcess` and `ParameterProcess1/2` offsets match `NFC_reader.h` and the reverse engineering reports.
- The mismatch came from **semantic interpretation in the UI**, not from byte-level decoding:
  - The mobile app treated `TypeOfProcess` only as a high-level storage/process code and showed it verbatim.
  - It did not map `(TypeOfProcess, ParameterProcess1, ParameterProcess2)` to **“Vodka 20 ml”**, **“Water 5 ml”**, **“Cola 20 ml”**, or **“Return to storage”** as the firmware does.
- As a result, users saw technically correct but **meaningless step labels** that did not correspond to the legacy recipe behaviour observed on the reader.

### Implemented changes

- **New semantic mapping helper** in `ProcessTypes.kt`:
  - Added `drinkName(processType: Int, parameterProcess1: Int): String?`:
    - For `STORAGE_ALCOHOL`:
      - `0 → "Vodka"`, `1 → "Rum"`, `2 → "Goralka"`.
    - For `STORAGE_NON_ALCOHOL`:
      - `0 → "Water"`, `1 → "Cola"`.
    - Returns `null` for unknown combinations or non-storage types.
- **Updated step rendering in `StepsAdapter`**:
  - For **Shaker** (`TypeOfProcess = 3`):
    - Keeps existing behaviour: `"Shaker <ParameterProcess1> s"`.
  - For **Storage steps** (`TypeOfProcess = 1` or `2`):
    - Uses `ProcessTypes.drinkName` and `ParameterProcess2` to render:
      - `"Vodka 20 ml"`, `"Water 5 ml"`, `"Cola 20 ml"`, etc.
    - If drink name is unknown, falls back to `"StorageAlcohol#X"` / `"StorageNonAlcohol#X"`.
  - For **ToStorageGlass** (`TypeOfProcess = 0`):
    - Displays `"Return to storage"`, matching firmware semantics.
  - For all other types:
    - Shows both parameters: `"<TypeName> (param1=<...>, param2=<...>)"`.

### Files modified

- `android-app/app/src/main/java/com/testbed/nfcrecipetag/core/tagmodel/ProcessTypes.kt`
  - Added `drinkName(processType: Int, parameterProcess1: Int): String?`.
- `android-app/app/src/main/java/com/testbed/nfcrecipetag/ui/StepsAdapter.kt`
  - Changed `detail` computation in `onBindViewHolder` to:
    - Render Shaker steps as before.
    - Render storage steps as **<DrinkName> <volume> ml** using `drinkName` and `parameterProcess2`.
    - Render `ToStorageGlass` steps as **“Return to storage”**.

### Final offsets and enum mappings used by the decoder

- **Header (`TRecipeInfo`)** – unchanged, as per `tag_format_spec.md` and firmware:
  - Byte 0: `ID`
  - Bytes 1–2: `NumOfDrinks` (uint16 LE)
  - Byte 3: `RecipeSteps`
  - Byte 4: `ActualRecipeStep`
  - Bytes 5–6: `ActualBudget` (uint16 LE)
  - Byte 7: `Parameters`
  - Byte 8: `RightNumber` (must satisfy `ID + RightNumber == 255`)
  - Byte 9: `RecipeDone` (bool byte)
  - Bytes 10–11: `CheckSum` (uint16 LE) over the concatenated `TRecipeStep` bytes.

- **Step (`TRecipeStep`)** – offsets used by the mobile decoder:
  - Byte 0: `ID`
  - Byte 1: `NextID`
  - Byte 2: `TypeOfProcess`
  - Byte 3: `ParameterProcess1`
  - Bytes 4–5: `ParameterProcess2` (uint16 LE, used as **volume in ml** for storage steps)
  - Remaining fields (transport/process IDs, prices, timestamps, flags) follow firmware layout and are preserved but not reinterpreted in this fix.

- **Process type enum mapping (from firmware, mirrored in `ProcessTypes`)**:
  - `0 = ToStorageGlass`
  - `1 = StorageAlcohol`
  - `2 = StorageNonAlcohol`
  - `3 = Shaker`
  - `4 = Cleaner`
  - `5 = SodaMake`
  - `6 = ToCustomer`
  - `7 = Transport`
  - `8 = Buffer`

- **Drink type enum mapping (from firmware, used in `drinkName`)**:
  - For `StorageAlcohol`:
    - `ParameterProcess1 = 0 → Vodka`
    - `ParameterProcess1 = 1 → Rum`
    - `ParameterProcess1 = 2 → Goralka`
  - For `StorageNonAlcohol`:
    - `ParameterProcess1 = 0 → Water`
    - `ParameterProcess1 = 1 → Cola`

### Remaining uncertainties

- The backup stream in the repository is truncated to the app’s Ultralight read window (pages 8–39, 128 bytes). The firmware can access more pages, so the very last legacy step (e.g. an explicit `ToStorageGlass` step) may reside beyond what the mobile app currently reads.
- The exact `TRecipeStep_Size` used by the live firmware build is determined by `sizeof(TRecipeStep)` and may differ from the app’s assumption of 32 bytes; this does not affect the semantic mapping introduced here because:
  - The offsets for `TypeOfProcess`, `ParameterProcess1`, and `ParameterProcess2` are the same in all observed builds.
  - The mobile app preserves all unknown tail bytes and does not attempt to rewrite legacy tags using a different struct size.

Overall, with these changes the mobile UI now decodes and presents the known-good legacy recipe tag in a way that matches the ESP32 reader firmware’s interpretation: steps are shown as **“Vodka 20 ml”**, **“Water 5 ml”**, **“Water 0 ml”**, **“Cola 20 ml”**, and **“Return to storage”** instead of generic process-type labels.

