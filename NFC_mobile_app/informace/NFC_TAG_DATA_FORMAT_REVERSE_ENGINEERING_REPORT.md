# NFC Tag Data Format — Reverse Engineering Report (Firmware-Only)

**Source:** ESP32 NFC reader firmware only (no real tag dumps).  
**Purpose:** Infer the recipe data structure and NFC memory layout for later comparison with actual tag dumps.  
**Date:** 2025-03-09

---

## 1. Firmware Overview

The firmware is an ESP32 application that:

- **Hardware:** NFC reader built around **PN532** (SPI), ESP32, Neopixel, Ethernet.
- **Role:** Reads and writes **recipe data** on NFC tags; integrates with OPC UA (open62541) and other systems.
- **Relevant components:**
  - **`components/pn532/`** — PN532 driver (ISO14443A, MIFARE Classic/Ultralight, NTAG2xx).
  - **`components/NFC_Reader/`** — Tag detection, memory read/write, mapping of raw bytes to recipe structures.
  - **`components/NFC_Handler/`** — Higher-level handling (load/sync/compare) using `NFC_Reader`.
  - **`components/NFC_Recipes/`** — Recipe semantics (process types, steps, examples); does not define the on-tag layout.
  - **`main/app.c`** — Application logic (OPC UA, state machine, reporting).

Recipe data is stored as **raw binary structures** (no NDEF parsing for recipe content). The firmware expects **two tag types**, distinguished by UID length: **MIFARE Classic** (4-byte UID) and **MIFARE Ultralight** (7-byte UID). NDEF is supported in the PN532 driver but **not used** for recipe read/write in the NFC_Reader layer.

---

## 2. NFC Libraries and Drivers Used

| Component | File(s) | Role |
|-----------|--------|------|
| **PN532 driver** | `pn532.c`, `pn532.h` | Low-level SPI, PN532 commands, MIFARE/NTAG primitives |
| **NFC Reader** | `NFC_reader.c`, `NFC_reader.h` | Tag type detection, block/page read/write, layout (offsets, block index mapping) |
| **NFC Handler** | `NFC_handler.c`, `NFC_handler.h` | Load/sync/compare, working vs integrity buffers |
| **open62541** | `open62541.c`, `open62541.h` | OPC UA; provides `UA_DateTime` type used in `TRecipeStep` |

**PN532 commands used for recipe data:**

- `INLISTPASSIVETARGET` — detect card, get UID length (4 or 7).
- **MIFARE Classic:** `INDATAEXCHANGE` with `MIFARE_CMD_AUTH_B` (key 0xFF...), `MIFARE_CMD_READ` (0x30), `MIFARE_CMD_WRITE` (0xA0); access by **block** (16 bytes).
- **MIFARE Ultralight:** same `INDATAEXCHANGE` with `MIFARE_CMD_READ` (0x30) and `MIFARE_ULTRALIGHT_CMD_WRITE` (0xA2); access by **page** (4 bytes per page; firmware reads 4 pages at a time and uses 16 bytes).

NDEF helpers (`pn532_mifareclassic_FormatNDEF`, `pn532_ntag2xx_WriteNDEFURI`, etc.) exist in the PN532 driver but are **not** called from `NFC_reader.c`; recipe payload is **raw structs**.

---

## 3. NFC Tag Type Expected by Firmware

- **Standard:** **ISO14443A** (constant `PN532_MIFARE_ISO14443A` = 0x00 in `pn532_readPassiveTargetID`).
- **Tag types:**  
  - **UID length 4** → treated as **MIFARE Classic** (sector/trailer layout, Key B 0xFF FF FF FF FF FF).  
  - **UID length 7** → treated as **MIFARE Ultralight** (page-based; pages 4 bytes; first user data at page offset 8).
- **Format:** **Raw memory**, not NDEF, for recipe. Data is written/read as a contiguous byte stream: **TRecipeInfo** first, then **TRecipeStep[]**.
- **Memory size:** Not explicitly limited in code; size is derived from `TRecipeInfo_Size + RecipeSteps * TRecipeStep_Size`. For Ultralight, reading uses 16-byte chunks (four pages); for Classic, 16-byte blocks with sector trailer blocks skipped in the index mapping.

---

## 4. NFC Memory Access Pattern

### 4.1 Constants (NFC_reader.c)

```c
#define OFFSETDATA_ULTRALIGHT 8   // First byte of user data (page offset on tag)
#define OFFSETDATA_CLASSIC 1      // First data block index (sector 0 reserved)
#define PAGESIZE_ULTRALIGHT 4     // Bytes per Ultralight page
#define PAGESIZE_CLASSIC 16       // Bytes per Classic block
```

### 4.2 MIFARE Ultralight (UID length 7)

- **Access:** By **page**. Each “logical block” in the reader is 16 bytes (4 pages).  
- **Page addressing:** `page = (logical_block_index * 4) + OFFSETDATA_ULTRALIGHT` → first user block starts at **page 8** (e.g. block index 0 → pages 8–11).  
- **Read:** `pn532_mifareultralight_ReadPage(aNFC, (i * 4) + OFFSETDATA_ULTRALIGHT, iData)` — reads 16 bytes (four pages) per call; only the first 4 bytes are used per page in the PN532 Ultralight read response, but the reader code uses a 16-byte buffer and copies 16 bytes from the response (see `pn532.c` around line 551: `memcpy(buffer, pn532_packetbuffer + 7, 16)`). So effectively the firmware reads **16 bytes per call** starting at page 8.  
- **Write:** `pn532_mifareultralight_WritePage(aNFC, i + OFFSETDATA_ULTRALIGHT, iData)` — here `i` is the **logical block index** (0-based), so the first write is to **page 8** (one page = 4 bytes). So for Ultralight, **write is 4 bytes per page**, read is 16 bytes per call (four pages).  
- **Order:** Sequential: block 0 (TRecipeInfo start), then further blocks for TRecipeInfo and then TRecipeStep array.

### 4.3 MIFARE Classic (UID length 4)

- **Access:** By **block** (16 bytes). Sector trailer blocks are **skipped** in the logical layout.
- **Block index mapping:** `NFC_GetMifareClassicIndex(i)` maps logical block index `i` to physical block number:
  - Starts at `1 + OFFSETDATA_CLASSIC` = block **2** (sector 0: block 0 = manufacturer, block 1 = first data; sector trailer is block 3).
  - For each logical index, increment; whenever the next physical block would be a sector trailer `(number % 4 == 0` before adding), increment again. So logical blocks map to data blocks only (no trailer blocks).
- **Authentication:** Key B `{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}` for every accessed block.
- **Read:** `pn532_mifareclassic_ReadDataBlock(aNFC, index, iData)` — 16 bytes per block.
- **Write:** `pn532_mifareclassic_WriteDataBlock(aNFC, index, iData)` — 16 bytes per block.
- **Order:** Sequential over logical blocks: first blocks hold TRecipeInfo, then TRecipeStep[].

### 4.4 Addresses and Bytes Read

- **TRecipeInfo:** Bytes `0` to `TRecipeInfo_Size - 1`.  
  - Ultralight: blocks `0` to `ceil(TRecipeInfo_Size/16) - 1` (pages 8, 12, …).  
  - Classic: logical blocks `0` to `ceil(TRecipeInfo_Size/16) - 1` (physical blocks 2, 3, 5, 6, …).
- **TRecipeStep[]:** Bytes `TRecipeInfo_Size` to `TRecipeInfo_Size + RecipeSteps * TRecipeStep_Size - 1`.  
  - Same block/page progression; `RecipeSteps` comes from the already-read `TRecipeInfo.RecipeSteps`.

So the firmware **always** reads **TRecipeInfo first** (to get `RecipeSteps`), then allocates and reads **TRecipeStep[0..RecipeSteps-1]**.

---

## 5. Recipe Parsing Logic

There is **no separate “parser”**: the tag payload is interpreted as **raw C structs**:

1. **Read TRecipeInfo** (first `TRecipeInfo_Size` bytes) into `TCardInfo.sRecipeInfo`.
2. **Validate** (in NFC_Handler): `ID + RightNumber == 255`; if not, card is considered invalid/broken (return 5).
3. **Allocate** `sRecipeStep` for `RecipeSteps` elements.
4. **Read** `TRecipeInfo_Size + RecipeSteps * TRecipeStep_Size` bytes (from the second region) into `sRecipeStep[]`.
5. **Checksum:** `CheckSum` in TRecipeInfo is compared to `NFC_GetCheckSum(aCardInfo)` (over TRecipeStep bytes only). No CRC over full tag.

**Checksum algorithm** (NFC_reader.c, `NFC_GetCheckSum`):

```c
uint16_t CheckSum = 0;
for (size_t i = 0; i < TRecipeStep_Size * aCardInfo.sRecipeInfo.RecipeSteps; ++i)
  CheckSum += *((uint8_t *)aCardInfo.sRecipeStep + i) * (i % 4 + 1);
return CheckSum;
```

So: **sum over all TRecipeStep bytes** of `byte_value * ((index % 4) + 1)`. Stored in `TRecipeInfo.CheckSum`. If `RecipeSteps == 0`, checksum is 0.

**RightNumber validation:** `NFC_handler.c` line 219: `if (aTempData.sRecipeInfo.ID + aTempData.sRecipeInfo.RightNumber != 255) return 5;`. So **RightNumber** is used as a simple integrity check: **RightNumber = 255 - ID**.

---

## 6. Candidate Data Structures

Defined in **`NFC_reader.h`**; both are **`__attribute__((packed))`**.

### TRecipeInfo (header / recipe info)

```c
typedef struct __attribute__((packed)) {
    uint8_t  ID;
    uint16_t NumOfDrinks;
    uint8_t  RecipeSteps;
    uint8_t  ActualRecipeStep;
    uint16_t ActualBudget;
    uint8_t  Parameters;
    uint8_t  RightNumber;    // Must satisfy: ID + RightNumber == 255
    bool     RecipeDone;
    uint16_t CheckSum;       // Checksum must be last (comment in header)
} TRecipeInfo;
```

**Size:** `TRecipeInfo_Size = sizeof(TRecipeInfo)` (compile-time). Typical layout (packed, little-endian):

| Byte range (approx.) | Field            | Type    | Notes                          |
|----------------------|------------------|---------|---------------------------------|
| 0                    | ID               | uint8_t | Recipe/card identifier          |
| 1–2                  | NumOfDrinks      | uint16_t|                                |
| 3                    | RecipeSteps      | uint8_t | Number of TRecipeStep entries   |
| 4                    | ActualRecipeStep | uint8_t | Current step index              |
| 5–6                  | ActualBudget     | uint16_t|                                |
| 7                    | Parameters       | uint8_t |                                |
| 8                    | RightNumber      | uint8_t | 255 - ID                        |
| 9                    | RecipeDone       | bool    |                                |
| 10–11                | CheckSum         | uint16_t| Over TRecipeStep[] only         |

### TRecipeStep (one recipe step)

```c
typedef struct __attribute__((packed)) {
    uint8_t       ID;
    uint8_t       NextID;
    uint8_t       TypeOfProcess;
    uint8_t       ParameterProcess1;
    uint16_t      ParameterProcess2;
    uint8_t       PriceForTransport;
    uint8_t       TransportCellID;
    uint16_t      TransportCellReservationID;
    uint8_t       PriceForProcess;
    uint8_t       ProcessCellID;
    uint16_t      ProcessCellReservationID;
    UA_DateTime   TimeOfProcess;
    UA_DateTime   TimeOfTransport;
    bool          NeedForTransport : 1;
    bool          IsTransport : 1;
    bool          IsProcess : 1;
    bool          IsStepDone : 1;
} TRecipeStep;
```

**Size:** `TRecipeStep_Size = sizeof(TRecipeStep)` (compile-time). `UA_DateTime` is 64-bit (e.g. int64) in open62541. With packed and 4× 1-bit bools typically in one byte:

| Byte range (approx.) | Field                     | Type         |
|----------------------|---------------------------|--------------|
| 0                    | ID                        | uint8_t      |
| 1                    | NextID                    | uint8_t      |
| 2                    | TypeOfProcess             | uint8_t      |
| 3                    | ParameterProcess1         | uint8_t      |
| 4–5                  | ParameterProcess2         | uint16_t     |
| 6                    | PriceForTransport         | uint8_t      |
| 7                    | TransportCellID           | uint8_t      |
| 8–9                  | TransportCellReservationID| uint16_t     |
| 10                   | PriceForProcess           | uint8_t      |
| 11                   | ProcessCellID             | uint8_t      |
| 12–13                 | ProcessCellReservationID  | uint16_t     |
| 14–21                 | TimeOfProcess             | UA_DateTime  |
| 22–29                 | TimeOfTransport           | UA_DateTime  |
| 30 (or 31)            | NeedForTransport:1, IsTransport:1, IsProcess:1, IsStepDone:1 | 4× 1-bit |

Exact offsets depend on compiler (alignment, bitfield packing). The firmware uses `sizeof(TRecipeInfo)` and `sizeof(TRecipeStep)` everywhere, so a writer must use the **same build** or the same sizes to stay compatible.

---

## 7. Possible NFC Memory Map

Logical layout (same for both tag types; only block/page addressing differs):

- **Byte 0 … TRecipeInfo_Size - 1:** **TRecipeInfo** (recipe header; must have `ID + RightNumber == 255`; `CheckSum` covers TRecipeStep region).
- **Byte TRecipeInfo_Size … TRecipeInfo_Size + RecipeSteps * TRecipeStep_Size - 1:** **TRecipeStep[0 … RecipeSteps-1]**.

No header magic, version, or length prefix is read or written; the only “header” is TRecipeInfo itself. Number of steps is taken from `TRecipeInfo.RecipeSteps`.

### MIFARE Ultralight (hypothetical)

- **Pages 0–7:** NFC/system (not used for recipe; first user data at page 8).
- **Page 8+:** Application data:
  - Block 0 (pages 8–11): TRecipeInfo start (first 16 bytes).
  - Further blocks: rest of TRecipeInfo, then TRecipeStep[].
- No reserved “checksum page” at a fixed address; checksum is the last two bytes of TRecipeInfo.

### MIFARE Classic (hypothetical)

- **Block 0:** Manufacturer data (not used for recipe).
- **Block 1:** Could be used; firmware starts data at **logical block 0 → physical block 2** (NFC_GetMifareClassicIndex), so **block 1 may or may not** be part of the recipe layout depending on intent; code uses block 2, 3, 5, 6, …
- **Logical blocks 0, 1, 2, …:** TRecipeInfo then TRecipeStep[] (physical blocks 2, 3, 5, 6, 9, 10, …; sector trailers 4, 8, 12, … skipped).

---

## 8. Functions Responsible for Decoding Recipe Data

| Function | File | Role |
|----------|------|------|
| `NFC_LoadTRecipeInfoStructure` | NFC_reader.c | Reads first TRecipeInfo_Size bytes into `aCardInfo->sRecipeInfo` (Ultralight: pages from 8; Classic: logical blocks from 0, physical per NFC_GetMifareClassicIndex). |
| `NFC_LoadTRecipeSteps` | NFC_reader.c | Reads bytes from offset TRecipeInfo_Size for RecipeSteps * TRecipeStep_Size into `aCardInfo->sRecipeStep[]`. |
| `NFC_LoadTRecipeStep` | NFC_reader.c | Reads one TRecipeStep at given index (same layout, partial range). |
| `NFC_LoadAllData` | NFC_reader.c | Calls LoadTRecipeInfoStructure → AllocTRecipeStepArray → LoadTRecipeSteps; full decode. |
| `NFC_Handler_IsSameData` | NFC_handler.c | After reading TRecipeInfo, checks `ID + RightNumber == 255`; compares CheckSum and optional byte-by-byte TRecipeInfo. |
| `NFC_GetCheckSum` | NFC_reader.c | Computes checksum over TRecipeStep[] for comparison with TRecipeInfo.CheckSum. |
| `NFC_WriteStructRange` / `NFC_WriteAllData` | NFC_reader.c | Encode: write (uint8_t*)&sRecipeInfo and (uint8_t*)sRecipeStep to the same logical block/page layout (and update CheckSum in TRecipeInfo when writing). |

There is no separate “decode” step: bytes are copied directly into the structs; decoding is the C type layout.

---

## 9. List of All Variables Related to Recipes

From the codebase (names and where they matter for the tag format):

**TRecipeInfo fields (on-tag):**  
`ID`, `NumOfDrinks`, `RecipeSteps`, `ActualRecipeStep`, `ActualBudget`, `Parameters`, `RightNumber`, `RecipeDone`, `CheckSum`.

**TRecipeStep fields (on-tag):**  
`ID`, `NextID`, `TypeOfProcess`, `ParameterProcess1`, `ParameterProcess2`, `PriceForTransport`, `TransportCellID`, `TransportCellReservationID`, `PriceForProcess`, `ProcessCellID`, `ProcessCellReservationID`, `TimeOfProcess`, `TimeOfTransport`, `NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`.

**Constants:**  
`TRecipeInfo_Size`, `TRecipeStep_Size`, `OFFSETDATA_ULTRALIGHT` (8), `OFFSETDATA_CLASSIC` (1), `PAGESIZE_ULTRALIGHT` (4), `PAGESIZE_CLASSIC` (16).

**Validation:**  
- `ID + RightNumber == 255`.  
- `CheckSum == NFC_GetCheckSum(...)` over TRecipeStep[].

**In-memory only (not stored as separate tag fields):**  
`TCardInfo.sUid`, `sUidLength`, `TRecipeInfoLoaded`, `TRecipeStepArrayCreated`, `TRecipeStepLoaded` — these are set after read and used by the application/OPC UA, not encoded on the tag.

---

## 10. Unknown / Unclear Parts of the Format

1. **Exact struct sizes:** `TRecipeInfo_Size` and `TRecipeStep_Size` are `sizeof(...)` at compile time. They depend on compiler, alignment, and bitfield packing. A writer must match the firmware build or known sizes (e.g. by dumping one known-good tag and comparing).
2. **Endianness:** Not explicitly swapped in code; ESP32 is little-endian. Tag data is assumed **little-endian** for uint16_t and UA_DateTime.
3. **MIFARE Classic sector 0, block 1:** Firmware uses logical block 0 → physical block 2. Whether block 1 is ever used or reserved is not evident from the reader code.
4. **Ultralight read vs write unit:** Read uses 16 bytes per call (four pages); write uses 4 bytes per page. The write loop uses the same logical block index `i` and writes 4 bytes to page `i + 8`; so the first write is page 8, bytes 0–3 of the stream. So the first 16 bytes of the stream span pages 8–11. This matches the read (which returns 16 bytes for “block” 0). Clarification: in `pn532_mifareultralight_ReadPage`, the PN532 actually returns 4 pages (16 bytes) but the function is named ReadPage; the buffer is 16 bytes. So the firmware’s “block” for Ultralight is 16 bytes (4 pages), and both read and write iterate in 16-byte logical blocks; for write, it writes 4 pages (4×4 bytes) per logical block. So layout is consistent.
5. **RecipeSteps == 0:** Allowed; checksum is 0; no TRecipeStep array is read. Writer should still write valid TRecipeInfo (e.g. RightNumber = 255 - ID).
6. **No version or magic:** There is no magic number or version byte checked; any tag that passes `ID + RightNumber == 255` and has readable blocks is interpreted as TRecipeInfo + TRecipeStep[]. Versioning or future format changes would need to be inferred from content or a new convention.
7. **NTAG2xx:** The driver has NTAG2xx read/write; the **NFC_Reader** layer only branches on UID length 4 (Classic) vs 7 (Ultralight). So NTAG213/215/216 are not explicitly used for recipe in the current reader code; they would be handled as 7-byte UID (Ultralight path).

---

## Summary Table (Quick Reference)

| Item | Value / Note |
|------|----------------|
| Tag types | MIFARE Classic (UID 4), MIFARE Ultralight (UID 7) |
| Standard | ISO14443A |
| Format | Raw binary (TRecipeInfo + TRecipeStep[]), no NDEF for recipe |
| First data (Ultralight) | Page 8 (byte offset 32 if page 0 = 4 bytes) |
| First data (Classic) | Logical block 0 → physical block 2 |
| Block/page size | Classic: 16 B; Ultralight: 4 B per page, 16 B logical block in reader |
| TRecipeInfo | First TRecipeInfo_Size bytes; last 2 bytes = CheckSum |
| TRecipeStep[] | Next RecipeSteps * TRecipeStep_Size bytes |
| Checksum | Sum of (TRecipeStep_byte[i] * (i % 4 + 1)); uint16_t |
| Validation | ID + RightNumber == 255 |
| Key (Classic) | Key B: FF FF FF FF FF FF |

This report is intended for designing the next phase: comparing this inferred layout with real NFC tag dumps and, if needed, a custom application to read/write recipe parameters on tags.
