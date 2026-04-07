# 1. Summary

This document is derived only from firmware source code in:

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`
- `ESP32_Firmware_ASS_Interpreter/main/app.c`

Low-level NFC binary format is raw packed C structs copied byte-for-byte to tag memory.

- Header (`TRecipeInfo`) size: **12 bytes**
- Step (`TRecipeStep`) size: **31 bytes**
- Endianness: **little-endian** for multi-byte numeric fields (native ESP32 layout, raw byte memcpy/write)
- Header checksum is computed **only over step bytes**, not header bytes.

---

# 2. Header specification

Source of truth: packed struct `TRecipeInfo` in `NFC_reader.h` and checksum function `NFC_GetCheckSum()` in `NFC_reader.c`.

## 2.1 Header binary layout (`TRecipeInfo`, 12 bytes)

Offset | Size | Field | Type | Notes
---|---:|---|---|---
0 | 1 | `ID` | `uint8_t` | Card/recipe ID
1 | 2 | `NumOfDrinks` | `uint16_t` | little-endian
3 | 1 | `RecipeSteps` | `uint8_t` | number of steps
4 | 1 | `ActualRecipeStep` | `uint8_t` | current step index
5 | 2 | `ActualBudget` | `uint16_t` | little-endian
7 | 1 | `Parameters` | `uint8_t` | generic parameter byte (not further decoded in current code)
8 | 1 | `RightNumber` | `uint8_t` | integrity partner of `ID`
9 | 1 | `RecipeDone` | `bool` | stored as 1 byte in packed struct
10 | 2 | `CheckSum` | `uint16_t` | little-endian; must be last field

## 2.2 `RightNumber` computation

Directly implemented in `ChangeID()`:

- `RightNumber = 255 - ID`

Validation in `NFC_Handler_IsSameData()`:

- Card accepted only if `ID + RightNumber == 255`

## 2.3 Checksum computation

Implemented in `NFC_GetCheckSum(TCardInfo aCardInfo)`:

- If `RecipeSteps == 0`: checksum = `0`
- Else iterate over all step bytes:
  - for byte index `i` from `0` to `(TRecipeStep_Size * RecipeSteps - 1)`:
  - `CheckSum += step_bytes[i] * (i % 4 + 1)`

Important:

- Checksum covers **step array bytes only**
- Header bytes are **not** included in checksum calculation
- Header `CheckSum` field is updated before write in `NFC_WriteStructRange()`

---

# 3. Step specification

Source of truth: packed struct `TRecipeStep` in `NFC_reader.h`.

## 3.1 Step size

- `sizeof(TRecipeStep)` used everywhere via `TRecipeStep_Size`
- From struct layout and code assumptions, step size is **31 bytes**

## 3.2 Step binary layout (`TRecipeStep`, 31 bytes)

Offset | Size | Field | Type | Meaning in firmware
---|---:|---|---|---
0 | 1 | `ID` | `uint8_t` | this step ID
1 | 1 | `NextID` | `uint8_t` | next step ID (graph/linked-list style)
2 | 1 | `TypeOfProcess` | `uint8_t` | process/action type code
3 | 1 | `ParameterProcess1` | `uint8_t` | process parameter A
4 | 2 | `ParameterProcess2` | `uint16_t` | process parameter B, little-endian
6 | 1 | `PriceForTransport` | `uint8_t` | budget decrement in transport phase
7 | 1 | `TransportCellID` | `uint8_t` | selected transport cell/device ID
8 | 2 | `TransportCellReservationID` | `uint16_t` | reservation ID, little-endian
10 | 1 | `PriceForProcess` | `uint8_t` | budget decrement in process phase
11 | 1 | `ProcessCellID` | `uint8_t` | selected process cell/device ID
12 | 2 | `ProcessCellReservationID` | `uint16_t` | reservation ID, little-endian
14 | 8 | `TimeOfProcess` | `UA_DateTime` | OPC UA time (64-bit), little-endian byte order on ESP32
22 | 8 | `TimeOfTransport` | `UA_DateTime` | OPC UA time (64-bit), little-endian byte order on ESP32
30 | 1 | flags byte | 4 x 1-bit bool bitfields | `NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`

## 3.3 Flags bit mapping

Defined as consecutive bitfields in this order:

1. `NeedForTransport:1`
2. `IsTransport:1`
3. `IsProcess:1`
4. `IsStepDone:1`

Because C bitfield packing is compiler/ABI-defined, exact bit positions inside byte 30 are not explicitly guaranteed by standard C. On this firmware toolchain they are used consistently as one trailing flag byte.

## 3.4 Default values

- New/expanded arrays are zero-filled in `NFC_ChangeRecipeStepsSize()`
- `EmptyRecipeStep` is all zero initializers
- Therefore unknown/unused fields default to `0x00`

---

# 4. Supported process types

## 4.1 Primary enum used by recipe generation (`NFC_recipes.h`)

`enum ProcessTypes` numeric mapping:

0. `ToStorageGlass`
1. `StorageAlcohol`
2. `StorageNonAlcohol`
3. `Shaker`
4. `Cleaner`
5. `SodaMake`
6. `ToCustomer`
7. `Transport`
8. `Buffer`

This mapping is used when generating recipe steps (`GetRecipeStepByNumber()`).

## 4.2 Human-action generation mapping (`GetRecipeStepByNumber`)

- Vodka/Rum/Goralka -> `TypeOfProcess = StorageAlcohol`, `ParameterProcess1 = alcohol kind`, `ParameterProcess2 = volume`
- Water/Cola -> `TypeOfProcess = StorageNonAlcohol`, `ParameterProcess1 = non-alcohol kind`, `ParameterProcess2 = volume`
- Shaker -> `TypeOfProcess = Shaker`, `ParameterProcess1 = duration`
- Cleaner -> `TypeOfProcess = Cleaner`, `ParameterProcess1 = duration`
- ToStorageGlass -> `TypeOfProcess = ToStorageGlass`
- ToCustomer -> `TypeOfProcess = ToCustomer`
- Transport -> `TypeOfProcess = Transport`, `ParameterProcess1 = target cell id`

## 4.3 Additional process-type interpretation in `app.c` (AAS route owner)

`resolve_owner_cell_id_from_process_type()` uses a **different semantic mapping**:

- 1 -> Skladkapalin
- 2 -> SodaMaker
- 3 -> Shaker
- 4 -> SkladAlkoholu
- 5 -> SkladSklenicek
- 6 -> DrticLedu

This is active runtime routing logic for local-vs-transport decision in current state machine.

---

# 5. Device/cell mapping

## 5.1 Which bytes store target device/cell

In step payload:

- `ProcessCellID` at byte offset **11**
- `TransportCellID` at byte offset **7**

Process type and target cell are separate fields:

- `TypeOfProcess` = what operation
- `ProcessCellID`/`TransportCellID` = where to execute/transport

## 5.2 Cell ID to endpoint mapping in firmware

From `assign_local_endpoint_from_cell_id()`:

- 1 -> `192.168.168.66:4840` (Skladkapalin)
- 2 -> `192.168.168.102:4840` (SodaMaker)
- 3 -> `192.168.168.150:4840` (Shaker)
- 4 -> `192.168.168.88:4840` (SkladAlkoholu)
- 5 -> `192.168.168.63:4840` (SkladSklenicek / StorageGlass)
- 6 -> `192.168.168.203:4840` (DrticLedu)

---

# 6. Parameter mapping by step type

## 6.1 `StorageAlcohol` (TypeOfProcess = 1 in `enum ProcessTypes`)

- Byte 3 (`ParameterProcess1`): alcohol subtype enum
  - `Vodka=0`, `Rum=1`, `Goralka=2`
- Bytes 4-5 (`ParameterProcess2`): amount, used as `uint16_t` (recipe generator comments indicate ml)

## 6.2 `StorageNonAlcohol` (TypeOfProcess = 2)

- Byte 3: non-alcohol subtype enum
  - `Water=0`, `Cola=1`
- Bytes 4-5: amount (`uint16_t`, comments indicate ml)

## 6.3 `Shaker` (TypeOfProcess = 3)

- Byte 3: duration (`aParam` from recipe generator, comments indicate seconds)
- Bytes 4-5: usually 0

## 6.4 `Cleaner` (TypeOfProcess = 4)

- Byte 3: duration (`aParam`, comments indicate seconds)
- Bytes 4-5: usually 0

## 6.5 `SodaMake` (TypeOfProcess = 5)

- No direct generator case currently assigns this in `GetRecipeStepByNumber()`
- Byte mapping therefore not explicitly instantiated in provided recipe templates
- Struct still defines generic parameters at byte 3 and bytes 4-5

## 6.6 `ToStorageGlass` (TypeOfProcess = 0)

- No explicit process parameters used in generator
- Byte 3 and bytes 4-5 default to 0

## 6.7 `ToCustomer` (TypeOfProcess = 6)

- No explicit process parameters used in generator
- Byte 3 and bytes 4-5 default to 0

## 6.8 `Transport` (TypeOfProcess = 7)

- Byte 3 (`ParameterProcess1`) used as target/process cell ID in `GetRecipeStepByNumber(10, aParam)`
- Bytes 4-5 usually 0

---

# 7. Evidence from source code

- Raw byte print proving binary serialization order:
  - `NFC_Print()` prints `Info tagu` and `Kroky Receptu` byte-by-byte
- Struct-to-tag write path:
  - `NFC_WriteStructRange()` copies bytes from `TRecipeInfo` then from `TRecipeStep[]`
- Struct load path:
  - `NFC_LoadTRecipeInfoStructure()`, `NFC_LoadTRecipeSteps()`, `NFC_LoadTRecipeStep()`
- Header integrity:
  - `ChangeID()` sets `RightNumber = 255 - ID`
  - `NFC_Handler_IsSameData()` validates `ID + RightNumber == 255`
- Checksum:
  - `NFC_GetCheckSum()` iterates step bytes only
- Step execution / ownership / transport decision:
  - State machine in `app.c` (`State_Machine()`)
  - Local process decision uses `resolve_owner_cell_id_from_process_type(TypeOfProcess)`
  - Otherwise transitions toward transport/reservation states

---

# 8. Unresolved fields

The following are present in binary structure but not fully semantically specified by current source:

- Header byte 7 (`Parameters`): **UNKNOWN semantic**
- Step byte 6 (`PriceForTransport`): treated as budget decrement, unit/range not defined
- Step byte 10 (`PriceForProcess`): treated as budget decrement, unit/range not defined
- Step bytes 14-21 (`TimeOfProcess`): UA_DateTime reservation/process schedule, exact external contract unit is UA_DateTime ticks
- Step bytes 22-29 (`TimeOfTransport`): same
- Step flag byte bit ordering: implementation-dependent in C standard; current toolchain uses one trailing byte consistently
- Process type semantic conflict:
  - `enum ProcessTypes` says code `1=StorageAlcohol`, `2=StorageNonAlcohol`, ...
  - AAS ownership function in `app.c` interprets codes `1..6` as concrete cells (`Skladkapalin`, `SodaMaker`, ...).

---

# 9. Final practical encoding guide

To encode deterministic step bytes without guessing:

1. Build 31-byte step using `TRecipeStep` order exactly (offsets in section 3).
2. Set `ID`, `NextID`, `TypeOfProcess`, and required parameter bytes.
3. Keep unused fields at `0x00` unless reservation/runtime logic fills them.
4. Build header 12 bytes:
   - set `RecipeSteps`, `ActualRecipeStep`, etc.
   - set `RightNumber = 255 - ID`
   - compute `CheckSum` over concatenated step bytes only:
     - `sum += byte[i] * (i % 4 + 1)`
5. Store multi-byte fields in little-endian.

For minimal pre-runtime recipe authoring, commonly fixed-zero bytes are:

- Step offsets 6..30 except fields explicitly needed for your action
- Header offsets 5..7 optionally project-defined

---

# 10. Minimal encoding templates

Format: `[byte0, byte1, ..., byte30]` for one step.

Legend:

- `ID` = step ID byte
- `NEXT` = next step ID byte
- `T` = type code
- `P1` = `ParameterProcess1`
- `P2L`,`P2H` = little-endian `ParameterProcess2`

All unspecified bytes below are `0`.

## 10.1 Shaker (enum `Shaker` = 3)

`[ID, NEXT, 3, P1_seconds, P2L, P2H, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]`

Typical minimal: `P2L=0`, `P2H=0`.

## 10.2 SkladKapalin

This name maps to two possible code interpretations in firmware:

- As `StorageNonAlcohol` action: `T=2`, `P1={Water(0)|Cola(1)}`, `P2=volume`
- As AAS owner-cell mapping in `app.c`: process code `1` is interpreted as Skladkapalin owner

Minimal step template using recipe enum (`StorageNonAlcohol`):

`[ID, NEXT, 2, P1_kind, P2L_ml, P2H_ml, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]`

## 10.3 Sodamaker

In `enum ProcessTypes`, `SodaMake=5`.

No explicit parameter contract found in recipe generator; use generic parameter fields only if your PLC side defines them.

`[ID, NEXT, 5, P1_optional, P2L_optional, P2H_optional, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]`

## 10.4 StorageGlass / ToStorageGlass

`ToStorageGlass` enum code is `0`.

`[ID, NEXT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]`

## 10.5 ToStorageGlass (explicit duplicate template)

`[ID, NEXT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]`

