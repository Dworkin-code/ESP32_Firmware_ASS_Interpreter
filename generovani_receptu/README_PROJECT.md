# NFC Recipe Generator Project

Tento projekt slouží k ručnímu i poloautomatickému generování binárních NFC receptů pro Testbed 4.0 tak, aby byly kompatibilní s firmware ESP32 NFC čtečky a šly zapsat přes externí nástroj typu NFC Tools.

## Cíl

Z textového nebo low-level zadání vytvořit:
- firmware-exact binární payload receptu,
- HEX výstup pro ruční zápis,
- BIN soubor,
- lidsky čitelný report s rozpadem headeru a kroků,
- kontrolu checksumu,
- kontrolu kapacity cílového tagu.

## Pevná pravidla formátu

- Header má vždy 12 B.
- Jeden krok má vždy 31 B.
- Celková velikost receptu je:
  `encodedRecipeSize = 12 + RecipeSteps * 31`
- Všechny vícebajtové hodnoty jsou little-endian.
- `RightNumber = 255 - ID`
- `CheckSum` se počítá pouze nad STEP bajty.
- Význam polí a mapování musí vycházet pouze z referenčních souborů v `references/`.

## Doporučený workflow

1. Do `inputs/` vlož zadání receptu.
2. Použij pravidla a šablony z `references/`.
3. Vygeneruj výstupy do `outputs/`:
   - `.hex`
   - `.bin`
   - `_report.md`
4. Před zápisem vždy zkontroluj:
   - velikost receptu,
   - typ tagu,
   - maximální počet kroků,
   - checksum.

## Poznámka ke kapacitě

Pro NTAG213 je uživatelská paměť 144 B. Recept ale používá firmware datovou oblast od page 8, takže v praxi je nutné vždy kontrolovat skutečný dostupný prostor podle používaného zápisového workflow. Pro malý tag je bezpečné počítat konzervativně a recepty držet krátké.
