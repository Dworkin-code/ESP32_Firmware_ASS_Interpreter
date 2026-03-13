# AAS GetSupported / ReserveAction – Analysis Report

**Goal:** Determine why `1177189415/0/0/0/0` returns `support:60_position:1` but `1177189415/0/3/5/0` (Shaker step: TypeOfProcess=3, ParameterProcess1=5) returns `support:0_position:1`.

**Conclusion:** The PLC does **not** treat fields 3–5 as “TypeOfProcess / ParameterProcess1 / ParameterProcess2” from the recipe. It treats them as **material**, **supportA**, and **supportB** and checks each value against **PLC-defined numeric ranges**. Only values inside those ranges are “supported”. The ranges are configured via symbolic constants (e.g. `SupportMaterialLow100`..`SupportMaterialHigh100`) whose **numeric values are not defined in the repository**; they live in the PLC project. Value **0** is inside the configured ranges (hence support 60 for 0/0/0). Values **3** (Shaker) and **5** (duration) are **outside** those ranges, so the PLC sets support to 0 and ReserveAction rejects the item.

---

## 1. Relevant files and functions

| Location | Role |
|----------|------|
| **PLC_code/program_block/AAS/OPC UA Methods/GetSupported.scl** | FB GetSupported: parses 5-field message, computes support from element[2..4] vs. support ranges, returns `support:X_position:Y`. |
| **PLC_code/program_block/AAS/OPC UA Methods/ReserveAction.scl** | FB ReserveAction: same support logic; if support=0 returns `Error:ErrItemNotSupported`, else pushes item to PQueue. |
| **PLC_code/program_block/AAS/low level functions/GetMessage.scl** | Splits `InputMessage` by `/` into `element[0]`..`element[WordNumberSpaces]`. |
| **PLC_code/program_block/Device Data/DeviceSlow.scl** | Uses same material/supportA/supportB ranges for wait-time and “supported” check when executing an item. |
| **ESP32_Firmware_ASS_Interpreter/main/app.c** (≈353–356) | Builds 5-field message: `sr_id/0/TypeOfProcess/ParameterProcess1/ParameterProcess2`. |
| **ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c** | `OPC_GetSupported`, `OPC_ReserveAction`: forward the 5-field string to the PLC method node. |

No other files implement GetSupported/ReserveAction or define the support-range constants. The constants (e.g. `SupportMaterialLow100`) are **referenced** in GetSupported.scl, ReserveAction.scl, and DeviceSlow.scl but **not defined** in the checked-in PLC code; they are PLC symbols (DB or constant table).

---

## 2. Meaning of each input field in GetSupported

The PLC parses the string with **GetMessage** (split by `/`). Indices match the reader’s 5-field format:

| Index | Reader sends | PLC variable / comment | Meaning on PLC |
|-------|----------------|-------------------------|-----------------|
| **element[0]** | `sr_id` | (GetSupported does not use it; ReserveAction uses as `tempItem.id`) | Product/sr ID. |
| **element[1]** | `0` (priority) | Used in GetSupported only for **position** (see below) | Priority level; position = sum of `PQueue.subElementCount[0..priority]`. |
| **element[2]** | `TypeOfProcess` (0 or 3) | Comment: “check **material**” | Treated as **material**; must be in range `SupportMaterialLow100`..`SupportMaterialHigh100` or `SupportMaterialLow60`..`SupportMaterialHigh60`. |
| **element[3]** | `ParameterProcess1` (0 or 5) | Comment: “check **supportA**” | Treated as **supportA**; must be in `SupportALow100`..`SupportAHigh100` or `SupportALow60`..`SupportAHigh60`. |
| **element[4]** | `ParameterProcess2` (0) | Comment: “check **supportB**” | Treated as **supportB**; same style of range check. |

So on the PLC, **field3 is not “TypeOfProcess” semantically** – it is “material”. **Field4 is not “ParameterProcess1” semantically** – it is “supportA”. The reader correctly sends TypeOfProcess and ParameterProcess1/2, but the PLC interprets them only as three numeric codes checked against fixed ranges.

---

## 3. Meaning of `support:60`

- Support is computed in GetSupported (and identically in ReserveAction) as follows:
  - Start: `tmpSupportValue := 100`.
  - For **element[2]** (material):  
    - if in `SupportMaterialLow100`..`SupportMaterialHigh100` → leave value;  
    - if in `SupportMaterialLow60`..`SupportMaterialHigh60` → subtract 40;  
    - else → set 0.
  - For **element[3]** (supportA): same rules (leave / subtract 40 / set 0).
  - For **element[4]** (supportB): same rules.
  - Clamp: if `tmpSupportValue < 0` then set to 0.

So the result is **100**, **60**, **20**, or **0** depending how many “60” ranges are hit (each subtracts 40) and whether any value falls in **ELSE** (then 0).

- **support:60** means: the final value is 60. So exactly one of the three checks hit a “60” range (100 − 40 = 60), and the other two hit “100” ranges; no check hit ELSE.
- **support:0** means: at least one of the three checks hit **ELSE** (value not in any of the configured ranges).

So **60 is not a bitmask**; it is a single support level (0, 20, 60, or 100) derived from how many “60” ranges are matched and that no value is unsupported.

---

## 4. Why support becomes 0 for `0/3/5/0` (Shaker)

- Input: `1177189415/0/3/5/0`  
  → element[0]=sr_id, element[1]=0, **element[2]=3**, **element[3]=5**, element[4]=0.

- For **0/0/0** the PLC returns 60, so the **numeric value 0** is inside the configured ranges for material, supportA, and supportB (in the “100” or “60” bands).

- For **3** (element[2], “material”): the CASE compares `3` to the PLC’s `SupportMaterialLow100`..`SupportMaterialHigh100` and `SupportMaterialLow60`..`SupportMaterialHigh60`. In the current PLC configuration, **3 is in neither** → ELSE → `tmpSupportValue := 0`.

- Even if 3 were supported, **5** (element[3], “supportA”) would be checked the same way; if 5 is not in the supportA ranges, ELSE would set support to 0.

So **support:0** for Shaker is because **at least one of (3, 5, 0)** is outside the PLC’s configured support ranges. Given that 0/0/0 gives 60, 0 is allowed; so the rejectors are **3** (TypeOfProcess = Shaker) and/or **5** (ParameterProcess1 = duration). The PLC logic is “reject if any of material/supportA/supportB is not in the allowed bands”; it does not know “3 = Shaker” or “5 = duration”.

---

## 5. Is Shaker “implemented” on the server side?

- **DeviceSlow.scl** uses the **same** material/supportA/supportB CASE structure: if a value is in the “100” or “60” ranges it contributes to wait time; else it sets `waitTime := 0` and the item is treated as not supported (`deviceRunning := FALSE`, `actionStatus := 'failed'`).
- So “support” is **only** “is this (material, supportA, supportB) triple in the allowed numeric ranges?”. There is **no** separate implementation or branch for “Shaker” (value 3) or “duration 5” in the PLC code we have. Shaker is not explicitly implemented; it would be accepted only if the PLC’s **range constants** are set so that 3 and 5 (and 0 for supportB) are inside the allowed bands.

---

## 6. State/position constraints

- **Position** in GetSupported is computed from **element[1]** (priority) only:  
  `tmpQueuePosition := sum of PQueue.subElementCount[0..priority]`.  
  So `position:1` means one item in the queue for priority 0. It does **not** depend on TypeOfProcess or ParameterProcess1/2.
- Support (0/20/60/100) depends **only** on whether element[2], [3], [4] lie in the configured ranges. There is **no** state machine in GetSupported/ReserveAction that restricts “Shaker only in state X” or “only in position Y”. The only condition for acceptance is: support value &gt; 0 after the three CASE blocks.

---

## 7. What must be changed so Shaker is accepted

Two possible approaches:

**A) Change PLC configuration (recommended)**  
- In the PLC project, locate the definitions of:
  - `SupportMaterialLow100`, `SupportMaterialHigh100`, `SupportMaterialLow60`, `SupportMaterialHigh60`
  - `SupportALow100`, `SupportAHigh100`, `SupportALow60`, `SupportAHigh60`
  - `SupportBLow100`, `SupportBHigh100`, `SupportBLow60`, `SupportBHigh60`
- Extend the **material** range so that **3** (Shaker) is included (e.g. in the “100” or “60” band).
- Extend the **supportA** range so that **5** (and other durations 0–255 if desired) is included.
- After download, GetSupported and ReserveAction will accept `.../3/5/0` and return support &gt; 0; DeviceSlow will also treat (3, 5, 0) as supported when executing.

**B) Change reader mapping (workaround)**  
- If the PLC ranges cannot be changed, the reader would have to send a (material, supportA, supportB) triple that **lies inside** the current ranges. That would mean **not** sending TypeOfProcess=3 and ParameterProcess1=5 literally, but mapping “Shaker, 5 s” to some other numeric triple that the PLC already accepts. That would be a semantic mismatch (e.g. Shaker could be executed as another operation type) and is not recommended unless as a temporary workaround.

**Recommended:** Extend the PLC support ranges (A) so that material=3 and supportA=5 are allowed. No change to the reader or NFC encoding is required; the reader already sends the correct 5-field message.

---

## 8. Confidence level

| Finding | Confidence |
|--------|------------|
| Input format: element[0]=sr_id, [1]=priority, [2]=material, [3]=supportA, [4]=supportB | **Confirmed** from GetMessage.scl and GetSupported.scl / ReserveAction.scl. |
| Reader builds msg5 as sr_id/0/TypeOfProcess/ParameterProcess1/ParameterProcess2 | **Confirmed** from app.c. |
| Support = 0/20/60/100 from three CASE blocks (100, minus 40 per “60” range, else 0) | **Confirmed** from GetSupported.scl. |
| support:60 for 0/0/0 means 0 is inside the configured ranges | **Inferred** from logic (0 is not in ELSE). |
| support:0 for 3/5/0 because 3 and/or 5 are outside configured ranges | **Inferred** from logic (no other path to 0). |
| Numeric values of SupportMaterial* / SupportA* / SupportB* constants | **Not in repo**; defined in PLC project/symbol table. |
| Shaker accepted after extending PLC ranges to include 3 and 5 | **Inferred** from same CASE logic. |

---

## Summary

- **GetSupported** and **ReserveAction** treat fields 3–5 as **material**, **supportA**, **supportB** and only allow values inside PLC-defined numeric ranges.
- **support:60** means one “60” range was matched and no ELSE; **support:0** means at least one value (here 3 or 5) hit ELSE.
- The Shaker step (3, 5, 0) is rejected because **3** and/or **5** are not in those ranges in the current PLC configuration.
- To accept Shaker: **extend the PLC’s support-range constants** so that material=3 and supportA=5 are included. No change is required in NFC encoding or reader message format.
