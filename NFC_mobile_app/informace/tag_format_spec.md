# NFC Recipe Tag Format Specification

**Source:** Reverse engineering of ESP32 NFC reader firmware and real tag dumps in this folder.  
**Purpose:** Definitive reference for the mobile app's decode/encode logic.  
**Status:** Parts are inferred; sizes and offsets must match the deployed ESP32 firmware.

---

## 1. Tag types and technology

| Aspect | Value |
|--------|--------|
| Standard | ISO14443A (Type A) |
| Recipe format | Raw binary (no NDEF for recipe data) |
| Tag types | MIFARE Ultralight/NTAG (7-byte UID), MIFARE Classic (4-byte UID) |
| Detection | By UID length: 4 → Classic, 7 → Ultralight/NTAG |

**Android techs:** `NfcA`, `MifareUltralight`, `MifareClassic`, `Ndef` (not used for recipe payload).

---

## 2. Memory layout constants (from firmware)

| Constant | Value | Meaning |
|----------|--------|---------|
| `OFFSETDATA_ULTRALIGHT` | 8 | First **page** index of user/recipe data (page 8) |
| `OFFSETDATA_CLASSIC` | 1 | First **logical** data block index (physical block 2) |
| `PAGESIZE_ULTRALIGHT` | 4 | Bytes per Ultralight page |
| `PAGESIZE_CLASSIC` | 16 | Bytes per Classic block |

### 2.1 Ultralight / NTAG213

- **User data start:** Page 8 (bytes 0–3 of recipe stream = page 8; stream bytes 4–7 = page 9; etc.).
- **Logical block:** 16 bytes = 4 consecutive pages. Block index `i` → pages `8 + i*4` .. `8 + i*4 + 3`.
- **Writable pages:** User area only. **Do not write** pages 0–7 (UID, config) or lock/config pages (e.g. NTAG213 pages 40–44).
- **NTAG213 user memory:** 144 bytes = 36 pages (pages 4–39 are often cited; pages 40+ are lock/config). Safe recipe region: pages 8–39 (128 bytes). *Assumption: use pages 8–39; lock bytes may further restrict.*

### 2.2 MIFARE Classic

- **Logical block 0** → **physical block 2** (sector 0: block 0 = manufacturer, block 1 = data, block 2 = data, block 3 = trailer).
- **Block index mapping:** For logical index `i`, physical block = skip sector trailers. Formula: start at 2, then for each logical block increment physical; whenever next physical block index is multiple of 4 (trailer), increment again. So: logical 0→2, 1→3, 2→5, 3→6, 4→9, 5→10, …
- **Authentication:** Key B `FF FF FF FF FF FF` for data blocks.
- **Never read/write** sector trailer blocks (keys, access bits) from recipe logic.

---

## 3. Recipe stream layout (logical)

The tag holds a **contiguous byte stream**:

- **Bytes 0 .. HEADER_SIZE-1:** `TRecipeInfo` (recipe header).
- **Bytes HEADER_SIZE .. HEADER_SIZE + RecipeSteps * STEP_SIZE - 1:** `TRecipeStep[0 .. RecipeSteps-1]`.
- **RecipeSteps** comes from `TRecipeInfo.RecipeSteps`. No separate length prefix or magic.

### 3.1 Struct sizes (must match firmware)

| Struct | Size | Note |
|--------|------|------|
| `TRecipeInfo` | **12** bytes | Packed, little-endian. |
| `TRecipeStep` | **31** bytes | Packed: 14 bytes through ProcessCellReservationID, 8 TimeOfProcess, 8 TimeOfTransport, 1 flags byte (no padding). |

*Confirmed from real tag dumps: step data aligns when STEP_SIZE=31; flags byte at offset 30.*

---

## 4. TRecipeInfo (header) – byte map

**Size:** 12 bytes. **Endianness:** Little-endian for multi-byte fields.

| Byte offset | Field | Type | Description |
|-------------|--------|------|-------------|
| 0 | ID | uint8 | Recipe/card identifier |
| 1–2 | NumOfDrinks | uint16 | Number of drinks |
| 3 | RecipeSteps | uint8 | Number of TRecipeStep entries (0 allowed) |
| 4 | ActualRecipeStep | uint8 | Current step index |
| 5–6 | ActualBudget | uint16 | Budget value |
| 7 | Parameters | uint8 | Parameters byte |
| 8 | RightNumber | uint8 | **Must satisfy:** ID + RightNumber == 255 |
| 9 | RecipeDone | bool (1 byte) | Recipe completed flag |
| 10–11 | CheckSum | uint16 | Checksum over TRecipeStep[] only (see below) |

**Validation:** Tag is considered valid only if `ID + RightNumber == 255`. When encoding, always set `RightNumber = 255 - ID`.

---

## 5. Checksum algorithm

- **Stored in:** `TRecipeInfo.CheckSum` (bytes 10–11, uint16 LE).
- **Scope:** Only the **TRecipeStep[]** bytes (all steps concatenated). Header is not included.
- **Formula:**  
  `CheckSum = sum over i in [0, STEP_SIZE * RecipeSteps) of (step_byte[i] * ((i % 4) + 1))`  
  (accumulated as uint16).
- **RecipeSteps == 0:** CheckSum must be 0.

---

## 6. TRecipeStep – byte map

**Size:** 31 bytes (packed, no trailing padding). **Endianness:** Little-endian for multi-byte fields.

| Byte offset | Field | Type | Description |
|-------------|--------|------|-------------|
| 0 | ID | uint8 | Step ID |
| 1 | NextID | uint8 | Next step ID |
| 2 | TypeOfProcess | uint8 | Process type code |
| 3 | ParameterProcess1 | uint8 | Parameter 1 |
| 4–5 | ParameterProcess2 | uint16 | Parameter 2 |
| 6 | PriceForTransport | uint8 | Transport price |
| 7 | TransportCellID | uint8 | Transport cell ID |
| 8–9 | TransportCellReservationID | uint16 | Transport reservation ID |
| 10 | PriceForProcess | uint8 | Process price |
| 11 | ProcessCellID | uint8 | Process cell ID |
| 12–13 | ProcessCellReservationID | uint16 | Process reservation ID |
| 14–21 | TimeOfProcess | int64 (UA_DateTime) | Process timestamp |
| 22–29 | TimeOfTransport | int64 (UA_DateTime) | Transport timestamp |
| 30 | Flags | 1 byte, 4× 1-bit | NeedForTransport, IsTransport, IsProcess, IsStepDone |

**Flags byte (offset 30):** LSB first: bit0 = NeedForTransport, bit1 = IsTransport, bit2 = IsProcess, bit3 = IsStepDone.

### 6.1 Shaker step encoding

- **Shaker** is encoded as **TypeOfProcess = 3** (ProcessTypes enum in firmware; see NFC_recipes.h).
- **ParameterProcess1** = duration in **seconds** (0–255). The reader firmware uses this for the shake duration (e.g. debug message “Protrepani o dobe trvani %d s”).
- The tag does **not** store a “recipe step number” (e.g. 1–10). The firmware’s “case 6” in `GetRecipeStepByNumber` refers to a **hardcoded** recipe step list; the **on-tag value** for Shaker is the enum value **3**, not 6. Writing 6 would mean ToCustomer in the enum.

---

## 7. Mapping: Ultralight pages ↔ recipe stream

- Recipe **byte index** `b` (0-based) → **page index** = `8 + (b / 4)`, **byte-in-page** = `b % 4`.
- **Read:** Read pages 8, 9, 10, … and concatenate 4-byte payloads to form the stream.
- **Write:** Write only pages that contain recipe data. Split stream into 4-byte chunks and write to page `8 + i` for chunk index `i`. Do not write beyond last page that contains recipe bytes, and never write lock/config pages.

**NTAG213 safe recipe page range:** Pages 8–39 (inclusive) = 32 pages = 128 bytes. Max recipe stream = 128 bytes (header 12 + steps: 12 + 3×31 = 105 → 3 full steps, 23 bytes unused).

---

## 8. Mapping: MIFARE Classic blocks ↔ recipe stream

- **Logical block index** `i` (0-based) → **physical block** via `NFC_GetMifareClassicIndex(i)`:
  - Start at physical block 2.
  - For each logical index 0, 1, 2, …: physical block = next data block (skip any block index that is 4k+3, i.e. sector trailers).
- **Stream:** Logical block 0 = bytes 0–15, logical block 1 = bytes 16–31, etc.
- **Never** read or write sector trailer blocks from recipe logic.

---

## 9. Real tag dump reference

### 9.1 NTAG213 (04:C7:88:62:6F:71:80)

- **User data (pages 8–11):**  
  `01 00 00 01` `01 00 00 00` `FE 01 18 00` `00 00 00 00`  
  → First 16 bytes of stream: `01 00 00 01 01 00 00 00 FE 01 18 00 00 00 00 00`
- **Interpretation (candidate):**  
  ID=1, NumOfDrinks=0, RecipeSteps=1, ActualRecipeStep=1, ActualBudget=254 (0x00FE LE), Parameters=1, RightNumber=254 (0xFE), RecipeDone=0, CheckSum=24 (0x0018 LE). ID+RightNumber=255 ✓.
- Pages 12+ contain mostly zeros; page 18 has `00 00 08 00`. With RecipeSteps=1, one step occupies bytes 12..43; remaining bytes in the safe region are “tail” and should be preserved when editing.

### 9.2 MIFARE Classic (82:87:09:7C)

- Sector 0 block 1 contains `01 00 00 01 00 00 00 00 FE 00 00 00 00 00 00 00` (TRecipeInfo-like).  
- Firmware starts recipe at **physical block 2** (sector 0), which in this dump is all zeros. So this card may not follow the same recipe layout or may use block 1 for something else; treat Classic recipe layout as “logical block 0 = physical block 2” and do not assume block 1 is part of the recipe stream.

---

## 10. Decoding rules (for mobile app)

1. Read tag type (UID length / tech list). If unsupported, show metadata only.
2. Read user/data region into a linear **recipe buffer** (see sections 7 and 8).
3. If buffer length < HEADER_SIZE (12), treat as invalid or empty.
4. Decode bytes 0..11 as TRecipeInfo (use table in section 4).
5. Validate: `ID + RightNumber == 255`. If not, mark invalid and do not offer write.
6. Parse `RecipeSteps`. If `RecipeSteps == 0`, steps = [] and checksum must be 0.
7. Required length = HEADER_SIZE + RecipeSteps * STEP_SIZE. If buffer is shorter, mark incomplete.
8. Decode steps from bytes 12 .. 12 + RecipeSteps*31 - 1 (STEP_SIZE=31).
9. Compute checksum over step bytes; compare with header CheckSum. If mismatch, show warning and in normal mode do not offer write.
10. Any bytes after the last step (up to end of recipe buffer) are **unknown tail**; store and preserve them when re-encoding.

---

## 11. Encoding rules (for mobile app)

1. Build header from edited model; set `RightNumber = 255 - ID`.
2. Build step bytes from edited steps (same layout as section 6).
3. Compute checksum over step bytes only; write result into header bytes 10–11.
4. Concatenate: header (12 bytes) + step bytes + **original unknown tail** (if any). Do not overwrite tail with zero unless expert mode allows.
5. Map resulting stream back to pages (Ultralight) or logical blocks (Classic); write only those pages/blocks that contain recipe data. Do not change bytes outside the recipe region.

---

## 12. Unknown / uncertain

- **Exact HEADER_SIZE and STEP_SIZE** on the actual ESP32 build (we use 12 and 31).
- **Bitfield order** in the last byte of TRecipeStep (we assume LSB first: NeedForTransport, IsTransport, IsProcess, IsStepDone).
- **Classic in production:** Whether Testbed 4.0 uses Classic tags for recipes or only Ultralight/NTAG.
- **Maximum RecipeSteps** in practice (derived from tag capacity: e.g. (128 - 12) / 31 ≈ 3 steps for 128-byte NTAG213 user region).

All of the above are isolated in the app as constants or commented assumptions so they can be updated after validation on real firmware and tags.
