# AAS Completion Poll Analysis

## 1. Relevant functions

### `OPC_CallAasMethod(...)`
- File: `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`
- Purpose: generic OPC UA method call wrapper for AAS methods in `ns=4`.
- Behavior:
  - Builds OPC UA string input (`InputMessage`).
  - Calls method node `UA_Client_call(...)`.
  - Returns `true` only when UA call result is `UA_STATUSCODE_GOOD`.
  - Copies first string output arg into `outBuf` and logs:
    - `OPC_CallAasMethod(ns=4;i=<id>): OutputMessage=<text>`
- Important: This function treats method transport/protocol success separately from business-state success. Any returned output text (including `"Success"`, `"running"`, etc.) is just payload, not auto-interpreted here.

### `OPC_GetStatus(...)`
- File: `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`
- Purpose: call AAS `GetStatus` method (`PLC_NODEID_GETSTATUS_ID = 7002`) with `sr_id` as input.
- Signature:
  - `bool OPC_GetStatus(const char *endpoint, const char *sr_id_decimal, char *outBuf, size_t outSize)`
- Behavior:
  - returns `false` only if params invalid or underlying UA call failed.
  - otherwise returns `true` and places raw `OutputMessage` in `outBuf`.
- No business parsing is done in this function.

### `OPC_AAS_WaitCompletionPoll(...)`
- File: `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`
- Purpose: poll `GetStatus` until action completes / errors / timeout.
- Signature:
  - `bool OPC_AAS_WaitCompletionPoll(const char *endpoint, const char *sr_id_decimal, uint32_t timeout_ms, uint32_t poll_interval_ms)`
- Core logic:
  - loops while `elapsed < timeout_ms`
  - calls `OPC_GetStatus(...)`
  - if call failed: waits interval, increments elapsed, continues
  - if output equals exactly `"finished"` -> success (`return true`)
  - if output starts with exactly lowercase `"error:"` -> error (`return false`)
  - any other output is treated as "still running/unknown", keeps polling
  - on loop expiry -> timeout (`return false`)

## 2. Expected GetStatus response format

Based on comments and parser behavior in `OPC_klient.c`, expected `GetStatus` output payload is one of:
- `"finished"` (exact lowercase) -> done
- `"error:XXXX"` (exact lowercase prefix) -> failure
- running-like values (not terminal), e.g. comment lists:
  - `"running"`
  - `"inProgress"`
  - `"position:N"`

Critical detail: parser is strict and case-sensitive:
- success only for `strcmp(outBuf, "finished") == 0`
- error only for `strncmp(outBuf, "error:", 6) == 0`

Any other payload (including `"Success"`) is not terminal and is treated as continue polling.

## 3. How polling decides DONE / RUNNING / ERROR

In `OPC_AAS_WaitCompletionPoll(...)`:

- DONE:
  - condition: `outBuf` exactly `"finished"`
  - log: `OPC_AAS_WaitCompletionPoll: finished`
  - return: `true`

- ERROR:
  - condition: `outBuf` starts with `"error:"`
  - log: `OPC_AAS_WaitCompletionPoll: <outBuf>` (error log)
  - return: `false`

- RUNNING (or unrecognized non-terminal):
  - condition: any other successful `GetStatus` output
  - no dedicated log in poll loop for this branch
  - action: sleep + continue until timeout

- GETSTATUS CALL FAILURE:
  - condition: `OPC_GetStatus(...) == false`
  - no dedicated error log from poll loop
  - action: sleep + continue until timeout

- TIMEOUT:
  - condition: elapsed reaches `timeout_ms`
  - log: `OPC_AAS_WaitCompletionPoll: timeout after <timeout> ms`
  - return: `false`

## 4. ToStorageGlass runtime path

File: `ESP32_Firmware_ASS_Interpreter/main/app.c`, state branch `State_Mimo_Polozena`, fast AAS path for `TypeOfProcess == ToStorageGlass`.

Flow:
1. Build `msg5 = "sr_id/0/material/parameterA/parameterB"`.
2. Take Ethernet semaphore.
3. Call `OPC_GetSupported(...)`.
   - if output starts with `"Error:"` -> mark recipe done and exit branch.
4. Call `OPC_ReserveAction(...)`.
   - if call fails or output starts with `"Error:"` -> mark recipe done and exit branch.
5. On ReserveAction success:
   - store re-scan guard values (`s_lastSeenSrId`, `s_lastActionTimestampMs`)
   - call `OPC_AAS_WaitCompletionPoll(endpoint, sr_id, AAS_COMPLETION_TIMEOUT_MS, 500)`
6. Release Ethernet semaphore.
7. If poll returns `false` (error or timeout):
   - log completion failure/timeout
   - mark `RecipeDone=true`
   - write back to tag
   - `RAF = State_Mimo_Polozena`
8. If poll returns `true`:
   - mark step done (`step->IsStepDone = 1`)
   - increment `ActualRecipeStep`
   - optionally set `RecipeDone`
   - write back to tag
   - log step done
   - `RAF = State_WaitUntilRemoved`

Interpretation for "does not continue to transport logic":
- In this branch, successful `ToStorageGlass` completion does not jump to transport state directly; it writes step result and moves to `State_WaitUntilRemoved`.
- If poll never reaches `"finished"`, it stays inside polling until timeout window (30s).

## 5. Possible mismatch with PLC response

There is a likely contract mismatch between PLC output text and ESP32 parser:

- Observed repeated log:
  - `OPC_CallAasMethod(ns=4;i=7002): OutputMessage=Success`
- ESP32 poll terminal checks expect:
  - success terminal: `"finished"` only
  - error terminal: `"error:"` prefix only

So repeated `"Success"` from `GetStatus` means:
- OPC UA call itself succeeded (transport/protocol success),
- but action is **not** recognized as completed by ESP32 parser.

Also possible format mismatches:
- case mismatch (`"Finished"` vs `"finished"`, `"Error:"` vs `"error:"`)
- alternate success token (`"Success"` instead of `"finished"`)
- trailing spaces/newline (`"finished "`), which would fail `strcmp`.

## 6. Expected logs vs observed logs

### Expected logs on successful completion
- `OPC_AAS_WaitCompletionPoll: sr_id=..., timeout ..., poll every ...`
- repeated method call logs from `OPC_CallAasMethod(...7002): OutputMessage=<status>`
- once terminal success arrives:
  - `OPC_AAS_WaitCompletionPoll: finished`
  - app log: `AAS: step <n> done, write-back OK`

### Expected logs on timeout
- repeated `OutputMessage=<non-terminal>` logs (or none if calls fail)
- then:
  - `OPC_AAS_WaitCompletionPoll: timeout after 30000 ms`
  - app log: `AAS: completion error or timeout -> RecipeDone`

### Expected logs on failure
- repeated status logs until first `error:...`
- then:
  - `OPC_AAS_WaitCompletionPoll: error:...`
  - app log: `AAS: completion error or timeout -> RecipeDone`

### Observed symptom
- repeated `OutputMessage=Success` after entering poll
- missing `OPC_AAS_WaitCompletionPoll: finished`
- therefore no poll success path taken.

## 7. Most likely root cause

Most likely root cause is **status-token contract mismatch**:
- PLC `GetStatus` (`ns=4;i=7002`) returns `"Success"` for completed action.
- ESP32 `OPC_AAS_WaitCompletionPoll` treats completion only as exact `"finished"`.
- Therefore each poll iteration sees `"Success"` as non-terminal and continues until timeout.

So `"Success"` here means:
- `GetStatus` method call was valid and returned a string payload,
- possibly PLC business completion too (depending on PLC intent),
- but **not recognized as completion by firmware parser**.

In short: this is not blocked transport; it is blocked by strict completion-string matching.

## 8. Best minimal patch points

No code changes made in this analysis, but minimal-risk patch targets would be:

1. `OPC_AAS_WaitCompletionPoll(...)` in `OPC_klient.c`
- Extend terminal success recognition to include `"Success"` (and optionally `"success"`).
- Optionally normalize/trim output before compare.

2. Same function, error parsing
- Accept both `"error:"` and `"Error:"` to avoid case-contract drift.

3. Optional diagnostics-only improvement
- Add log for non-terminal status values inside poll loop (e.g., `running status=<outBuf>`), which would make this class of mismatch immediately visible in runtime logs.

4. Contract alignment (preferred long-term)
- Define one canonical `GetStatus` terminal vocabulary between PLC and firmware (exact casing and tokens) and keep both sides consistent.
