## NFC tag capacity and per-tag write limits

This document summarizes how the Android app detects tag families, computes usable recipe capacity for each physical tag, and enforces safe write limits without changing the logical recipe format.

---

## 1. Logical recipe format (unchanged)

- **Header size**: `HEADER_SIZE = 12` bytes.
- **Step size**: `STEP_SIZE = 31` bytes.
- **Payload layout**: `TRecipeInfo` header followed by `TRecipeStep[]` steps.

Total encoded recipe size is:

- `totalRecipeBytes = HEADER_SIZE + recipeSteps * STEP_SIZE`

None of the capacity logic changes `HEADER_SIZE`, `STEP_SIZE`, `TRecipeStep` layout, or decoding behaviour.

Key code: `RecipeCodec.kt`.

---

## 2. Tag type detection (Ultralight/NTAG vs Classic)

Physical tag family is detected in `NfcTagReader.getTagType(tag)`:

- 7-byte UID → `TagType.ULTRALIGHT_NTAG` (Mifare Ultralight / NTAG family).
- 4-byte UID → `TagType.CLASSIC` (Mifare Classic).
- Otherwise → `TagType.UNKNOWN` (unsupported in this app).

The result is stored in `TagMetadata.tagType` inside `RawTagDump.metadata` and used everywhere that needs tag family information.

Key code:

- `NfcTagReader.kt` → `getTagType(tag)`.
- `TagMetadata.kt` → `TagType` enum and NFC metadata container.

---

## 3. TagCapacityInfo model and resolver

New model in `TagCapacityInfo.kt`:

```kotlin
data class TagCapacityInfo(
    val tagType: TagType,
    val usableRecipeBytes: Int,
    val maxRecipeSteps: Int,
    val writeSupported: Boolean,
    val explanation: String
)
```

A single resolver computes capacity per physical tag:

```kotlin
fun resolveTagCapacity(metadata: TagMetadata): TagCapacityInfo
```

### 3.1 Ultralight / NTAG (NTAG213 user memory)

For `TagType.ULTRALIGHT_NTAG`:

- **Usable recipe bytes**:
  - `usableRecipeBytes = NTAG213_USER_MEMORY = 144`.
  - This corresponds to NTAG213 user memory pages 8..39 that the app uses for recipe storage.
  - Config/lock pages are excluded by design.
- **Write support**:
  - `writeSupported = true` (Ultralight/NTAG write is implemented).
- **maxRecipeSteps**:
  - `maxRecipeSteps = floor((NTAG213_USER_MEMORY - HEADER_SIZE) / STEP_SIZE)`.
  - With current constants: `floor((144 - 12) / 31) = 4`.
- **Explanation**: documents that we assume NTAG213 with 144 bytes of user memory for the recipe region.

### 3.2 Mifare Classic (logical data blocks only)

For `TagType.CLASSIC`:

- **Usable recipe bytes**:
  - `usableRecipeBytes = metadata.memorySizeBytes.coerceAtLeast(0)`.
  - `memorySizeBytes` is set from `RawTagDump.bytes.size` when the tag is read.
  - For Classic, `RawTagDump.bytes` is produced by `NfcTagReader.readClassicBytes(tag)`, which:
    - Starts at `CLASSIC_FIRST_DATA_BLOCK = 2` (first data block after manufacturer data).
    - Maps **logical** data blocks to **physical** blocks via `logicalToPhysicalClassicBlock`, skipping sector trailers (blocks 3, 7, 11, ...).
    - Reads up to 48 logical data blocks (enough to cover all data blocks on Classic 1K) or until `blockCount`/auth failure.
  - As a result, `usableRecipeBytes` counts only the data blocks that are actually accessible through the app’s mapping; manufacturer blocks, sector trailers, and reserved blocks are excluded.
- **Write support**:
  - `writeSupported = false` (Classic write is intentionally disabled; only capacity and limits are shown).
- **maxRecipeSteps**:
  - Computed from `usableRecipeBytes` using the same formula as for Ultralight (see below).
- **Explanation**: documents that capacity comes from logical data block bytes and that Classic write is not currently enabled.

This means Classic capacity is **not hardcoded**; it is derived from the real bytes the app can read from the tag, specific to the current card size and mapping.

### 3.3 Unknown tags

For `TagType.UNKNOWN`:

- `usableRecipeBytes = 0`
- `maxRecipeSteps = 0`
- `writeSupported = false`
- Explanation indicates unsupported/unknown tag type.

---

## 4. Capacity formula and max step count

For every tag family, the maximum recipe steps are computed uniformly from `TagCapacityInfo.usableRecipeBytes`:

```kotlin
if (usableRecipeBytes > HEADER_SIZE) {
    maxRecipeSteps = (usableRecipeBytes - HEADER_SIZE) / STEP_SIZE
} else {
    maxRecipeSteps = 0
}
```

This matches:

- `totalRecipeBytes = HEADER_SIZE + recipeSteps * STEP_SIZE`
- `maxSteps = floor((usableRecipeBytes - HEADER_SIZE) / STEP_SIZE)`

The result is exposed as `TagCapacityInfo.maxRecipeSteps` and used by both the UI and the write guard.

---

## 5. UI integration: per-tag capacity and limits

### 5.1 Tag detail screen (`TagDetailActivity`)

When a tag is scanned and opened in the detail view:

- The app calls `resolveTagCapacity(dump.metadata)` and shows:
  - **Tag UID** and **TagType** (existing).
  - **Memory size**: total bytes in `RawTagDump.bytes` (existing `memory_size` field).
  - **Usable recipe bytes**: `TagCapacityInfo.usableRecipeBytes` (`tag_capacity` TextView).
  - **Max recipe steps**: `TagCapacityInfo.maxRecipeSteps` (`tag_max_steps` TextView).
  - **Write supported**: `TagCapacityInfo.writeSupported` (`tag_write_support` TextView).
  - **Capacity explanation**: human-readable explanation from `TagCapacityInfo.explanation` (`tag_capacity_explanation` TextView).
- If a decoded recipe is available:
  - The app computes `encodedSize = HEADER_SIZE + decoded.steps.size * STEP_SIZE + decoded.unknownTail.size`.
  - If `encodedSize > usableRecipeBytes`, it shows a red warning:
    - `"WARNING: Encoded recipe size X bytes exceeds this tag capacity (Y bytes)."` (`tag_capacity_warning` TextView).
  - Otherwise, the warning is empty.

This provides a clear per-tag view of:

- Detected family (Ultralight/NTAG vs Classic).
- Usable bytes for recipes.
- Maximum recipe steps for this specific tag.
- Whether writing is supported to this tag family.

### 5.2 Write screen (`WriteTagActivity`)

The write screen also resolves capacity from the original `RawTagDump.metadata`:

- Summary line now includes tag type:
  - `"Recipe ID=<id> Steps=<header.recipeSteps>. Tag: <uidHex> (<TagType>)"`.
- Capacity line (`write_capacity` TextView) shows:
  - `"Tag capacity: <usableRecipeBytes> bytes, max recipe steps: <maxRecipeSteps>, write supported: Yes/No"`.
- If the current edited recipe is already known to exceed capacity:
  - `encodedSize = HEADER_SIZE + decoded.steps.size * STEP_SIZE + decoded.unknownTail.size`.
  - If `encodedSize > usableRecipeBytes`, show red warning:
    - `"WARNING: Current recipe requires X bytes, which exceeds this tag capacity (Y bytes)."`

This gives the user immediate feedback on whether the edited recipe fits into the scanned tag **before** tapping “Write”.

---

## 6. Encoding guard: validation before write

`WriteTagActivity.performWrite(tag: Tag)` now enforces capacity and type-safety checks **per physical tag**:

1. **Consistency of tag family between scan and write**
   - Re-detects the tag family with `getTagType(tag)` on the tag presented for writing.
   - Compares with `dump.metadata.tagType` (the type used when reading the original recipe).
   - If they differ, the write is refused:
     - `"Tag type mismatch between scanned tag (X) and original dump (Y). Refusing to write."`

2. **Capacity-based guard using TagCapacityInfo**
   - Resolves `capacity = resolveTagCapacity(dump.metadata)`.
   - Computes `encodedSize = HEADER_SIZE + steps.size * STEP_SIZE + decoded.unknownTail.size`.
   - Uses `capacity.maxRecipeSteps` and `capacity.usableRecipeBytes`:
     - If `encodedSize > usableRecipeBytes` **or** `steps.size > maxRecipeSteps`, write is blocked with:
       - `"This tag supports at most M recipe steps (N bytes usable). Current recipe requires Z bytes and S step(s)."`
   - This implements:
     - `maxSteps = floor((usableRecipeBytes - HEADER_SIZE) / STEP_SIZE)`.
     - Exact comparison against the encoded recipe size.

3. **Write-support check per family**
   - If `getTagType(tag) != TagType.ULTRALIGHT_NTAG` **or** `!capacity.writeSupported`, the write is blocked:
     - `"Write not supported for this tag type. Detected <TagType> with capacity N bytes (max M steps). Classic write path is not implemented yet."`
   - This means:
     - Ultralight/NTAG can be written (if capacity allows).
     - Classic is always read-only from the app’s perspective in this revision.

4. **Ultralight write path (unchanged logic, now capacity-aware)**
   - For `TagType.ULTRALIGHT_NTAG` with `writeSupported = true` and capacity OK:
     - The app:
       - Performs AAS profile validation for the active step.
       - Saves a backup (`BackupManager`) from a fresh read (`readTagToDump(tag)`).
       - Encodes the recipe to canonical bytes via `encodeRecipe`.
       - Writes bytes via `writeUltralightRecipeBytes`.
       - Re-reads and verifies checksum and integrity.

The result: **writes are only allowed when the recipe fits into the specific physical tag’s usable recipe region, and only for Ultralight/NTAG**.

---

## 7. Debug output extensions

`DebugRecipeActivity` now includes capacity information in its textual report (screen and logcat):

- Uses `resolveTagCapacity(dump.metadata)` when `RawTagDump` is available.
- Logs:
  - Tag type.
  - Usable recipe bytes.
  - Max recipe steps for that tag.
  - Whether writing is supported for that tag.
  - Resolver explanation string.
- Also logs:
  - `totalRecipeSize = HEADER_SIZE + decoded.steps.size * STEP_SIZE`.
  - Whether header-declared steps or actual decoded steps exceed this tag’s capacity.

This allows direct verification of capacity assumptions against real tag dumps.

---

## 8. Classic tag support status and assumptions

### 8.1 What is supported for Classic

- Tag detection (`TagType.CLASSIC`) via UID length.
- Capacity computation from actual bytes read via the app’s logical block mapping:
  - Counts bytes from data blocks starting at block 2 (manufacturer and sector trailer blocks excluded).
  - Lower-level details:
    - `CLASSIC_FIRST_DATA_BLOCK = 2`.
    - `logicalToPhysicalClassicBlock` skips blocks 3, 7, 11, ... (trailers).
    - `readClassicBytes` authenticates and reads up to 48 logical data blocks or until the tag’s own `blockCount`/auth restricts it.
- Display of:
  - Usable recipe bytes.
  - Max recipe steps for this specific Classic tag.
  - Clear explanation that Classic write is currently disabled.

### 8.2 What is not yet implemented

- No `writeClassicRecipeBytes` implementation.
- No Classic write path in `WriteTagActivity`:
  - All writes to `TagType.CLASSIC` are refused, even if the recipe would fit.

### 8.3 Assumptions about Classic size

- There is **no hard-coded card size** (e.g., 1K vs 4K).
- Instead, capacity is derived from:
  - The number of data blocks that can be read with the current mapping and keys.
  - The physical `blockCount` reported by the tag.
- This makes `usableRecipeBytes` specific to:
  - The actual Classic variant in use.
  - The set of sectors that are readable with the default key.

---

## 9. Behaviour examples

### Example A – NTAG213

- Detected type: `ULTRALIGHT_NTAG`.
- `usableRecipeBytes = 144` (NTAG213_USER_MEMORY).
- `maxRecipeSteps = floor((144 - 12) / 31) = 4`.
- A recipe with 5 steps:
  - `encodedSize = 12 + 5 * 31 = 167 > 144`.
  - Write blocked with:
    - `"This tag supports at most 4 recipe steps (144 bytes usable). Current recipe requires 167 bytes and 5 step(s)."`

### Example B – Classic (capacity depends on mapping)

- Detected type: `CLASSIC`.
- Suppose `RawTagDump.bytes.size = 768` bytes from logical data blocks:
  - `usableRecipeBytes = 768`.
  - `maxRecipeSteps = floor((768 - 12) / 31) = 24`.
- A recipe with 10 steps:
  - `encodedSize = 12 + 10 * 31 = 322 <= 768` → fits.
  - Capacity UI shows that it fits.
  - **Write is still blocked** with a message that Classic write is not implemented yet.

---

## 10. Key files and locations

- Tag type and read mapping:
  - `NfcTagReader.kt`
    - `getTagType(tag)`
    - `ULTRALIGHT_FIRST_PAGE`, `ULTRALIGHT_MAX_RECIPE_PAGE`, `ULTRALIGHT_MAX_READ_PAGE`
    - `CLASSIC_FIRST_DATA_BLOCK`, `logicalToPhysicalClassicBlock`
    - `readUltralightBytes(tag)`, `readClassicBytes(tag)`
- Recipe encoding/decoding:
  - `RecipeCodec.kt`
    - `HEADER_SIZE`, `STEP_SIZE`
    - `NTAG213_USER_MEMORY`
    - `decodeRecipe`, `encodeRecipe`
- Capacity model and resolver:
  - `TagCapacityInfo.kt`
    - `TagCapacityInfo`
    - `resolveTagCapacity(metadata: TagMetadata)`
- UI and write flow:
  - `TagDetailActivity.kt` + `activity_tag_detail.xml`
  - `WriteTagActivity.kt` + `activity_write_tag.xml`
  - `DebugRecipeActivity.kt`

These components together implement safe, per-tag capacity handling and write limits for both Ultralight/NTAG and Classic tags without changing the underlying recipe format.

