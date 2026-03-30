# Post AAS Success Trace

## 1. Success branch location

- File: `ESP32_Firmware_ASS_Interpreter/main/app.c`
- Function/task: `State_Machine(void *pvParameter)`
- AAS branch is inside state `State_Mimo_Polozena` after tag load and `ReportProductEx` handling.
- Exact success check:
  - `bool completion_ok = OPC_AAS_WaitCompletionPoll(...);`
  - then `if (!completion_ok) { ... } else { post-success updates ... }`
- Relevant block is around:
  - `if (step->TypeOfProcess == ToStorageGlass) { ... }`
  - lines ~`370-455` in current `app.c`.

Key snippet:

```c
bool completion_ok = OPC_AAS_WaitCompletionPoll(MyCellInfo.IPAdress, sr_id_buf, (uint32_t)AAS_COMPLETION_TIMEOUT_MS, 500);
xSemaphoreGive(Parametry->xEthernet);
if (!completion_ok) {
  ...
  RAF = State_Mimo_Polozena;
  continue;
}
step->IsStepDone = 1;
iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep = curStep + 1;
...
NFC_STATE_DEBUG(GetRafName(RAF), "AAS: step %u done, write-back OK\n", (unsigned)curStep);
RAF = State_WaitUntilRemoved;
continue;
```

## 2. Exact control flow after poll success

After `OPC_AAS_WaitCompletionPoll(...) == true` in `ToStorageGlass` branch:

1. `xSemaphoreGive(Parametry->xEthernet);`
2. `step->IsStepDone = 1;`
3. `ActualRecipeStep` increment:
   - `iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep = curStep + 1;`
4. `RecipeDone` potentially set:
   - if new `ActualRecipeStep >= numSteps`, then `RecipeDone = true`
5. NFC write-back attempt (conditional on semaphore):
   - `if (xSemaphoreTake(Parametry->xNFCReader, 10000) == pdTRUE) { ... }`
   - inside:
     - `NFC_Handler_WriteStep(&iHandlerData, step, curStep);`
     - `NFC_Handler_WriteSafeInfo(&iHandlerData, &...sRecipeInfo);`
     - `NFC_Handler_Sync(&iHandlerData);`
     - `xSemaphoreGive(Parametry->xNFCReader);`
6. Log:
   - `NFC_STATE_DEBUG(..., "AAS: step %u done, write-back OK\n", curStep);`
7. State transition:
   - `RAF = State_WaitUntilRemoved;`
8. `continue;` (immediate next loop iteration, no fall-through)

Important behavior detail:
- If `xSemaphoreTake(xNFCReader)` fails, code **does not** log failure in this branch, but still prints `"AAS: step ... write-back OK"` and still sets `RAF = State_WaitUntilRemoved`.

## 3. NFC write-back path

### Functions called post-success

In `app.c` post-success block:

- `NFC_Handler_WriteStep(...)`
- `NFC_Handler_WriteSafeInfo(...)`
- `NFC_Handler_Sync(...)`

Definitions and return types in `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`:

- `uint8_t NFC_Handler_WriteStep(...)`  
  - returns `0` success, `2` out-of-range index.
- `uint8_t NFC_Handler_WriteSafeInfo(...)`  
  - returns `0` success, `1` write failure.
- `uint8_t NFC_Handler_Sync(...)`  
  - returns multiple codes (`0`, `1`, `2`, `9`, `10`, ... depending on internal path).

### Are failures checked?

- In this AAS success branch: **No**.
  - Return values are ignored for all three calls.
  - There is no validation that write-back succeeded.

### Success/failure logging

- In this branch:
  - No per-call success/failure logging for these three NFC write functions.
  - Only one generic log exists after the whole block:  
    `"AAS: step %u done, write-back OK"` (printed regardless of write result, and even if NFC semaphore was not obtained).
- In `NFC_handler.c` there are internal debug macros (`NFC_HANDLER_DEBUG`), but they are disabled unless compile flags enable them (`#define NFC_HANDLER_DEBUG_EN` is commented out there).

## 4. State transition after success

- `RAF = State_WaitUntilRemoved` is assigned in `app.c` directly after post-success write-back block.
- It is not nested under write-return checks.
- It is reached after successful `completion_ok`, unless execution is interrupted before line (e.g., crash/hard fault/watchdog).

### Is it always reached?

For normal control flow after `completion_ok == true`: **yes**.

Potential preventers:

- Not `continue`/`break`/`return` in this block (none before assignment).
- But runtime interruption can prevent it:
  - fault/reset during NFC write function internals,
  - task abort,
  - watchdog reset.

After assignment, `State_WaitUntilRemoved` behavior:

- Case `State_WaitUntilRemoved` logs:
  - `"Cekam nez tag zmizi po odebrani transportem"`
  - if card removed: `"Zmizel"` then `RAF = State_Mimo_Polozena`.
- If card remains on reader, state machine stays in this state and repeats wait log every cycle.

## 5. Expected logs vs observed logs

After `OPC_AAS_WaitCompletionPoll: Success`, expected next logs from `State_Machine` path are:

1. `"AAS: step %u done, write-back OK"`
2. then repeated state log in `State_WaitUntilRemoved`:
   - `"Cekam nez tag zmizi po odebrani transportem"`
3. once card removed:
   - `"Zmizel"`

If none of these appear, then code likely did **not** complete this branch to the post-success debug line, despite poll success log in OPC client.

Most likely places where progression can disappear:

- Between poll return and post-success log (inside NFC write call chain).
- State machine task no longer running (crash/reset/starvation), while another task still logs (`OPC_Permanent_Test`).

## 6. Source of repeated OPC_TEST reconnect messages

Message source:

- File: `ESP32_Firmware_ASS_Interpreter/main/app.c`
- Function: `OPC_Permanent_Test(void *pvParameter)`
- Log line:
  - `printf("OPC_TEST: Pripojeni na %s USPELO.\n", MyCellInfo.IPAdress);`

Behavior:

- Infinite loop (`while(true)`), every ~5 seconds:
  - takes `xEthernet` semaphore,
  - creates OPC UA client (`ClientStart`),
  - logs success/failure,
  - disconnects/deletes client,
  - releases semaphore,
  - delays 5s.
- Task is created in `app_main()`:
  - `xTaskCreate(&OPC_Permanent_Test, "OPC_Test", 6144, ..., 6, NULL);`

Conclusion:

- Repeated `OPC_TEST` success messages indicate this background test task is active and reconnecting periodically.
- This is largely **separate** from the AAS success branch, but it competes for `xEthernet` semaphore and can dominate logs, masking missing state-machine output.

## 7. Most likely root cause

Most likely based on current code path and your symptom:

1. **State machine does not reach/finish post-success tail despite poll success** (likely interruption in/around NFC write-back calls).
2. **Logging blind spot in success branch**:
   - no log before/after each NFC write call,
   - no log on `xNFCReader` semaphore failure,
   - return values ignored, so silent failure is possible.
3. `OPC_TEST` loop is noisy and can make it look like “only reconnect happens”, but that message is from a different task.

Less likely:

- A pure logical branch miss after success (because code is linear and should set `RAF = State_WaitUntilRemoved`).
- Conditional skip by `continue/break/return` in this exact post-success path (none present).

## 8. Best minimal debug points

Without refactor, minimal targeted instrumentation points (for next step) should be:

1. Immediately after `OPC_AAS_WaitCompletionPoll` returns (print `completion_ok` and current `RAF`).
2. Before and after each call:
   - `NFC_Handler_WriteStep`
   - `NFC_Handler_WriteSafeInfo`
   - `NFC_Handler_Sync`
   including returned `uint8_t` values.
3. Log result of `xSemaphoreTake(Parametry->xNFCReader, ...)` in post-success branch.
4. Log just before `RAF = State_WaitUntilRemoved` and immediately in `case State_WaitUntilRemoved`.
5. Temporarily disable or reduce `OPC_Permanent_Test` noise during this trace session so state-machine logs are visible.

Bottom line: the success branch exists and should transition to `State_WaitUntilRemoved`; observed absence of those logs points to runtime interruption or silent NFC write/back path failure visibility, while `OPC_TEST` reconnect logs come from an independent periodic test task.
