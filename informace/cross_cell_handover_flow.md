# Cross-cell handover runtime flow

## Exact state-machine trigger point

Remote reservation starts in:

- `main/app.c`
- state `State_Vyroba_SpravneProvedeni`
- switch branch where `IsDoneReservation(...)` returns `1` (local process finished successfully)

At this moment, local processing is considered done for current step, and the next production step can be prepared remotely.

## Step-by-step flow

1. Reader detects local process completion (`Process je hotov`).
2. Reader builds `sr_id` from NFC UID (`OPC_BuildSrIdFromUid`).
3. Reader determines next step index from recipe graph:
   - prefers `currentStep.NextID`
   - fallback to `current + 1` when needed
4. Reader resolves candidate target production cell for next step type:
   - runtime discovery via `GetCellInfoFromLDS(nextStep.TypeOfProcess, ...)`
   - skip local cell (`MyCellInfo.IDofCell`)
   - prefer `nextStep.ProcessCellID` if already specified
5. Reader builds AAS `InputMessage`:
   - `sr_id/0/TypeOfProcess/ParameterProcess1/ParameterProcess2`
6. Reader sends remote `GetSupported(InputMessage)`.
7. If support is positive:
   - reader sends remote `ReserveAction(InputMessage)`.
8. If reservation accepted:
   - reader optionally polls `GetStatus(sr_id)` for visibility.
9. Reader logs remote reservation result.
10. Existing local write-back and state transitions continue unchanged.

## Success path

- local process finished
- next target cell resolved (remote, not local)
- `GetSupported` returns supported capacity
- `ReserveAction` returns non-error
- optional `GetStatus` returns visible status
- log indicates remote reservation success

## Failure path

Failure is logged and does not introduce transport flow changes in this milestone:

- next target cannot be resolved
- target resolves to local cell (remote handover skipped)
- Ethernet semaphore unavailable
- `GetSupported` call fails or returns unsupported/error
- `ReserveAction` fails or returns `Error:*`
- `GetStatus` poll unavailable (logged only)

In failure cases, the new remote handover step exits safely and original flow continues (no transport implementation added).
