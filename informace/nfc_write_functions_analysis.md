# NFC Write Functions Analysis

## 1. Function locations

### `NFC_Handler_WriteStep(...)`
- **File:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`
- **Signature:** `uint8_t NFC_Handler_WriteStep(THandlerData* aHandlerData, TRecipeStep* aRecipeStep, size_t aIndex)`
- **Declaration:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.h`
- **Direct helper calls:**
  - `NFC_Handler_ResizeIndexArray(...)`
  - `NFC_Handler_AddIndex(...)`
- **Does NOT call NFC hardware directly.**

### `NFC_Handler_WriteSafeInfo(...)`
- **File:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`
- **Signature:** `uint8_t NFC_Handler_WriteSafeInfo(THandlerData* aHandlerData, TRecipeInfo* aRecipeInfo)`
- **Declaration:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.h`
- **Direct helper calls:**
  - `NFC_WriteCheck(...)` (in `components/NFC_Reader/NFC_reader.c`)
  - `NFC_ChangeRecipeStepsSize(...)`
  - `NFC_Handler_CopyToWorking(...)`
- **Calls NFC hardware indirectly via `NFC_WriteCheck(...)`.**

### `NFC_Handler_Sync(...)`
- **File:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`
- **Signature:** `uint8_t NFC_Handler_Sync(THandlerData* aHandlerData)`
- **Declaration:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.h`
- **Direct helper calls:**
  - `NFC_CheckStructArrayIsSame(...)`
  - `NFC_WriteCheck(...)`
  - `NFC_ChangeRecipeStepsSize(...)`
- **Calls NFC hardware indirectly via `NFC_CheckStructArrayIsSame(...)` and `NFC_WriteCheck(...)`.**

---

## 2. What `NFC_Handler_WriteStep(...)` does

`NFC_Handler_WriteStep(...)` is a **buffer/update marker** function, not a physical tag writer.

- Validates `aIndex < RecipeSteps`; otherwise returns `2`.
- Copies provided `TRecipeStep` into in-memory working buffer:
  - `aHandlerData->sWorkingCardInfo.sRecipeStep[aIndex] = *aRecipeStep;`
- Ensures index bitmap size matches current recipe size:
  - `NFC_Handler_ResizeIndexArray(...)`
- Marks this step index as dirty/pending write:
  - `NFC_Handler_AddIndex(aHandlerData, aIndex)`
- Returns `0` on successful buffer update.

### Hardware and blocking behavior
- **No direct NFC IO**.
- **No polling/wait/retry loops inside this function.**
- Time cost is memory copy + index operations (fast).
- It **depends only on valid in-memory state**, not immediate tag presence.

---

## 3. What `NFC_Handler_WriteSafeInfo(...)` does

`NFC_Handler_WriteSafeInfo(...)` performs an **immediate write of `TRecipeInfo` to NFC** with rollback on failure.

### Main behavior
1. Saves old integrity info (`tempData`, `tempDataLoaded`).
2. Forces integrity info loaded and replaces it with new `*aRecipeInfo`.
3. Tries to physically write+verify info range `(0..0)` using:
   - `NFC_WriteCheck(&aHandlerData->sNFC, &aHandlerData->sIntegrityCardInfo, 0, 0)`
4. Retries this up to `MAXERRORREADING` times in handler (`MAXERRORREADING = 3` in `NFC_handler.c`).
5. On failure:
   - restores previous `sIntegrityCardInfo.sRecipeInfo`
   - returns `1`
6. On success:
   - optionally resizes step array (`NFC_ChangeRecipeStepsSize(...)`) if step count changed
   - copies info to working (`NFC_Handler_CopyToWorking(..., 1, 0)`)
   - returns `0`

### Hardware and blocking behavior
- **Yes, communicates with hardware** (through `NFC_WriteCheck`).
- **Yes, retries and can block for long time**.
- **Requires tag presence/readability/writability during write-check cycle**.

---

## 4. What `NFC_Handler_Sync(...)` does

`NFC_Handler_Sync(...)` flushes pending working-buffer changes (dirty step indexes + possibly info) to NFC and then updates integrity mirror.

### Main behavior
- Scans `sRecipeStepIndexArray` for dirty entries.
- If no dirty step:
  - Checks whether info struct differs (`NFC_CheckStructArrayIsSame(..., 0, 0)`).
  - If same => returns `1` ("nothing to write").
  - If different => writes info with `NFC_WriteCheck(..., 0, 0)`.
- If dirty steps exist:
  - Builds write ranges and writes chunks using `NFC_WriteCheck(...)` for each chunk.
  - On successful chunk write:
    - copies written data from working to integrity mirror
    - clears corresponding dirty indexes.

### Hardware and blocking behavior
- **Yes, communicates with hardware** through `NFC_WriteCheck` and compare-read path.
- **Yes, retries and can block**.
- **Strongly depends on tag presence and stable read/write conditions**.
- Writes are not "fire-and-forget"; they are write+verify cycles.

---

## 5. Return codes and their meanings

No dedicated enum names are defined for these functions in `NFC_handler.h/.c`; numeric codes are raw `uint8_t`.

### `NFC_Handler_WriteStep(...)`
- `0` = success; step was placed into working buffer and dirty index marked.
- `2` = `aIndex` out of range (`aIndex >= RecipeSteps`).

**Your observed `NFC_Handler_WriteStep=0` means:** buffer update succeeded. It does **not** guarantee physical tag write happened.

### `NFC_Handler_WriteSafeInfo(...)`
- `0` = immediate info write+verify succeeded; working copy updated.
- `1` = immediate info write failed (after retries); old integrity info restored.

**Your observed `NFC_Handler_WriteSafeInfo=1` means:** info write path failed (likely from lower-layer write/check timeout/read/write/auth issue).

### `NFC_Handler_Sync(...)`
- `0` = synchronization write succeeded.
- `1` = nothing needed writing (already same / no pending changes).
- `2` = write failure (`NFC_WriteCheck` failed in step/info sync path).
- `9` = failed to resize integrity structure after successful write.
- `10` = unexpected result from compare (`NFC_CheckStructArrayIsSame` default path).

**Your observed `NFC_Handler_Sync=2` means:** sync attempted physical write and lower write-check failed.

---

## 6. Most likely source of the long delay

Most likely delay source is **inside `NFC_WriteCheck(...)` and its callees**, reached from `NFC_Handler_WriteSafeInfo(...)` and/or `NFC_Handler_Sync(...)`.

### Why this is likely
- `NFC_Handler_WriteStep(...)` is memory-only and should be fast.
- `NFC_Handler_WriteSafeInfo(...)` and `NFC_Handler_Sync(...)` both call `NFC_WriteCheck(...)`.
- `NFC_WriteCheck(...)` has nested retry loops and each low-level write/read can wait on PN532 timeout.

### Critical timing path
- In `NFC_reader.c`:
  - `MAXTIMEOUT = 5000` ms used in `pn532_readPassiveTargetID(...)`.
  - `NFC_WriteCheck(...)`:
    - outer retry loop up to 5 (`k < MAXERRORREADING`, where reader-layer `MAXERRORREADING = 5`)
    - inner write loop up to 5 (`i < MAXERRORREADING`) calling `NFC_WriteStructRange(...)`
  - `NFC_WriteStructRange(...)` calls `pn532_readPassiveTargetID(..., MAXTIMEOUT)` before write.

If tag is absent/unstable, one `NFC_WriteCheck` call can spend roughly:
- `5 (inner attempts) * 5000ms = ~25s` before returning nonzero in worst common path.

Then handler-level functions may retry `NFC_WriteCheck` up to 3 times (`NFC_handler.c` `MAXERRORREADING = 3`), making total delay potentially much larger.

Given your observed sequence:
- `WriteStep=0` (fast),
- `WriteSafeInfo=1` (failed),
- `Sync=2` (failed),
the delay is almost certainly in `WriteSafeInfo` and/or `Sync`, not in `WriteStep`.

---

## 7. Current failure-handling weaknesses

### In the AAS write-back block (`main/app.c`)
Current sequence:
1. `write_step_res = NFC_Handler_WriteStep(...)`
2. `write_info_res = NFC_Handler_WriteSafeInfo(...)`
3. `sync_res = NFC_Handler_Sync(...)`
4. Always logs: `"AAS: step %u done, write-back OK"`

### Problems
- **Errors are not acted upon**: return values are logged but not used for branching/recovery.
- **Partial success is possible**:
  - step marked dirty in working memory (`WriteStep=0`)
  - info write fails (`WriteSafeInfo=1`)
  - sync fails (`Sync=2`)
  - yet code still prints "write-back OK".
- **Potentially misleading system state**:
  - application state progresses (`ActualRecipeStep` advanced in memory),
  - but NFC persistence may have failed.

So yes: code can report success text even when writes fail.

---

## 8. Best debug points

Without code changes now, these are the best places to add timestamped logs later:

1. **Before and after each call in AAS write-back block**
   - around:
     - `NFC_Handler_WriteStep(...)`
     - `NFC_Handler_WriteSafeInfo(...)`
     - `NFC_Handler_Sync(...)`
   - Goal: isolate which top-level call consumes time.

2. **Inside `NFC_Handler_WriteSafeInfo(...)` around each `NFC_WriteCheck(...)` attempt**
   - Log attempt index (0..2), start/end, and returned error.
   - Goal: confirm retries and per-attempt duration.

3. **Inside `NFC_Handler_Sync(...)` before each `NFC_WriteCheck(...)` range write**
   - Log `zacatek/konec` range and elapsed time per call.
   - Goal: determine if delay is in info write or step-range write.

4. **Inside `NFC_WriteCheck(...)` around:**
   - each `NFC_WriteStructRange(...)` call
   - each `NFC_CheckStructArrayIsSame(...)` call
   - plus loop counters `k` and `i`
   - Goal: identify whether delay is write phase or verify/read phase.

5. **Inside `NFC_WriteStructRange(...)` around `pn532_readPassiveTargetID(..., MAXTIMEOUT)`**
   - Log start/end and return.
   - Goal: verify repeated 5-second waits are source of ~30s stall.

---

## 9. Final conclusion

- `NFC_Handler_WriteStep(...)` only updates working memory and dirty flags; it is fast and does not touch hardware.
- `NFC_Handler_WriteSafeInfo(...)` and `NFC_Handler_Sync(...)` perform real NFC write-check operations and can block due to PN532/tag timeouts and retry loops.
- Observed values mean:
  - `WriteStep=0` -> local buffering succeeded.
  - `WriteSafeInfo=1` -> immediate info write failed.
  - `Sync=2` -> sync write failed.
- The ~30s delay is most plausibly from low-level `NFC_WriteCheck`/`NFC_WriteStructRange` timeout-retry behavior (`MAXTIMEOUT=5000ms` with nested retries), not from `WriteStep`.
