# Transport Time Variant 2 - Firmware Report

## Scope and constraints

- Changes were made only on ESP32 firmware side.
- No PLC code was modified.
- Payload shape remains unchanged: `sr_id/sourceCell/type/p1/p2` (5 fields).

## Where `TimeOfTransport` is read from

- The transport time value is stored in `TRecipeStep.TimeOfTransport` in `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`.
- It is written during reservation handling in `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`, where:
  - `tempStep.TimeOfTransport = tempReservation.TimeOfReservation;`
- Runtime payload composition then reads `step->TimeOfTransport` in `request_transport_plc(...)` in `ESP32_Firmware_ASS_Interpreter/main/app.c`.

## Firmware files and functions modified

- Modified file: `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - `build_target_action_message(...)`
    - Added debug log for production/target payload field `p2`:
      - `TARGET_PLC payload p2=%u`
  - `request_transport_plc(...)`
    - Changed transport payload `p2` source from `step->ParameterProcess2` to `step->TimeOfTransport`.
    - Added conversion to `uint16` seconds (with saturation to `UINT16_MAX`).
    - Added debug logs:
      - `TRANSPORT_PLC payload transportTime=%u`
      - `TRANSPORT_PLC payload final field=%u`

## Production/local/target behavior status

- Production/target payload content is unchanged:
  - `p2` is still `ParameterProcess2`.
- Local process payload builder is unchanged:
  - `p2` is still `ParameterProcess2`.
- No changes were made to target reserve flow, local process flow, transport gate logic, post-step decision logic, or field count.

## Transport payload behavior after change

- Transport payload still uses 5 fields:
  - `sr_id/sourceCell/type/p1/p2`
- For transport PLC calls:
  - `p2` is now `TimeOfTransport` converted to `uint16` seconds on firmware side.

## Payload examples

- Production PLC (unchanged):
  - `123456789/63/5/1/250`
  - Meaning: `sr_id=123456789`, `sourceCell=63`, `type=5`, `p1=1`, `p2=ParameterProcess2=250`

- Transport PLC (Variant 2):
  - `123456789/63/7/0/18`
  - Meaning: `sr_id=123456789`, `sourceCell=63`, `type=7`, `p1=0`, `p2=TimeOfTransportSeconds=18`

## Note for later manual PLC implementation

- The transport PLC should interpret the last payload field (`p2`) as transport time in **seconds**.
- The PLC should convert this value to **milliseconds** before writing to `"Simulace".Set_time_simulace`.
