# NFC Recipe Tag Mobile App — Plan

**Project:** Testbed 4.0 NFC recipe tag reader/editor.  
**Working directory:** `NFC_mobile_app`.  
**App location:** `NFC_mobile_app/android-app`.

---

## 1. Chosen technology

**Native Android (Kotlin)** was chosen because:

- The app needs **raw page/block access** for NTAG213 and MIFARE Classic (no NDEF for recipe data).
- Firmware layout (Ultralight page 8+, Classic logical block mapping, checksum) must be mirrored exactly; native `MifareUltralight` and `MifareClassic` APIs give full control.
- Flutter/React Native would require custom native plugins for the same low-level access.
- For a technical student project, Android Studio + Kotlin is straightforward and well documented.

---

## 2. Architecture overview

- **core.nfc:** Tag detection (UID length → Ultralight vs Classic), raw read (Ultralight pages 8–39, Classic data blocks with Key B), raw write (Ultralight only; never lock/config pages).
- **core.tagmodel:** `TagMetadata`, `RawTagDump`, `RecipeHeader`, `RecipeStep`, `DecodedRecipe` (all Serializable for intents).
- **core.codec:** Decode/encode `TRecipeInfo` and `TRecipeStep` (12 + 32 bytes), checksum over steps only, validation (ID + RightNumber == 255).
- **core.backup:** Save/list backups (raw hex + metadata JSON + optional decoded recipe JSON) in app files dir.
- **ui:** MainActivity (home, NFC status, Scan / Backups / Settings), ScanActivity (reader mode → read → TagDetail), TagDetailActivity (raw hex, decoded summary, steps list, Save backup / Edit / Write), EditRecipeActivity (edit header fields → WriteTag), WriteTagActivity (confirm → backup → write → verify), BackupsActivity (list → open dump in TagDetail), SettingsActivity (write enabled, expert mode).

---

## 3. Screen list

| Screen | Role |
|--------|------|
| Home | NFC/write status, Scan tag, View backups, Settings |
| Scan NFC tag | Reader mode; on tag → read dump → TagDetail |
| Tag detail | UID, type, memory size, checksum/integrity, raw hex, decoded summary, steps list; Save backup, Edit recipe, Write to tag |
| Raw memory | Shown as linear hex in Tag detail (page/block view can be extended later) |
| Decoded recipe | Header + steps in Tag detail |
| Edit recipe | Edit ID, num drinks, step count, budget → Done → WriteTag |
| Write to tag | Summary, confirmation, backup, write (NTAG only), verification read |
| Save backup | From Tag detail; backups listed in Backups screen |
| Backups | List by time/UID; tap → load raw dump → TagDetail |
| Settings | Write enabled (default OFF), Expert mode (default OFF) |

---

## 4. Data flow

- **Read path:** Tag → `NfcTagReader.readTagToDump()` → `RawTagDump` → `RecipeCodec.decodeRecipe()` → `DecodedRecipe` → UI (TagDetail).
- **Edit path:** TagDetail → EditRecipe (clone header/steps in memory) → user edits → WriteTag with `DecodedRecipe`.
- **Write path:** WriteTag → BackupManager.saveBackup(pre-write dump) → encode recipe (RightNumber, CheckSum recomputed) → `NfcTagWriter.writeUltralightRecipeBytes()` → re-read tag → decode and verify checksum/integrity.

---

## 5. NFC workflow

- **Detection:** UID length 7 → Ultralight/NTAG, 4 → Classic. Tech list used for Android API selection.
- **Ultralight read:** Pages 8–39 (4 bytes per page); `readPages(page)` returns 16 bytes per call (4 pages), increment page by 4.
- **Classic read:** Logical block index → physical block via `logicalToPhysicalClassicBlock` (skip trailers); authenticate sector with Key B `FF FF FF FF FF FF`, then read block.
- **Write:** Only Ultralight; stream split into 4-byte pages starting at page 8; never write lock/config pages.

---

## 6. Risks and open questions

- **Struct sizes:** HEADER_SIZE=12 and STEP_SIZE=32 are assumed; they must match ESP32 firmware. Document in code and `tag_format_spec.md`; make configurable if needed.
- **Classic write:** Not implemented; Classic support is read-only. Expert mode could enable it later.
- **Real tag testing:** NTAG213 dump in `real_tag_info` matches the inferred header; full validation (checksum, step decode) should be confirmed on device with a known-good tag.
- **Unknown bytes:** When editing, only known header/step fields are changed; `unknownTail` and any un-decoded bytes are preserved in encoding.
