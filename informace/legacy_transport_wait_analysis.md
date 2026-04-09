# Overview

Legacy "wait for transport / wait until tag disappears" behavior is implemented in the reader state machine in `ESP32_Firmware_ASS_Interpreter/main/app.c`, primarily via `State_Transport` -> `State_WaitUntilRemoved` -> `State_Mimo_Polozena`.

The dedicated wait state is `State_WaitUntilRemoved` (declared in `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`). It is used after successful transport execution and also after some non-transport flows to force physical tag removal before continuing.

# File(s) and function(s)

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - `void State_Machine(void *pvParameter)`:
    - Top-level card presence pre-check (`Parametry->CardOnReader`) and state dispatch.
    - `case State_Transport:` performs transport request execution and transitions to wait-for-removal.
    - `case State_WaitUntilRemoved:` waits until tag disappears.
    - `case State_Mimo_Polozena:` handles initial tag appearance and routing into legacy/AAS branches.
  - `static bool transport_gate_matches_runtime(const THandlerData *handler, uint16_t myCellId)`:
    - Gate used before allowing transport-related legacy path execution.
  - `static void legacy_flow_guard_mark_transport_request_executed(...)` and related guard helpers:
    - Mark that transport request was executed in target-first bridge path before entering legacy routing.
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`
  - `enum StavovyAutomat`:
    - Contains exact state names (`State_Transport`, `State_WaitUntilRemoved`, `State_Mimo_Polozena`, etc.).

# State names involved

Primary legacy transport-wait states:

- `State_Poptavka_Transporty`
- `State_Rezervace`
- `State_Transport`
- `State_WaitUntilRemoved`
- `State_Mimo_Polozena`

Common upstream routing states that feed this path:

- `State_Inicializace_ZiskaniAdres`
- `State_Poptavka_Vyroba`
- `State_Vyroba_Objeveni` (post-transport arrival handling)

# Entry condition into legacy transport wait behavior

The specific legacy wait-for-removal behavior is entered by assigning:

- `RAF = State_WaitUntilRemoved;`

Main legacy transport entry to that wait state is in `case State_Transport` after successful transport reservation execution:

1. In `State_Transport`, code verifies transport is needed (`NeedForTransport != 0`), validates reservations, updates NFC step/info (`tempStep.IsTransport = 1`, budget update), then calls:
   - `DoReservation(&iHandlerData, Bunky, BunkyVelikost, false);`
2. If `DoReservation` succeeds (`Error == 0`):
   - Log: `[TRANSPORT_REQUEST] executed reason=transport_success ...`
   - `transport_gate_reset("successful_transport");`
   - `RAF = State_WaitUntilRemoved;`

How execution reaches `State_Transport` in legacy flow:

- Via routing in `State_Mimo_Polozena` / `State_Poptavka_Vyroba` / `State_Poptavka_Transporty` / `State_Rezervace`, when step metadata indicates transport is required (`NeedForTransport`, transport reservations, or transport process type), and gate checks pass (`transport_gate_matches_runtime(...)`).

# Exit condition from legacy transport wait behavior

Exit from `State_WaitUntilRemoved` occurs only when card/tag is no longer present:

- Condition: `if (!Parametry->CardOnReader)`
- Action:
  - debug log `"Zmizel"`
  - transition `RAF = State_Mimo_Polozena;`

No other transition is present in this case block; if the card is still present, state remains `State_WaitUntilRemoved`.

# How tag disappearance is detected

Tag presence is represented by `Parametry->CardOnReader`.

There are two relevant checks:

- Global pre-check at top of loop:
  - `if (!Parametry->CardOnReader && RAF != State_WaitUntilRemoved) { RAF = State_Mimo_Polozena; ... }`
  - This intentionally excludes `State_WaitUntilRemoved` so that this state handles removal explicitly itself.
- Dedicated wait-state check:
  - In `case State_WaitUntilRemoved`, disappearance is detected by `if (!Parametry->CardOnReader)`.

So the actual "wait until removed" detection is the `!Parametry->CardOnReader` condition inside `State_WaitUntilRemoved`.

# Which parts are reusable for the new transport PLC integration

After future transport PLC AAS calls succeed, the following legacy pieces are directly reusable:

- Start of wait-for-removal phase:
  - Reuse the exact transition `RAF = State_WaitUntilRemoved;` currently used after successful transport in `State_Transport`.
- Monitoring disappearance:
  - Reuse `case State_WaitUntilRemoved` with `if (!Parametry->CardOnReader)` polling logic.
- Resume after disappearance:
  - Reuse transition `RAF = State_Mimo_Polozena;` on disappearance; this re-enters normal tag-appearance/routing flow.

Bridge context already present for target-first:

- In AAS target-first path (`AAS_DECISION: REQUEST_TRANSPORT`), successful gating marks and routes back into legacy pipeline:
  - `legacy_flow_guard_mark_transport_request_executed(...)`
  - `RAF = State_Inicializace_ZiskaniAdres; RAFnext = State_Poptavka_Vyroba;`
- This is the integration point that currently hands control to legacy transport states, culminating in `State_Transport` and then `State_WaitUntilRemoved`.

# Risks / caveats if this logic is reused unchanged

- Wait can block indefinitely:
  - `State_WaitUntilRemoved` has no timeout/fallback; if `CardOnReader` stays true (sensor issue/mechanical hold), flow stalls.
- Trigger source is purely local reader presence:
  - No additional confirmation from transport PLC in this wait state; it only observes card disappearance.
- Shared use of `State_WaitUntilRemoved`:
  - Same state is also used by non-transport paths (e.g., empty-tag handling and local-process rescan guard), so semantics are broader than transport-only.
- Dependence on gate/guard correctness:
  - Entry into legacy transport branch in mixed target-first flow depends on `transport_gate_matches_runtime(...)` and legacy guard flags staying synchronized with current tag/step.
- Presence edge handling remains polling-based:
  - Behavior depends on periodic loop checks (`vTaskDelay(500 ms)` loop), so removal detection latency and transient bouncing are tied to this polling design.
