# Process Types and Known Templates

## ProcessTypes enum

| Value | Name |
|---:|---|
| 0 | ToStorageGlass |
| 1 | StorageAlcohol |
| 2 | StorageNonAlcohol |
| 3 | Shaker |
| 4 | Cleaner |
| 5 | SodaMake |
| 6 | ToCustomer |
| 7 | Transport |
| 8 | Buffer |

## Potvrzené šablony

### Vodka 20 ml
- TypeOfProcess = 1 (StorageAlcohol)
- ParameterProcess1 = 20
- ParameterProcess2 = 0

### Voda 80 ml
- TypeOfProcess = 2 (StorageNonAlcohol)
- ParameterProcess1 = 80
- ParameterProcess2 = 0

### ToCustomer
- TypeOfProcess = 6
- ParameterProcess1 = 0
- ParameterProcess2 = 0

## Poznámky

- U některých kroků není zatím ze souborů 100% potvrzeno, jaký význam mají všechny ostatní parametry mimo `TypeOfProcess`, `ParameterProcess1`, `ParameterProcess2`.
- Pro generování základních receptů lze nezadaná pole nulovat, pokud neexistuje ověřený referenční dump, který ukazuje jiné hodnoty.
