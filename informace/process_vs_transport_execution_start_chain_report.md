# Process vs Transport Execution-Start Chain Comparison

## 1. Executive summary

The queue/matching logic (`ReportProduct`, `ReserveAction`, `CheckAction`, and item removal) is effectively the same between `PLC_code` and `PLC_code -Transport`.  
The first behavioral divergence is in the execution interface wiring inside `Passive_AAS`: transport is wired to `CommBlock_SIM` (`SetReady`, `DW5`, etc.), while process is wired to `CommBlock` (`Set_Ready`, `ProductId`, etc.).  
In transport, `DeviceSlow` depends on status feedback (`Ready/Busy/Done/Error`) from that interface, but no active caller was found that drives `CommBlock_SIM` state transitions (only a standalone `Simulace1` definition was found). As a result, reservation succeeds, `Valid` can become TRUE, but execution cannot progress to Busy/Done.

## 2. Side-by-side comparison table

| Topic | Process PLC (`PLC_code`) | Transport PLC (`PLC_code -Transport`) | Same / Different |
|---|---|---|---|
| `ReportProduct` insert to reported queue | `program_block/AAS/OPC UA Methods/ReportProduct.scl`: parses ID, checks duplicate by `Queue_find`, inserts by `Queue_push` into `"RPQueue".ReportedProductsQueue` | `program_block/AAS/OPC UA Methods/ReportProduct.scl`: same code path (`Queue_find` + `Queue_push` into `"RPQueue".ReportedProductsQueue`) | Same |
| `ReserveAction` insert to PQueue | `program_block/AAS/OPC UA Methods/ReserveAction.scl`: parses item, duplicate check by `PQueue_find`, validates support, inserts by `PQueue_push` into `"PQueue".PQueue` | `program_block/AAS/OPC UA Methods/ReserveAction.scl`: same logic and same calls/conditions | Same |
| `CheckAction` queue matching | `program_block/Device Data/CheckAction.scl`: loops PQueue (`PQueue_get`), tries match in RP queue (`Queue_find`), sets `foundItem` + positions | `program_block/Device Data/CheckAction.scl`: identical logic/conditions | Same |
| `isFoundItem -> Valid` handoff | `program_block/AAS/Passive_AAS.scl`: `foundItem => #isFoundItem`, then `DeviceSlow_Instance(... Valid := #isFoundItem ...)` | `program_block/AAS/Passive_AAS.scl`: same handoff | Same |
| Device start handshake wiring | `program_block/AAS/Passive_AAS.scl`: wired to `"CommBlock".Idle/Ready/Busy/Done/Error` and command outputs to `"CommBlock".Set_Ready/Start/ResetDone/ProductId` | `program_block/AAS/Passive_AAS.scl`: wired to `"CommBlock_SIM".Idle/Ready/Busy/Done/Error` and outputs to `"CommBlock_SIM".SetReady/Start/ResetDone/DW5` | **Different (critical)** |
| Handshake state producer | No `Simulace1` function in process tree; `CommBlock` is the live interface used by `DeviceSlow` | `program_block/Simulace1.scl` exists (state-machine function for SetReady/Start/Ready/Busy/Done), but search found only definition, no call site in `PLC_code -Transport/program_block/*.scl` | **Different (critical)** |
| Queue removal lifecycle | `program_block/AAS/Passive_AAS.scl` + `program_block/AAS/low level functions/RemoveFinishedProduct.scl`: on falling edge of `deviceRunning`, remove from RP (`Queue_remove`) and PQueue (`PQueue_removeByPosition`) | Same call chain and same remove operations | Same |

## 3. Exact first behavioral difference

The earliest execution-start divergence is in `PLC_code -Transport/program_block/AAS/Passive_AAS.scl` at the `DeviceSlow` call wiring:

- Process: status/commands mapped to `"CommBlock"` (`Set_Ready`, `ProductId`).
- Transport: status/commands mapped to `"CommBlock_SIM"` (`SetReady`, `DW5`).

This is the first point where runtime behavior diverges after queue matching succeeds and `Valid` is passed into `DeviceSlow`.

## 4. Why process starts but transport does not

`DeviceSlow` requires feedback transitions in this order: `Ready -> Busy -> Done -> Idle`.  
In transport, `DeviceSlow` waits for those via `StsReady/StsBusy/StsDone` mapped from `CommBlock_SIM`.  
However, in available transport SCL files, the only logic that can drive those bits is `program_block/Simulace1.scl`, and no caller invocation was found (search found only the function declaration).  
So execution can reach `Valid=TRUE`, emit command pulses, but feedback never advances; therefore the state machine never reaches Busy/Done in practice.

## 5. Minimal fix recommendation

Use one consistent handshake channel end-to-end:

1. Either wire transport `Passive_AAS` back to the same live block as process (`CommBlock` mapping), **or**
2. Keep `CommBlock_SIM` mapping, but ensure `Simulace1` (or equivalent producer) is called cyclically with `CommBlock_SIM` in/out parameters so `Ready/Busy/Done/Idle` are actually driven.

Most minimal, lowest-risk change is to align transport `Passive_AAS` interface wiring with the known working process wiring unless there is a strict reason to keep simulation channel separation.

## 6. Confidence / uncertainty notes

- High confidence that `ReportProduct`, `ReserveAction`, `CheckAction`, and queue-removal lifecycle are not the root cause (code parity confirmed).
- High confidence that execution-start failure is in handshake/interface wiring, not in ID matching, because `Valid` source logic is identical.
- Medium confidence on final root-cause closure: this depends on whether transport currently compiles/runs the `Passive_AAS` version wired to `CommBlock_SIM` (editor/export state may differ). The observed failure mode exactly matches missing `CommBlock_SIM` state producer.
