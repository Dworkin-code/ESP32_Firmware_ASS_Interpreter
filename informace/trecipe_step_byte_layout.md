# TRecipeStep Byte Layout

## 1. Struct definition

### Source files
- Struct definition: `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
- Process type enum values: `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`
- Runtime use of fields (`TypeOfProcess`, `ParameterProcess1`, `ParameterProcess2`): `ESP32_Firmware_ASS_Interpreter/main/app.c`
- NFC byte loading into struct memory: `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c` (`NFC_LoadTRecipeStep`, `NFC_LoadTRecipeSteps`)

### Exact struct (firmware)
```c
typedef struct __attribute__((packed))
{
  uint8_t ID;
  uint8_t NextID;
  uint8_t TypeOfProcess;
  uint8_t ParameterProcess1;
  uint16_t ParameterProcess2;
  uint8_t PriceForTransport;
  uint8_t TransportCellID;
  uint16_t TransportCellReservationID;
  uint8_t PriceForProcess;
  uint8_t ProcessCellID;
  uint16_t ProcessCellReservationID;
  UA_DateTime TimeOfProcess;
  UA_DateTime TimeOfTransport;
  bool NeedForTransport:1;
  bool IsTransport:1;
  bool IsProcess:1;
  bool IsStepDone:1;
} TRecipeStep;
```

### Packing / alignment
- `__attribute__((packed))` is applied directly on `TRecipeStep`.
- NFC read path copies bytes directly into the struct memory (`*((uint8_t*)aCardInfo->sRecipeStep + IndexovaPosun) = ...`), so on-tag byte order must match this exact memory layout.

### Total size
- `sizeof(TRecipeStep)` is used as `TRecipeStep_Size` in firmware.
- Field-size sum in this layout is **31 bytes**:
  - scalar fields up to `ProcessCellReservationID`: 14 bytes
  - `TimeOfProcess` (`UA_DateTime`): 8 bytes
  - `TimeOfTransport` (`UA_DateTime`): 8 bytes
  - bitfields (`NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`): 1 byte storage unit
  - total: 14 + 8 + 8 + 1 = 31

## 2. Byte offset table

| Byte offset | Field | Size (bytes) | Meaning |
|---:|---|---:|---|
| 0 | `ID` | 1 | Step ID |
| 1 | `NextID` | 1 | Next step ID |
| 2 | `TypeOfProcess` | 1 | Process type selector |
| 3 | `ParameterProcess1` | 1 | Process parameter 1 |
| 4-5 | `ParameterProcess2` | 2 | Process parameter 2 (`uint16_t`, little-endian) |
| 6 | `PriceForTransport` | 1 | Budget/price field for transport |
| 7 | `TransportCellID` | 1 | Reserved transport cell ID |
| 8-9 | `TransportCellReservationID` | 2 | Transport reservation ID (`uint16_t`, little-endian) |
| 10 | `PriceForProcess` | 1 | Budget/price field for process |
| 11 | `ProcessCellID` | 1 | Reserved process cell ID |
| 12-13 | `ProcessCellReservationID` | 2 | Process reservation ID (`uint16_t`, little-endian) |
| 14-21 | `TimeOfProcess` | 8 | Process timestamp (`UA_DateTime`) |
| 22-29 | `TimeOfTransport` | 8 | Transport timestamp (`UA_DateTime`) |
| 30 | bitfield byte | 1 | Flags: `NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone` |

### `TypeOfProcess` numeric values used by firmware
From `enum ProcessTypes` in `NFC_recipes.h`:
- `0 = ToStorageGlass`
- `1 = StorageAlcohol`
- `2 = StorageNonAlcohol`
- `3 = Shaker`
- `4 = Cleaner`
- `5 = SodaMake`
- `6 = ToCustomer`
- `7 = Transport`
- `8 = Buffer`

## 3. Decoding of the provided raw step

Provided 31-byte step:
`20 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 2 0 1 20 0 0`

Offset-by-offset:
- Byte 0 -> `ID = 20`
- Byte 1 -> `NextID = 0`
- Byte 2 -> `TypeOfProcess = 0`
- Byte 3 -> `ParameterProcess1 = 0`
- Bytes 4-5 -> `ParameterProcess2 = 0`
- Byte 6 -> `PriceForTransport = 0`
- Byte 7 -> `TransportCellID = 0`
- Bytes 8-9 -> `TransportCellReservationID = 0`
- Byte 10 -> `PriceForProcess = 0`
- Byte 11 -> `ProcessCellID = 0`
- Bytes 12-13 -> `ProcessCellReservationID = 0`
- Bytes 14-21 -> `TimeOfProcess = 0`
- Bytes 22-29 -> `TimeOfTransport` bytes = `0 0 0 2 0 1 20 0` (non-zero timestamp-like payload)
- Byte 30 -> flags byte = `0` (`NeedForTransport=0`, `IsTransport=0`, `IsProcess=0`, `IsStepDone=0` in usual GCC layout)

Explicitly requested fields from this raw step:
- `ID = 20`
- `NextID = 0`
- `TypeOfProcess = 0`
- `ParameterProcess1 = 0`
- `ParameterProcess2 = 0`
- `ProcessCellID = 0`
- `TransportCellID = 0`
- Flags/reservation fields:
  - `TransportCellReservationID = 0`
  - `ProcessCellReservationID = 0`
  - flag byte (offset 30) = `0`

## 4. Why firmware sees TypeOfProcess=0

Firmware reads `TypeOfProcess` from **byte offset 2** of each `TRecipeStep`.

In the provided raw step, byte offset 2 is `0`, so firmware correctly resolves:
- `TypeOfProcess = 0` -> `ToStorageGlass`.

The non-zero bytes (`2 0 1 20`) are located at offsets 25-28, which belong to `TimeOfTransport`, not to `TypeOfProcess`/`ParameterProcess*`.  
So the generator appears to place semantic step data at the end of the 31-byte record, while firmware expects it at the beginning (offsets 2-5).

## 5. Correct encoding example for StorageNonAlcohol 20 ml

Requested concrete example for step 1 meaning:
- `TypeOfProcess = StorageNonAlcohol (2)`
- `ParameterProcess1 = 20`

One valid 31-byte encoding (example values for IDs, all other fields zero):
- `ID = 1`, `NextID = 2`
- bytes:

`1 2 2 20 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0`

Notes:
- If you intend **Water 20 ml** in current firmware semantics from `GetRecipeStepByNumber`, firmware typically uses:
  - `TypeOfProcess = 2` (`StorageNonAlcohol`)
  - `ParameterProcess1 = Water (0)`
  - `ParameterProcess2 = 20`
  - in bytes: `... 2 0 20 0 ...` at offsets 2..5.

## 6. Final conclusion

- The firmware `TRecipeStep` layout is packed, fixed, and 31 bytes.
- `TypeOfProcess` is unambiguously at byte offset 2.
- The provided raw step has offset 2 = 0, so firmware correctly interprets it as `ToStorageGlass`.
- The observed mismatch is encoding-order mismatch on the producer side, not NFC transport corruption.
