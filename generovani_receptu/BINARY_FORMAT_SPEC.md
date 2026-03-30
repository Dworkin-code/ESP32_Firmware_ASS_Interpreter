# Binary Format Specification

## Header (12 B)

| Offset | Size | Field |
|---|---:|---|
| 0 | 1 | ID |
| 1 | 2 | NumOfDrinks (uint16 LE) |
| 3 | 1 | RecipeSteps |
| 4 | 1 | ActualRecipeStep |
| 5 | 2 | ActualBudget (uint16 LE) |
| 7 | 1 | Parameters |
| 8 | 1 | RightNumber |
| 9 | 1 | RecipeDone |
| 10 | 2 | CheckSum (uint16 LE) |

## Step (31 B)

| Offset | Size | Field |
|---|---:|---|
| 0 | 1 | ID |
| 1 | 1 | NextID |
| 2 | 1 | TypeOfProcess |
| 3 | 1 | ParameterProcess1 |
| 4 | 2 | ParameterProcess2 (uint16 LE) |
| 6 | 1 | PriceForTransport |
| 7 | 1 | TransportCellID |
| 8 | 2 | TransportCellReservationID (uint16 LE) |
| 10 | 1 | PriceForProcess |
| 11 | 1 | ProcessCellID |
| 12 | 2 | ProcessCellReservationID (uint16 LE) |
| 14 | 8 | TimeOfProcess (uint64 LE) |
| 22 | 8 | TimeOfTransport (uint64 LE) |
| 30 | 1 | Flags byte |

## Flags byte

- bit0 = NeedForTransport
- bit1 = IsTransport
- bit2 = IsProcess
- bit3 = IsStepDone

## Layout

- Header: bytes `0..11`
- Step 0: bytes `12..42`
- Step 1: bytes `43..73`
- Obecně: `stepOffset = 12 + index * 31`

## Checksum

Počítá se jen nad všemi STEP bajty:

`checksum = Σ( byte[i] * ((i % 4) + 1) ) mod 65536`

## Integrity rule

`ID + RightNumber == 255`
