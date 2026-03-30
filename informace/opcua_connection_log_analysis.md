# OPC UA Connection Log Analysis

## 1. Connection-related log locations

Nize jsou vsechny relevantni logy spojene s navazanim OPC UA spojeni (nebo jeho vysledkem), ktere jsem nasel ve firmware.

### A) `components/OPC_Klient/OPC_klient.c` -> `ClientStart()`

- **Function:** `bool ClientStart(UA_Client **iManagement_client, const char *IPAdress)`
- **Connection attempt log text:**
  - `"Pokus o pripojeni na OPC UA server: %s\n"`
- **Failure result log text (per attempt):**
  - `"OPC UA connect failed (0x%08x), pokus %d\n"`
- **Final failure result log text (after retries):**
  - `"OPC UA se nepodarilo pripojit ani po 5 pokusech, koncim.\n"`
- **Success result log text:**
  - `"OPC UA klient pripojen.\n"`
- **When executed:**
  - Pri kazdem volani `ClientStart()`, ktere se pouziva jako centralni connection wrapper pro vsechny OPC volani.

### B) `main/app.c` -> `OPC_Permanent_Test()`

- **Function:** `void OPC_Permanent_Test(void *pvParameter)`
- **Success log text:**
  - `// printf("OPC_TEST: Pripojeni na %s USPELO.\n", MyCellInfo.IPAdress);`
  - Poznamka: tento radek je **zakomentovany**, tedy se nevypisuje.
- **Failure log text:**
  - `printf("OPC_TEST: Pripojeni na %s SELHALO.\n", MyCellInfo.IPAdress);`
- **Other related log text:**
  - `printf("OPC_TEST: Nelze ziskat semafor k Ethernetu.\n");`
- **When executed:**
  - Task bezi periodicky kazdych 5 s (vytvoren v `app_main()` pres `xTaskCreate(&OPC_Permanent_Test, ...)`).
  - Vypisuje jen fail/semaphore issue; success je zamerne potlacen.

### C) `components/OPC_Klient/OPC_klient.c` -> call-site wrappers po connectu

Tyto funkce neprimo informuji o connection vysledku (kdyz `ClientStart()` vrati false):

- `OPC_WriteCurrentId()` -> `"OPC_WriteCurrentId: connect failed"`
- `OPC_ReportProduct()` -> `"OPC_ReportProduct: connect failed"`
- `OPC_ReportProductEx()` -> `"OPC_ReportProductEx: connect failed"`
- `OPC_CallAasMethod()` -> `"OPC_CallAasMethod(...): connect failed"`

To jsou ale uz logy na urovni konkretniho RPC callu, ne jednotny "connection succeeded/failed" banner.

## 2. Main OPC UA connection flow

Hlavni tok:

1. `main/app.c`:
   - `connection_scan()` inicializuje Ethernet.
   - pote startuje tasky `OPC_Permanent_Test` a `State_Machine`.

2. Vsechny OPC UA operace jdou pres `ClientStart()` v `components/OPC_Klient/OPC_klient.c`:
   - slozi endpoint `opc.tcp://%s`
   - vytvori `UA_Client`
   - vola `UA_Client_connect(...)`
   - pri neuspechu retry (az 5 pokusu), mezi pokusy delay 1 s
   - pri uspechu vraci `true`, jinak `false`

3. Propagace resultu:
   - `ClientStart() == true` -> volajici funkce pokracuje v UA callu.
   - `ClientStart() == false` -> volajici funkce vraci chybu / vypise "connect failed" na urovni danei metody.

## 3. Conditions controlling success/failure prints

### 3.1 Kompilacni podminky / debug makra

V `OPC_klient.c` jsou definovane:

- `//#define OPC_KLIENT_ALL_DEBUG_EN 1`
- `//#define OPC_KLIENT_DEBUG_EN 1`

Makra:

- `OPC_KLIENT_DEBUG(...)`
- `OPC_KLIENT_ALL_DEBUG(...)`

jsou aktivni **jen pokud** jsou tyto define zapnute. Aktualne jsou zakomentovane, tzn.:

- logy z `ClientStart()` (`"Pokus o pripojeni..."`, `"OPC UA connect failed..."`, `"OPC UA klient pripojen."`) se vubec nevypisuji.

To je hlavni podminka, ktera ridi, zda uvidite "connection result" log.

### 3.2 Runtime podminky

- `OPC_Permanent_Test()` vypisuje fail jen kdyz:
  - `ClientStart()` selze, nebo
  - nejde ziskat Ethernet semaphore.
- Pri success je print explicitne zakomentovan.

### 3.3 Kontrolni tok

- V AAS flow (`USE_PLC_AAS_FLOW`) se casto connectuje uvnitr helperu (`OPC_ReportProductEx`, `OPC_GetSupported`, `OPC_ReserveAction`, `OPC_GetStatus`), ale bez jednotneho explicitniho success printu v `app.c`.
- Pokud jsou debug makra vypnuta, centralni "connect success/fail" vypis z `ClientStart()` zmizi.

## 4. Why the previous log may no longer appear

Nejpravdepodobnejsi a kodem potvrzeny duvod:

1. **Puvodni centralni success/fail log je v `ClientStart()` pres `OPC_KLIENT_DEBUG`.**
2. **`OPC_KLIENT_DEBUG_EN` je vypnuty (zakomentovany).**
3. **Soucasne v `OPC_Permanent_Test()` je success print zakomentovan.**

Deklarativne:

- `ClientStart()` stale probiha a spojeni se navazuje.
- Ale success/fail zprava uz neni stabilne viditelna, protoze:
  - centralni debug printy jsou compile-time potlacene,
  - test task uz nevypisuje uspech.

Sekundarni vlivy:

- Existuje vice call path, nektere jen loguji "connect failed" az uvnitr konkretnich wrapperu (`OPC_ReportProductEx` atd.), a to jen pri chybe.
- To vytvari dojem "nekdy se vypise, nekdy ne", protoze success v beznem path nemusi vypsat nic.

## 5. Other connection/reconnect paths

Ano, je vice cest:

1. **Hlavni produkcni cesta (State machine / AAS):**
   - `State_Machine()` -> `OPC_WriteCurrentId`, `OPC_ReportProductEx`, `OPC_GetSupported`, `OPC_ReserveAction`, `OPC_AAS_WaitCompletionPoll` -> kazda z nich uvnitr vola `ClientStart()`.

2. **Legacy cesta (starsi reservation flow):**
   - funkce jako `Inquire`, `GetInquireIsValid`, `Reserve`, `DoReservation_klient`, `IsFinished`, `Occupancy` -> taky kazda vola `ClientStart()`.

3. **Background test cesta:**
   - `OPC_Permanent_Test()` periodicky vola `ClientStart()` kazdych 5 s.
   - Success print je vypnuty, fail print zustal aktivni.

Neni zde samostatny "reconnect manager"; reconnect je implicitne realizovan opakovanym volanim `ClientStart()` z jednotlivych callu + internimi retry v `ClientStart()`.

## 6. Best stable logging point

Bez zmen kodu lze pouze doporucit:

- Nejstabilnejsi misto pro jednotny connect result log je **primo `ClientStart()`**, protoze je to centralni brana pro vsechny connection cesty.
- Druhe vhodne misto (doplnkove) je vstup/vystup `OPC_CallAasMethod()` pro AAS branch, ale to uz neni globalni pro legacy path.

Proc `ClientStart()`:

- jednotny endpoint,
- centralizovane retry + finalni rozhodnuti success/fail,
- pokryje produkcni, legacy i test flow.

## 7. Final conclusion

- Presna zprava o vysledku pripojeni se tiskne v `components/OPC_Klient/OPC_klient.c` ve funkci `ClientStart()`, ale jen pres debug makro `OPC_KLIENT_DEBUG`.
- Toto makro je aktualne compile-time vypnute (`OPC_KLIENT_DEBUG_EN` je zakomentovane), proto se spojeni muze uspesne/navazovat, ale bez viditelneho success/fail logu.
- Dalsi drive viditelna zprava z test tasku `OPC_Permanent_Test()` ("USPELO") je navic zakomentovana, takze z test path zustava viditelny jen failure print.
- Vysledkem je nekonzistentni dojem: "connect se deje, ale connection-result log casto chybi", i kdyz runtime spojeni funguje.
