# Final Transport Reader Call Sequence

## Exact reader call order for transport

Scope: production-cell reader calling remote transport provider PLC AAS endpoint.  
Transport cell has no NFC reader; all calls are initiated by production-cell reader firmware.

Recommended order (target-first, then transport):

1. `GetSupported` on target production provider (target action payload, existing target mapping).
2. `ReserveAction` on target production provider.
3. `GetSupported` on fixed transport provider using final transport payload.
4. `ReserveAction` on fixed transport provider using final transport payload.
5. Poll `GetStatus(sr_id)` on transport provider until in-progress/terminal condition per existing logic.
6. Poll `GetStatus(sr_id)` on target provider until takeover/in-progress condition per existing logic.
7. On partial failure (target reserved but transport reserve fails), call `FreeFromQueue(sr_id)` on target provider.

## Exact example messages for GetSupported / ReserveAction / GetStatus

Assume:

- `sr_id = 1177189415`
- `priority = 0`
- final preferred transport tokens = `0/0/0`

### Transport provider calls (preferred mapping)

- `GetSupported` input:
  - `1177189415/0/0/0/0`
- `ReserveAction` input:
  - `1177189415/0/0/0/0`
- `GetStatus` input:
  - `1177189415`

Expected output style (current PLC behavior):

- `GetSupported` -> `support:<v>_position:<p>`
- `ReserveAction` -> `Success` or `Error:<hex>`
- `GetStatus` -> `position:<n>` or `inProgress` or `Error:<hex>`

### Transport provider calls (fallback route-coded mapping)

Assume source cell `2`, target cell `5`:

- `GetSupported` input:
  - `1177189415/0/0/2/5`
- `ReserveAction` input:
  - `1177189415/0/0/2/5`
- `GetStatus` input:
  - `1177189415`

Use only when PLC support logic accepts route-coded token3/token4.

## Notes for later implementation

- Keep `sr_id` stable across target and transport provider calls for the same product instance.
- Use identical reader firmware on all production cells; only endpoint/config differs per deployment.
- Keep `priority=0` initially; only raise if transport QoS policy is introduced and PLC buckets are validated.
- Parse `GetSupported` and reserve only when `support > 0`.
- Preserve existing rollback rule: if transport reserve fails after target reserve success, release target reservation with `FreeFromQueue(sr_id)`.
- Do not introduce transport-reader behavior in transport cell firmware (none exists by design).

## Explicit uncertainties

- Current `GetStatus` does not expose explicit `done`; completion detection may rely on queue disappearance or external conditions.
- Whether `FreeFromPosition(sr_id)` is required in this transport path is deployment-dependent and not fully confirmed in current reports.
- Fallback route-coded payload requires PLC-side support-range/config confirmation.
