# Reader–PLC AAS Flow (Minimal Viable)

This document describes the minimal AAS-compatible flow between the ESP32 NFC reader firmware and the Siemens S7-1200 PLC. **No PLC code is changed**; only the reader firmware was modified.

---

## 1. InputMessage formats (contract)

The PLC expects these **exact** message formats. No extra fields.

| Method | InputMessage format | Notes |
|--------|---------------------|--------|
| **ReportProduct** | `sr_id` (decimal integer string only) | PLC parses with STRING_TO_DINT; `sr_id != 0` required. |
| **ReserveAction** | `sr_id/priority/material/parameterA/parameterB` | Exactly 5 fields, `/` separated. |
| **GetSupported** | `sr_id/priority/material/parameterA/parameterB` | Same 5-field format. |
| **FreeFromPosition** | `sr_id` | Decimal only. |

Returns (typical): ReportProduct/ReserveAction/FreeFromPosition → `"Success"` or `"Error:XXXX"`; GetSupported → `"Support:X_Position:Y"` or `"Error:XXXX"`.

---

## 2. Step → message mapping (recipe → PLC)

From the current recipe step (`TRecipeStep`):

- **priority**: fixed `0` (for now).
- **material**: `(int) TypeOfProcess`
- **parameterA**: `(int) ParameterProcess1`
- **parameterB**: `(int) ParameterProcess2`

5-field string:

```text
sr_id/0/material/parameterA/parameterB
```

Example: `12345/0/2/10/100` for sr_id=12345, TypeOfProcess=2, ParameterProcess1=10, ParameterProcess2=100.

---

## 3. State machine overview

```
READ (NFC) → REPORT (ReportProduct) → [optional: GET_SUPPORTED] → RESERVE (ReserveAction) → WAIT → WRITEBACK
```

1. **READ**: Read UID (PN532), store in `TCardInfo.sUid` via `NFC_saveUID`; load `TRecipeInfo` and all `TRecipeStep` from tag (no tag format change).
2. **BUILD sr_id**: From UID bytes: `OPC_BuildSrIdFromUid()` → decimal string; if conversion would be 0, use FNV-1a hash fallback so `sr_id` is always non-zero.
3. **REPORT**: `ReportProduct(endpoint, sr_id)` with **only** the decimal `sr_id` string.
4. **STEP CHECK**: Current step index = `TRecipeInfo.ActualRecipeStep`. If `>= RecipeSteps`, set recipe status (e.g. RecipeDone) and write back to tag, then treat as invalid/end.
5. **GET_SUPPORTED (optional)**: Call `GetSupported` with the 5-field message. If response starts with `"Error:"` → mark step failed, write tag, abort session.
6. **RESERVE**: Call `ReserveAction` with the 5-field message. If response is `"Error:"` → mark failed, write tag. If `"Success"` → proceed.
7. **WAIT**: Completion is **timeout-based** (no PLC code change): `OPC_AAS_WaitCompletion(AAS_COMPLETION_TIMEOUT_MS)` (e.g. 30 s). No polling of PLC status variables in this minimal version.
8. **WRITEBACK**: Update `TRecipeInfo`: set `ActualRecipeStep` (increment on success), `RecipeDone` when last step; set step `IsStepDone`. Write only `TRecipeInfo` and the modified step (existing NFC write helpers); do not change recipe/tag layout.

---

## 4. Timeouts and error handling

- **ReportProduct**: Connection/method timeout from OPC UA client (e.g. 1 s in `ClientStart`).
- **GetSupported / ReserveAction**: Same client timeout.
- **Completion**: `AAS_COMPLETION_TIMEOUT_MS` (default 30 s). If PLC does not signal completion via a variable, this is the only completion criterion; document as “timeout-based completion” in logs.
- **Errors**: On any `"Error:"` from GetSupported or ReserveAction, or on step index out of range, the reader sets recipe/step state (e.g. RecipeDone or error), writes back `TRecipeInfo` (and step if applicable) to the tag, and aborts the AAS session for that tag.

---

## 5. Code changes summary

- **NFC_reader.c**: After loading `TRecipeInfo` from tag, call `NFC_saveUID(aCardInfo, iuid, iuidLength)` so `TCardInfo.sUid` is set and `sr_id` (and uidStr) are correct.
- **OPC_klient.c**:  
  - `OPC_BuildSrIdFromUid(uid, uidLen, buf, size)` builds decimal `sr_id` from UID bytes (non-zero; FNV-1a fallback).  
  - `OPC_ReportProduct(endpoint, sr_id_decimal)` sends **only** digits (validated).  
  - `OPC_GetSupported`, `OPC_ReserveAction`, `OPC_FreeFromPosition(endpoint, inputMessage, outBuf, outSize)`.  
  - `OPC_AAS_WaitCompletion(timeout_ms)` for timeout-based completion.
- **app.c**:  
  - Build `sr_id` with `OPC_BuildSrIdFromUid` from `sWorkingCardInfo.sUid/sUidLength`; call `ReportProduct(..., sr_id_buf)`.  
  - When `USE_PLC_AAS_FLOW` is set: run AAS sequence (step check, 5-field build, optional GetSupported, ReserveAction, wait, write-back).  
  - Legacy path (Inquire/Rezervation/DoProcess/IsFinished, etc.) is used when `LEGACY_FLOW` is defined (then `USE_PLC_AAS_FLOW` is not set).

---

## 6. Validation checklist

- [x] **ReportProduct** sends only decimal digits (no hex, no extra text).
- [x] **ReserveAction** and **GetSupported** send exactly 5 fields separated by `/`.
- [x] **sr_id** is never 0 (builder uses FNV-1a fallback and final clamp to 1 if needed).
- [x] **UID** is stored in `TCardInfo.sUid` during load (`NFC_saveUID` in `NFC_LoadTRecipeInfoStructure`).
- [x] **Tag write-back** updates only `TRecipeInfo` (and the current step’s `IsStepDone` / `ActualRecipeStep` / `RecipeDone`); no change to recipe layout or step structure.

---

## 7. Logging

The firmware logs:

- UID bytes and computed `sr_id` (e.g. in `OPC_BuildSrIdFromUid` and app state debug).
- Built InputMessage strings (ReportProduct sr_id, GetSupported/ReserveAction 5-field).
- Method responses (OutputMessage) for ReportProduct, GetSupported, ReserveAction.
- Tag write-back result (via existing NFC_Handler write paths).

---

## 8. Build flags

- **Default**: `USE_PLC_AAS_FLOW=1` (AAS path active). `LEGACY_FLOW` is not defined.
- **Legacy**: Define `LEGACY_FLOW` (e.g. in `CMakeLists.txt` or `sdkconfig`) so that `USE_PLC_AAS_FLOW` is not set; the reader uses the original Inquire/Rezervation/DoProcess/IsFinished flow.

---

*Document generated for the minimal AAS-compatible reader–PLC flow; no PLC code changes.*
