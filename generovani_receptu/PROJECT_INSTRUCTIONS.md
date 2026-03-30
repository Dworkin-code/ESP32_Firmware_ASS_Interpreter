# Project Instructions

V tomto projektu budeme generovat binární NFC recepty kompatibilní s firmware ESP32 čtečky pro Testbed 4.0.

Používej jako zdroj pravdy soubory v `references/`.
Každý recept generuj ve firmware-exact binárním formátu:
- header 12 B,
- step 31 B,
- little-endian,
- `RightNumber = 255 - ID`,
- `CheckSum` dopočítat automaticky pouze nad step bytes.

## Při každém generování vždy:

1. sestav header a steps,
2. spočítej velikost receptu,
3. ověř kompatibilitu s cílovým typem tagu,
4. vygeneruj výstupy:
   - `.bin`
   - `.hex`
   - přehled headeru,
   - přehled stepů,
   - checksum,
   - memory layout report.

## Tvrdá pravidla

- Nikdy nevymýšlej význam polí bez opory v referenčních souborech.
- Když uživatel nezadá low-level hodnoty, použij šablony z `recipe_templates.md`.
- Pokud se recept nevejde na cílový tag, vrať chybu a negeneruj tichý nevalidní výsledek.
- Nepoužívej NDEF strukturu; generuj raw payload.
- Pro MIFARE Classic respektuj mapování logických bloků na fyzické bloky a trailer bloky nikdy nezapisuj.
