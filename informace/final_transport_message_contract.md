# Final Transport Message Contract (ReserveAction / GetSupported)

## Executive summary

This document defines the final transport payload contract in the existing PLC AAS format:

`<sr_id>/<priority>/<material>/<parameterA>/<parameterB>`

Two mappings are proposed:

- **Preferred mapping (recommended for immediate reader implementation with minimal changes):** fixed transport profile values `0/0/0` for token2-4.
- **Fallback mapping (for richer PLC-side routing):** route-coded values in token3/token4 (source/destination), requiring PLC support-range updates.

The preferred mapping is the safest with current PLC AAS support logic because it is already known to return non-zero support on current systems.

## Existing PLC payload semantics

Based on current reports and PLC AAS method behavior:

- `token0` (`sr_id`): product/request ID (`id` in `ReserveAction`, `GetStatus`).
- `token1` (`priority`): queue priority (used by `PQueue_push`, `GetSupported` position estimate).
- `token2` (`material`): first support classifier (range-checked).
- `token3` (`parameterA`): second support classifier (`initialPosition/operation`, range-checked).
- `token4` (`parameterB`): third support classifier (`finalPosition/operationParameter`, range-checked).

Important: current PLC logic does not parse named transport semantics from token2-4; it only applies range-based support checks and queue handling.

## Preferred transport mapping

### Contract

- `sr_id` (token0): stable transport request ID for one physical product/tag instance.
- `priority` (token1): `0` by default (same behavior as current reader AAS calls).
- `material` (token2): `0` (fixed transport profile code).
- `parameterA` (token3): `0` (fixed transport profile code).
- `parameterB` (token4): `0` (fixed transport profile code).

Final payload used for transport precheck/reservation:

`<sr_id>/0/0/0/0`

### Field meaning for transport provider PLC

- `sr_id`: unique key for queue identity, deduplication, and status tracking.
- `priority`: queue bucket selector (normal = `0`).
- `material/parameterA/parameterB`: transport capability profile selector only (not route parameters), fixed to baseline profile accepted by current support logic.

### Why this is preferred

- Minimal reader change (no new encoding rules beyond selecting transport profile values).
- Compatible with current `GetSupported`/`ReserveAction` support checks.
- Compatible with current queue and `GetStatus(sr_id)` flow.
- Avoids immediate PLC parser redesign.

## Fallback transport mapping

### Contract

Use route-coded transport payload:

`<sr_id>/<priority>/<material=0>/<parameterA=source_cell_id>/<parameterB=target_cell_id>`

Example:

`1177189415/0/0/2/5`

### Field meaning for transport provider PLC

- `sr_id`: queue/status identity.
- `priority`: queue bucket.
- `material=0`: transport action class.
- `parameterA`: source production cell ID.
- `parameterB`: target production cell ID.

### When to use fallback

Use only if transport PLC must choose behavior explicitly by route from message tokens.

## Compatibility with current PLC AAS

### Preferred mapping compatibility

- **Support logic:** compatible if `(0,0,0)` remains in configured support ranges (already observed in current analysis).
- **Queue logic:** compatible; queue uniqueness and status polling continue to rely on `sr_id`.
- **GetStatus:** unchanged (`GetStatus(<sr_id>)`).
- **Reader flow:** unchanged method set and call order, only concrete token values fixed for transport.

### Fallback mapping compatibility

- **Support logic risk:** source/target cell IDs may fall outside support ranges for token3/token4 and return `support:0`.
- **Queue logic:** still compatible if reservation accepted.
- **Additional requirement:** PLC support ranges must include expected cell ID ranges or explicit mapping logic must be added.

## Required PLC changes if any

### For preferred mapping

- **Mandatory changes:** none, assuming current `(0,0,0)` remains supported.
- **Recommended validation:** confirm transport provider returns non-zero support for `<sr_id>/0/0/0/0` in `GetSupported`.

### For fallback mapping

At least one PLC-side change is required:

- Extend support-range constants for token3/token4 to include source/target cell IDs, or
- Add explicit semantic parsing in `GetSupported`/`ReserveAction` for route-coded transport.

Without this, fallback may be rejected as unsupported.

## Final recommendation

Adopt the **preferred mapping** as final implementation contract for now:

- `token0 (sr_id)`: stable request/product ID
- `token1 (priority)`: `0`
- `token2 (material)`: `0`
- `token3 (parameterA)`: `0`
- `token4 (parameterB)`: `0`

Transport payload string:

`<sr_id>/0/0/0/0`

This is the lowest-risk, minimal-change option consistent with current PLC AAS support and queue behavior.  
Keep the route-coded mapping as a controlled next step only if transport PLC later requires route semantics in token3/token4.

## Explicit uncertainties

- Not confirmed from current reports: whether transport provider needs route info (`source/target`) immediately for physical dispatch decisions.
- Not confirmed from current reports: exact configured numeric ranges on the transport PLC for support constants.
- Not confirmed from current reports: whether any deployment uses non-zero `priority` buckets for transport QoS.
