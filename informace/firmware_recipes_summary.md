# Firmware recipes export – summary

## How many recipes were found

- **Full card recipes (selectable):** 4  
  - **Recipe 0:** Vodka s vodou (20+80 ml) – 5 steps  
  - **Recipe 1:** Rum s colou (40+60 ml) – 6 steps (ActualBudget=200)  
  - **Recipe 2:** Vodka s vodou (20+80 ml) – 5 steps (same as recipe 0)  
  - **Recipe 3:** Návrat do skladu – 1 step (return to storage)  

- **Default/empty:** 1 variant  
  - **Default case:** `GetCardInfoByNumber` for any other index returns an empty card (0 steps). Exported as recipe index 255 “Empty/Default” for completeness.

- **Step types (building blocks):** 10  
  - `GetRecipeStepByNumber(aNumOfRecipe, aParam)` supports step types 1–10 (Vodka, Rum, Goralka, Water, Cola, Shaker, Cleaner, ToStorageGlass, ToCustomer, Transport). These are used to build the steps of the full recipes above and for dynamic steps (e.g. `AddRecipe` with type 10 for transport).

---

## How recipes are selected

- **Function:** `TCardInfo GetCardInfoByNumber(uint8_t aNumOfRecipe)`  
- **Location:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c` (and declaration in `NFC_recipes.h`).

- **Parameter:** `aNumOfRecipe`  
  - **0** → Vodka s vodou (5 steps)  
  - **1** → Rum s colou (6 steps)  
  - **2** → Vodka s vodou (5 steps, same as 0)  
  - **3** → Návrat do skladu (1 step)  
  - **Other** → Empty card (default).

- **Where used in app:**  
  - **Recipe 3:** `app.c` ~line 493, state `State_Mimo_NastaveniNaPresunDoSkladu` – loads “return to storage” and writes to tag.  
  - **Recipes 0–3 (cycling):** `app.c` ~line 952, state `State_NovyRecept` – `GetCardInfoByNumber(ReceiptCounter++)` with `ReceiptCounter` wrapping 0–3.

Step type for a single step is selected by:

- **Function:** `TRecipeStep GetRecipeStepByNumber(uint8_t aNumOfRecipe, uint16_t aParam)`  
- **Parameter:** `aNumOfRecipe` = 1..10 (step type), `aParam` = volume/duration/ProcessCellID as per type.  
- **Dynamic use:** `app.c` ~line 813 – `AddRecipe(..., GetRecipeStepByNumber(10, ProcessCellID), ...)` to insert a transport step.

---

## Gaps / unknowns

1. **Recipe 0 vs 2:** Recipes 0 and 2 are identical (same name and same step list). The firmware uses recipe index only; there is no separate “name” stored on the tag. The Excel export keeps both as separate recipe numbers to match the code.
2. **In-code comments:** Some comments in `NFC_recipes.c` (e.g. “Cisteni”, “Drink k zakaznikovi”) are attached to steps whose `GetRecipeStepByNumber` call uses a different step type (e.g. Water/Cola). The Excel “Steps” sheet reflects the actual step type and parameters from the code, not the comment text.
3. **ActualBudget:** Only recipe 1 sets `ActualBudget = 200` in the firmware; the other full recipes leave it 0. Meaning and use of this field elsewhere (e.g. PLC/app) are not inferred here.
4. **ProcessCellID / TransportCellID:** Built-in recipe steps are generated with ProcessCellID and TransportCellID 0. They are filled at runtime when cells are chosen (e.g. OPC/AddRecipe). The export shows the firmware-generated values only.
5. **Enum ProcessTypes:** Values 0–8 are defined in `NFC_recipes.h`. Step type numbers 1–10 in `GetRecipeStepByNumber` map to ProcessTypes (and AlcoholType/NonAlcoholType) as documented in the Enums and Steps sheets.

---

## Output files

- **Excel:** `./informace/firmware_recipes.xlsx`  
  - **Sheet 1 – Recipes:** RecipeNumber, RecipeName, NumSteps, Description/Notes, SourceLocation.  
  - **Sheet 2 – Steps:** One row per step (RecipeNumber, StepIndex, StepName/Label, TypeOfProcess, TypeOfProcessName, ParameterProcess1, ParameterProcess2, ProcessCellID, TransportCellID, SourceLocation).  
  - **Sheet 3 – Enums:** ProcessTypes, AlcoholType, NonAlcoholType (value, name, source).  
  - **Sheet 4 – TagInfoFields:** TRecipeInfo and TRecipeStep field names, types, meaning, source.

- **Script:** `./informace/scripts/export_firmware_recipes.py`  
  - Re-run with: `python informace/scripts/export_firmware_recipes.py` to regenerate the Excel from the same extracted data.

- **Source references:**  
  - Recipes/steps: `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`  
  - Structs: `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`  
  - Enums: `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`
