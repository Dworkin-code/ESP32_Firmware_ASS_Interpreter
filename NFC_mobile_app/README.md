# NFC Recipe Tag Mobile App

Android app for **reading**, **decoding**, **editing**, and **writing** NFC recipe tags used in the Testbed 4.0 project. The tag format was reverse-engineered from the ESP32 NFC reader firmware and real tag dumps; see `NFC_TAG_DATA_FORMAT_REVERSE_ENGINEERING_REPORT.md` and `tag_format_spec.md`.

---

## Requirements

- Android device with NFC (and NFC enabled in settings).
- Android SDK 24+ (min), 34 (target).
- **Android Studio** (recommended) or Gradle 8.x with Android Gradle Plugin 8.2.

---

## Build and run

1. Open the project in Android Studio: **File → Open** → select the `NFC_mobile_app/android-app` folder.
2. Wait for Gradle sync to finish.
3. Connect an Android device (with USB debugging enabled) or start an emulator (note: **NFC is not available on most emulators**; use a real device for tag read/write).
4. Run the app: **Run → Run 'app'** (or press Shift+F10).

From the command line (with Gradle wrapper present):

```bash
cd NFC_mobile_app/android-app
./gradlew assembleDebug
# APK: app/build/outputs/apk/debug/app-debug.apk
```

---

## Permissions

- **NFC** is declared in the manifest and required. The app does not request runtime permissions; NFC is used when the user holds a tag to the device.

---

## How NFC reading works

- **Tag discovery:** From the Home screen you can either tap **Scan tag** (opens Scan screen and enables NFC reader mode) or hold a tag to the phone when the app is in the foreground; the app receives the tag via `TAG_DISCOVERED` / `TECH_DISCOVERED` and reads it.
- **Tag type:** Detected by UID length (7 bytes → Ultralight/NTAG, 4 bytes → MIFARE Classic). Only these two types are supported for recipe decoding.
- **Reading:** For **Ultralight/NTAG213**, the app reads pages 8–39 (user data region) and builds a linear byte stream. For **MIFARE Classic**, it maps logical blocks to physical blocks (skipping sector trailers) and authenticates with Key B `FF FF FF FF FF FF` before reading.
- **Decoding:** The stream is interpreted as `TRecipeInfo` (12 bytes) plus `TRecipeStep[]` (32 bytes per step). Checksum (over steps only) and integrity (ID + RightNumber == 255) are validated and shown on the Tag detail screen.

---

## How writing is protected

- **Default:** Writing is **disabled**. The Home screen shows “Writing is disabled” until the user enables it in **Settings**.
- **Settings:** **Write enabled** must be turned ON to use “Write to tag” from the Tag detail screen. **Expert mode** is reserved for future options (e.g. raw dump write, Classic write).
- **Flow:** To write, the user opens a tag → **Edit recipe** (optional) → **Write to tag**. A **confirmation dialog** is shown. On confirm, the app enables reader mode and waits for the same or another tag. Before writing, a **backup** of the current tag content is saved (raw hex + metadata). Only **NTAG/Ultralight** tags are written; Classic write is not implemented. After writing, the app **re-reads** the tag and verifies checksum and integrity.

---

## Current limitations

- **Struct sizes:** Header 12 bytes and step 32 bytes are fixed in code; they must match the ESP32 firmware. See `tag_format_spec.md` and code comments.
- **MIFARE Classic:** Read-only; write is not implemented. Sector 0 may use a custom key on some cards.
- **Expert mode:** Toggle is present but no extra features (e.g. raw dump write) are implemented yet.
- **Diff view:** Edit screen shows header fields; a full “original vs modified” diff screen is not implemented; the write screen shows the recipe summary that will be written.
- **Emulator:** NFC is not available on the standard Android emulator; use a physical device for tag read/write.

---

## Project layout

- `android-app/` — Android (Kotlin) app source.
- `NFC_TAG_DATA_FORMAT_REVERSE_ENGINEERING_REPORT.md` — Firmware-based format analysis.
- `tag_format_spec.md` — Inferred memory map, struct layout, and decoding/encoding rules.
- `app_plan.md` — Technology choice, architecture, screens, data flow, and risks.
- `real_tag_info/` — Real tag dumps (NTAG213 and MIFARE Classic) for reference.
