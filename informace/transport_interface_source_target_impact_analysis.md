# Impact Analysis: Transport Interface Migration (Time-Based -> Source/Target Cells)

## 1. Executive summary

This firmware currently uses a mixed transport model:
- New AAS orchestration paths in `main/app.c` already do remote target reservation + transport PLC request.
- Legacy reservation path still writes and checks transport reservation time (`TimeOfTransport`) on NFC tag.
- Transport PLC payload currently sends a time-like value as the 5th token (derived from `TimeOfTransport`), not destination cell.

For the supervisor requirement (`P1=source cell`, `P2=target cell`, no transport time to PLC), the **definitely required firmware change** is concentrated in:
- transport payload builder in `request_transport_plc()` (`main/app.c`)
- transport-entry conditions in legacy state-machine branches that still gate by `TimeOfTransport`
- logs/debug text that explicitly describe time-based transport payload semantics

Reservation ID protection and broader reservation lifecycle can remain unchanged, as requested.

---

## 2. Current transport flow in firmware

### 2.1 AAS path (current primary orchestration)

In `main/app.c`:
- `reserve_remote_target()` resolves target cell and calls remote target PLC:
  - `OPC_GetSupported(endpoint, inputMsg, ...)`
  - `OPC_ReserveAction(endpoint, inputMsg, ...)`
- If successful, transport gate is opened with `transport_gate_set(sr_id, stepIndex, targetCellId)`.
- `request_transport_plc(sr_id, MyCellInfo.IDofCell, step, ...)` is called.

Key functions:
- `resolve_target_cell_for_step()`
- `resolve_next_target_cell()`
- `build_target_action_message()`
- `request_transport_plc()`
- `transport_gate_matches_runtime()`

### 2.2 Legacy state-machine transport path

Still present in `main/app.c` states:
- `State_Poptavka_Vyroba`
- `State_Poptavka_Transporty`
- `State_Rezervace`
- `State_Transport`
- `State_Vyroba_Objeveni`
- `State_Vyroba_SpravneProvedeni`

Legacy path still uses:
- transport-cell reservation (`TransportCellID`, `TransportCellReservationID`)
- reservation time writeback (`TimeOfTransport` in `ReserveAllOfferedReservation()`)
- state branch using `TimeOfTransport > 0` for deciding "reserved transport present"

---

## 3. All locations using `TimeOfTransport`

## 3.1 Declaration / storage

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
  - `TRecipeStep.TimeOfTransport` declared as `UA_DateTime`.
  - Stored as packed field in NFC step struct (raw binary via whole-struct read/write).

## 3.2 Parsed / loaded from tag

No dedicated parser field-level function exists. It is loaded implicitly by raw structure copy:
- `components/NFC_Reader/NFC_reader.c`
  - `NFC_LoadTRecipeSteps()` reads bytes into `TRecipeStep` array.
  - `NFC_LoadTRecipeStep()` reads one step by byte copy.

So `TimeOfTransport` is read as part of full `TRecipeStep`.

## 3.3 Written back to tag

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
  - `ReserveAllOfferedReservation()`
    - For transport reservation: `tempStep.TimeOfTransport = tempReservation.TimeOfReservation;`
    - Then `NFC_Handler_WriteStep(...)` and final `NFC_Handler_Sync(...)`.

This is explicit time-driven reservation writeback.

## 3.4 Runtime logic usage

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - `request_transport_plc()`:
    - Converts `step->TimeOfTransport` to `transportTimeSeconds`.
    - Uses that value as final payload token in transport request.
  - State condition near transport decision:
    - `else if (... TimeOfTransport > 0 && MyCellInfo.IDofCell != ...ProcessCellID) { ... }`
  - `State_Transport`:
    - Contains disabled guard:
      - `if (ActualTime < ...TimeOfTransport && false)` (dead code due to `&& false`)
    - Still logs `"Jeste nezacal cas transportu..."` in that dead branch.

## 3.5 OPC UA payload mapping

- `main/app.c` in `request_transport_plc()`:
  - `snprintf(inputMsg, "%s/%u/%u/%u/%u", sr_id, localCellId, step->TypeOfProcess, step->ParameterProcess1, transportTimeSeconds);`
  - Comment explicitly states old variant:
    - `/* Variant 2: transport payload p2 carries TimeOfTransport as uint16 seconds. */`

## 3.6 Conditions / delays / simulation / logging / debug

Time-specific transport semantics appear in:
- `request_transport_plc()` logs:
  - `TRANSPORT_PLC payload transportTime=...`
  - `TRANSPORT_PLC payload final field=...`
- `State_Transport` debug (disabled branch):
  - `"Jeste nezacal cas transportu ... Cas transportu ..."`
- `ReserveAllOfferedReservation()` debug:
  - `"Zapisuji cas transportu..."`
  - `"Zapisuji casy rezervaci."`

---

## 4. All locations building transport payload

## 4.1 Transport PLC payload builder (critical)

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - Function: `request_transport_plc(const char *sr_id, uint16_t localCellId, const TRecipeStep *step, ...)`
  - Current payload format:
    - token0 = `sr_id`
    - token1 = `localCellId`
    - token2 = `step->TypeOfProcess`
    - token3 = `step->ParameterProcess1`
    - token4 = `transportTimeSeconds` (derived from `TimeOfTransport`)

This is the primary place that must be changed for source/target transport contract.

## 4.2 Cross-cell target reservation payload builder

- `main/app.c`
  - `build_target_action_message(step, sr_id, localCellId, outMsg, ...)`
  - Current format:
    - token0 = `sr_id`
    - token1 = `localCellId`
    - token2 = `step->TypeOfProcess`
    - token3 = `step->ParameterProcess1`
    - token4 = `step->ParameterProcess2`

This is not transport-PLC payload; this is target-cell AAS action reservation. Keep distinct.

## 4.3 Local AAS action payload builder

- `main/app.c`
  - `build_local_aas_action_message()`
  - Current format:
    - token0 = `sr_id`
    - token1 = `priority` (0)
    - token2 = `material/classifier` (`TypeOfProcess`)
    - token3 = `ParameterProcess1`
    - token4 = `ParameterProcess2`

This is also not transport-PLC payload, but it proves token semantics are context-dependent.

---

## 5. How source cell can be determined

Source/current cell is deterministically known in runtime:

- `main/app.c`
  - Global: `CellInfo MyCellInfo;`
  - Boot initialization in `app_main()`:
    - `MyCellInfo.IDofCell = id_interpretter;` (loaded from NVS key `ID_Interpretter`)
    - endpoint derived by `assign_local_endpoint_from_cell_id(MyCellInfo.IDofCell)`
  - All transport/target flows pass/use `MyCellInfo.IDofCell` as local cell.

In transport request:
- `request_transport_plc(sr_id, MyCellInfo.IDofCell, step, ...)`
- So source cell already exists as direct input and can map to new `P1` without additional discovery.

---

## 6. How target cell is currently determined

Target cell is derived through multiple mechanisms:

## 6.1 Runtime target resolver for AAS transport gate

- `main/app.c`
  - `resolve_runtime_target_cell(handler, myCellId)`
  - priority order:
    1. `step->ProcessCellID` if non-zero and not local
    2. owner mapping from `resolve_owner_cell_id_from_process_type(step->TypeOfProcess)` if non-local

## 6.2 Explicit target resolution before remote reservation

- `main/app.c`
  - `reserve_remote_target()`:
    - tries `resolve_target_cell_for_step(...)`
    - fallback `resolve_next_target_cell(...)`
    - then performs remote GetSupported/ReserveAction on resolved target endpoint
    - stores target in transport gate (`transport_gate_set(... targetCellId)`).

## 6.3 Legacy transport reservation selection

- `main/app.c` in `State_Poptavka_Transporty`:
  - calls `GetWinningCell(..., Transport, param1 or ProcessCellID, 0, ...)`
  - writes selected `TransportCellID` and reservation ID into step.

## 6.4 Existing fields carrying target-like semantics

- `TRecipeStep.ProcessCellID` = target process cell (most direct destination for next operation)
- `TRecipeStep.TransportCellID` = chosen transport resource cell, not destination process cell

Important: under new requirement, `target/destination cell` should map to process destination cell, not transport resource cell (unless PLC contract explicitly says otherwise). This point is **uncertain without PLC-side contract text in code**.

---

## 7. What exactly must change in firmware

## 7.1 Definitely required changes

1) Transport PLC payload composition in `request_transport_plc()` (`main/app.c`)
- Replace time-based token with destination cell ID.
- Required semantic target:
  - `P1 = source/current cell` (already `localCellId`)
  - `P2 = target/destination cell` (must be resolved cell ID, likely process destination)
- Remove conversion block from `TimeOfTransport` -> `transportTimeSeconds`.
- Update payload logs accordingly (no "transportTime").

2) Transport state-entry condition using `TimeOfTransport` (`main/app.c`)
- Existing branch:
  - `... TimeOfTransport > 0 && MyCellInfo.IDofCell != ...ProcessCellID`
- This encodes time-driven reservation semantics.
- Must be reworked to gate by source/target/transport-gate readiness instead of reservation time presence.

3) Dead but misleading time gate/log in `State_Transport` (`main/app.c`)
- `if (ActualTime < ...TimeOfTransport && false)` is functionally dead.
- Must be removed or updated to avoid preserving obsolete time-driven semantics.

4) Transport payload/debug wording
- Update messages in `request_transport_plc()`:
  - `TRANSPORT_PLC payload transportTime=...`
  - `TRANSPORT_PLC payload final field=...`
- Replace with explicit source/target logging to align operations and diagnostics.

## 7.2 Likely required changes

1) `reserve_remote_target()` / transport call parameterization (`main/app.c`)
- Today `request_transport_plc()` only gets `step`, not explicit resolved target cell.
- New model needs explicit `targetCellId` for payload P2.
- Likely API/signature adjustment needed so transport payload uses the same target used by gate/reservation.

2) Legacy reservation writeback behavior in `ReserveAllOfferedReservation()` (`NFC_recipes.c`)
- It writes `TimeOfTransport` from reservation response.
- If firmware no longer uses transport time, keeping writes is not functionally required.
- Might still be retained for backward compatibility/auditing, but semantically obsolete.

3) Legacy transition logic depending on `TimeOfTransport > 0`
- There may be behavior coupling in fallback path when `USE_PLC_AAS_FLOW` is bypassed.
- If legacy path is still expected to run, this condition likely must be replaced with reservation-ID or gate-based condition.

## 7.3 Optional cleanup

1) Keep struct field but mark deprecated
- `TRecipeStep.TimeOfTransport` in `NFC_reader.h` can remain for binary layout compatibility.
- Add comment/deprecation note in future code update (not in this report task).

2) Legacy log text cleanup in `NFC_recipes.c`
- Strings about writing reservation "times" can be renamed if kept only for process reservation metadata.

3) Documentation alignment
- Comments in `main/app.c` referencing "Variant 2 ... TimeOfTransport as uint16" should be updated.

---

## 8. What can remain unchanged

- Reservation ID mechanisms:
  - `ProcessCellReservationID`, `TransportCellReservationID`
  - `DoReservation()`, `IsDoneReservation()`, `Reserve()`, `GetInquireIsValid()`
  - These support reservation integrity and are independent from transport-time payload.

- Source-cell identity and endpoint initialization:
  - `MyCellInfo.IDofCell` from NVS
  - `assign_local_endpoint_from_cell_id()`

- Target resolution helpers (mostly):
  - `resolve_target_cell_for_step()`
  - `resolve_next_target_cell()`
  - `resolve_runtime_target_cell()`
  - They already provide destination candidate IDs needed for new P2.

- OPC generic call layer:
  - `OPC_CallAasMethod()`, `OPC_GetSupported()`, `OPC_ReserveAction()`, `OPC_GetStatus()`
  - These pass opaque 5-field `InputMessage`; transport semantics are composed upstream in `app.c`.

---

## 9. Risks / compatibility concerns

1) **Payload token meaning drift across methods (high risk)**
- In same firmware, token1 means:
  - local cell (`build_target_action_message`, `request_transport_plc`)
  - priority (`build_local_aas_action_message`)
- Changing transport token semantics without strict endpoint-scoped contract can break PLC interpretation.

2) **Destination cell ambiguity (high risk, currently uncertain)**
- Candidate target values:
  - `ProcessCellID` (process destination)
  - resolved owner cell by process type
  - `TransportCellID` (transport resource, likely wrong as destination)
- Firmware suggests destination should be process owner/ProcessCellID; confirm with transport PLC contract.

3) **Legacy path behavior if AAS flow disabled/falls through (medium risk)**
- Some legacy branches still use `TimeOfTransport` checks.
- If not updated, behavior may diverge between AAS and fallback paths.

4) **NFC binary layout compatibility (medium risk)**
- Removing/reordering `TRecipeStep` fields would change serialized tag layout.
- Not required for payload migration; keep layout stable.

5) **Monitoring observability gap during rollout (medium risk)**
- Current logs emit transport-time payload details.
- Without replacing these with source/target logs, debugging post-migration gets harder.

---

## 10. Recommended implementation order

1) **Transport payload contract switch first**
- Update `request_transport_plc()` to emit source/target payload fields.
- Ensure target cell input is explicit and consistent with gate-resolved target.

2) **Align transport gating and branch conditions**
- Replace `TimeOfTransport`-based branch condition with gate/reservation-target condition.
- Remove dead time-gate branch in `State_Transport`.

3) **Update diagnostics**
- Logs, debug labels, comments: time -> source/target semantics.

4) **Review legacy reservation time writeback**
- Decide to keep as backward-compatible metadata or stop writing `TimeOfTransport`.
- If kept, mark as unused by transport orchestration.

5) **Validate fallback/legacy behavior**
- Verify both `USE_PLC_AAS_FLOW` path and fall-through legacy path maintain consistent decisions.

---

## 11. Final checklist of code areas to modify

### Definitely required

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - `request_transport_plc()`
    - remove `TimeOfTransport` conversion block
    - change payload token mapping to source/target cell IDs
    - update transport payload logs
  - state decision branch containing:
    - `TimeOfTransport > 0 && MyCellInfo.IDofCell != ...ProcessCellID`
  - `State_Transport` branch with:
    - `ActualTime < ...TimeOfTransport && false`
    - obsolete time-based debug text

### Likely required

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - call chain around:
    - `reserve_remote_target()`
    - `transport_gate_set()`
    - `request_transport_plc(...)`
  - ensure `targetCellId` used for payload P2 is the same resolved destination used by gating.

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
  - `ReserveAllOfferedReservation()`
    - line setting `tempStep.TimeOfTransport = tempReservation.TimeOfReservation;`
    - decide keep/remove based on backward-compatibility policy.

### Optional cleanup

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
  - `TRecipeStep.TimeOfTransport` comment/deprecation note only (no layout change).

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - comments/log text referencing "Variant 2" and transport-time payload.

---

## Evidence note

This analysis is based on direct inspection of:
- `ESP32_Firmware_ASS_Interpreter/main/app.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.h`
- `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`
- `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.h`

Uncertain points are explicitly marked as uncertain; no assumptions were promoted to definitive change requirements.
