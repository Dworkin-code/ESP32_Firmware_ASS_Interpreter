# Overview

This report analyzes how to implement **Variant 2** (reuse 5-field payload, switch only transport `p2` semantics) in the current codebase.

Target behavior:
- Payload format remains `sr_id/sourceCell/type/p1/p2`.
- Production PLC calls keep current meaning (`p2 = ParameterProcess2`).
- Transport PLC calls reuse the same payload shape, but `p2 = TransportTime`.

The findings below are tied to concrete firmware and PLC code locations.

# ESP32 side findings

## Where normal (production/target) payload is constructed

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - `build_target_action_message(...)` builds 5-field message and currently sets:
    - `p1 = step->ParameterProcess1`
    - `p2 = step->ParameterProcess2`
  - `build_local_aas_action_message(...)` does the same for local production AAS path:
    - `p2 = step->ParameterProcess2`
  - Both are used before `OPC_GetSupported(...)` and `OPC_ReserveAction(...)`.

## Where transport payload is currently constructed

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - `request_transport_plc(...)` builds transport InputMessage with:
    - `snprintf("%s/%u/%u/%u/%u", sr_id, localCellId, step->TypeOfProcess, step->ParameterProcess1, step->ParameterProcess2)`
    - so transport currently also sends `p2 = ParameterProcess2`.

## Where `ParameterProcess2` is currently read from recipe step

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
  - `TRecipeStep` contains `uint16_t ParameterProcess2`.
- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - All message builders read `step->ParameterProcess2` for field 5.
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
  - `GetRecipeStepByNumber(...)` writes `ParameterProcess2` for material/volume style recipe steps.
  - `GetWinningCell(...)` and related reservation logic pass `param2` through inquiry paths.

## Where `TimeOfProcess` is available / can be read

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
  - `TRecipeStep` contains `UA_DateTime TimeOfProcess` and `UA_DateTime TimeOfTransport`.
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
  - In `ReserveAllOfferedReservation(...)`:
    - process reservation time is written into `tempStep.TimeOfProcess`
    - transport reservation time is written into `tempStep.TimeOfTransport`

Important implication:
- `TimeOfProcess` is available in `TRecipeStep`, but type is `UA_DateTime` (64-bit), while payload field `p2` is decimal `uint16` in current message construction. Variant 2 needs explicit conversion/scaling to fit `p2`.

# PLC side findings

## Where incoming payload is parsed

- `PLC_code/program_block/AAS/low level functions/GetMessage.scl`
  - Splits incoming `InputMessage` by `'/'` into `InputArray.element[]`.

## Where transport/production currently map field 5 (`p2`)

- `PLC_code/program_block/AAS/OPC UA Methods/ReserveAction.scl`
  - `InputArray.element[4]` is parsed into:
    - `tempItem."finalPosition/operationParameter" := STRING_TO_INT(#InputArray.element[4]);`
  - This is shared method-level parsing for all callers (production and transport endpoints that run this same logic).

## Where `p2` is currently used in PLC logic

- `ReserveAction.scl`
  - Support checks run `CASE` also on `InputArray.element[4]`.
- `PLC_code/program_block/Device Data/DeviceSlow.scl`
  - Runtime support checks include:
    - `CASE #item."finalPosition/operationParameter" OF ...`
- `PLC_code/plc_data_types/typeProductinQueue.pdf`
  - UDT `typeProductInQueue` field is:
    - `finalPosition/operationParameter : Int`
  - This confirms field 5 currently maps to generic operation parameter in queue item.

## Transport-specific parsing location

- There is no separate transport parser in `PLC_code`.
- Parsing and mapping happen in shared AAS blocks (`GetMessage` + `ReserveAction`).
- Therefore, transport interpretation change must be gated by transport context (not by payload shape).

# Proposed Variant 2 implementation

## ESP32 payload contract strategy

- Keep all payloads at 5 fields and same order.
- For **production/target PLC calls**:
  - unchanged: `p2 = step->ParameterProcess2`.
- For **transport PLC calls** (`request_transport_plc(...)` path only):
  - set `p2 = TransportTime` derived from recipe step time field (as requested: from `TimeOfProcess` mapping source).

## Recommended source field for transport time on ESP32

- Primary source (per requirement): `step->TimeOfProcess`.
- Conversion layer required before payload:
  - Convert `UA_DateTime` absolute timestamp to transport duration representation expected by transport PLC.
  - Clamp/scale to `uint16` range before formatting decimal `p2`.

Because `TimeOfProcess` is timestamp-like and not a direct duration value, conversion must be explicitly defined (see Risks section).

## PLC interpretation strategy (without changing payload contract)

- Keep `GetMessage` unchanged.
- In `ReserveAction.scl`, treat `InputArray.element[4]` contextually:
  - if request is for transport PLC logic, interpret as `TransportTime`;
  - else keep existing interpretation (`operationParameter` / supportB-like behavior).
- Optional clean PLC approach:
  - parse once into local variable `incomingP2`;
  - branch by transport context and map to either:
    - transport-time variable (for transport path),
    - existing `tempItem."finalPosition/operationParameter"` (for production path).

# Required firmware changes

- Modify only transport message composition in:
  - `ESP32_Firmware_ASS_Interpreter/main/app.c` (`request_transport_plc(...)`).
- Keep production builders unchanged:
  - `build_local_aas_action_message(...)`
  - `build_target_action_message(...)`
- Add deterministic conversion helper for `TimeOfProcess -> uint16 transportP2` used only by transport request path.
- Ensure conversion failure fallback behavior is defined (reject call / default value / clamp).

# Required PLC changes

- Main change point:
  - `PLC_code/program_block/AAS/OPC UA Methods/ReserveAction.scl`
    - add transport-context branch for interpreting `InputArray.element[4]`.
- Potential secondary change point:
  - `PLC_code/program_block/AAS/OPC UA Methods/GetSupported.scl`
    - if transport PLC should validate transport time differently than current supportB logic, this block also needs transport-branch handling for `element[4]`.
- `GetMessage.scl` does not need format changes.

# Scope of PLC impact

- Based on repository content, impact is primarily in shared AAS method blocks (`ReserveAction`, possibly `GetSupported`).
- If transport PLC runtime uses the same shared block set as production PLCs (very likely from current structure), change is **not strictly local** unless transport context gating is added.
- `DeviceSlow.scl` may remain unchanged if `finalPosition/operationParameter` is no longer relied on for transport behavior, but this depends on transport PLC execution model. Current file shows generic support checks only.

# Risks / caveats

- **Time semantics ambiguity**: `TimeOfProcess` in firmware is `UA_DateTime` timestamp, not clearly a duration. Transport PLC likely needs duration/time budget, so conversion rule must be agreed.
- **Unit mismatch risk**: transport PLC may expect seconds/ms/ticks; firmware currently sends plain integer `p2`.
- **Shared-method regression risk**: changing `ReserveAction` without transport-context guard may alter production behavior.
- **Support check coupling**: current `GetSupported` evaluates `element[4]` as supportB class range; this conflicts with using `p2` as transport time unless transport path bypasses/branches this logic.
- **Transport PLC source not isolated in repo**: no separate transport-specific parser block found; implementation likely touches common AAS logic.

# Recommended next implementation order

1. Define exact conversion contract: `TimeOfProcess -> TransportTime(uint16)` (unit, rounding, bounds, fallback).
2. Confirm transport-context discriminator in PLC (endpoint instance / process type / dedicated flag) so only transport calls reinterpret `p2`.
3. Implement PLC branch in `ReserveAction.scl` first (and `GetSupported.scl` if transport validation differs), preserving production path unchanged.
4. Implement firmware transport-only `p2` override in `request_transport_plc(...)`; keep both production builders unchanged.
5. Run end-to-end test matrix:
   - production call with normal `ParameterProcess2`;
   - transport call with converted `TransportTime`;
   - verify same 5-field payload length and no contract change.
