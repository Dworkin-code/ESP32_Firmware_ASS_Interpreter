## Post-step transport trigger report

### Old unconditional transition location

The old unconditional transition was in `ESP32_Firmware_ASS_Interpreter/main/app.c` in the local AAS success path right after:
- completion poll success
- `ActualRecipeStep` increment
- NFC write-back (`NFC_Handler_WriteStep`, `NFC_Handler_WriteSafeInfo`, `NFC_Handler_Sync`)

At that point, code always did:
- `AAS: transitioning to State_WaitUntilRemoved`
- `RAF = State_WaitUntilRemoved;`

### Where post-step decision was inserted

Post-step decision was inserted in the same local-success block, replacing the unconditional `State_WaitUntilRemoved` transition.

New logic now executes immediately after write-back success and:
- reads updated `ActualRecipeStep` and `RecipeSteps`
- checks if recipe is done
- if not done, inspects the active next step (`sRecipeStep[ActualRecipeStep]`)
- resolves owner cell by `resolve_owner_cell_id_from_process_type(TypeOfProcess)`
- compares resolved owner with local cell id (`MyCellInfo.IDofCell`)

### How next step owner is resolved

Owner resolution uses the existing function:
- `resolve_owner_cell_id_from_process_type(uint8_t typeOfProcess)`

No new owner-mapping mechanism was introduced. This keeps owner decision consistent with existing `State_Mimo_Polozena` AAS decision logic.

### How existing non-local transport flow is reused

When post-step decision detects a remote next step:
- it logs `POST_STEP_DECISION: NEXT_STEP_REMOTE -> REQUEST_TRANSPORT`
- sets `RAF = State_Mimo_Polozena`

From there, existing already-working non-local chain is reused unchanged:
- target PLC `GetSupported / ReserveAction / GetStatus` via `reserve_remote_target(...)`
- transport gate checks (`transport_gate_*`)
- transport PLC `GetSupported / ReserveAction / GetStatus` via `request_transport_plc(...)`
- transition to `State_WaitUntilRemoved`

### Confirmation about duplication

No second independent transport implementation was added.

Post-step logic only decides routing and re-enters existing orchestration state (`State_Mimo_Polozena`) so the same non-local transport path is executed.

### Expected example logs (local done -> next step remote)

```text
AAS: completion poll SUCCESS, entering write-back path
AAS: new ActualRecipeStep=1
AAS: step 0 done, write-back OK
POST_STEP_DECISION: ActualRecipeStep=1 RecipeSteps=4 TypeOfProcess=7
POST_STEP_DECISION: owner_cell_id=5 local_cell_id=3
POST_STEP_DECISION: NEXT_STEP_REMOTE -> REQUEST_TRANSPORT
... (next loop in State_Mimo_Polozena)
AAS_DECISION: REQUEST_TRANSPORT (...)
TARGET_RESERVE start ...
TRANSPORT_PLC start
TRANSPORT_PLC result=SUCCESS
TRANSPORT_PLC success -> entering State_WaitUntilRemoved
```
