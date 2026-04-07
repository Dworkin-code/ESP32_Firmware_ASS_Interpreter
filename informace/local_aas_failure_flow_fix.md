# Local AAS Failure Flow Fix

## Why it previously ended as RECIPE_FINISHED
In local AAS path, on local request failure, firmware did this:
- on `GetSupported` error response
- on `ReserveAction` call failure or `Error:*` response

it set `iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeDone = true`, wrote info back to tag, and synced.

On next loop in `State_Mimo_Polozena`, early condition checks `RecipeDone` and/or bounds and can go into recipe-finished path (`RECIPE_FINISHED`/`State_KonecReceptu`).

So a temporary reservation failure looked like completed recipe.

## What logic was corrected
In local AAS failure branches, firmware now:
- does **not** set `RecipeDone = true`
- does **not** write completion info to tag
- keeps current step pending for retry
- returns to `State_Mimo_Polozena`

This applies to:
- `GetSupported` error response branch
- `ReserveAction` failure/error branch

## Behavior after fix
After local reserve/support failure:
- `ActualRecipeStep` remains unchanged
- `IsStepDone` for current step remains unchanged
- `RecipeDone` remains unchanged (typically `false`)
- no false completion write-back is persisted
- next iteration re-evaluates the same step as recoverable work

## Added diagnostics
New logs added to clearly show failure handling intent:
- `AAS_FAIL_LOCAL: GetSupported Error response=... -> keep step=... pending (RecipeDone=...)`
- `AAS_FAIL_LOCAL: ReserveAction callOk=... response=... -> keep step=... pending (RecipeDone=...)`

These logs make it explicit why recipe is not marked finished.

## Exact changed places
File: `ESP32_Firmware_ASS_Interpreter/main/app.c`

Inside `State_Machine`, local `LOCAL_PROCESS` branch:
- removed `RecipeDone=true` + NFC writeback in local GetSupported error handling
- removed `RecipeDone=true` + NFC writeback in local ReserveAction failure handling
- kept state transition back to `State_Mimo_Polozena` for retry flow
