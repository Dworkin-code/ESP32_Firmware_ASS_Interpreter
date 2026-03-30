## Live vs cached tag data audit

### 1. Screens audited

- TagDetailActivity (tag detail / metadata screen)
- DebugRecipeActivity (full recipe debug screen + Logcat dump)
- WriteTagActivity (write/preview screen for edited recipes)
- ScanActivity and MainActivity NFC entry points (for how live dumps reach TagDetailActivity)
- BackupsActivity (for how backup/cached dumps reach TagDetailActivity)

### 2. Data source before the fix

- **TagDetailActivity**
  - Always received a `RawTagDump` via `EXTRA_DUMP` and showed its metadata, raw hex and decoded recipe.
  - Callers:
    - `ScanActivity` and `MainActivity` passed a `RawTagDump` created from the *current* NFC `Tag` using `readTagToDump(tag)` (live scan).
    - `BackupsActivity` loaded a `RawTagDump` from JSON backup on disk (cached data) and passed it in the same way.
  - The screen did **not** distinguish between:
    - a live scan dump, and
    - a backup/cached dump.
  - No explicit indication of the UID length, tag type, or data source (live vs backup) beyond the raw metadata fields.

- **DebugRecipeActivity**
  - Received:
    - `EXTRA_DUMP`: optional `RawTagDump` (may be null).
    - `EXTRA_DECODED`: `DecodedRecipe` (can come from a live tag or from an edited/test recipe).
  - Started from:
    - `TagDetailActivity` via `DebugRecipeActivity.start(this, dump, decoded!!)`.
  - Logged a detailed recipe dump (header, steps, checksum/integrity, capacity) based on:
    - `dump.bytes` when available, otherwise `decoded.rawBytes`.
  - It did **not**:
    - Track whether `dump`/`decoded` came from a live scan, a backup, or an edited recipe.
    - Show UID length or tag type at the top of the debug text.
    - Warn when debug data was not based on the currently scanned tag.

- **WriteTagActivity**
  - Received:
    - `EXTRA_DUMP`: `RawTagDump` from the tag that was originally read in `TagDetailActivity`.
    - `EXTRA_ORIGINAL_DECODED`: original decoded recipe from that tag.
    - `EXTRA_DECODED`: edited `DecodedRecipe` from `EditRecipeActivity`.
  - Displayed:
    - A summary line combining decoded header and `dump.metadata` (`uidHex`, `tagType`).
    - Capacity and preview information based on `dump.metadata` + edited recipe.
  - This screen always used a **cached** `RawTagDump` plus an **edited** `DecodedRecipe`. It did **not**:
    - Label itself as non-live.
    - Explain that no active NFC scan was in progress.
    - Show UID length, tag type, and data source in a dedicated identity block.

- **ScanActivity / MainActivity / BackupsActivity**
  - Correctly read or loaded `RawTagDump` objects and passed them to `TagDetailActivity.start(this, dump)`.
  - There was a single `start()` entry point in `TagDetailActivity`, so the downstream screen had no way to know whether a given dump was:
    - from live scan (`ScanActivity` / `MainActivity`), or
    - from backup (`BackupsActivity`), or
    - from any other cached source.

### 3. Data source after the fix

- **TagDetailActivity**
  - Now receives an additional `EXTRA_SOURCE` flag describing where the `RawTagDump` came from:
    - `"live_scan"` for `ScanActivity` reader-mode scans.
    - `"external_nfc"` for `MainActivity` foreground NFC intents.
    - `"backup"` for dumps loaded from `BackupsActivity`.
    - `"unknown"` as a fallback.
  - New public entry points:
    - `start(activity, dump)` – kept for backwards compatibility, treated as `"live_scan"`.
    - `startFromScan(activity, dump)` – explicitly marks `"live_scan"`.
    - `startFromMainNfcIntent(activity, dump)` – marks `"external_nfc"`.
    - `startFromBackup(activity, dump)` – marks `"backup"`.
  - Callers updated:
    - `ScanActivity` → `TagDetailActivity.startFromScan(this, dump)` (live).
    - `MainActivity` → `TagDetailActivity.startFromMainNfcIntent(this, dump)` (live).
    - `BackupsActivity` → `TagDetailActivity.startFromBackup(this, dump)` (cached).
  - New on-screen **identity/debug block**:
    - Uses `dump.metadata` to compute:
      - `LIVE TAG UID`
      - `LIVE TAG UID LENGTH`
      - `LIVE TAG TYPE`
      - `DATA SOURCE` (descriptive text for the current `RawTagDump`).
    - Shown in a dedicated `TextView` (`tag_identity_debug`).
  - New **warning for non-live data**:
    - When `EXTRA_SOURCE` is anything other than `"live_scan"` or `"external_nfc"`, a red warning `TextView` (`tag_identity_warning`) displays:
      - “WARNING: This screen is showing cached/backup tag data, not the most recent live NFC scan.”
  - New **Logcat output**:
    - On creation, `TagDetailActivity` logs the entire identity block and source key via `Log.d` with `LOG_TAG = "RecipeDebug"`.
  - Debug recipe button:
    - When launching `DebugRecipeActivity`, TagDetail now forwards the same `EXTRA_SOURCE` so the debug screen/logs stay consistent with the tag detail source.

- **DebugRecipeActivity**
  - Now accepts and uses `EXTRA_SOURCE` to identify the data origin:
    - `"live_scan"`, `"external_nfc"`, `"backup"`, or `"unknown"`.
  - Companion object changes:
    - Added constants mirroring the same source keys.
    - `logRecipe(logTag, dump, decoded, source)` now takes an optional `source` (default `"unknown"`).
    - `start(activity, dump, decoded, source)` stores `EXTRA_SOURCE` for the UI.
    - Old `start(activity, dump, decoded)` remains and delegates to the new method with `"unknown"` for backwards compatibility.
  - TagDetail now calls:
    - `DebugRecipeActivity.logRecipe(LOG_TAG, dump!!, decoded, source)` and
    - `DebugRecipeActivity.start(this, dump, decoded, source)`,
    ensuring the debug screen and logs use the same source information as TagDetail.
  - New **identity and data source block** at the top of the debug text:
    - Uses `dump?.metadata` when available, otherwise falls back to `decoded.rawBytes` length:
      - `LIVE TAG UID` (or a placeholder when no metadata is present).
      - `LIVE TAG UID LENGTH` (bytes).
      - `LIVE TAG TYPE` (from metadata, or an explicit “unknown” note).
      - `DATA SOURCE` (human-readable description reflecting the source key and whether a `RawTagDump` was present).
    - If the source is **not** `"live_scan"` or `"external_nfc"`, an extra line is added:
      - “WARNING: This debug output is based on non-live data (cached/backup/edited).”
  - The rest of the recipe debug output (header, steps, checksum, integrity, offsets) remains unchanged, but is now prefixed by the identity block so you can immediately see which tag/data it represents.

- **WriteTagActivity**
  - Still receives:
    - `dump`: the `RawTagDump` originally read in TagDetail/EditRecipe flow.
    - `originalDecoded`: the original decoded recipe from the tag.
    - `decoded`: the edited recipe to be written.
  - Data flow remains unchanged, but the screen now explicitly documents that it is **not** a live scan:
    - A new identity `TextView` (`write_identity_debug`) shows:
      - `LIVE TAG UID` from `dump.metadata.uidHex`.
      - `LIVE TAG UID LENGTH` from `dump.metadata.uid.size`.
      - `LIVE TAG TYPE` from `dump.metadata.tagType`.
      - `DATA SOURCE: edited recipe based on last loaded RawTagDump (no live NFC scan active on this screen)`.
    - A new warning `TextView` (`write_identity_warning`) states:
      - “WARNING: This screen shows an edited recipe and cached tag metadata. The live tag will only be read during write/verify.”
  - New **Logcat output**:
    - On creation, `WriteTagActivity` logs the same identity block with `TAG = "WriteTagActivity"`.
  - The actual write/verify logic (including capacity limits, checksum handling, Classic/Ultralight differences) is untouched.

### 4. Stale/cached data findings

- **TagDetailActivity**
  - Before the fix, TagDetail could display either:
    - The most recently scanned live tag (`ScanActivity` / `MainActivity`), or
    - A previously saved backup (`BackupsActivity`),
    without any visual or logging distinction between the two.
  - This made it hard to tell whether a given TagDetail view reflected the **current physical tag** or **a cached backup dump**.

- **DebugRecipeActivity**
  - Debug data was always based on the `RawTagDump` (when present) and `DecodedRecipe` passed in, but the screen and logs did **not** say whether that data came from:
    - the current live tag,
    - a backup,
    - a test/edited recipe,
    or some other cached context.
  - In particular, it was possible to:
    - Open TagDetail on a backup,
    - Launch DebugRecipe,
    - And misinterpret the debug output as relating to the current physical tag, because the identity and data source were not called out.

- **WriteTagActivity**
  - Always operated on:
    - `dump`: the tag read earlier in the flow, and
    - `decoded`: a potentially edited recipe,
    but did not label itself as a non-live view. The actual live tag is only read inside `performWrite()` via `readTagToDump(tag)`.
  - Without explicit labeling, it could be unclear that:
    - The summary/preview shows **what will be written**, not necessarily **what is currently on any NFC tag**.

- **Conclusion**
  - Stale/cached data was not *incorrectly* used by the core logic, but it was **not clearly identified**:
    - TagDetail could be showing either live or backup data without distinction.
    - DebugRecipe could be showing data derived from non-live sources without any warning.
    - WriteTagActivity always worked on cached/edited data but did not make that explicit.
  - The new identity blocks and source flags remove this ambiguity and make it immediately clear when you are not looking at the most recent live NFC scan.

### 5. Example: new debug identity block

**Example TagDetailActivity identity block for a live scan:**

```text
LIVE TAG UID: 04A224B3C91280
LIVE TAG UID LENGTH: 7 bytes
LIVE TAG TYPE: ULTRALIGHT_NTAG
DATA SOURCE: RawTagDump from current ScanActivity NFC reader mode
```

**Example DebugRecipeActivity identity block for the same live scan (UI and Logcat):**

```text
=== RECIPE IDENTITY ===
LIVE TAG UID: 04A224B3C91280
LIVE TAG UID LENGTH: 7 bytes
LIVE TAG TYPE: ULTRALIGHT_NTAG
DATA SOURCE: RawTagDump from current NFC scan (ScanActivity/MainActivity)
```

**Example DebugRecipeActivity identity block when debugging a backup:**

```text
=== RECIPE IDENTITY ===
LIVE TAG UID: 12345678
LIVE TAG UID LENGTH: 4 bytes
LIVE TAG TYPE: CLASSIC
DATA SOURCE: RawTagDump loaded from JSON backup (cached data)
WARNING: This debug output is based on non-live data (cached/backup/edited).
```

### 6. Example warning for non-live data

- **TagDetailActivity warning when viewing a backup dump:**

```text
WARNING: This screen is showing cached/backup tag data, not the most recent live NFC scan.
```

- **WriteTagActivity warning (always non-live by design):**

```text
WARNING: This screen shows an edited recipe and cached tag metadata. The live tag will only be read during write/verify.
```

With these changes:

- Whenever a tag is scanned through `ScanActivity` or a foreground NFC intent in `MainActivity`, both the TagDetail and DebugRecipe screens clearly identify that they are showing **live scanned tag data**, including UID, UID length, and tag type.
- Any screen that is backed by a backup or edited recipe is explicitly labeled as such, with a visible warning and matching Logcat entries, making it clear why the Android app’s output might differ from the ESP32 reader’s live measurements.

