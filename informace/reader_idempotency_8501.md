# Reader idempotency for ReportProduct Error:8501

This document describes the ESP32 reader firmware behaviour for PLC ReportProduct responses, idempotent handling of duplicate reports (Error:8501), and re-scan safety. **Only reader (ESP32) code is described; PLC behaviour is assumed and not modified.**

---

## What "Error:8501" means

- The PLC returns **Error:8501** when the **sr_id** is already present in **ReportedProductsQueue** (PLC status symbol **RQErrIdAlreadyKnown**).
- That is: the product was already reported in a previous call (e.g. first scan or earlier re-scan). The PLC does **not** add it again.
- The reader treats this as **idempotent success** (“already reported”): the desired state is “product is reported”; 8501 means we are already in that state, so the operation is considered successful.

---

## Behaviour by ReportProduct response

| OutputMessage   | Reader action |
|----------------|----------------|
| **Success**    | Proceed as normal: run step check, then optionally GetSupported / ReserveAction, wait, write-back. |
| **Error:8501** | Treat as success: same as above (step check → end-of-recipe handling or GetSupported/ReserveAction). |
| **Error:XXXX** (any other) | Treat as failure: set RecipeDone, write error state to tag, abort session (no GetSupported/ReserveAction). |

---

## Step check after report (Success or Error:8501)

After treating Success or Error:8501 as “report OK”:

- **Always** run the step-index check:
  - If **ActualRecipeStep >= RecipeSteps**: mark recipe **DONE** on the tag (write-back), **skip** GetSupported and ReserveAction, exit gracefully (no “invalid” hard failure).
- This avoids repeated PLC calls and broken state when re-scanning **end-of-recipe** tags.

---

## Re-scan guard (time window)

To avoid **double ReserveAction** when the same tag is scanned again while the previous session is still in progress:

- The reader keeps **lastSeenSrId** and **lastActionTimestamp** (ms).
- If the **same sr_id** is scanned again within a short window (e.g. **5 seconds**) after the last ReserveAction for that sr_id:
  - **Do not** call ReserveAction again.
  - Transition to **State_WaitUntilRemoved** (continue waiting for tag removal); no “invalid” failure.
- Time window used in firmware: **AAS_RESCAN_GUARD_MS** (default **5000** ms). Reader-side only; PLC code is unchanged.

---

## Summary

- **Error:8501** = duplicate sr_id already reported → treat as success, then apply step logic.
- **Success** → same flow.
- **Other Error:*** → failure, write tag, abort.
- **End-of-recipe** (ActualRecipeStep >= RecipeSteps) after report OK → RecipeDone on tag, skip GetSupported/ReserveAction.
- **Re-scan** of same sr_id within the guard window → skip ReserveAction, wait for removal.
