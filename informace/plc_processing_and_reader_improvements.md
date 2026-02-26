# PLC AAS processing and reader improvements (analysis + proposal)

Analysis of PLC-side AAS request processing and minimal **reader-only** firmware improvements. No PLC code changes.

---

# 1) PLC AAS processing overview

## PLC functions / FB / FC related to AAS methods and queues

| Role | Path in PLC_code | Name |
|------|------------------|------|
| Message parsing | `program_block/AAS/low level functions/GetMessage.scl` | FC **GetMessage** |
| Report product | `program_block/AAS/OPC UA Methods/ReportProduct.scl` | FB **ReportProduct** |
| Get support + position | `program_block/AAS/OPC UA Methods/GetSupported.scl` | FB **GetSupported** |
| Reserve action | `program_block/AAS/OPC UA Methods/ReserveAction.scl` | FB **ReserveAction** |
| Free output position | `program_block/AAS/OPC UA Methods/FreeFromPosition.scl` | FB **FreeFromPosition** |
| Get status (position/inProgress) | `program_block/AAS/OPC UA Methods/GetStatus.scl` | FB **GetStatus** |
| Free from PQueue (remove by id) | `program_block/AAS/OPC UA Methods/FreeFromQueue.scl` | FB **FreeFromQueue** |
| Reported-products queue find | `program_block/Queue functions/Queue_find.scl` | FC **Queue_find** |
| Reported-products queue push | `program_block/Queue functions/Queue_push.scl` | FC **Queue_push** |
| Reported-products queue remove | `program_block/Queue functions/Queue_remove.scl` | FC **Queue_remove** |
| Priority queue find | `program_block/Priority Queue functions/PQueue_find.scl` | FC **PQueue_find** |
| Priority queue push | `program_block/Priority Queue functions/PQueue_push.scl` | FC **PQueue_push** |
| Priority queue remove by position | `program_block/Priority Queue functions/PQueue_removeByPosition.scl` | FC **PQueue_removeByPosition** |
| Remove finished product (RP + PQueue) | `program_block/AAS/low level functions/RemoveFinishedProduct.scl` | FB **RemoveFinishedProduct** |
| Device / output position | `program_block/Device Data/DeviceSlow.scl` | FB **DeviceSlow** |
| Check action (item from PQueue in RP queue) | `program_block/Device Data/CheckAction.scl` | FB **CheckAction** |

Data: **RPQueue**.ReportedProductsQueue (reported products), **PQueue**.PQueue (priority queue for reserved actions), **PassiveAAS_DB**.OutputPositionSr (WString, sr_id at output position).

---

## GetMessage – exact parsing of InputMessage

- **File**: `program_block/AAS/low level functions/GetMessage.scl`
- **Function**: `GetMessage` (FC)
- **Parsing**: Input is a **String**; split by delimiter **`/`**; result in **typeArrayMessage** with `element[0]` … `element[WordNumberSpaces]`. No trailing empty elements; last segment has no trailing `/`. So for `"A/B/C/D/E"` → `element[0]=A`, `element[1]=B`, `element[2]=C`, `element[3]=D`, `element[4]=E`. All methods use this same parsing.

---

## ReportProduct

- **File**: `program_block/AAS/OPC UA Methods/ReportProduct.scl`
- **Block**: FB **ReportProduct**
- **InputMessage**: Only **element[0]** is used: `tempItem.id := STRING_TO_DINT(#InputArray.element[0])`. So a **single field** (decimal integer string) or first segment of a slash-separated string. Extra fields are ignored.
- **Validation**:
  - `id <> 0` (otherwise error).
  - **Queue_find**(id, **RPQueue**.ReportedProductsQueue) → if **found** → error **RQErrIdAlreadyKnown** (duplicate sr_id).
  - If not found → **Queue_push**(item, RPQueue.ReportedProductsQueue); push can set error (e.g. buffer full, index limits).
- **Output**: On error, `OutputMessage := "Error:" + HTA(tempStatus, 4)` (4 hex digits). If subFunctionStatus <> PQNoError, format is `"Error1:XXX_Error2:YYY"`. On success: `"Success"`.
- **Error codes (mapping)**:
  - **Error:8501** ← **RQErrIdAlreadyKnown** (sr_id already in ReportedProductsQueue).
  - **Error:XXXX** for **PQErrInvalidItemId** when id = 0 (exact hex from symbol value).
  - Other Queue_push/Queue_find status codes as Error:XXXX or Error1/Error2.

---

## GetSupported

- **File**: `program_block/AAS/OPC UA Methods/GetSupported.scl`
- **Block**: FB **GetSupported**
- **InputMessage**: **element[1]** (priority), **element[2]** (material), **element[3]** (supportA), **element[4]** (supportB). element[0] (sr_id) not used for support calculation; used for queue position: `FOR i := 0 TO STRING_TO_INT(element[1])` to sum `PQueue.subElementCount[i]` → **tmpQueuePosition**.
- **Validation**: Material/supportA/supportB must fall in support ranges (SupportMaterialLow100..High100, SupportALow100..High100, etc.); else support value becomes 0. No explicit "Error:" for invalid material; output is always `support:N_position:M`.
- **Output**: `"support:" + supportValue + "_position:" + queuePosition`. No "Error:XXXX" in this block.

---

## ReserveAction

- **File**: `program_block/AAS/OPC UA Methods/ReserveAction.scl`
- **Block**: FB **ReserveAction**
- **InputMessage**: **element[0]** = id (DInt), **element[1]** = priority (UInt), **element[2]** = material, **element[3]** = initialPosition/operation, **element[4]** = finalPosition/operationParameter. Full **5-field** format: `sr_id/priority/material/parameterA/parameterB`.
- **Validation**:
  - `id <> 0` (else **PQErrInvalidItemId**).
  - **PQueue_find**(id, PQueue) → if **found** → **PQErrItemAlreadyInQueue** (duplicate in PQueue).
  - If not found: material/supportA/supportB checked; if combined support = 0 → **ErrItemNotSupported**; else **PQueue_push**(item, priority, PQueue).
- **Output**: On error, same pattern as ReportProduct: `"Error:" + HTA(status)` (or Error1/Error2). Success: `"Success"`.
- **Semantics**: Registers the product in the **priority queue** for this SP; product must already be **reported** (in ReportedProductsQueue). Cell logic uses CheckAction to find an item that is both in PQueue and in ReportedProductsQueue, then DeviceSlow runs and eventually **RemoveFinishedProduct** removes from both queues.

---

## FreeFromPosition

- **File**: `program_block/AAS/OPC UA Methods/FreeFromPosition.scl`
- **Block**: FB **FreeFromPosition**
- **InputMessage**: **element[0]** only, as **WString**: compared to **PassiveAAS_DB**.OutputPositionSr. So `InputMessage` is effectively **sr_id** (string, same format as OutputPositionSr).
- **Validation**: If `OutputPositionSr = STRING_TO_WSTRING(element[0])` → clear `OutputPositionSr := '0'`, return **Success**. Else → **PErrItemNotInPosition** → `"Error:XXXX"`.
- **Semantics**: Tells the PLC that the product at the **output position** (the one whose id is in OutputPositionSr) has been taken away. DeviceSlow sets OutputPositionSr when the device finishes (`#OutputPositionSr := DELETE(DINT_TO_WSTRING(#item.id), L:=1, P:=1)`). FreeFromPosition is the way for the client to release that position so the PLC can accept the next product at output.

---

## FreeFromQueue

- **File**: `program_block/AAS/OPC UA Methods/FreeFromQueue.scl`
- **Block**: FB **FreeFromQueue**
- **InputMessage**: **element[0]** = item id (DInt). Rest unused.
- **Validation**: PQueue_find(id); if not found → PQErrItemNotFound; if found but position = activeItemPosition → PQErrItemAlreadyInProgress; else PQueue_removeByPosition(position).
- **Output**: Success or Error:XXXX.
- **Semantics**: Removes the product from the **PQueue** by id (if not currently in progress). Does **not** remove from ReportedProductsQueue.

---

## GetStatus

- **File**: `program_block/AAS/OPC UA Methods/GetStatus.scl`
- **Block**: FB **GetStatus**
- **InputMessage**: **element[0]** = item id (DInt).
- **Validation**: id <> 0 else PQErrInvalidItemId; PQueue_find(id); if not found → PQErrItemNotFound.
- **Output**: If found: `"position:" + position` or `"inProgress"` if position = PQueue.activeItemPosition. On error: Error:XXXX.
- **Semantics**: Indicates whether the product is in PQueue and whether it is the one currently in progress.

---

## Error code 8501

- **8501** is the **hex representation** of the PLC status symbol **RQErrIdAlreadyKnown** (ReportProduct sets `tempStatus := "RQErrIdAlreadyKnown"` when Queue_find reports **found** on ReportedProductsQueue). So **Error:8501** = **sr_id already in ReportedProductsQueue** (duplicate report).

---

# 2) Error:8501 root cause

- **Exact condition**: In **ReportProduct.scl**, after `GetMessage` and `tempItem.id := STRING_TO_DINT(element[0])`, the PLC calls **Queue_find**(id, **RPQueue**.ReportedProductsQueue). If **found = TRUE**, it sets `tempError := TRUE` and `tempStatus := "RQErrIdAlreadyKnown"`. The output is then `"Error:" + HTA(tempStatus, 4)` → **Error:8501** (assuming the symbol value is 0x8501).
- **Meaning**: Duplicate **sr_id**: the same product id was already reported and is still in the ReportedProductsQueue. The PLC does **not** add it again.
- **What the client should do**: Treat **Error:8501** as **idempotent success** (“already reported”) and continue: run the same step logic (step index check, then optionally GetSupported/ReserveAction). If the tag is already at the last step (ActualRecipeStep >= RecipeSteps), mark recipe done and skip further PLC calls. If not at last step, the product may already be in PQueue (e.g. re-scan); then either skip ReserveAction and go to “wait/removed” or use GetStatus to detect “inProgress” and avoid double ReserveAction.

---

# 3) Reader firmware: current behavior assessment

- **Sequence**: Read NFC → build sr_id → write CurrentId → **ReportProduct(sr_id)** → (if AAS flow) step check → GetSupported → ReserveAction → fixed timeout → write-back. This order is **compatible** with the PLC: ReportProduct first, then 5-field methods; step check after ReportProduct is correct.
- **Correct**:
  - ReportProduct sends digits-only sr_id; ReserveAction/GetSupported use 5-field format.
  - Step index check (curStep >= numSteps) is done and recipe done is written to the tag; then AAS is skipped.
- **Problems / risks**:
  1. **ReportProduct response ignored**: `OPC_ReportProduct()` does not return **OutputMessage** to the app. When the PLC returns **Error:8501**, the reader still continues and only later hits “step index 1 >= steps 1” and marks recipe invalid. So the **same tag** causes ReportProduct → 8501 (logged but not handled), then step check → “recipe invalid” and RecipeDone. That matches the observed “Error:8501” and “step index 1 >= steps 1, recipe invalid”.
  2. **No idempotency for 8501**: Re-scanning the same tag (or tag already at end) always calls ReportProduct; PLC correctly rejects duplicate → 8501. Firmware should treat 8501 as success and then apply step logic.
  3. **Step check after ReportProduct**: Step check is done **after** ReportProduct. For a tag with ActualRecipeStep >= RecipeSteps, ReportProduct is still called (and may return 8501). Correct fix: after ReportProduct (or 8501), if curStep >= numSteps, mark recipe done and skip GetSupported/ReserveAction.
  4. **FreeFromPosition never called**: The PLC sets OutputPositionSr when the device finishes (DeviceSlow). FreeFromPosition(sr_id) is intended to clear that when the product leaves the output. The reader never calls it (e.g. when the user removes the tag after completion). Not calling it does not cause 8501, but calling it when the tag is removed after completion would align with the PLC’s “release output position” semantics.
  5. **Completion**: Completion is **timeout-only**; no use of GetStatus (position/inProgress). Acceptable for current milestone; optional improvement is to poll GetStatus to detect “inProgress” → done.
  6. **No retry/backoff**: Any "Error:" from GetSupported/ReserveAction aborts and writes the tag; no retry. For transient errors a limited retry with backoff could be added (without spamming the PLC).

**Verdict**: For the current AAS-only PLC milestone, the flow is **mostly correct** but **not good enough** for reliability: duplicate tags and end-of-recipe tags must be handled explicitly (8501 as success + step check first after report). FreeFromPosition and GetStatus are optional improvements.

---

# 4) Proposed reader-only improvements (minimal patch plan)

## 4.1 ReportProduct: return OutputMessage and treat Error:8501 as success

- **Why**: PLC returns Error:8501 when sr_id is already in ReportedProductsQueue. The reader should treat this as “already reported” and continue (idempotency).
- **Where**:  
  - **OPC_klient.c**: Add a variant that returns the method output, e.g. `OPC_ReportProductEx(endpoint, sr_id_decimal, outBuf, outSize)` returning bool and filling `outBuf` with OutputMessage (or keep `OPC_ReportProduct` and add an optional `outBuf`/`outSize`).  
  - **app.c**: After calling ReportProduct, read the response; if it equals **"Error:8501"**, treat as success (do not abort). Then run the existing step check and AAS sequence.
- **Behavior**:  
  - If response is **Success** → continue as now.  
  - If response is **Error:8501** → treat as success; then if curStep >= numSteps → mark RecipeDone, write tag, skip GetSupported/ReserveAction; else continue to GetSupported/ReserveAction (ReserveAction may then return “already in queue” if re-scan; handle that separately if desired).  
- **Risk**: None if only 8501 is treated as success; other errors (e.g. buffer full) should still be treated as failure.

## 4.2 Step check before GetSupported/ReserveAction (after ReportProduct)

- **Why**: Tags with ActualRecipeStep >= RecipeSteps should not call GetSupported/ReserveAction. Currently the step check is already after ReportProduct; the only change is that after treating 8501 as success, the **same** step check must run: if curStep >= numSteps → RecipeDone, write tag, skip PLC.
- **Where**: **app.c** (AAS block): After ReportProduct (and 8501 handling), the existing `if (curStep >= numSteps)` block already does the right thing; ensure it is reached when ReportProduct returns 8501 (i.e. do not treat 8501 as a hard error that skips this block).
- **Behavior**: For 8501 or Success, always evaluate curStep vs numSteps; if curStep >= numSteps, write RecipeDone and skip GetSupported/ReserveAction.
- **Risk**: None.

## 4.3 Optional: FreeFromPosition when tag is removed after completion

- **Why**: DeviceSlow sets OutputPositionSr when the device finishes. FreeFromPosition(sr_id) tells the PLC the product has left the output position. Calling it when the user removes the tag after a successful step (or recipe done) matches the PLC contract.
- **Where**: **app.c**: In the state where the card is removed after AAS (e.g. State_WaitUntilRemoved or when leaving “step done” with tag removed), if we have sr_id and the last action was success/recipe done, call **OPC_FreeFromPosition(endpoint, sr_id_buf, outBuf, sizeof(outBuf))** once (e.g. when transitioning to “card removed”). Store sr_id in the handler/state so it is available when the card is removed.
- **Behavior**: On “tag removed after completion”, one FreeFromPosition(sr_id) call; ignore or log response (Success / Error).
- **Risk**: If the PLC has not set OutputPositionSr yet (e.g. device still running), FreeFromPosition returns Error (PErrItemNotInPosition). That is acceptable; no retry needed. Do not call FreeFromPosition before the PLC has set OutputPositionSr (e.g. do not call it immediately after ReserveAction; only when user removes tag after completion).

## 4.4 Optional: Use GetStatus for completion instead of fixed timeout only

- **Why**: GetStatus(sr_id) returns "position:N" or "inProgress". Polling until the item is no longer in PQueue (or GetStatus returns error “not found”) could signal completion instead of a fixed 30 s timeout.
- **Where**: **OPC_klient.c** (already has OPC_CallAasMethod / GetStatus node); **app.c**: In the wait phase, optionally poll GetStatus(sr_id) every 1–2 s; if response is "inProgress" keep waiting; if "position:N" or item not found (Error), consider step done and exit wait early.
- **Behavior**: Reduces unnecessary wait when PLC finishes quickly; still cap with a max timeout to avoid infinite wait.
- **Risk**: Depends on cell program actually removing the item from PQueue when done; otherwise “inProgress” may never clear. Use as optional improvement with a fallback timeout.

## 4.5 Optional: Retry with backoff for transient errors only

- **Why**: Network or PLC busy can cause transient failures. Retrying once or twice with short delay can improve reliability without spamming.
- **Where**: **app.c** or **OPC_klient.c**: For ReportProduct/GetSupported/ReserveAction, if the call fails (connect/call error) or returns an error string that is considered transient (e.g. not 8501, not “invalid id”), retry up to N times with 500–1000 ms delay.
- **Behavior**: Limit to 1–2 retries; do not retry on 8501 (treat as success) or on permanent errors (e.g. invalid id). Only for connection/timeout or generic “Error:XXXX” that might be transient.
- **Risk**: If PLC returns a permanent error, retries add delay; keep N small and do not retry on known permanent codes (8501, invalid id, etc.).

---

# 5) Recommended test cases (to validate improvements)

1. **New tag (first scan)**  
   - Fresh tag, ActualRecipeStep = 0, RecipeSteps > 1.  
   - Expect: ReportProduct → Success; GetSupported → support/position; ReserveAction → Success; wait; write-back with ActualRecipeStep = 1.

2. **Repeated same tag (duplicate sr_id)**  
   - Same tag put back before PLC has removed it from ReportedProductsQueue (e.g. immediately or after short delay).  
   - Expect: ReportProduct → Error:8501; firmware treats as success; step check runs. If step already at end → RecipeDone written, no GetSupported/ReserveAction. If step not at end → either “already in progress” path or second ReserveAction returns “already in queue”; no crash, clear tag state.

3. **Recipe end-state tag**  
   - Tag with ActualRecipeStep = RecipeSteps (e.g. 1 and RecipeSteps = 1).  
   - Expect: ReportProduct (Success or 8501) → step check → curStep >= numSteps → RecipeDone written to tag, no GetSupported/ReserveAction; log “recipe invalid” or “recipe done” as appropriate.

4. **PLC error paths**  
   - Invalid sr_id (e.g. 0 or non-numeric): ReportProduct should fail; no 8501 handling.  
   - ReserveAction with unsupported material: Expect "Error:XXXX"; reader marks step/recipe failed, writes tag, aborts session.  
   - GetSupported/ReserveAction connection timeout: Optional retry; then fail and write tag.

5. **Write-back verification**  
   - After Success: ActualRecipeStep incremented, current step IsStepDone = 1, RecipeDone set when last step.  
   - After 8501 + end step: RecipeDone set, no increment of ActualRecipeStep.  
   - Read tag again and confirm stored TRecipeInfo matches expected state.

6. **FreeFromPosition (if implemented)**  
   - Complete one step; remove tag; call FreeFromPosition(sr_id).  
   - Expect: Success if OutputPositionSr was set by PLC; or Error if not (e.g. device not finished). No side effect on next scan.

---

**Summary**: The PLC returns **Error:8501** when the same sr_id is already in ReportedProductsQueue (duplicate). The reader should **treat Error:8501 as success** and then run the step check; for tags at recipe end, mark RecipeDone and skip GetSupported/ReserveAction. Minimal code changes: (1) ReportProduct returns OutputMessage; (2) app treats "Error:8501" as success and proceeds to step check and optional GetSupported/ReserveAction. Optional: FreeFromPosition on tag removal after completion; GetStatus for completion; limited retries for transient errors.
