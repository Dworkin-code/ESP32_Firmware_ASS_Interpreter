# Tag Capacities

## NTAG213
- user memory: 144 B
- example scanned tag type: NTAG213
- firmware raw recipe area for Ultralight/NTAG starts at page 8
- vždy kontroluj skutečný dostupný prostor pro raw payload

### Výpočet
- header = 12 B
- step = 31 B
- payload size = `12 + steps * 31`

## MIFARE Classic 1K
- 1 kB fyzická paměť
- 16 sektorů, 4 bloky po 16 B
- trailer bloky se přeskočí
- manufacturer block se nepoužívá pro recept
- pro firmware je datová oblast tvořena logickými data bloky

## Obecné pravidlo
Vždy před generováním ověř:
1. cílový typ tagu,
2. maximální dostupné raw bytes,
3. počet kroků,
4. výslednou velikost payloadu.
