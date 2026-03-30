# ActualRecipeStep Write Trace

## 1. TRecipeInfo layout

**Definition file:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`

`TRecipeInfo` is defined as packed:

```c
typedef struct __attribute__((packed))
{
  uint8_t ID;                 // byte 0
  uint16_t NumOfDrinks;       // bytes 1-2
  uint8_t RecipeSteps;        // byte 3
  uint8_t ActualRecipeStep;   // byte 4
  uint16_t ActualBudget;      // bytes 5-6
  uint8_t Parameters;         // byte 7
  uint8_t RightNumber;        // byte 8
  bool RecipeDone;            // byte 9
  uint16_t CheckSum;          // bytes 10-11 (last)
} TRecipeInfo;
```

**Packing/alignment:** `__attribute__((packed))` is present, so byte index mapping is deterministic and byte `4` is indeed `ActualRecipeStep`.

## 2. Where ActualRecipeStep is updated

**Update location:** `ESP32_Firmware_ASS_Interpreter/main/app.c` inside `State_Machine(...)`, AAS success branch.

After AAS completion success:
- `step->IsStepDone = 1;`
- `iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep = curStep + 1;`

So update happens on the **live working structure** (`iHandlerData.sWorkingCardInfo.sRecipeInfo`), not a temporary copy.

Then code calls (same branch):
1. `NFC_Handler_WriteStep(&iHandlerData, step, curStep);`
2. `NFC_Handler_WriteSafeInfo(&iHandlerData, &iHandlerData.sWorkingCardInfo.sRecipeInfo);`
3. `NFC_Handler_Sync(&iHandlerData);`

## 3. WriteSafeInfo data path

**Function:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c` -> `NFC_Handler_WriteSafeInfo(...)`

Data flow:
1. `aRecipeInfo` points to `sWorkingCardInfo.sRecipeInfo` (contains new `ActualRecipeStep=1`).
2. Function copies it to integrity buffer:
   - `aHandlerData->sIntegrityCardInfo.sRecipeInfo = *aRecipeInfo;`
3. It writes using:
   - `NFC_WriteCheck(&aHandlerData->sNFC, &aHandlerData->sIntegrityCardInfo, 0, 0);`

So for this write path, source struct instance is `sIntegrityCardInfo.sRecipeInfo` after being overwritten from working info.

## 4. Write path for range 0-0

### 4.1 Call chain

`NFC_Handler_WriteSafeInfo`  
-> `NFC_WriteCheck(..., range=0,0)`  
-> `NFC_WriteStructRange(..., range=0,0)`

### 4.2 Source buffer written

In `NFC_WriteStructRange`, for info range (`0-0`):
- `zacatek = 0`
- `konec = TRecipeInfo_Size - 1` (packed size = 12, so bytes `0..11`)

Per-byte source is taken directly from:
- `((uint8_t *)&aCardInfo->sRecipeInfo)[...]`

So byte 4 source at write time is:
- `((uint8_t *)&aCardInfo->sRecipeInfo)[4]` = `ActualRecipeStep` (expected value `1` in your evidence).

### 4.3 Tag page/block mapping for byte index 4

Given `OFFSETDATA_CLASSIC=1`, `OFFSETDATA_ULTRALIGHT=8`:

- **MIFARE Classic (UID len 4):**
  - page/block chunk = 16 bytes
  - byte 4 belongs to internal chunk `i=0`, offset `k=4`
  - physical block index from `NFC_GetMifareClassicIndex(0)` is `1`
  - write call: `pn532_mifareclassic_WriteDataBlock(..., index=1, iData)`

- **MIFARE Ultralight (UID len 7):**
  - page chunk = 4 bytes
  - byte 4 belongs to chunk `i=1`, offset `k=0`
  - physical page = `i + OFFSETDATA_ULTRALIGHT = 9`
  - write call: `pn532_mifareultralight_WritePage(..., page=9, iData)`

So range `0-0` absolutely includes byte 4.

## 5. Verify read-back path

Verify chain:
- `NFC_WriteCheck` -> `NFC_CheckStructArrayIsSame(..., 0, 0)`
- For structure `0`, it calls `NFC_LoadTRecipeInfoStructure(...)` into temp card struct `idataNFC1`.
- Compare loop:
  - expected: `((uint8_t *)&aCardInfo->sRecipeInfo)[j]`
  - actual: `((uint8_t *)&idataNFC1.sRecipeInfo)[j]`

This is raw byte-to-byte compare with same packed `TRecipeInfo` layout on both sides.

Therefore mismatch at `j=4` means:
- expected side contained byte 4 = `1`
- read-back side contained byte 4 = `0`

not a decode/layout artifact.

## 6. Most likely direct cause of byte 4 mismatch

Most likely direct cause is in the low-level write routine behavior:

1. `NFC_WriteStructRange` prepares the correct source buffer (with byte 4 = 1), **but**
2. it does **not validate write success robustly per page/block**:
   - Classic path stores `Zapsano` from `pn532_mifareclassic_WriteDataBlock(...)` but does not branch on failure.
   - Ultralight path stores `Zapsano` from `pn532_mifareultralight_WritePage(...)` but does not branch on failure.
3. Function still returns success unless card/auth high-level failure occurs.
4. `NFC_WriteCheck` then detects read-back mismatch (`expected 1`, `actual 0`).

This exactly matches your runtime evidence:
- first mismatch is byte 4 (`ActualRecipeStep`)
- checksum mismatch is secondary (expected/actual checksum differ, but mismatchInCheckSum=0)

The compare evidence strongly indicates stale byte on tag after attempted write, not wrong struct mapping.

## 7. Best minimal fix point

Best minimal fix point is inside `NFC_WriteStructRange(...)` in `NFC_reader.c`:
- right after each low-level write call
  - `pn532_mifareclassic_WriteDataBlock(...)`
  - `pn532_mifareultralight_WritePage(...)`
- if write return indicates failure, return error immediately (instead of silently continuing).

Why this point:
- It is exactly where value leaves RAM and should enter NFC memory.
- It explains why expected buffer can be correct while read-back remains old.
- It avoids false-positive "write success" states that currently defer failure to compare stage only.

## 8. Final conclusion

`TRecipeInfo.ActualRecipeStep` (byte 4) is updated correctly in working RAM after AAS success and is passed through `NFC_Handler_WriteSafeInfo` into the info-write path (`range 0-0`) with correct packed byte layout.

The mismatch occurs at persistence/transport layer: write-back path builds the right source byte, but the low-level write functions are not enforced as hard failures per block/page, so tag content can remain old (`0`) while expected buffer is already `1`. Verify then correctly reports the first mismatch at byte 4.

In short: this is most consistent with **write not truly committing to tag memory (or failing silently) at `NFC_WriteStructRange`**, not with struct layout, wrong range, or read decode mismatch.
