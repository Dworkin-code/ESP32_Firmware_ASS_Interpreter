# Unified Reader Runtime Configuration Proposal

## Required configuration for each reader

Each reader instance must be configured by data, not code forks.

Required items:
- `reader_id` (diagnostics identity)
- `local_cell_id` (maps to `MyCellInfo.IDofCell`)
- `local_provider_endpoint` (AAS URL for this cell)
- `transport_provider_id` (fixed cell ID for transport provider)
- `transport_provider_endpoint`
- `process_cell_map`:
  - mapping from logical/recipe process targets to `{cell_id, endpoint}`
- `priority_default` (if not provided by recipe)
- timeout settings:
  - `connect_timeout_ms`
  - `reserve_timeout_ms`
  - `transport_timeout_ms`
  - `target_takeover_timeout_ms`
- polling settings:
  - `status_poll_period_ms`
  - `max_retries`
  - `retry_backoff_profile`
- behavior flags:
  - `enable_free_from_position` (bool)
  - `enable_getsupported_precheck` (bool)
  - `strict_target_first` (bool; should stay true by default)

## What is derived from ID/configuration

At runtime, firmware derives:
- "am I target?" from `local_cell_id == target_cell_id`
- local endpoint from `local_cell_id`
- target endpoint from `process_cell_map[target_cell_id]`
- transport endpoint from fixed transport config
- whether transport is required from ID mismatch rule
- which retry/timeout behavior to apply from config profiles

No compile-time per-cell behavior should be necessary.

## What must stay generic in firmware

Firmware must keep generic:
- one common state machine for all readers
- one common AAS method wrapper (`GetSupported`, `ReserveAction`, `GetStatus`, `FreeFromQueue`, optional `FreeFromPosition`)
- one common reservation/rollback policy
- one common status polling engine
- one common message formatter (`sr_id/priority/material/parameterA/parameterB`)
- one common interpretation layer for method outputs (`Success`, `Error:*`, `position:*`, `inProgress`)

## What may differ between installations

Allowed installation-specific differences:
- IP addresses / OPC UA endpoints
- cell IDs and mapping tables
- transport provider fixed address/ID
- timeout and retry tuning (network/PLC speed dependent)
- role flags if cell topology differs

Not allowed as installation difference:
- custom firmware branches for specific cell numbers
- custom hardcoded process routing logic compiled per reader
- custom transport handshake logic for one location only

## Required runtime comparisons

Minimum comparisons at decision points:
1. `target_cell_id == local_cell_id`
2. `target_endpoint exists in process_cell_map`
3. transport provider configured and reachable
4. status/timeouts exceeded?
5. pending reservation exists for same `sr_id`?

These checks are sufficient for consistent behavior across all readers.

## Suggested minimal role model

To keep firmware generic but explicit, support small runtime roles:
- `ROLE_PRODUCTION_READER` (default)
- `ROLE_TRANSPORT_READER` (if physically attached to transport station)
- `ROLE_HYBRID` (if one station can process and transport)

Role changes only configuration gating of state transitions; code path remains shared.

## Information each reader must know at runtime

- own `local_cell_id`
- own provider endpoint
- fixed transport provider endpoint and ID
- process-cell routing map (target ID -> endpoint)
- supported role type(s)
- polling periods
- timeout budgets
- retry limits/backoff profile
- output parsing rules for AAS responses

## Not confirmed

- Final canonical source for `target_cell_id` extraction from recipe step is not confirmed.
- Whether PLC `GetStatus` alone is sufficient for all transport completion semantics is not confirmed.
- Whether one global transport provider endpoint is always reachable from all reader VLAN/subnets is not confirmed.
