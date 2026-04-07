# Remaining risks after minimal revert in main build

## Remaining uncertainty

- This change set is intentionally minimal and tag-specific; functional confidence is highest for MIFARE Classic 1K write/read stability.
- Full certainty still depends on hardware validation in your exact runtime timing and tag population.

## NTAG213 behavior status

- NTAG213-specific max-user-page write guard was intentionally removed.
- Therefore, NTAG213 protection behavior is intentionally degraded compared to guarded variant:
  - Writes are no longer blocked by that page limit condition in `NFC_WriteStructRange(...)`.
- If NTAG213 remains part of deployment, treat support as "not protected by guard" and validate carefully.

## Possible side effects

- On 7-byte UID paths, more retry attempts will now occur (fixed retry count), which can increase write/verify duration in some cases.
- Retry profile logging tied to UID-length helper was removed; timing logs remain.
- No expected side effects to:
  - AAS/OPC UA logic
  - cross-cell routing/handover
  - NVS cell identity and endpoint assignment
  - process-owner decisions
  - PN532 interface behavior
  - struct/checksum layout and handling
