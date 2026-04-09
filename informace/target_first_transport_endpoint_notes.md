# Target-First Transport Endpoint Notes

## How to use existing production PLC endpoint mapping
- Keep existing production mapping unchanged:
  - local endpoint assignment remains in `assign_local_endpoint_from_cell_id(...)`.
- Keep remote production endpoint resolution unchanged:
  - `resolve_next_target_cell(...)` returns target cell and `IPAdress`,
  - `normalize_cell_endpoint(...)` keeps endpoint format stable.
- Continue using the same mapping for target reservation calls (`GetSupported`, `ReserveAction`).

## How to integrate fixed transport PLC endpoint `192.168.168.64:4840`
- Add one dedicated transport endpoint constant in firmware runtime context:
  - `TRANSPORT_PLC_ENDPOINT = "192.168.168.64:4840"`.
- Use this constant only for transport PLC AAS calls.
- Do not route transport calls through production-cell candidate list.
- Keep transport endpoint independent from `MyCellInfo.IDofCell` local mapping.

## Assumptions
- Transport PLC exposes compatible AAS methods required by transport request flow.
- Input payload contract for transport is defined and stable for all readers.
- Shared transport PLC supports concurrent requests from multiple readers.
- Existing ethernet semaphore discipline is sufficient for endpoint switching.

## Open questions
- Which exact AAS methods and payload fields are required by transport PLC in final contract?
- Is there a supported release/cancel method for stuck reservations?
- What is authoritative transport success signal: `GetStatus` terminal token, callback, or both?
- Timeout values for:
  - target reservation validity,
  - transport reservation validity,
  - expected physical arrival window.
- Should transport retries keep original target reservation alive, or force re-reservation?
