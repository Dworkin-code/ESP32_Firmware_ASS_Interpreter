# Reader recipe & PLC contract analysis report

Analysis of the ESP32 NFC reader firmware repository: OPC UA interaction points, recipe data sources, data model, runtime flow, and gaps vs. the stated PLC method contracts. Read-only; no code was modified.

---

# A) Repo overview (relevant only)

## Relevant folder and file tree

```
ESP32_Firmware_ASS_Interpreter/
├── main/
│   ├── app.c                    # State machine, boot, NFC→OPC flow, uidStr build, ReportProduct/WriteCurrentId calls
│   ├── opcua_esp32.h
│   └── CMakeLists.txt
├── components/
│   ├── OPC_Klient/
│   │   ├── OPC_klient.c         # OPC UA client: connect, WriteCurrentId, ReportProduct, Inquire, Rezervation, DoProcess, IsFinished, Occupancy
│   │   ├── OPC_klient.h
│   │   └── CMakeLists.txt
│   ├── NFC_Reader/
│   │   ├── NFC_reader.c         # PN532 low-level: LoadTRecipeInfoStructure, LoadTRecipeSteps, getUID, saveUID, tag memory read/write
│   │   ├── NFC_reader.h         # TCardInfo, TRecipeStep, TRecipeInfo
│   │   └── CMakeLists.txt
│   ├── NFC_Handler/
│   │   ├── NFC_handler.c        # LoadData, IsSameData, CopyToWorking (recipe + UID copy)
│   │   ├── NFC_handler.h        # THandlerData
│   │   └── CMakeLists.txt
│   ├── NFC_Recipes/
│   │   ├── NFC_recipes.c        # GetRecipeStepByNumber, GetCardInfoByNumber (hardcoded recipes), AddRecipe
│   │   ├── NFC_recipes.h        # CellInfo, Reservation, ProcessTypes enums, StavovyAutomat
│   │   └── CMakeLists.txt
│   ├── open62541lib/            # open62541 (UA_Client_call, UA_Client_writeValueAttribute)
│   ├── pn532/                   # PN532 driver (readPassiveTargetID, mifare read/write)
│   ├── ethernet_init/
│   └── ethernet/
└── CMakeLists.txt               # ESP-IDF project
```

## Build system / toolchain

- **ESP-IDF** (CMake): root `CMakeLists.txt` includes `$ENV{IDF_PATH}/tools/cmake/project.cmake`, project name `write`. No PlatformIO or Arduino-IDF found.
- Components registered via `idf_component_register` / `register_component()`.

---

# B) OPC UA interaction points

## 1. Write: CurrentId variable

| Item | Value |
|------|--------|
| **File** | `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c` |
| **Function** | `OPC_WriteCurrentId(const char *endpoint, const char *value)` |
| **Server endpoint** | Built in caller: `endpoint` is passed through (from `MyCellInfo.IPAdress`, e.g. `"192.168.0.1:4840"`); URL built as `opc.tcp://%s` in `ClientStart`. |
| **Node resolution** | Hardcoded numeric: `UA_NODEID_NUMERIC(PLC_NODEID_CURRENTID_NS, PLC_NODEID_CURRENTID_ID)` with `PLC_NODEID_CURRENTID_NS = 4`, `PLC_NODEID_CURRENTID_ID = 6101` (i.e. **ns=4;i=6101**). |
| **API** | `UA_Client_writeValueAttribute(client, nodeId, &variant)` with variant type `UA_TYPES_STRING`. |

**Value written:** The `value` string passed in (see section E for how it is built).

```c
// OPC_klient.c (excerpt)
UA_String uaStr = UA_String_fromChars(value);
UA_Variant variant;
UA_Variant_init(&variant);
UA_Variant_setScalar(&variant, &uaStr, &UA_TYPES[UA_TYPES_STRING]);
UA_StatusCode ret = UA_Client_writeValueAttribute(client,
                                                   UA_NODEID_NUMERIC(PLC_NODEID_CURRENTID_NS, PLC_NODEID_CURRENTID_ID),
                                                   &variant);
```

---

## 2. Method call: ReportProduct (PLC AAS)

| Item | Value |
|------|--------|
| **File** | `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c` |
| **Function** | `OPC_ReportProduct(const char *endpoint, const char *uidStr)` |
| **Server endpoint** | Same as above (`MyCellInfo.IPAdress`). |
| **Node resolution** | Hardcoded: method node **ns=4;i=7004** (`PLC_NODEID_REPORTPRODUCT_METHOD_NS`, `PLC_NODEID_REPORTPRODUCT_METHOD_ID`). `UA_Client_call(client, methodId, methodId, 1, &inputVar, ...)`. |
| **API** | `UA_Client_call`. |

**InputMessage construction (exact):**

1. `uidStr` is the hex string of the NFC UID (e.g. from tag, or default — see C and E).
2. Last 8 hex chars of `uidStr` are taken (or left-padded with `'0'` if shorter).
3. Parsed as hex with `strtoul(last8, &endptr, 16)` → `v`.
4. `id = v & 0x7FFFFFFF`; if `id == 0` then `id = 1`.
5. `InputMessage` = decimal string of `id`: `snprintf(inputMessage, sizeof(inputMessage), "%" PRIu32, id)` (buffer 12 bytes).

So **ReportProduct receives a single decimal integer string** (e.g. `"1"`, `"12345678"`), which matches the contract “expects ONLY a decimal integer string: sr_id”.

```c
// OPC_klient.c (excerpt, lines 563-617)
char last8_buf[9];
const char *last8;
if (len >= 8)
    last8 = uidStr + (len - 8);
else {
    memset(last8_buf, '0', 8);
    last8_buf[8] = '\0';
    if (len > 0)
        memcpy(last8_buf + (8 - len), uidStr, len);
    last8 = last8_buf;
}
uint32_t v = (uint32_t)strtoul(last8, &endptr, 16);
uint32_t id = v & 0x7FFFFFFF;
if (id == 0)
    id = 1;
char inputMessage[12];
int n = snprintf(inputMessage, sizeof(inputMessage), "%" PRIu32, id);
UA_String inputMsg = UA_String_fromChars(inputMessage);
// ... UA_Client_call(client, methodId, methodId, 1, &inputVar, &outputSize, &output);
```

---

## 3. Other OPC UA method calls (custom / non-PLC AAS)

These use **namespace 0 (Objects folder)** and **namespace 1 (string node id)**. They are **not** the PLC methods FreeFromPosition, GetSupported, ReserveAction; they use different signatures (scalar arguments, not a single `InputMessage` string).

| Function | File | ObjectId | Method NodeId | Inputs |
|----------|------|----------|----------------|--------|
| `Inquire` | OPC_klient.c | `UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER)` | `UA_NODEID_STRING(1, "Inquire")` | 5 variants: UInt16, Byte, Boolean, Byte, UInt16 |
| `GetInquireIsValid` | OPC_klient.c | same | `UA_NODEID_STRING(1, "IsValid")` | 1 variant: UInt16 (IDofReservation) |
| `Reserve` | OPC_klient.c | same | `UA_NODEID_STRING(1, "Rezervation")` | 1 variant: UInt16 (IDofReservation) |
| `DoReservation_klient` | OPC_klient.c | same | `UA_NODEID_STRING(1, "DoProcess")` | 1 variant: UInt16 (IDofReservation) |
| `IsFinished` | OPC_klient.c | same | `UA_NODEID_STRING(1, "IsFinished")` | 1 variant: UInt16 (IDofReservation) |
| `Occupancy` | OPC_klient.c | same | `UA_NODEID_STRING(1, "Occupancy")` | 1 variant: Boolean |

**PLC AAS methods (FreeFromPosition, GetSupported, ReserveAction)** from the contract are **not** called in this firmware. Reservation flow uses the custom **Rezervation** (UInt16), not PLC **ReserveAction**(InputMessage: String).

---

# C) Where “recipe” data comes from

## 1. NFC UID

- **Source:** PN532 via `pn532_readPassiveTargetID()`.
- **Where read:**  
  - `NFC_reader.c`: `NFC_LoadTRecipeInfoStructure()`, `NFC_LoadTRecipeSteps()`, `NFC_LoadTRecipeStep()` each use a local `iuid[]` / `iuidLength` and call `pn532_readPassiveTargetID(..., iuid, &iuidLength, ...)` to detect card and (for Mifare Classic) authenticate.  
  - `NFC_getUID()` in `NFC_reader.c` also calls `pn532_readPassiveTargetID()` and returns UID/length.
- **Where stored in TCardInfo:**  
  - **Inference:** `NFC_saveUID(TCardInfo*, uint8_t*, uint8_t)` exists in `NFC_reader.c` and writes into `aCardInfo->sUid[]` and `sUidLength`, but **no caller in the codebase ever calls `NFC_saveUID` or `NFC_getUID`** in the load path. So the UID read in the load functions is used only for Mifare auth and is **not** persisted into `TCardInfo`.  
  - **Evidence:** `NFC_InitTCardInfo()` sets `sUidLength = 7` and `sUid[i] = 0` for all `i`. No other assignment to `sUid`/`sUidLength` appears except in `NFC_saveUID` (unused in load flow) and `NFC_CopyTCardInfo` (copy). So at runtime, when building the string for CurrentId/ReportProduct, **sWorkingCardInfo.sUid** may still be all zeros unless some other path (e.g. test or future code) sets it.
- **Format:** 4 or 7 bytes (Mifare Classic vs Ultralight); in app converted to hex string (2 hex digits per byte, no separator), e.g. `"04A1B2C3D4E5F6"`.

## 2. NDEF payload (text/JSON/binary)

- **Not used.** Recipe and card identity come from raw tag memory (TRecipeInfo/TRecipeStep layout) and UID, not NDEF.

## 3. Tag memory blocks

- **Source:** Mifare Classic (16-byte blocks) or Mifare Ultralight (4-byte pages).  
- **Files:** `NFC_reader.c`: `NFC_LoadTRecipeInfoStructure()` (reads first `TRecipeInfo_Size` bytes from tag), `NFC_LoadTRecipeSteps()` / `NFC_LoadTRecipeStep()` (read `TRecipeStep` array).  
- **Format:** Binary layout of `TRecipeInfo` and `TRecipeStep` structs (see D) written/read as raw bytes (no JSON).  
- **Offsets:** Ultralight: data offset `OFFSETDATA_ULTRALIGHT = 8`; Classic: block index via `NFC_GetMifareClassicIndex()`.

## 4. Hardcoded tables in firmware

- **File:** `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`  
- **Functions:** `GetRecipeStepByNumber(uint8_t aNumOfRecipe, uint16_t aParam)`, `GetCardInfoByNumber(uint8_t aNumOfRecipe)`.  
- **Content:** Recipe step types (e.g. StorageAlcohol/NonAlcohol, Shaker, Cleaner, ToStorageGlass, ToCustomer, Transport) and full recipe definitions (e.g. recipe 1: 5 steps; recipe 2: 6 steps; recipe 3: “return to storage”). Used when creating a new “return to storage” recipe or when adding transport steps (e.g. `GetRecipeStepByNumber(10, ...)`).  
- **Data format:** C structs `TRecipeStep` / `TRecipeInfo` / `TCardInfo` built in code, then written to tag or used in handler.

## 5. Loaded from SD / flash / WiFi

- **NVS (flash):** `app.c` reads `MyCellInfo.IDofCell` from NVS key `"ID_Interpretter"` and uses `MyCellInfo.IPAdress = "192.168.0.1:4840"`. No recipe or UID loaded from NVS/SD/WiFi.

---

# D) Current recipe / action data model (as implemented)

## Structs (from `NFC_reader.h` and `NFC_recipes.h`)

### TRecipeInfo (packed)

| Field | Type | Notes |
|-------|------|--------|
| ID | uint8_t | Card/recipe id (used in IsSameData for “same card”) |
| NumOfDrinks | uint16_t | |
| RecipeSteps | uint8_t | Number of steps |
| ActualRecipeStep | uint8_t | Current step index |
| ActualBudget | uint16_t | |
| Parameters | uint8_t | |
| RightNumber | uint8_t | Validation: ID + RightNumber must equal 255 |
| RecipeDone | bool | |
| CheckSum | uint16_t | Must be last; checksum over steps |

### TRecipeStep (packed)

| Field | Type | Notes |
|-------|------|--------|
| ID | uint8_t | Step index |
| NextID | uint8_t | Next step index |
| TypeOfProcess | uint8_t | ProcessTypes enum (e.g. Transport, StorageAlcohol, Shaker, Cleaner, ToCustomer, ToStorageGlass) |
| ParameterProcess1 | uint8_t | e.g. material or duration |
| ParameterProcess2 | uint16_t | e.g. volume or param |
| PriceForTransport | uint8_t | |
| TransportCellID | uint8_t | |
| TransportCellReservationID | uint16_t | |
| PriceForProcess | uint8_t | |
| ProcessCellID | uint8_t | |
| ProcessCellReservationID | uint16_t | |
| TimeOfProcess | UA_DateTime | |
| TimeOfTransport | UA_DateTime | |
| NeedForTransport | bool :1 | |
| IsTransport | bool :1 | |
| IsProcess | bool :1 | |
| IsStepDone | bool :1 | |

### TCardInfo

| Field | Type | Notes |
|-------|------|--------|
| sRecipeInfo | TRecipeInfo | |
| sRecipeStep | TRecipeStep * | Dynamic array, length RecipeSteps |
| sUid | uint8_t[7] | UID bytes (see C: may be unused in load path) |
| sUidLength | uint8_t | Initialized to 7 |
| TRecipeInfoLoaded | bool | |
| TRecipeStepArrayCreated | bool | |
| TRecipeStepLoaded | bool | |

### Supporting types (NFC_recipes.h)

- **CellInfo:** IDofCell, IPAdress (char*), ProcessTypes array (for cell selection).  
- **Reservation:** IDofCell, IDofReservation, ProcessType, Price, TimeOfReservation (used by custom Inquire/Rezervation/DoProcess/IsFinished).  
- **ProcessTypes enum:** ToStorageGlass, StorageAlcohol, StorageNonAlcohol, Shaker, Cleaner, SodaMake, ToCustomer, Transport, Buffer.  
- **StavovyAutomat enum:** State machine states (State_Mimo_Polozena, State_Inicializace_*, State_Poptavka_*, State_Rezervace, State_Transport, State_Vyroba_*, etc.).

### Step sequencing

- Steps are indexed by `ActualRecipeStep`; each step has `NextID` pointing to the next. Last step typically has `NextID` pointing to itself or end. Sequence is determined by the recipe on the tag (and by hardcoded recipes when creating new cards).

### Constants / lookup

- `EmptyRecipeStep`, `EmptyRecipeInfo` in `NFC_recipes.h`.  
- Recipe number → step list in `GetRecipeStepByNumber` / `GetCardInfoByNumber` (e.g. case 1–10 for step types, case 1–4 for full card recipes).  
- Cell list from `GetCellInfoFromLDS()` (type-based cell discovery; LDS not shown in scanned files).

---

# E) Current behavior summary (end-to-end)

1. **Boot/init**  
   - NVS: load `ID_Interpretter` → `MyCellInfo.IDofCell`; set `MyCellInfo.IPAdress = "192.168.0.1:4840"`.  
   - Ethernet: `connection_scan()` (static IP 192.168.0.10, etc.).  
   - SNTP / time (optional).  
   - NFC: `NFC_Reader_Init()` (PN532 SPI); `NFC_Handler_Init` / `SetUp`.  
   - Tasks: `Is_Card_On_Reader` (sets `Parametry->CardOnReader` via `NFC_isCardReady`), `State_Machine`, `OPC_Permanent_Test` (connectivity check).

2. **Connect OPC UA**  
   - Per call: `ClientStart(&client, endpoint)` with `endpoint = "opc.tcp://" + IPAdress` (e.g. `opc.tcp://192.168.0.1:4840`). No browse; node ids are hardcoded (ns=4 for PLC CurrentId/ReportProduct).

3. **Wait for tag**  
   - State machine runs; when `!CardOnReader` it stays in `State_Mimo_Polozena`.  
   - When `CardOnReader` becomes true, state stays `State_Mimo_Polozena` and proceeds to load data.

4. **Parse tag**  
   - Take NFC semaphore; `NFC_Handler_LoadData(&iHandlerData)`.  
   - LoadData calls `NFC_Handler_IsSameData()` which:  
     - Calls `NFC_LoadTRecipeInfoStructure(&sNFC, &aTempData)` (reads TRecipeInfo from tag; UID read into local `iuid`, not saved to TCardInfo).  
     - Validates `ID + RightNumber == 255`; compares with previous card (sIntegrityCardInfo); returns 0–5.  
   - Then, if needed, loads steps via `NFC_LoadTRecipeSteps()` (again UID only in local buffer).  
   - `NFC_Handler_ResizeIndexArray` and `NFC_Handler_CopyToWorking` copy Integrity → Working (including sUid/sUidLength, which were never set from tag in this path).

5. **Compute sr_id**  
   - Only for **ReportProduct**. In app: build `uidStr` from `sWorkingCardInfo.sUid[0..sUidLength-1]` as hex: `sprintf(uidStr + 2*i, "%02X", sUid[i])` → e.g. 14-char hex string for 7 bytes.  
   - In `OPC_ReportProduct`: take last 8 hex chars → parse as hex → `id = v & 0x7FFFFFFF`, if `id==0` then `id=1` → decimal string `inputMessage`. So **sr_id** is derived from (last 8 hex digits of UID string), not from a separate field.

6. **Write CurrentId (if exists)**  
   - If `sWorkingCardInfo.sUidLength > 0` (always true after init, since it’s 7), take Ethernet semaphore; call `OPC_WriteCurrentId(MyCellInfo.IPAdress, uidStr)` then `OPC_ReportProduct(MyCellInfo.IPAdress, uidStr)`.  
   - **Exact strings:**  
     - **CurrentId:** the full hex UID string (e.g. `"00000000000000"` if sUid is never set, or 4/7-byte hex for real UID).  
     - **ReportProduct InputMessage:** decimal string of derived id (e.g. `"1"` when last 8 hex are zeros because id is forced to 1).

7. **Other method calls (order in state machine)**  
   - After ReportProduct, state machine branches by recipe state (broken recipe, recipe done, IsProcess, IsTransport, TimeOfProcess, TimeOfTransport, reservations, etc.).  
   - It may call **Inquire**, **Reserve** (Rezervation), **DoReservation_klient** (DoProcess), **IsFinished**, **Occupancy** — all against the **custom** server (ns=0/1), with scalar arguments (UInt16, Byte, Boolean), **not** the PLC AAS methods (GetSupported, ReserveAction, FreeFromPosition).  
   - **FreeFromPosition, GetSupported, ReserveAction** are never called in this codebase.

---

# F) Gaps vs PLC contract (analysis only)

1. **ReportProduct InputMessage format**  
   - Contract: “expects ONLY a decimal integer string: sr_id”; PLC parses with STRING_TO_DINT, id != 0.  
   - Firmware: Sends exactly that (decimal string of id, with id forced to 1 when derived value is 0). So format is aligned.  
   - **Potential gap:** The **sr_id** is derived from last 8 **hex** digits of the **UID string**. If UID is never saved (see C), the UID string is all zeros → id becomes 1 → ReportProduct always sends `"1"`. So all tags would report the same sr_id unless UID is persisted.

2. **CurrentId value**  
   - Contract (implied): Variable holds current product/carrier id (e.g. string form of id or hex UID).  
   - Firmware: Writes the **full hex UID string** (e.g. `"00000000000000"` or 14/8 hex chars). If PLC expects a decimal sr_id string here too, delimiter/length/format may differ (e.g. hex vs decimal).

3. **UID not persisted into TCardInfo in load path**  
   - `NFC_LoadTRecipeInfoStructure` / `NFC_LoadTRecipeSteps` read UID into local `iuid` for authentication only; **NFC_saveUID is never called**. So `sIntegrityCardInfo.sUid` / `sWorkingCardInfo.sUid` remain the zeros from `NFC_InitTCardInfo`.  
   - **Evidence:** No references to `NFC_saveUID` or `NFC_getUID` in the load/state-machine path; only definition in `NFC_reader.c` and declaration in `NFC_reader.h`.  
   - **Effect:** CurrentId and ReportProduct may always use the same effective id (e.g. `"1"`) and same hex string (zeros) unless another code path or future change saves UID.

4. **FreeFromPosition, GetSupported, ReserveAction not implemented**  
   - PLC contract: FreeFromPosition(InputMessage: STRING) = "sr_id"; GetSupported / ReserveAction(InputMessage: STRING) = "sr_id/priority/material/parameterA/parameterB" (5 fields, "/").  
   - Firmware: No calls to these methods. Reservation uses custom **Rezervation**(UInt16) and **Inquire**(5 scalars), not PLC **ReserveAction**(String) or **GetSupported**(String). So no 5-field string is built or sent.

5. **Namespace index**  
   - Firmware uses **ns=4** for CurrentId (6101) and ReportProduct (7004). Some project docs (e.g. `informace/PLC_final_aas_extraction.json`) use **ns=1** for method node ids (7000–7005). If the actual PLC server uses ns=1, the reader would be calling the wrong nodes; if the PLC uses ns=4, it’s correct. This is a deployment/configuration mismatch risk, not a code bug per se.

6. **Edge cases (id=0, overflow, truncation)**  
   - **id=0:** Handled: reader forces `id = 1` before building InputMessage (`OPC_klient.c`).  
   - **Overflow:** `v = strtoul(..., 16)` then `id = v & 0x7FFFFFFF`; max 31-bit positive; snprintf with 12-byte buffer fits.  
   - **Truncation:** If `uidStr` is longer than 8 hex chars, only last 8 are used; leading digits are dropped (design choice for “last 8 hex = sr_id” mapping).

7. **Delimiter / 5-field format**  
   - No code builds a string with "/" or 5 fields. So there is no delimiter mismatch for GetSupported/ReserveAction; those methods are simply not used.

---

# G) Open questions / what to confirm on hardware

1. **Actual tag UID usage**  
   - Is `NFC_saveUID` intended to be called somewhere (e.g. in `NFC_LoadTRecipeInfoStructure` after reading `iuid`) so that CurrentId and ReportProduct use the real tag UID? Without that, does the system rely on a different “product id” (e.g. TRecipeInfo.ID) for the PLC?

2. **Namespace index on target PLC**  
   - Are CurrentId and ReportProduct on the real S7-1200 OPC UA server under **ns=4** (as in firmware) or **ns=1** (as in some informace docs)? One should confirm with UAExpert or server config.

3. **CurrentId semantics on PLC**  
   - Does the PLC expect CurrentId to be the same decimal sr_id as ReportProduct, or a hex UID string, or another format? Affects whether the current “full hex UID string” write is correct.

4. **Real tag payload examples**  
   - For tags that have been through the reader: what exact bytes are in the first block(s) (TRecipeInfo) and what UID (4 or 7 bytes) do they have? Confirms layout and whether ID + RightNumber == 255 is used in practice.

5. **Reservation vs ReserveAction**  
   - Is the intention to keep using the custom Inquire/Rezervation (and possibly a different server) for cell reservation and only use PLC ReportProduct/CurrentId for product reporting? Or should the reader eventually call PLC **ReserveAction** and **GetSupported** with the 5-field string? That would require new code paths and string building.

6. **sUidLength and 4-byte UIDs**  
   - For 4-byte UIDs, app builds 8 hex chars; last 8 chars = full UID. For 7-byte UIDs, 14 hex chars; last 8 = last 4 bytes. So sr_id from ReportProduct is always “last 4 bytes of UID in hex, as integer” (or 1 if zero). Is that the intended mapping on the PLC side?

---

*End of report. No code was modified; analysis only.*
