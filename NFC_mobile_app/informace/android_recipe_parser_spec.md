### Android Firmware-Exact Recipe Parser/Writer Specification

This document defines how the Android NFC app parses and writes recipe data so that it matches the ESP32 firmware implementation exactly. The only source of truth for the binary format is the firmware analysis in `firmware_recipe_format_report.md`.

---

### 1. Binary Layout Overview

- **Header size**: 12 bytes (`TRecipeInfo`).
- **Step size**: 31 bytes (`TRecipeStep`).
- **Memory layout**:
  - Bytes `0..11`: header.
  - Bytes `12..(12 + RecipeSteps * 31 - 1)`: consecutive steps.
  - No padding between fields or between header and steps.
- **Total encoded size for N steps**:
  - `encodedRecipeSize = 12 + RecipeSteps * 31`.

All multi-byte integers are stored **little-endian**, matching the ESP32 and the C firmware implementation.

---

### 2. Header Structure (`TRecipeInfo`)

Android represents the firmware header as:

```kotlin
data class FirmwareRecipeInfo(
    val id: Int,               // uint8
    val numOfDrinks: Int,      // uint16 LE
    val recipeSteps: Int,      // uint8
    val actualRecipeStep: Int, // uint8
    val actualBudget: Int,     // uint16 LE
    val parameters: Int,       // uint8
    val rightNumber: Int,      // uint8
    val recipeDone: Boolean,   // bool (1 byte)
    val checksum: Int          // uint16 LE
)
```

Field layout on the wire (offsets from start of header, sizes in bytes):

| offset | size | field             | notes                                        |
|--------|------|-------------------|----------------------------------------------|
| 0      | 1    | `ID`              | `id`                                         |
| 1      | 2    | `NumOfDrinks`     | `numOfDrinks` (uint16 LE)                   |
| 3      | 1    | `RecipeSteps`     | `recipeSteps`                               |
| 4      | 1    | `ActualRecipeStep`| `actualRecipeStep`                          |
| 5      | 2    | `ActualBudget`    | `actualBudget` (uint16 LE)                  |
| 7      | 1    | `Parameters`      | `parameters`                                |
| 8      | 1    | `RightNumber`     | `rightNumber` (`ID + RightNumber == 255`)   |
| 9      | 1    | `RecipeDone`      | `recipeDone` (`0` = false, non-zero = true) |
| 10     | 2    | `CheckSum`        | `checksum` (uint16 LE)                      |

The header is always exactly 12 bytes.

---

### 3. Step Structure (`TRecipeStep`)

Android represents the firmware step as:

```kotlin
data class FirmwareRecipeStep(
    val id: Int,
    val nextId: Int,
    val typeOfProcess: Int,
    val parameterProcess1: Int,
    val parameterProcess2: Int,          // uint16 LE
    val priceForTransport: Int,
    val transportCellId: Int,
    val transportCellReservationId: Int, // uint16 LE
    val priceForProcess: Int,
    val processCellId: Int,
    val processCellReservationId: Int,   // uint16 LE
    val timeOfProcess: Long,             // 64-bit LE
    val timeOfTransport: Long,           // 64-bit LE
    val needForTransport: Boolean,       // bit 0 of flags byte
    val isTransport: Boolean,            // bit 1 of flags byte
    val isProcess: Boolean,              // bit 2 of flags byte
    val isStepDone: Boolean              // bit 3 of flags byte
)
```

Field layout on the wire (offsets from start of step, sizes in bytes):

| offset | size | field                        |
|--------|------|------------------------------|
| 0      | 1    | `ID`                         |
| 1      | 1    | `NextID`                     |
| 2      | 1    | `TypeOfProcess`              |
| 3      | 1    | `ParameterProcess1`          |
| 4      | 2    | `ParameterProcess2` (uint16 LE) |
| 6      | 1    | `PriceForTransport`          |
| 7      | 1    | `TransportCellID`            |
| 8      | 2    | `TransportCellReservationID` (uint16 LE) |
| 10     | 1    | `PriceForProcess`            |
| 11     | 1    | `ProcessCellID`              |
| 12     | 2    | `ProcessCellReservationID` (uint16 LE) |
| 14     | 8    | `TimeOfProcess` (64-bit LE)  |
| 22     | 8    | `TimeOfTransport` (64-bit LE)|
| 30     | 1    | Flags byte (4 bits used)     |

Flags byte (offset 30):

- Bit 0 (`0x01`): `NeedForTransport`
- Bit 1 (`0x02`): `IsTransport`
- Bit 2 (`0x04`): `IsProcess`
- Bit 3 (`0x08`): `IsStepDone`

Total step size: **31 bytes**.

---

### 4. Parser Logic

The firmware-compatible parser operates on the linear NFC memory dump (header + steps) produced by `NfcTagReader`:

1. **Read header**  
   - Take the first 12 bytes (`0..11`) and decode them as `FirmwareRecipeInfo` using little-endian decoding for all multi-byte fields.

2. **Determine step count**  
   - `stepCount = info.recipeSteps` (uint8).
   - If `stepCount < 0` or `stepCount > 64`, the recipe is treated as invalid/empty.

3. **Ensure enough bytes for steps**  
   - Required bytes for all declared steps:  
     `requiredBytes = FW_HEADER_SIZE + stepCount * FW_STEP_SIZE`  
     where `FW_HEADER_SIZE = 12`, `FW_STEP_SIZE = 31`.
   - If the NFC dump length is `< requiredBytes`, parsing fails (Android does not guess or truncate steps; it requires the full step array described by the header).

4. **Parse steps**  
   - For each `i` in `0 until stepCount`:
     - `offset = 12 + i * 31`.
     - Decode a `FirmwareRecipeStep` from exactly 31 bytes starting at `offset`.
   - No reading beyond `stepCount` is performed, even if the NFC memory contains additional bytes.

5. **Checksum verification**  
   - Build a contiguous array of all step bytes: `stepBytes = NFC[12 .. 12 + stepCount*31)`.
   - Compute checksum over `stepBytes` using the firmware algorithm (see section 5).
   - Compare the computed checksum with `info.checksum`.

6. **Integrity check**  
   - Check `info.id + info.rightNumber == 255` as a basic integrity check.

7. **Empty recipe detection**  
   - Apply the firmware rules from `app.c` (see section 6) to decide whether the tag is logically empty.

The result is exposed as `FirmwareDecodedRecipe`, which includes:

- `info`: decoded header.
- `steps`: decoded steps.
- `rawBytes`: original NFC dump.
- `checksumValid`: stored vs computed checksum match.
- `integrityValid`: `ID + RightNumber == 255`.
- `isEmptyRecipe`: result of empty-tag rules.

---

### 5. Checksum Algorithm

The checksum is defined in firmware (`NFC_GetCheckSum`) as:

- Input bytes: **all step bytes only**, in order:
  - `stepBytes = NFC[12 .. 12 + RecipeSteps*31)`.
- Formula:

\[
\text{CheckSum} = \sum_{i=0}^{N-1} b[i] \cdot (i \bmod 4 + 1) \pmod{2^{16}}
\]

Where:

- \( b[i] \) is the i‑th step byte.
- \( N = 31 \times \text{RecipeSteps} \).

Android implementation:

```kotlin
fun firmwareComputeChecksum(stepBytes: ByteArray): Int {
    var sum = 0
    for (i in stepBytes.indices) {
        sum += (stepBytes[i].toInt() and 0xFF) * ((i % 4) + 1)
    }
    return sum and 0xFFFF
}
```

The resulting 16-bit checksum is stored in the header at offsets 10–11 (uint16 little-endian).

---

### 6. Empty-Tag Validation Rules

Android mirrors the firmware (`NFC_IsRecipeEmpty` in `app.c`) for determining whether a tag is considered **empty**:

A tag is EMPTY if **any** of the following holds:

1. `RecipeSteps == 0`.
2. `RecipeSteps > 64` (application-level maximum).
3. `ActualRecipeStep >= RecipeSteps` **and** `RecipeDone == false`.
4. First step and core header fields are zero:
   - `steps[0].TypeOfProcess == 0`
   - `steps[0].ParameterProcess1 == 0`
   - `steps[0].ParameterProcess2 == 0`
   - `steps[0].ProcessCellID == 0`
   - `steps[0].TransportCellID == 0`
   - `info.ID == 0`
   - `info.NumOfDrinks == 0`
   - `info.ActualBudget == 0`
   - `info.Parameters == 0`
   - `info.RightNumber == 0`

These rules are applied after successful header + step parsing; if parsing fails (e.g. insufficient bytes), the tag is also treated as invalid/empty from the app’s point of view.

---

### 7. Writer Logic (Firmware-Compatible)

Android writes recipe data in the same way as the ESP32 firmware:

1. **Serialize header**  
   - Build a 12-byte header buffer:
     - Write all fields as in section 2.
     - Set `RecipeSteps = steps.size`.
     - Compute `RightNumber = 255 - ID` and write it.
     - Temporarily set `CheckSum = 0`.

2. **Serialize steps**  
   - For each `FirmwareRecipeStep`:
     - Create a 31-byte buffer and fill all fields exactly as in section 3 (little-endian for multi-byte fields, flags byte at offset 30).
   - Concatenate all step buffers into a single `stepBytes` array.

3. **Compute checksum**  
   - Compute `checksum = firmwareComputeChecksum(stepBytes)`.

4. **Write checksum into header**  
   - Overwrite header bytes at offsets 10–11 with `checksum` in little-endian order.

5. **Build final stream**  
   - Final recipe byte stream = `headerBytes + stepBytes`.

6. **Write to NFC tag**  
   - Ultralight/NTAG: `writeUltralightRecipeBytes` writes this stream starting at page 8, padding any partial 4-byte page with zeros.
   - Classic: `writeClassicRecipeBytes` writes this stream to logical data blocks, padding any partial 16-byte block with zeros.
   - In both cases, the **remaining bytes of the last block** are explicitly filled with `0`, matching firmware behavior.

The firmware never includes bytes beyond `12 + RecipeSteps*31` in the checksum, and the Android writer follows the same rule.

---

### 8. Debug Output (Firmware-Style)

When reading a tag, Android emits debug output that mirrors the ESP32 firmware debugging:

- **Tag identity**:
  - Tag UID (hex).
  - Tag type (Classic vs Ultralight/NTAG), derived from UID length.
- **Header**:
  - Raw header bytes `0..11` (hex).
  - Parsed fields: `RecipeSteps`, `ActualRecipeStep`.
- **Checksum**:
  - Stored checksum (from header).
  - Computed checksum (from step bytes only).
- **Step bytes**:
  - Raw bytes for Step 0.
  - Raw bytes for Step 1 (if present).

This is implemented via a helper (`firmwareDebugDump`) which takes `TagMetadata` and `FirmwareDecodedRecipe` and builds a text block that can be logged or displayed in the debug UI.

---

### 9. Memory Offsets Summary

- **Header**:
  - Base = 0.
  - Size = 12 bytes.
- **Step k** (0-based):
  - Base offset = `12 + k * 31`.
  - Size = 31 bytes.
  - Flags byte offset within step = 30 (absolute NFC offset = `12 + k * 31 + 30`).
- **Total recipe region**:
  - From 0 to `12 + RecipeSteps * 31 - 1` inclusive.

The Android implementation strictly uses these offsets and sizes so that tags written by Android are fully compatible with the ESP32 firmware, and tags written by the ESP32 firmware are decoded correctly by Android.

