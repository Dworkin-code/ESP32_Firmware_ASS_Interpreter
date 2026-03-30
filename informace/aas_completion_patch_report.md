# AAS Completion Patch Report

## 1. Modified file(s)
- `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`

## 2. Exact code change
- In `OPC_AAS_WaitCompletionPoll(...)`, terminal success check changed from only exact `"finished"` to:
  - `"finished"`, `"Finished"`, `"success"`, `"Success"`
- Terminal error prefix check changed from only `"error:"` to:
  - `"error:"` and `"Error:"`
- Added one debug log for non-terminal status values:
  - `ESP_LOGD(TAG, "OPC_AAS_WaitCompletionPoll: non-terminal status=%s", outBuf);`
- Polling loop structure, timeout behavior, and delay logic remain unchanged.

## 3. New accepted success values
- `finished`
- `Finished`
- `success`
- `Success`

## 4. New accepted error prefixes
- `error:`
- `Error:`

## 5. Any added debug logs
- Added inside polling loop for diagnostics when status is neither terminal success nor terminal error:
  - `OPC_AAS_WaitCompletionPoll: non-terminal status=<value>`

## 6. Expected runtime behavior after patch
- If PLC returns `Success` (or `success`, `Finished`, `finished`), polling ends immediately with success and recipe execution can continue to next step.
- If PLC returns `Error:...` (or `error:...`), polling ends immediately with failure.
- Any other returned status continues polling until terminal status or timeout, same as before.
