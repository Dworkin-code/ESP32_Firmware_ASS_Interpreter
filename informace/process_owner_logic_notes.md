# Process owner logic notes

## Assumptions

- `MyCellInfo.IDofCell` loaded from NVS key `ID_Interpretter` is the authoritative local reader identity.
- Current mapping is one-to-one between `TypeOfProcess` and owning production cell ID:
  - 1..6 map to cells 1..6 respectively.
- The required confirmed mapping `TypeOfProcess=3 -> owner cell 3 (Shaker)` is valid.

## Uncertain mappings / future clarification

- If future recipes introduce process types outside `1..6`, they currently resolve as unknown owner (`0`) and will be treated as transport path.
- If any process type is served by multiple cells in future, the current helper will need extension (current implementation assumes a single owner cell per process type).

## Remaining limitations

- This fix only changes the early ownership decision in `State_Mimo_Polozena`.
- It does not redesign later reservation/transport/handover logic (kept intentionally unchanged).
- The mapping is currently static in firmware source; if production mapping changes, helper mapping must be updated accordingly.
