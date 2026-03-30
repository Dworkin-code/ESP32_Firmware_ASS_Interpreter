# Mifare Classic Write Implementation

## 1. Overview of Classic write architecture

The Classic write path mirrors the existing Classic read mapping. Recipes are encoded into a contiguous byte stream (using the existing `encodeRecipe` function and the fixed `HEADER_SIZE = 12` and `STEP_SIZE = 31` layout). For Mifare Classic tags, this stream is written sequentially into *logical data blocks* that are mapped to *physical blocks* using the same `logicalToPhysicalClassicBlock` mapping used by `readClassicBytes`.

Only data blocks are written:

- Manufacturer block (physical block 0) is never touched.
- Sector trailer blocks (last block in each sector: 3, 7, 11, ...) are never written.

Each data block is written as 16 bytes. The final block is padded with zeros if the encoded recipe stream does not align to a 16‑byte boundary. Overall capacity and maximum recipe steps are enforced by the shared `TagCapacityInfo` / `resolveTagCapacity` mechanism so that no partial writes are allowed.

## 2. Files modified

- `core/nfc/NfcTagWriter.kt`
  - Added Classic write support via `writeClassicRecipeBytes` and logging.
- `core/tagmodel/TagCapacityInfo.kt`
  - Enabled write support for Classic tags using the dynamically computed `memorySizeBytes` based on the Classic logical mapping.
- `ui/WriteTagActivity.kt`
  - Integrated Classic write path into `performWrite`, reusing existing capacity checks, validation, backup, and post‑write verification.

## 3. Description of writeClassicRecipeBytes()

The new function:

```kotlin
fun writeClassicRecipeBytes(tag: Tag, data: ByteArray): Boolean
```

Key characteristics:

- **Tag type guard**: Immediately returns `false` unless `getTagType(tag) == TagType.CLASSIC`.
- **Empty payload**: If `data` is empty, returns `true` (nothing to write).
- **MifareClassic API**: Obtains `MifareClassic.get(tag)` and `connect()`s.
- **Sequential logical blocks**: Starts at logical block index 0 and keeps writing until all bytes in `data` are written.
- **Authentication**: For each new sector (derived from `blockToSector(physicalBlock)`), authenticates once using `authenticateSectorWithKeyB(sector, CLASSIC_KEY_DEFAULT)` (all 0xFF default key). Authenticated sectors are tracked in a `Set` to avoid redundant authentication.
- **Block writes**: For each logical block, computes the corresponding `physicalBlock` via `logicalToPhysicalClassicBlock(logicalIndex)` and writes exactly one 16‑byte block with `writeBlock`.
- **Padding**: If fewer than 16 data bytes remain, the rest of the block is zero‑padded.
- **Return value**: Returns `true` only if the full `data` payload has been written; otherwise returns `false` (for example, if it runs out of blocks or authentication fails).

In addition, the method logs:

- Total bytes written.
- Logical block indices used.
- Physical block indices used.
- Number of sectors successfully authenticated.

This satisfies the requirement for detailed Classic write debug output.

## 4. Logical → physical block mapping

The mapping is centralized in `logicalToPhysicalClassicBlock` (in `NfcTagReader.kt`):

- `CLASSIC_FIRST_DATA_BLOCK = 2`.
- Logical block 0 corresponds to physical block 2.
- Subsequent logical blocks increment the physical block index while skipping sector trailers:
  - Blocks where `physical % 4 == 3` are treated as sector trailers and are never used for data.

Conceptually:

- Logical 0 → Physical 2
- Logical 1 → Physical 4
- Logical 2 → Physical 5
- Logical 3 → Physical 6
- Logical 4 → Physical 8
- … etc., always skipping blocks 3, 7, 11, …

The same mapping is used for both reading (`readClassicBytes`) and writing (`writeClassicRecipeBytes`), so the mapping is symmetrical and consistent.

## 5. Capacity calculation method

Capacity remains centralized in `TagCapacityInfo` / `resolveTagCapacity`:

- For Classic tags:
  - `metadata.memorySizeBytes` is populated from the byte length of the Classic dump built by `readClassicBytes`.
  - This dump is composed solely of logical data blocks (starting at physical block 2 and skipping all trailers and manufacturer).
  - `resolveTagCapacity` uses `metadata.memorySizeBytes` as `usableRecipeBytes` and sets `writeSupported = true` for Classic.
- `maxRecipeSteps` is calculated generically as:
  - `max((usableRecipeBytes - HEADER_SIZE) / STEP_SIZE, 0)`.

Before writing in `WriteTagActivity`:

- `encodedSize = HEADER_SIZE + steps.size * STEP_SIZE + unknownTail.size`.
- The write is rejected if:
  - `encodedSize > capacity.usableRecipeBytes`, **or**
  - `steps.size > capacity.maxRecipeSteps`.

This ensures:

- No partial writes.
- Writes never exceed the region that was actually read via the Classic mapping.

## 6. Safety mechanisms preventing trailer writes

Safety is enforced at multiple levels:

1. **Logical mapping**:
   - `logicalToPhysicalClassicBlock` never returns manufacturer block 0 or any sector trailer (3, 7, 11, …) when iterating through logical indices.
2. **Runtime block safety check in writer**:
   - `writeClassicRecipeBytes` performs an explicit guard:
     - Aborts and returns `false` if `physicalBlock == 0` or `physicalBlock % 4 == 3`.
3. **Capacity alignment**:
   - `usableRecipeBytes` for Classic is derived from the existing read dump (data blocks only), so there is no attempt to write into blocks that were not part of the logical read.
4. **Authentication per sector**:
   - Each sector is authenticated with the default key before any write.
   - If authentication fails for any sector, the entire write is aborted and `false` is returned.

Together, these mechanisms guarantee that:

- Manufacturer and trailer blocks are never overwritten.
- Only the same logical data area that is used for reading is used for writing.

## 7. Example block layout for a written recipe

Assume:

- Encoded recipe length: 64 bytes.
- Tag uses 16‑byte Classic blocks.

Logical data blocks used:

- Logical 0 → Physical 2: bytes 0–15
- Logical 1 → Physical 4: bytes 16–31
- Logical 2 → Physical 5: bytes 32–47
- Logical 3 → Physical 6: bytes 48–63

Notes:

- Physical block 3 is the sector trailer for sector 0 and is skipped.
- If the encoded recipe were, for example, 50 bytes, the mapping would be:
  - Logical 0 / Physical 2: bytes 0–15
  - Logical 1 / Physical 4: bytes 16–31
  - Logical 2 / Physical 5: bytes 32–47
  - Logical 3 / Physical 6: bytes 48–49 plus 14 bytes of zero padding.

In all cases, the mapping matches `readClassicBytes`, so reading back the tag returns the same logical stream (up to the recipe length) as was written.

## 8. Example debug output

Classic write operations log a detailed summary for troubleshooting. Example logcat output:

```text
performWrite: starting Classic write, bytes=124
writeClassicRecipeBytes: bytesWritten=124, logicalBlocks=[0, 1, 2, 3, 4, 5, 6, 7], physicalBlocks=[2, 4, 5, 6, 8, 9, 10, 12], sectorsAuthenticated=3
RecipeDebug: === RECIPE DEBUG ===
Tag type: CLASSIC
Usable recipe bytes for this tag: 128
Max recipe steps for this tag: 3
Write supported for this tag: true
Capacity resolver explanation: Mifare Classic: using 128 bytes from logical data blocks (physical block 2 onwards, sector trailers and reserved blocks excluded).
Total encoded recipe size (header + steps): 124 bytes
checksumValid=true
integrityValid=true
...
```

This output shows:

- The number of bytes written.
- Logical and physical blocks used.
- Number of authenticated sectors.
- Capacity and recipe validity information from the shared debug view.

