# NFC Page Mapping Trace

## 1. TRecipeInfo byte layout

Source: `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`

`TRecipeInfo` is declared as `__attribute__((packed))`, so there is no padding between fields.

```18:29:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h
typedef struct __attribute__((packed))
  {
    uint8_t ID;
    uint16_t NumOfDrinks;
    uint8_t RecipeSteps;
    uint8_t ActualRecipeStep;
    uint16_t ActualBudget;
    uint8_t Parameters;
    uint8_t RightNumber;
    bool RecipeDone;
    uint16_t CheckSum;   //Checksum musi byt vzdy posledni
  } TRecipeInfo;
```

Exact byte offsets (little-endian for `uint16_t`):

- byte `0`: `ID`
- byte `1..2`: `NumOfDrinks`
- byte `3`: `RecipeSteps`
- byte `4`: `ActualRecipeStep`  **(target)**
- byte `5..6`: `ActualBudget`
- byte `7`: `Parameters`
- byte `8`: `RightNumber`
- byte `9`: `RecipeDone` (`bool`, 1 byte in this ABI)
- byte `10..11`: `CheckSum`

Total size: `TRecipeInfo_Size = 12` bytes.

## 2. Write mapping for range 0-0

Write path:

- `NFC_Handler_Sync(...)` calls `NFC_WriteCheck(..., 0, 0)` in `NFC_handler.c`
- `NFC_WriteCheck(...)` calls `NFC_WriteStructRange(..., 0, 0)` in `NFC_reader.c`

In `NFC_WriteStructRange(...)` for `range=0-0`:

- `zacatek = 0`
- `konec = TRecipeInfo_Size - 1 = 11`
- write source bytes are `((uint8_t*)&aCardInfo->sRecipeInfo)[0..11]`

```340:350:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
  size_t zacatek = 0;
  size_t konec = TRecipeInfo_Size - 1;

  if (NumOfStructureStart > 0)
  {
    zacatek = TRecipeInfo_Size + (NumOfStructureStart - 1) * TRecipeStep_Size;
  }
  if (NumOfStructureEnd > 0)
  {
    konec = TRecipeInfo_Size + (NumOfStructureStart - 1) * TRecipeStep_Size + TRecipeStep_Size * (NumOfStructureEnd - NumOfStructureStart + 1) - 1;
  }
```

### MIFARE Classic (UID length 4)

- logical chunk size: `PAGESIZE_CLASSIC = 16`
- `PrvniBunka = 0 / 16 = 0`
- `PosledniBunka = 11 / 16 = 0`
- only chunk `i=0` is written
- bytes `0..11` copied from `sRecipeInfo` into `iData[0..11]`, `iData[12..15]=0`
- physical block index: `NFC_GetMifareClassicIndex(0) = 1`

So `TRecipeInfo` byte `4` is written to:

- **Classic block `1`, byte offset `4`**

### MIFARE Ultralight / NTAG path (UID length 7)

- logical chunk size: `PAGESIZE_ULTRALIGHT = 4`
- `PrvniBunka = 0 / 4 = 0`
- `PosledniBunka = 11 / 4 = 2`
- writes 3 pages with `pn532_mifareultralight_WritePage(aNFC, i + OFFSETDATA_ULTRALIGHT, iData)`
- `OFFSETDATA_ULTRALIGHT = 8`

Mapping:

- `i=0` -> page `8`: bytes `0..3`
- `i=1` -> page `9`: bytes `4..7`
- `i=2` -> page `10`: bytes `8..11`

So `TRecipeInfo` byte `4` is written to:

- **Ultralight page `9`, byte offset `0`**

## 3. Read-back mapping for range 0-0

Verify path:

- `NFC_WriteCheck(...)` calls `NFC_CheckStructArrayIsSame(..., 0, 0)`
- for struct `i==0`, `NFC_CheckStructArrayIsSame(...)` calls `NFC_LoadTRecipeInfoStructure(...)`
- then compares `((uint8_t*)&aCardInfo->sRecipeInfo)[j]` vs `((uint8_t*)&idataNFC1.sRecipeInfo)[j]`

```1412:1446:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
  for (size_t i = NumOfStructureStart; i <= NumOfStructureEnd; ++i)
  {
    if (i == 0)
    {
      ...
      Error = NFC_LoadTRecipeInfoStructure(aNFC, &idataNFC1);
      ...
      for (int j = 0; j < TRecipeInfo_Size; ++j)
      {
        if (*(((uint8_t *)&aCardInfo->sRecipeInfo) + j) != *(((uint8_t *)&idataNFC1.sRecipeInfo) + j))
```

### MIFARE Classic read-back

- `PosledniBunka = (TRecipeInfo_Size - 1)/16 = 0`
- only chunk `i=0`
- physical block index `NFC_GetMifareClassicIndex(0)=1`
- read block 1 into `iData[0..15]`
- copy `iData[k] -> sRecipeInfo[k]` for `k=0..11`

So verify interprets byte index `4` from:

- **Classic block `1`, byte offset `4`**

### MIFARE Ultralight read-back

- `PosledniBunka = (TRecipeInfo_Size - 1)/16 = 0`
- only one read call: `pn532_mifareultralight_ReadPage(aNFC, (0*4)+8, iData)` => page `8`
- this returns 16 bytes (`pages 8..11`) into `iData[0..15]`
- copy `iData[k] -> sRecipeInfo[k]` for `k=0..11`

Therefore byte index `4` is interpreted from:

- `iData[4]` = **Ultralight page `9`, byte offset `0`**

## 4. Physical location of byte 4 (ActualRecipeStep)

`TRecipeInfo` byte index `4` (`ActualRecipeStep`) maps to:

- **Classic:** block `1`, byte offset `4`
- **Ultralight/NTAG path:** page `9`, byte offset `0`

## 5. Write vs read mapping comparison

For `range=0-0`, mapping is identical between write and read-back verify:

- Classic: write block `1:4` and read block `1:4`
- Ultralight: write page `9:0` and read page `9:0` (via 16-byte read starting from page 8)

Conclusion for mapping parity:

- **No direct page/offset mismatch found for byte index 4 in range 0-0.**

## 6. Most likely mapping bug

Strictly from write/read physical mapping code in `NFC_reader.c`, there is no mismatch for this byte.

The most likely direct cause consistent with your logs (`write success`, `verify sees old byte 4`) is therefore **not** a simple address mapping difference between write and verify for `TRecipeInfo[4]`.

Most plausible low-level reason still compatible with this code:

- `pn532_mifareultralight_WritePage(...)` / `pn532_mifareclassic_WriteDataBlock(...)` returns success even when tag write is not actually committed/updated (timing/tag-state/card-family behavior), while subsequent read returns older content.

Secondary mapping-adjacent risk (not affecting 0-0 info mapping directly):

- Different card families are split only by UID length (`4` => Classic, `7` => Ultralight path). If a non-classic 4-byte UID tag or unexpected tag type appears, API path could be wrong despite identical internal math.

## 7. Best minimal fix point

Best minimal place to harden behavior is `NFC_WriteStructRange(...)` in `NFC_reader.c`, directly after each low-level write call:

- immediate read-back of the same physical location (same block/page just written)
- compare written bytes vs read bytes at low level
- fail early with detailed page/block + byte diff

Why this point:

- isolates hardware/protocol write-ack vs persistence issues
- does not depend on higher-level `NFC_CheckStructArrayIsSame(...)`
- provides definitive proof whether physical page `9:0` (or block `1:4`) changed right after write

## 8. Final conclusion

For `TRecipeInfo` byte `4` (`ActualRecipeStep`) and `range=0-0`, write and verify paths use the same physical NFC location:

- Classic: block `1`, offset `4`
- Ultralight: page `9`, offset `0`

So the observed mismatch (`expected=1`, `actual=0`, first mismatch index `4`) is **not explained by page/offset mapping divergence** in the current firmware logic. The bug is more likely in effective write persistence (driver/tag behavior/timing/path selection), not in logical byte-to-page mapping for this field.
