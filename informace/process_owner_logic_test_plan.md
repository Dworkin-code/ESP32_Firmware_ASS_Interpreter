# Process owner logic test plan

## Test 1: Positive case (local cell 3, process type 3)

### Setup

- Ensure NVS key `ID_Interpretter` is `3`.
- Boot reader and verify startup log contains local ID 3 and Shaker endpoint.
- Use a tag where current recipe step has `TypeOfProcess = 3`.

### Expected decision logs

In `State_Mimo_Polozena` AAS decision logs, expect:

- `AAS_DECISION: TypeOfProcess=3 owner_cell_id=3 local_cell_id=3 => LOCAL_PROCESS`
- `AAS_DECISION: LOCAL_PROCESS`

And **must not** see:

- `AAS_DECISION: REQUEST_TRANSPORT (TypeOfProcess=3 ...)`

### Expected behavior

- Reader goes through local PLC AAS path (GetSupported / ReserveAction / completion polling / write-back).
- Reader does not route this step to `State_Poptavka_Vyroba` via request-transport shortcut.

## Test 2: Negative case (owner != local)

### Setup

- Keep NVS local cell ID = `3`.
- Use a tag where current step has `TypeOfProcess = 2`.

### Expected logs

- `AAS_DECISION: TypeOfProcess=2 owner_cell_id=2 local_cell_id=3 => REQUEST_TRANSPORT`
- `AAS_DECISION: REQUEST_TRANSPORT (TypeOfProcess=2 owner_cell_id=2 local_cell_id=3)`

### Expected behavior

- Reader follows existing transport/remote path (unchanged logic after decision point).

## Test 3: Another negative case (owner != local)

### Setup

- Keep NVS local cell ID = `3`.
- Use a tag where current step has `TypeOfProcess = 6`.

### Expected logs

- `AAS_DECISION: TypeOfProcess=6 owner_cell_id=6 local_cell_id=3 => REQUEST_TRANSPORT`

### Expected behavior

- Reader routes to transport/remote branch.

## Optional robustness test: unknown process type

- Use a recipe step with unmapped `TypeOfProcess` value (not 1..6).
- Expected: `owner_cell_id=0` and decision `REQUEST_TRANSPORT`.
