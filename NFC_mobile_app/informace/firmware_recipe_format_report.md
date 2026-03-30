### 1. Relevant Firmware Files

- **`components/pn532/pn532.c`**  
  - **Purpose**: Low-level PN532 NFC controller driver (SPI bit-banging, ISO14443A, MIFARE Classic, Ultralight, NTAG2xx helpers).  
  - **Relevant functions**:  
    - Tag presence, UID and type detection:
      - `pn532_readPassiveTargetID`  
        ```353:407:ESP32_Firmware_ASS_Interpreter/components/pn532/pn532.c
        bool pn532_readPassiveTargetID(pn532_t *obj, uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength, uint16_t timeout)
        {
            ...
            pn532_readdata(obj, pn532_packetbuffer, 20);
            ...
            *uidLength = pn532_packetbuffer[12];
            for (uint8_t i = 0; i < pn532_packetbuffer[12]; i++)
            {
                uid[i] = pn532_packetbuffer[13 + i];
            }
            ...
        }
        ```  
    - MIFARE Classic data I/O:
      - `pn532_mifareclassic_AuthenticateBlock`  
      - `pn532_mifareclassic_ReadDataBlock`  
      - `pn532_mifareclassic_WriteDataBlock`  
    - MIFARE Ultralight data I/O:
      - `pn532_mifareultralight_ReadPage` (reads 16 bytes = 4 pages)  
      - `pn532_mifareultralight_WritePage` (writes 4-byte pages)  
    - NTAG2xx helpers (not currently used by recipe code):
      - `pn532_ntag2xx_ReadPage` / `pn532_ntag2xx_WritePage`

- **`components/NFC_Reader/NFC_reader.h`**  
  - **Purpose**: Defines the high-level NFC “card recipe” structures and exported NFC reader API.  
  - **Relevant definitions**:  
    - Recipe header structure and layout:  
      ```18:29:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h
      typedef struct __attribute__((packed))
        {
          uint8_t ID;
          uint16_t NumOfDrinks;
          uint8_t RecipeSteps;
          uint8_t ActualRecipeStep;
          uint16_t ActualBudget;
          uint8_t Parameters;
          uint8_t RightNumber;
          bool RecipeDone;
          uint16_t CheckSum;   //Checksum musi byt vzdy posledni
        } TRecipeInfo;
      ```  
    - Recipe step structure and layout:  
      ```30:50:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h
      typedef struct __attribute__((packed))
        {
          uint8_t ID;
          uint8_t NextID;
          uint8_t TypeOfProcess;
          uint8_t ParameterProcess1;
          uint16_t ParameterProcess2;
          uint8_t PriceForTransport;
          uint8_t TransportCellID;
          uint16_t TransportCellReservationID;
          uint8_t PriceForProcess;
          uint8_t ProcessCellID;
          uint16_t ProcessCellReservationID;
          UA_DateTime TimeOfProcess;
          UA_DateTime TimeOfTransport;
          bool NeedForTransport:1;
          bool IsTransport:1;
          bool IsProcess:1;
          bool IsStepDone:1;
        } TRecipeStep;
      ```  
    - Card-level container and size constants:  
      ```53:66:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h
        typedef struct
        {
          TRecipeInfo sRecipeInfo;
          TRecipeStep *sRecipeStep;
          uint8_t sUid[7];
          uint8_t sUidLength;
          bool TRecipeInfoLoaded;
          bool TRecipeStepArrayCreated;
          bool TRecipeStepLoaded;
        } TCardInfo;
        ...
        static const size_t TRecipeInfo_Size = sizeof(TRecipeInfo);
        static const size_t TRecipeStep_Size = sizeof(TRecipeStep);
      ```  
  - **Relevant functions**:  
    - Memory-level load/store: `NFC_LoadTRecipeInfoStructure`, `NFC_LoadTRecipeSteps`, `NFC_LoadTRecipeStep`, `NFC_WriteStructRange`, `NFC_WriteAllData`.  
    - Checksum and structure helpers: `NFC_GetCheckSum`, `NFC_CreateCardInfoFromRecipeInfo`, `NFC_AddRecipeStepsToCardInfo`, `NFC_ChangeRecipeStepsSize`.

- **`components/NFC_Reader/NFC_reader.c`**  
  - **Purpose**: Implements reading and writing of the binary recipe format to NFC tags for MIFARE Classic and Ultralight, including checksum maintenance.  
  - **Relevant functions**:  
    - High-level printing of raw header + steps:  
      ```113:133:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
      void NFC_Print(TCardInfo aCardInfo)
      {
        ...
        for (int i = 0; i < TRecipeInfo_Size; ++i)
        {
          printf("%d ", *(((uint8_t *)&aCardInfo.sRecipeInfo) + i));
        }
        ...
        for (int j = 0; j < aCardInfo.sRecipeInfo.RecipeSteps; j++)
        {
          ...
          for (int i = 0; i < TRecipeStep_Size; i++)
          {
            printf("%d ", *(((uint8_t *)aCardInfo.sRecipeStep) + j * TRecipeStep_Size + i));
          }
        }
      }
      ```  
    - Writing a contiguous range of bytes (header + selected steps) to tag memory with checksum update: `NFC_WriteStructRange` (see more below).  
    - Reading the header (`TRecipeInfo`) from tag memory: `NFC_LoadTRecipeInfoStructure`.  
    - Reading all steps (`TRecipeStep` array) from tag memory: `NFC_LoadTRecipeSteps`.  
    - Reading a single step: `NFC_LoadTRecipeStep`.  
    - Memory index mapping for MIFARE Classic: `NFC_GetMifareClassicIndex`.  
    - Alloc and lifetime of step array: `NFC_AllocTRecipeStepArray`, `NFC_DeAllocTRecipeStepArray`, `NFC_InitTCardInfo`.  
    - Binary checksum algorithm over all steps: `NFC_GetCheckSum`.  
    - In-memory recipe construction / serialization helpers: `NFC_CreateCardInfoFromRecipeInfo`, `NFC_AddRecipeStepsToCardInfo`, `NFC_ChangeRecipeStepsSize`, `NFC_CopyTCardInfo`.

- **`components/NFC_Handler/NFC_handler.h` / `NFC_handler.c`**  
  - **Purpose**: Manages an in-memory working copy (`sWorkingCardInfo`) and an “integrity” copy (`sIntegrityCardInfo`) around NFC I/O, tracking which steps are dirty and syncing ranges to the tag.  
  - **Relevant structures**:  
    ```15:22:ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.h
      typedef struct
        {
          pn532_t sNFC;
          TCardInfo sIntegrityCardInfo;
          TCardInfo sWorkingCardInfo;
          size_t sRecipeStepIndexArraySize;
          bool* sRecipeStepIndexArray;
        } THandlerData;
    ```  
  - **Relevant functions**:  
    - Lifecycle: `NFC_Handler_Init`, `NFC_Handler_SetUp`.  
    - Loading from NFC tag into handler: `NFC_Handler_LoadData`.  
    - Working/integrity synchronization and dirty-range tracking: `NFC_Handler_WriteStep`, `NFC_Handler_WriteInfo`, `NFC_Handler_Sync`, `NFC_Handler_WriteSafeInfo`, `NFC_Handler_WriteSafeStep`, `NFC_Handler_AddCardInfoToWorking`.  
    - Helpers for in-memory access: `NFC_Handler_GetRecipeInfo`, `NFC_Handler_GetRecipeStep`, `NFC_Handler_ResizeIndexArray`.

- **`components/NFC_Recipes/NFC_recipes.h` / `NFC_recipes.c`**  
  - **Purpose**: Defines logical “recipes” (sequences of `TRecipeStep`) and provides helpers for synthesizing card contents without external input (reader-generated recipes).  
  - **Relevant constructs**:  
    - Process type enums, used in `TRecipeStep.TypeOfProcess`:  
      ```28:38:ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h
      enum ProcessTypes {
          ToStorageGlass,
          StorageAlcohol,
          StorageNonAlcohol,
          Shaker,
          Cleaner,
          SodaMake,
          ToCustomer,
          Transport,
          Buffer
      };
      ```  
    - Empty recipes for initialization:  
      ```50:55:ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h
      static const TRecipeStep EmptyRecipeStep = {
          0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
      };
      static const TRecipeInfo EmptyRecipeInfo= {
          0,0,0,0,0,0,0,0,0
      };
      ```  
  - **Relevant functions**:  
    - `GetRecipeStepByNumber`: maps numeric “step templates” to a specific `TRecipeStep` (storage, shaker, cleaner, transport etc.).  
    - `GetCardInfoByNumber`: constructs a full `TCardInfo`+steps in memory, used when the reader generates a new recipe (e.g. “return to storage”).  
    - `AddRecipe`: dynamically inserts a new step into an existing recipe on the tag, updating `RecipeSteps` and `NextID` chain.  
    - `ChangeID`, `SaveLocalsData`, `GetMinule`: helpers for ID and linked-list navigation.

- **`main/app.c`**  
  - **Purpose**: Application entry point and state machine. Orchestrates NFC reading/writing, PLC OPC UA communication, recipe state machine, and dynamic recipe modification.  
  - **Relevant parts**:  
    - “Recipe empty” detection logic, which interprets header and first step to decide if the tag has a valid recipe:  
      ```81:133:ESP32_Firmware_ASS_Interpreter/main/app.c
      static bool NFC_IsRecipeEmpty(const TRecipeInfo *info, const TRecipeStep *steps, int stepCount,
                                    char *reasonBuf, size_t reasonBufSize)
      {
        ...
        if (info->RecipeSteps == 0)
        ...
        if (info->RecipeSteps > MAX_RECIPE_STEPS)
        ...
        if (stepCount > 0 && (unsigned)info->ActualRecipeStep >= (unsigned)stepCount && !info->RecipeDone)
        ...
        if (stepCount > 0 && steps != NULL)
        {
          const TRecipeStep *s0 = &steps[0];
          if (s0->TypeOfProcess == 0 && s0->ParameterProcess1 == 0 && s0->ParameterProcess2 == 0
              && s0->ProcessCellID == 0 && s0->TransportCellID == 0
              && info->ID == 0 && info->NumOfDrinks == 0 && info->ActualBudget == 0
              && info->Parameters == 0 && info->RightNumber == 0)
          {
            ...
            return true;
          }
        }
        return false;
      }
      ```  
    - High-level state machine (`State_Machine`) that:  
      - Triggers `NFC_Handler_LoadData` when a card is detected.  
      - Interprets `RecipeSteps`, `ActualRecipeStep`, `RecipeDone`, and each `TRecipeStep`’s flags to decide state transitions.  
      - Modifies recipes dynamically (e.g. auto-inserting transport steps, generating new recipes when a drink is finished).  
      - Writes back changes via `NFC_Handler_WriteInfo`, `NFC_Handler_WriteStep`, and `NFC_Handler_Sync`.  
    - Reader-generated recipe paths: `State_Mimo_NastaveniNaPresunDoSkladu` and `State_NovyRecept` (detailed in section 8).

---

### 2. Firmware Data Flow

#### 2.1 Read path: NFC tag → raw bytes → `TCardInfo`

1. **Detect card & UID**  
   - `Is_Card_On_Reader` periodically checks presence via `NFC_isCardReady`, which calls `pn532_readPassiveTargetID`, only returning a boolean presence flag (`true` if at least one ISO14443A tag is reported).  
     ```1153:1168:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
     bool NFC_isCardReady(pn532_t *aNFC)
     {
       ...
       bool iStatus = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, iuid, &iuidLength, TIMEOUTCHECKCARD);
       ...
       return iStatus;
     }
     ```  

2. **Load data into handler (`THandlerData`)**  
   - When the card is first detected in the state machine (`State_Mimo_Polozena`):  
     ```291:360:ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c
     uint8_t NFC_Handler_LoadData(THandlerData* aHandlerData)
     {
       ...
       uint8_t Error = NFC_Handler_IsSameData(aHandlerData,&tempRecipeInfo);
       ...
       if(NeedLoadInfo || NewCard)
       {
         ...
         if(tempRecipeInfo.RecipeSteps != aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps)
         {
           NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo,tempRecipeInfo.RecipeSteps);
         }
         aHandlerData->sIntegrityCardInfo.sRecipeInfo = tempRecipeInfo;
       }
       if(NeedLoadData || NewCard)
       {
         ...
         Error = NFC_LoadTRecipeSteps(&aHandlerData->sNFC,&aHandlerData->sIntegrityCardInfo);
         ...
         aHandlerData->sIntegrityCardInfo.TRecipeStepLoaded = true;
       }
       ...
       NFC_Handler_ResizeIndexArray(aHandlerData,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
       NFC_Handler_CopyToWorking(aHandlerData,NeedLoadInfo,NeedLoadData);
       return 0;
     }
     ```  
   - `NFC_Handler_IsSameData` always reads `TRecipeInfo` from tag into a temporary `TCardInfo` (`aTempData`) using `NFC_LoadTRecipeInfoStructure`, then compares against the last known integrity copy:  
     - If `ID + RightNumber != 255`, the card is treated as “other card” and its header is returned.  
     - The checksum (`sRecipeInfo.CheckSum`) is also compared with recalculated `NFC_GetCheckSum` over steps (see section 6).  

3. **Load header structure from tag (`TRecipeInfo`)**  
   - `NFC_LoadTRecipeInfoStructure` reads bytes from tag, mapping them into `aCardInfo->sRecipeInfo` sequentially:  
     - Card type is determined by UID length: `iuidLength == 4` → “NFC classic” (MIFARE Classic), `iuidLength == 7` → “NFC ultralight”.  
     - For Classic, 16-byte blocks (`PAGESIZE_CLASSIC`) are authenticated and read with `pn532_mifareclassic_ReadDataBlock`, then copied linearly into the `TRecipeInfo` struct until `TRecipeInfo_Size` bytes are filled.  
       ```559:590:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
       if (iuidLength == 4)
       {
         ...
         for (int i = 0; i <= PosledniBunka; ++i)
         {
           size_t index = NFC_GetMifareClassicIndex(i);
           ...
           uint8_t success = pn532_mifareclassic_ReadDataBlock(aNFC, index, iData);
           ...
           for (int k = 0; k < PAGESIZE_CLASSIC; ++k)
           {
             if (k + i * PAGESIZE_CLASSIC < TRecipeInfo_Size)
             {
               *(((uint8_t *)&aCardInfo->sRecipeInfo) + k + i * PAGESIZE_CLASSIC) = iData[k];
             }
             else
             {
               break;
             }
           }
         }
       }
       ```  
     - For Ultralight, 16-byte logical “blocks” are read via `pn532_mifareultralight_ReadPage` (4 pages/16 bytes) and similarly copied into `sRecipeInfo`.  
   - After reading, `NFC_saveUID` persists UID and UID length into `TCardInfo.sUid` and `sUidLength` so the application can derive an `sr_id`:  
     ```668:673:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
     NFC_saveUID(aCardInfo, iuid, iuidLength);
     aCardInfo->TRecipeInfoLoaded = true;
     ```  

4. **Allocate and load steps (`TRecipeStep` array)**  
   - `NFC_AllocTRecipeStepArray` uses `TRecipeStep_Size` and `sRecipeInfo.RecipeSteps` to allocate a contiguous array of steps:  
     ```1071:1087:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
     aCardInfo->sRecipeStep = (TRecipeStep *)malloc(TRecipeStep_Size * aCardInfo->sRecipeInfo.RecipeSteps);
     ...
     aCardInfo->TRecipeStepArrayCreated = true;
     ```  
   - `NFC_LoadTRecipeSteps` reads all step bytes following the header region into this array:  
     - For Classic, it computes `zacatek = TRecipeInfo_Size` and `konec = TRecipeInfo_Size + RecipeSteps * TRecipeStep_Size - 1`, then reads all required blocks, remapping bytes so that the first recipe step starts at offset 0 in the `sRecipeStep` array.  
       ```705:752:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
       size_t zacatek = TRecipeInfo_Size;
       size_t konec = TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size - 1;
       ...
       size_t Posun = +zacatek % PAGESIZE_CLASSIC;
       ...
       *(((uint8_t *)aCardInfo->sRecipeStep) + IndexovaPosun) = iData[Propocet];
       ```  
     - Ultralight uses similar logic with a 16-byte block size and `OFFSETDATA_ULTRALIGHT` for the starting page.  
   - `NFC_LoadTRecipeStep` is a single-step version, useful for comparisons and partial checks.

5. **Handler working copy**  
   - Once `sIntegrityCardInfo` has valid header and steps, `NFC_Handler_CopyToWorking` copies them into `sWorkingCardInfo` (the mutable “working buffer” used by the application), taking care to resize the working step array as needed.  
     ```375:439:ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c
     uint8_t NFC_Handler_CopyToWorking(THandlerData* aHandlerData,bool InfoData,bool StepsData)
     {
       ...
       if(aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps != aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps)
       {
         Error = NFC_ChangeRecipeStepsSize(&aHandlerData->sWorkingCardInfo,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
       }
       ...
       if(InfoData)
       {
         aHandlerData->sWorkingCardInfo.sRecipeInfo = aHandlerData->sIntegrityCardInfo.sRecipeInfo;
       }
       ...
       if(StepsData)
       {
         for (size_t i = 0; i < aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps; ++i)
         {
           if(aHandlerData->sRecipeStepIndexArray[i])
             continue;
           for (size_t j = 0; j < TRecipeStep_Size; j++)
           {
             *((uint8_t *)(aHandlerData->sWorkingCardInfo.sRecipeStep)+j+i*TRecipeStep_Size) =
               *((uint8_t *)(aHandlerData->sIntegrityCardInfo.sRecipeStep)+j+i*TRecipeStep_Size);
           }
         }
       }
       ...
     }
     ```  

#### 2.2 Write path: `TCardInfo` → buffer → NFC tag

1. **Modify working copy (`sWorkingCardInfo`)**  
   - Application logic (state machine or recipe helpers) edits the working header or steps:  
     - Steps: `NFC_Handler_WriteStep` copies a `TRecipeStep` into `sWorkingCardInfo.sRecipeStep[aIndex]` and marks that index as dirty in `sRecipeStepIndexArray`.  
       ```555:567:ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c
       uint8_t NFC_Handler_WriteStep(THandlerData* aHandlerData,TRecipeStep *  aRecipeStep, size_t aIndex)
       {
         ...
         aHandlerData->sWorkingCardInfo.sRecipeStep[aIndex] = *aRecipeStep;
         NFC_Handler_ResizeIndexArray(aHandlerData,aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps);
         NFC_Handler_AddIndex(aHandlerData,aIndex);
         return 0;
       }
       ```  
     - Header: `NFC_Handler_WriteInfo` copies `TRecipeInfo` into `sWorkingCardInfo.sRecipeInfo` and resizes the index array.  

2. **High-level sync (`NFC_Handler_Sync`)**  
   - This function determines which ranges of “structures” (index 0 = header, 1..N = steps) must be written to the tag and uses `NFC_WriteCheck` / `NFC_WriteStructRange` to write and verify them.  
   - If no steps are marked dirty (`zapis == false`), it still checks whether header bytes match the tag via `NFC_CheckStructArrayIsSame` and, if not, writes just structure 0 (header):  
     ```600:641:ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c
     if (!zapis)
     {
       Error = NFC_CheckStructArrayIsSame(&aHandlerData->sNFC, &aHandlerData->sWorkingCardInfo, 0, 0);
       ...
       case 1:
         stejne = false;
         ...
         Error = NFC_WriteCheck(&aHandlerData->sNFC,&aHandlerData->sWorkingCardInfo,0,0);
         ...
         aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded = true;
         ...
         aHandlerData->sIntegrityCardInfo.sRecipeInfo = aHandlerData->sWorkingCardInfo.sRecipeInfo;
       ...
     }
     ```  
   - If steps are dirty, it groups consecutive dirty indices into ranges `[zacatek, konec]` (counted in structures, not bytes) and for each range:  
     - Calls `NFC_WriteCheck` (which eventually calls `NFC_WriteStructRange`).  
     - On success, copies the corresponding bytes from working to integrity `TCardInfo` and clears the dirty flags for that sub-range.  
     ```664:707:ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c
     for (size_t i = 0; i < aHandlerData->sRecipeStepIndexArraySize; ++i)
     {
       if(aHandlerData->sRecipeStepIndexArray[i] == 0 && zacatekSet)
       {
         konec = i;
         ...
         Error = NFC_WriteCheck(&aHandlerData->sNFC, &aHandlerData->sWorkingCardInfo, zacatek, konec);
         ...
         for (size_t j = zacatek+(zacatek == 0); j <= konec; ++j)
         {
           for (size_t k = 0; k < TRecipeStep_Size; k++)
           {
             *((uint8_t *)aHandlerData->sIntegrityCardInfo.sRecipeStep+(j-1)*TRecipeStep_Size + k) =
               *((uint8_t *)aHandlerData->sWorkingCardInfo.sRecipeStep+(j-1)*TRecipeStep_Size + k);
           }
           aHandlerData->sRecipeStepIndexArray[j-1] = false;
         }
       }
     }
     ```  

3. **Low-level buffer construction and write (`NFC_WriteStructRange`)**  
   - Before writing any bytes, `NFC_WriteStructRange` recomputes the checksum over all steps and, if it differs from the header’s `CheckSum`, updates `sRecipeInfo.CheckSum` and (optionally) arranges to also write the header structure:  
     ```234:244:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
     uint16_t CheckSumNew = NFC_GetCheckSum(*aCardInfo);
     if (CheckSumNew != aCardInfo->sRecipeInfo.CheckSum)
     {
       aCardInfo->sRecipeInfo.CheckSum = CheckSumNew;
       ...
       if (NumOfStructureStart != 0)
       {
         ...
         NFC_WriteStruct(aNFC, aCardInfo, 0);
       }
     }
     ```  
   - It then determines the byte range `[zacatek, konec]` in the linear recipe memory:  
     - `zacatek = 0, konec = TRecipeInfo_Size - 1` for header-only (`NumOfStructureStart == 0 && NumOfStructureEnd == 0`).  
     - For steps:  
       ```222:232:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
       size_t zacatek = 0;
       size_t konec = TRecipeInfo_Size - 1;
       if (NumOfStructureStart > 0)
       {
         zacatek = TRecipeInfo_Size + (NumOfStructureStart - 1) * TRecipeStep_Size;
       }
       if (NumOfStructureEnd > 0)
       {
         konec = TRecipeInfo_Size + (NumOfStructureStart - 1) * TRecipeStep_Size +
                 TRecipeStep_Size * (NumOfStructureEnd - NumOfStructureStart + 1) - 1;
       }
       ```  
   - For **MIFARE Classic** (UID length 4):  
     - The tag is treated as consecutive 16-byte “cells” of recipe data, with logical cell index `i` mapped to physical block index via `NFC_GetMifareClassicIndex`.  
     - For each cell `i` between `PrvniBunka = zacatek / 16` and `PosledniBunka = konec / 16`, bytes are filled from:  
       - Header (`sRecipeInfo`) if `i*16 + k < TRecipeInfo_Size`.  
       - Step array (`sRecipeStep`) if inside recipe range.  
       - Zero if beyond `TRecipeInfo_Size + RecipeSteps*TRecipeStep_Size`.  
       ```261:286:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
       uint8_t iData[PAGESIZE_CLASSIC];
       size_t PrvniBunka = zacatek / PAGESIZE_CLASSIC;
       size_t PosledniBunka = konec / PAGESIZE_CLASSIC;
       for (int i = PrvniBunka; i <= PosledniBunka; ++i)
       {
         ...
         for (size_t k = 0; k < PAGESIZE_CLASSIC; k++)
         {
           if (i * PAGESIZE_CLASSIC + k < TRecipeInfo_Size)
           {
             iData[k] = *(((uint8_t *)&(aCardInfo->sRecipeInfo)) + i * PAGESIZE_CLASSIC + k);
           }
           else if (i * PAGESIZE_CLASSIC + k < TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size)
           {
             iData[k] = *(((uint8_t *)aCardInfo->sRecipeStep) + i * PAGESIZE_CLASSIC + k - TRecipeInfo_Size);
           }
           else
           {
             iData[k] = 0;
           }
         }
         ...
         uint8_t Zapsano = pn532_mifareclassic_WriteDataBlock(aNFC, index, iData);
       }
       ```  
   - For **MIFARE Ultralight** (UID length 7):  
     - Logical cell size is 4 bytes (`PAGESIZE_ULTRALIGHT`), but writes are performed as 16-byte “blocks” corresponding to 4 pages starting at `OFFSETDATA_ULTRALIGHT`.  
     - The same header/steps/zeroing logic is applied with a 4-byte stride.  
       ```311:341:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
       uint8_t iData[PAGESIZE_ULTRALIGHT];
       size_t PrvniBunka = zacatek / PAGESIZE_ULTRALIGHT;
       size_t PosledniBunka = konec / PAGESIZE_ULTRALIGHT;
       for (int i = PrvniBunka; i <= PosledniBunka; ++i)
       {
         ...
         if (i * PAGESIZE_ULTRALIGHT + k < TRecipeInfo_Size)
           iData[k] = *(((uint8_t *)&(aCardInfo->sRecipeInfo)) + i * PAGESIZE_ULTRALIGHT + k);
         else if (i * PAGESIZE_ULTRALIGHT + k <
                  TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size)
           iData[k] = *(((uint8_t *)aCardInfo->sRecipeStep) + i * PAGESIZE_ULTRALIGHT + k - TRecipeInfo_Size);
         else
           iData[k] = 0;
         ...
         uint8_t Zapsano = pn532_mifareultralight_WritePage(aNFC, i + OFFSETDATA_ULTRALIGHT, iData);
       }
       ```  

4. **“Write with verification” (`NFC_WriteCheck`)**  
   - Called by `NFC_Handler_Sync` for each dirty range:  
     - Retries `NFC_WriteStructRange` with up to `MAXERRORREADING` attempts.  
     - Then uses `NFC_CheckStructArrayIsSame` (which reads back the same range from the tag using `NFC_LoadTRecipeInfoStructure` / `NFC_LoadTRecipeStep`) to confirm byte-for-byte equality.  
     ```1349:1430:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
     uint8_t NFC_WriteCheck(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t NumOfStructureStart, uint16_t NumOfStructureEnd)
     {
       ...
       Error = NFC_WriteStructRange(aNFC, aCardInfo, NumOfStructureStart, NumOfStructureEnd);
       ...
       Error = NFC_CheckStructArrayIsSame(aNFC, aCardInfo, NumOfStructureStart, NumOfStructureEnd);
       ...
     }
     ```  

---

### 3. Binary Recipe Format (Ground Truth)

All binary data is written as a **raw memory image of packed C structs**, with no padding between fields thanks to `__attribute__((packed))` in `TRecipeInfo` and `TRecipeStep`. The firmware always reads/writes entire bytes and 16-bit fields via `uint8_t` pointers, so on the wire:

- **Byte order**: All multi-byte integers (`uint16_t`, `UA_DateTime`) are stored in **little-endian** order, matching the ESP32’s native layout and the sequential `uint8_t` copy operations.  
- **Layout**:  
  - First `TRecipeInfo_Size` bytes: header (`TRecipeInfo`).  
  - Immediately followed by `RecipeSteps` consecutive `TRecipeStep` structs, each of size `TRecipeStep_Size`.  
  - No explicit terminator; `RecipeSteps` and `NextID`/`RecipeDone` drive parsing.

#### 3.1 Header layout (`TRecipeInfo`)

From `NFC_reader.h` and the packed attribute, and confirmed by `TRecipeInfo_Size = sizeof(TRecipeInfo)`:

- Field sequence (in memory and on tag):  
  ```18:29:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h
  typedef struct __attribute__((packed))
    {
      uint8_t ID;
      uint16_t NumOfDrinks;
      uint8_t RecipeSteps;
      uint8_t ActualRecipeStep;
      uint16_t ActualBudget;
      uint8_t Parameters;
      uint8_t RightNumber;
      bool RecipeDone;
      uint16_t CheckSum;   //Checksum musi byt vzdy posledni
    } TRecipeInfo;
  ```  

Assuming standard sizes on ESP32 (`bool` = 1 byte, `uint16_t` = 2 bytes), the header byte layout is:

| offset | size (bytes) | name              | description                                                                                  |
|--------|--------------|-------------------|----------------------------------------------------------------------------------------------|
| 0      | 1            | `ID`              | Card/recipe identifier. Used with `RightNumber` as a basic integrity check (ID + RightNumber == 255). |
| 1      | 2 (LE)       | `NumOfDrinks`     | Counter of how many drinks have been produced from this recipe.                              |
| 3      | 1            | `RecipeSteps`     | Number of valid `TRecipeStep` structures following the header.                              |
| 4      | 1            | `ActualRecipeStep`| Index of current step (0-based). Must be `< RecipeSteps` unless `RecipeDone == true`.       |
| 5      | 2 (LE)       | `ActualBudget`    | Remaining “budget” (used in state machine for transport/process costs).                      |
| 7      | 1            | `Parameters`      | Generic parameter byte (interpreted by application logic; not parsed in NFC layer).         |
| 8      | 1            | `RightNumber`     | Must satisfy `ID + RightNumber == 255` for a valid card (checked in `NFC_Handler_IsSameData`). |
| 9      | 1            | `RecipeDone`      | Boolean flag; when true, recipe is considered finished regardless of `ActualRecipeStep`.    |
| 10     | 2 (LE)       | `CheckSum`        | 16-bit checksum of all recipe step bytes; must be last in the struct.                       |

- **Header size**: `TRecipeInfo_Size = 12` bytes (1+2+1+1+2+1+1+1+2).  
- **First step offset**: `TRecipeInfo_Size` (12).  

#### 3.2 Step layout (`TRecipeStep`)

Struct definition:

```30:50:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h
typedef struct __attribute__((packed))
  {
    uint8_t ID;
    uint8_t NextID;
    uint8_t TypeOfProcess;
    uint8_t ParameterProcess1;
    uint16_t ParameterProcess2;
    uint8_t PriceForTransport;
    uint8_t TransportCellID;
    uint16_t TransportCellReservationID;
    uint8_t PriceForProcess;
    uint8_t ProcessCellID;
    uint16_t ProcessCellReservationID;
    UA_DateTime TimeOfProcess;
    UA_DateTime TimeOfTransport;
    bool NeedForTransport:1;
    bool IsTransport:1;
    bool IsProcess:1;
    bool IsStepDone:1;
  } TRecipeStep;
```  

`UA_DateTime` from open62541 is a 64-bit signed integer representing 100-ns intervals since 1601-01-01. With packing and 4 1-bit flags packed into a single underlying `uint8_t`, the layout is:

| offset | size (bytes) | name                        | description                                                                                           |
|--------|--------------|-----------------------------|-------------------------------------------------------------------------------------------------------|
| 0      | 1            | `ID`                        | Step ID (0..RecipeSteps-1). Application builds a linked list using `NextID`.                         |
| 1      | 1            | `NextID`                    | ID of next step. If `NextID == ID`, this is considered the last step.                               |
| 2      | 1            | `TypeOfProcess`             | Process type (enum `ProcessTypes`: `ToStorageGlass`, `StorageAlcohol`, …, `Transport`, `Buffer`).    |
| 3      | 1            | `ParameterProcess1`         | First parameter; semantics depend on `TypeOfProcess` (e.g. alcohol type, volume, etc.).             |
| 4      | 2 (LE)       | `ParameterProcess2`         | Second parameter; semantics depend on `TypeOfProcess` (e.g. volume in ml).                          |
| 6      | 1            | `PriceForTransport`         | Cost contribution used in budget calculation during transport.                                      |
| 7      | 1            | `TransportCellID`           | ID of chosen transport cell (from LDS).                                                              |
| 8      | 2 (LE)       | `TransportCellReservationID`| Reservation ID for transport cell (used in OPC UA calls).                                           |
| 10     | 1            | `PriceForProcess`           | Cost contribution for process operation.                                                              |
| 11     | 1            | `ProcessCellID`             | ID of chosen process cell.                                                                           |
| 12     | 2 (LE)       | `ProcessCellReservationID`  | Reservation ID for process cell.                                                                     |
| 14     | 8 (LE)       | `TimeOfProcess`             | Planned/actual time for process action (UA_DateTime).                                               |
| 22     | 8 (LE)       | `TimeOfTransport`           | Planned/actual time for transport action (UA_DateTime).                                             |
| 30     | 1 (4 bits)   | `NeedForTransport` (bit 0)  | 1-bit flag; true if a transport must be executed for this step.                                     |
| 30     | 1 (4 bits)   | `IsTransport` (bit 1)       | 1-bit flag; set when transport action has been executed.                                            |
| 30     | 1 (4 bits)   | `IsProcess` (bit 2)         | 1-bit flag; set when process action is currently in progress / permitted.                           |
| 30     | 1 (4 bits)   | `IsStepDone` (bit 3)        | 1-bit flag; set when the step as a whole is finished.                                               |

- **Step size**: `TRecipeStep_Size = 31` bytes (as used in allocation and copy loops in `NFC_reader.c`).  

#### 3.3 Overall recipe layout and limits

- **Header size**: `TRecipeInfo_Size` = 12 bytes.  
- **Step size**: `TRecipeStep_Size` = 31 bytes.  
- **First step offset**: 12 (immediately after header).  
- **Total size for N steps**:  
  - `TotalBytes = TRecipeInfo_Size + RecipeSteps * TRecipeStep_Size`.  
  - Used consistently in `NFC_LoadTRecipeSteps`, `NFC_WriteStructRange`, and related index computations.  
- **Maximum number of steps**:  
  - Hard application-level bound `MAX_RECIPE_STEPS = 64` in `main/app.c` for considering a recipe valid:  
    ```71:75:ESP32_Firmware_ASS_Interpreter/main/app.c
    #define MAX_RECIPE_STEPS 64
    ```  
  - `NFC_IsRecipeEmpty` treats any tag with `RecipeSteps > MAX_RECIPE_STEPS` as “empty/uninitialized”.  
  - There is no lower-level NFC bound besides tag capacity.  

---

### 4. Recipe Parsing Logic

This section describes how the firmware decodes an existing tag into a coherent recipe and how it determines if the recipe is valid, how many steps it has, and when it ends.

#### 4.1 Header read and validation

1. **Read header bytes** using `NFC_LoadTRecipeInfoStructure` as described in section 2.1.  
2. **Persist UID and mark header as loaded**:  
   ```668:671:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
   NFC_saveUID(aCardInfo, iuid, iuidLength);
   aCardInfo->TRecipeInfoLoaded = true;
   ```  
3. **Application-level integrity checks (`NFC_Handler_IsSameData`)**:  
   - Verifies `ID + RightNumber == 255`; otherwise returns error code 5 (“other card”) and treats data as untrusted.  
     ```219:223:ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c
     if(aTempData.sRecipeInfo.ID + aTempData.sRecipeInfo.RightNumber != 255)
     {
       return 5;
     }
     ```  
   - Compares header bytes (excluding checksum) between last integrity copy and newly read header.  
   - Recomputes checksum via `NFC_GetCheckSum` and compares to `sRecipeInfo.CheckSum` to see if step bytes have changed.  

4. **“Empty recipe” detection (`NFC_IsRecipeEmpty`)**:  
   - Once `NFC_Handler_LoadData` loads header+steps into `sWorkingCardInfo`, the state machine calls `NFC_IsRecipeEmpty` to decide whether to skip PLC logic and treat the tag as empty/uninitialized:  
     - Conditions considered “empty”:  
       - `RecipeSteps == 0`.  
       - `RecipeSteps > MAX_RECIPE_STEPS`.  
       - `steps == NULL` while `stepCount > 0`.  
       - `ActualRecipeStep >= stepCount` and `RecipeDone == false`.  
       - First step and core header fields all zero.  
     ```81:132:ESP32_Firmware_ASS_Interpreter/main/app.c
     if (info->RecipeSteps == 0) ...
     if (info->RecipeSteps > MAX_RECIPE_STEPS) ...
     if (steps == NULL && stepCount > 0) ...
     if (stepCount > 0 && (unsigned)info->ActualRecipeStep >= (unsigned)stepCount && !info->RecipeDone) ...
     if (stepCount > 0 && steps != NULL)
     {
       const TRecipeStep *s0 = &steps[0];
       if (s0->TypeOfProcess == 0 && s0->ParameterProcess1 == 0 && s0->ParameterProcess2 == 0
           && s0->ProcessCellID == 0 && s0->TransportCellID == 0
           && info->ID == 0 && info->NumOfDrinks == 0 && info->ActualBudget == 0
           && info->Parameters == 0 && info->RightNumber == 0)
       {
         ...
         return true;
       }
     }
     ```  
   - For the Android app, these same rules must be mirrored when deciding whether a tag contains a usable recipe.

#### 4.2 Step array parsing

1. **Allocate step array** sized as `RecipeSteps * TRecipeStep_Size` via `NFC_AllocTRecipeStepArray`.  
2. **Fill raw bytes** using `NFC_LoadTRecipeSteps` or `NFC_LoadTRecipeStep`, which copy each byte in order into the allocated memory.  
3. **Interpretation as `TRecipeStep[]`** is purely by struct layout; the firmware never adds per-field decoding logic in the NFC layer. Instead, higher-level code in `NFC_recipes.c` and `main/app.c` interprets:  
   - `TypeOfProcess` against `enum ProcessTypes`.  
   - `ParameterProcess1` / `ParameterProcess2` according to process type (e.g. alcohol type, volume, cleaning time).  
   - `NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`, `TimeOfProcess`, `TimeOfTransport` for state transitions.  

#### 4.3 Determining the number of steps

- **Primary source**: `RecipeSteps` field in `TRecipeInfo`.  
  - This is used everywhere as the canonical step count:  
    - Allocation: `NFC_AllocTRecipeStepArray`, `NFC_ChangeRecipeStepsSize`.  
    - Step iteration: loops in `NFC_Print`, `NFC_LoadTRecipeSteps`, `NFC_Handler_CopyToWorking`, state machine loops.  
    - Validation: `NFC_IsRecipeEmpty`, `NFC_Handler_LoadData` broken recipe detection.  
  - For example, state machine guard in `State_Mimo_Polozena`:  
    ```470:475:ESP32_Firmware_ASS_Interpreter/main/app.c
    if (iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep >=
        iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeSteps)
    {
      ...
      RAF = State_Mimo_NastaveniNaPresunDoSkladu; // broken recipe
    }
    ```  

#### 4.4 Step ordering and determining end of recipe

The firmware uses a **linked-list style** ordering inside the fixed-size step array:

- **ID**: step index (0..RecipeSteps-1).  
- **NextID**: next logical step’s `ID`. If `NextID == ID`, the step is considered the **final step**.  
  - Example when generating in `GetCardInfoByNumber`:  
    ```195:205:ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c
    for (size_t i = 0; i < tempRecipeInfo.RecipeSteps; ++i)
    {
      tempRecipeSteps[i].ID = i;
      if (i != tempRecipeInfo.RecipeSteps - 1)
      {
        tempRecipeSteps[i].NextID = i + 1;
      }
      else
      {
        tempRecipeSteps[i].NextID = i;
      }
    }
    ```  
- **ActualRecipeStep**: index of the current step in the array (starting from 0).  
- **RecipeDone**: when `true`, the entire recipe is considered finished even if `ActualRecipeStep < RecipeSteps`; `NFC_IsRecipeEmpty` uses this to allow “done” states with out-of-range `ActualRecipeStep`.  
- **End-of-recipe update path**:  
  - In `State_Vyroba_SpravneProvedeni`, once a process step is confirmed “done” by OPC / reservation logic, the state machine:  
    - Marks `IsStepDone = 1`.  
    - If `NextID != ID`, sets `ActualRecipeStep = NextID`.  
    - Else, sets `RecipeDone = 1` and increments `NumOfDrinks`.  
    ```911:946:ESP32_Firmware_ASS_Interpreter/main/app.c
    case 1:
    {
      ...
      tempStep.IsProcess = 0;
      tempStep.IsStepDone = 1;
      ...
      if (tempStep.NextID != tempStep.ID)
      {
        tempInfo.ActualRecipeStep = tempStep.NextID;
      }
      else
      {
        tempInfo.RecipeDone = 1;
        ++tempInfo.NumOfDrinks;
      }
      ...
    }
    ```  

---

### 5. Recipe Writing Logic

#### 5.1 Constructing `TCardInfo` from recipe info and steps

There are two main “writers” of recipe contents:

1. **External writer** (e.g. Android app) that populates `TCardInfo` / `TRecipeInfo` / `TRecipeStep` externally and uses NFC APIs.  
2. **On-reader generators** (`NFC_recipes.c` + state machine) that synthesize new card contents or modify existing ones directly on the ESP32.

Core NFC-level serialization is the same in both cases.

1. **Create header and allocate steps (`NFC_CreateCardInfoFromRecipeInfo`)**  
   - Copies the entire `TRecipeInfo` struct into `aCardInfo->sRecipeInfo`.  
   - Allocates a zero-initialized step array if `RecipeSteps > 0` and sets `CheckSum` to 0.  
     ```1474:1503:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
     for (size_t i = 0; i < TRecipeInfo_Size; ++i)
     {
       *(((uint8_t *)(&aCardInfo->sRecipeInfo)) + i) = *(((uint8_t *)(&aRecipeInfo)) + i);
     }
     ...
     if (aRecipeInfo.RecipeSteps > 0)
     {
       ...
       Error = NFC_AllocTRecipeStepArray(aCardInfo);
       ...
       for (size_t i = 0; i < TRecipeStep_Size * aCardInfo->sRecipeInfo.RecipeSteps; ++i)
       {
         *((uint8_t *)aCardInfo->sRecipeStep + i) = 0;
       }
       aCardInfo->sRecipeInfo.CheckSum = 0;
     }
     ```  

2. **Populate steps (`NFC_AddRecipeStepsToCardInfo`)**  
   - Ensures the array is allocated and sized to `SizeOfRecipeSteps`.  
   - Copies raw bytes from `aRecipeStep` into `aCardInfo->sRecipeStep`.  
   - Frees the source array if `DeAlloc` is true.  
   - Finally recomputes and stores the checksum:  
     ```1528:1583:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
     for (size_t i = 0; i < TRecipeStep_Size * aCardInfo->sRecipeInfo.RecipeSteps; ++i)
     {
       *((uint8_t *)aCardInfo->sRecipeStep + i) = *((uint8_t *)aRecipeStep + i);
     }
     ...
     aCardInfo->sRecipeInfo.CheckSum = NFC_GetCheckSum(*aCardInfo);
     ```  

3. **Dynamic size changes (`NFC_ChangeRecipeStepsSize`)**  
   - When adding/removing steps, this function either:  
     - Reallocates to new size, copying existing step bytes and zero-initializing any new steps; or  
     - Deallocates entirely if `NewSize == 0`.  
   - It does not write to the tag directly; that’s left for `NFC_Handler_Sync`.  

#### 5.2 Working buffer to tag

After steps and header in `sWorkingCardInfo` are in their desired in-memory form:

1. `NFC_Handler_WriteInfo` / `NFC_Handler_WriteStep` set the desired state and mark relevant indices as dirty.  
2. `NFC_Handler_Sync` computes dirty ranges and calls `NFC_WriteCheck` for each, which:  
   - Invokes `NFC_WriteStructRange` to rebuild the raw byte buffer for the range and write via PN532.  
   - Verifies written bytes with `NFC_CheckStructArrayIsSame`.  
3. On success, the integrity copy (`sIntegrityCardInfo`) is updated byte-for-byte from working memory.

#### 5.3 Checksum generation and inclusion

- See section 6 for full details; summary here:  
  - Checksum is recalculated on each `NFC_WriteStructRange` call and whenever `NFC_AddRecipeStepsToCardInfo` is used.  
  - Only **step bytes** are included.  
  - The checksum field itself is stored in the header and is never included in its own calculation.

#### 5.4 Unused memory / leftover data handling

Within the recipe region `[0 .. TRecipeInfo_Size + RecipeSteps*TRecipeStep_Size - 1]`, all bytes are significant and are read/written as-is. Beyond this region:

- **When writing**:  
  - For any 16-byte cell where `i*CellSize + k` is beyond total recipe length, the firmware explicitly writes 0. This applies to both Classic and Ultralight paths.  
  - Example for Classic:  
    ```272:286:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
    else
    {
      iData[k] = 0;
      NFC_READER_ALL_DEBUG("", "%d ", 0);
    }
    ```  
  - Example for Ultralight:  
    ```332:335:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
    else
    {
      iData[k] = 0;
      NFC_READER_ALL_DEBUG("", "%d ", 0);
    }
    ```  
- **When reading**:  
  - The reader never interprets bytes beyond `TRecipeInfo_Size` + computed recipe bytes. These extra bytes are ignored and never enter `TCardInfo`.

For Android, this means that after writing the header+steps, any bytes beyond the recipe region **should be written as zero** to stay consistent with firmware behavior. However, the firmware itself only guarantees zeroing in the last partially used block; bytes in completely unused blocks beyond `konec` are untouched.

---

### 6. Checksum Algorithm

#### 6.1 Implementation and location

- The checksum is implemented in `NFC_GetCheckSum` in `NFC_reader.c`:  
  ```1442:1461:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
  uint16_t NFC_GetCheckSum(TCardInfo aCardInfo)
  {
    ...
    if (aCardInfo.sRecipeInfo.RecipeSteps == 0)
    {
      aCardInfo.sRecipeInfo.CheckSum = 0;
      return 0;
    }
    uint16_t CheckSum = 0;
    ...
    for (size_t i = 0; i < TRecipeStep_Size * aCardInfo.sRecipeInfo.RecipeSteps; ++i)
    {
      CheckSum += *((uint8_t *)aCardInfo.sRecipeStep + i) * (i % 4 + 1);
    }
    ...
    return CheckSum;
  }
  ```  

#### 6.2 Bytes included

- **Included**: every byte of every recipe step in order:  
  - Index `i` runs from `0` to `TRecipeStep_Size * RecipeSteps - 1`.  
  - Each byte is taken from the contiguous array backing `TRecipeStep[]` via `(uint8_t*)aCardInfo.sRecipeStep + i`.  
- **Excluded**:  
  - Header bytes (`TRecipeInfo`), including the `CheckSum` field itself.  
  - Any tag memory beyond the recipe region.  

#### 6.3 Algorithm

- Let `b[i]` be the i-th byte of the step memory (0-based). The checksum is:

\[
\text{CheckSum} = \sum_{i=0}^{N-1} b[i] \cdot (i \bmod 4 + 1) \quad \text{mod } 2^{16}
\]

Where \( N = TRecipeStep\_Size \times \text{RecipeSteps} \).  
This is exactly what the loop `CheckSum += byte * (i % 4 + 1)` implements, with C’s 16-bit overflow naturally truncating the result.

#### 6.4 Where checksum is stored and updated

- The computed checksum is stored in `aCardInfo.sRecipeInfo.CheckSum` (last header field).  
- It is updated in two places:  
  - **When constructing card info from steps**:  
    ```1577:1583:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
    aCardInfo->sRecipeInfo.CheckSum = NFC_GetCheckSum(*aCardInfo);
    ```  
  - **Just before writing any range to NFC**:  
    ```234:243:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
    uint16_t CheckSumNew = NFC_GetCheckSum(*aCardInfo);
    if (CheckSumNew != aCardInfo->sRecipeInfo.CheckSum)
    {
      aCardInfo->sRecipeInfo.CheckSum = CheckSumNew;
      ...
      if (NumOfStructureStart != 0)
      {
        ...
        NFC_WriteStruct(aNFC, aCardInfo, 0);
      }
    }
    ```  
  - This ensures the header is always kept in sync with any changes in step bytes.

#### 6.5 Checksum verification

- **NFC layer**: `NFC_CheckStructArrayIsSame` compares raw bytes from tag vs in-memory `TCardInfo` including the stored `CheckSum`, but it does not recompute the checksum itself.  
- **Handler layer**: `NFC_Handler_IsSameData` recomputes the checksum from the in-memory `sIntegrityCardInfo` and compares it with the `CheckSum` read from tag (`aTempData.sRecipeInfo.CheckSum`):  
  ```257:265:ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c
  if(NFC_GetCheckSum(aHandlerData->sIntegrityCardInfo) == aTempData.sRecipeInfo.CheckSum)
  {
    SameData = true;
  }
  else
  {
    SameData = false;
  }
  ```  
- The combination of these ensures both **on-tag vs in-memory equality** and **logical consistency of header vs steps**.

---

### 7. Tag Type Handling (MIFARE Classic vs Ultralight / NTAG)

The firmware uses UID length from `pn532_readPassiveTargetID` to branch between tag types:

- **MIFARE Classic**: `iuidLength == 4`.  
  - Used in both read and write flows (`NFC_LoadTRecipeInfoStructure`, `NFC_LoadTRecipeSteps`, `NFC_LoadTRecipeStep`, `NFC_WriteStructRange`).  
  - Uses 16-byte data blocks (`PAGESIZE_CLASSIC = 16`), with memory layout remapped via `NFC_GetMifareClassicIndex` to skip sector trailers.  
  - Authentication with universal key `FF FF FF FF FF FF` for each data block.  
- **MIFARE Ultralight**: `iuidLength == 7`.  
  - Also used in read and write flows.  
  - Uses 4-byte pages (`PAGESIZE_ULTRALIGHT = 4`) but reads/writes 16-byte chunks at a time (4 pages).  
  - Recipe data starts at page index `OFFSETDATA_ULTRALIGHT = 8`, i.e. block address `(i*4) + OFFSETDATA_ULTRALIGHT`.  
- **NTAG2xx**:  
  - `pn532_ntag2xx_ReadPage` / `pn532_ntag2xx_WritePage` helpers are present in `pn532.c`, but the recipe reader/writer code (`NFC_reader.c`) does **not** call them. Only Ultralight and Classic are used.  

#### 7.1 Classic memory mapping

- Constants:  
  ```10:13:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
  #define OFFSETDATA_ULTRALIGHT 8
  #define OFFSETDATA_CLASSIC 1
  #define PAGESIZE_ULTRALIGHT 4
  #define PAGESIZE_CLASSIC 16
  ```  
- `NFC_GetMifareClassicIndex` converts a sequential data-block index `i` into a physical block number, skipping sector trailers every 4 blocks:  
  ```412:424:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
  uint8_t NFC_GetMifareClassicIndex(size_t i)
  {
    size_t number = 1 + OFFSETDATA_CLASSIC;
    for (int k = 0; k < i; ++k)
    {
      ++number;
      if (number % 4 == 0)
      {
        ++number;
      }
    }
    return number - 1;
  }
  ```  
- All reads/writes for header and steps use this mapping, building data blocks with linear offsets based on `TRecipeInfo_Size` and `TRecipeStep_Size`.

#### 7.2 Ultralight memory mapping

- Data region starts at page `OFFSETDATA_ULTRALIGHT = 8`.  
- For reads:  
  - `NFC_LoadTRecipeInfoStructure` uses:  
    ```620:645:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
    size_t PosledniBunka = (TRecipeInfo_Size - 1) / 16;
    for (int i = 0; i <= PosledniBunka; ++i)
    {
      uint8_t success = pn532_mifareultralight_ReadPage(aNFC, (i * 4) + OFFSETDATA_ULTRALIGHT, iData);
      ...
      for (int k = 0; k < 16; ++k)
      {
        if (k + i * 16 < TRecipeInfo_Size)
        {
          *(((uint8_t *)&aCardInfo->sRecipeInfo) + k + i * 16) = iData[k];
        }
      }
    }
    ```  
  - `NFC_LoadTRecipeSteps` uses a similar pattern for the step range, with careful handling of the initial offset inside the first 16-byte chunk:  
    ```783:823:ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c
    size_t zacatek = TRecipeInfo_Size;
    size_t konec = TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size - 1;
    ...
    size_t Posun = +zacatek % 16;
    ...
    *(((uint8_t *)aCardInfo->sRecipeStep) + IndexovaPosun) = iData[Propocet];
    ```  
- For writes (`NFC_WriteStructRange`), `iData` is filled from header and step bytes and written via `pn532_mifareultralight_WritePage` using a 4-byte page index `i + OFFSETDATA_ULTRALIGHT`.  

#### 7.3 Capacity and limits

The firmware does not hard-code specific capacity limits based on tag type; instead, it:

- Assumes sufficient capacity for `TRecipeInfo_Size + RecipeSteps*TRecipeStep_Size`.  
- Uses application-level `MAX_RECIPE_STEPS = 64` to guard against pathological or corrupted tags.  

For Android:

- For MIFARE Classic/Ultralight tags with smaller capacity, you must ensure `RecipeSteps` is chosen such that `TRecipeInfo_Size + RecipeSteps*TRecipeStep_Size` fits into usable data blocks. The firmware does no explicit capacity checking; overflow would result in incomplete writes/reads at PN532 level.

---

### 8. Legacy / Alternative Formats and Reader-Generated Recipes

The firmware contains logic where the reader **generates or modifies recipes directly** without external input. These paths are crucial to mirror when interpreting tags created by the ESP32 itself.

#### 8.1 Template-based step generation (`GetRecipeStepByNumber`)

- `GetRecipeStepByNumber(uint8_t aNumOfRecipe, uint16_t aParam)` maps numeric codes to specific step configurations:  
  ```48:123:ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c
  TRecipeStep GetRecipeStepByNumber(uint8_t aNumOfRecipe, uint16_t aParam)
  {
    TRecipeStep tempRecipeStep = EmptyRecipeStep;
    ...
    switch (aNumOfRecipe)
    {
    case 1:
      tempRecipeStep.TypeOfProcess = StorageAlcohol;
      tempRecipeStep.ParameterProcess1 = Vodka;
      tempRecipeStep.ParameterProcess2 = aParam;
      break;
    case 2:
      tempRecipeStep.TypeOfProcess = StorageAlcohol;
      tempRecipeStep.ParameterProcess1 = Rum;
      tempRecipeStep.ParameterProcess2 = aParam;
      break;
    ...
    case 6:
      tempRecipeStep.TypeOfProcess = Shaker;
      tempRecipeStep.ParameterProcess1 = aParam;
      break;
    ...
    case 10:
      tempRecipeStep.TypeOfProcess = Transport;
      tempRecipeStep.ParameterProcess1 = aParam;
      break;
    ...
    }
    return tempRecipeStep;
  }
  ```  

#### 8.2 Reader-generated full recipes (`GetCardInfoByNumber`)

- `GetCardInfoByNumber(uint8_t aNumOfRecipe)` builds complete recipes in memory (info + steps) using `EmptyRecipeInfo`, `GetRecipeStepByNumber`, and `NFC_CreateCardInfoFromRecipeInfo` / `NFC_AddRecipeStepsToCardInfo`.  
  ```134:212:ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c
  TCardInfo GetCardInfoByNumber(uint8_t aNumOfRecipe)
  {
    TCardInfo tempCardInfo;
    TRecipeInfo tempRecipeInfo = EmptyRecipeInfo;
    TRecipeStep *tempRecipeSteps = NULL;
    ...
    switch (aNumOfRecipe)
    {
      case 0:
        tempRecipeInfo.RecipeSteps = 5;
        ...
        tempRecipeSteps = malloc(TRecipeStep_Size * tempRecipeInfo.RecipeSteps);
        tempRecipeSteps[0] = GetRecipeStepByNumber(7, 5);
        ...
        break;
      case 1:
        tempRecipeInfo.RecipeSteps = 6;
        tempRecipeInfo.ActualRecipeStep = 0;
        tempRecipeInfo.ActualBudget = 200;
        ...
        break;
      ...
      case 3:
        tempRecipeInfo.RecipeSteps = 1;
        ...
        tempRecipeSteps[0] = GetRecipeStepByNumber(8, 0);
        ...
        break;
    }
    NFC_CreateCardInfoFromRecipeInfo(&tempCardInfo, tempRecipeInfo);
    ...
    for (size_t i = 0; i < tempRecipeInfo.RecipeSteps; ++i)
    {
      tempRecipeSteps[i].ID = i;
      if (i != tempRecipeInfo.RecipeSteps - 1)
        tempRecipeSteps[i].NextID = i + 1;
      else
        tempRecipeSteps[i].NextID = i;
    }
    if (tempRecipeSteps != NULL)
    {
      NFC_AddRecipeStepsToCardInfo(&tempCardInfo, tempRecipeSteps, tempRecipeInfo.RecipeSteps, true);
    }
    return tempCardInfo;
  }
  ```  

#### 8.3 State machine: “Return to storage” recipe generation

- When a broken or invalid recipe is detected (`State_Mimo_NastaveniNaPresunDoSkladu`), the state machine creates and writes a **“return to storage”** recipe (ID 3 in `GetCardInfoByNumber`):  
  ```529:555:ESP32_Firmware_ASS_Interpreter/main/app.c
  case State_Mimo_NastaveniNaPresunDoSkladu:
  {
    NFC_Handler_Init(&iHandlerData);
    NFC_Handler_SetUp(&iHandlerData, Parametry->NFC_Reader);
    ...
    TCardInfo iCardInfo = GetCardInfoByNumber(3);
    ...
    if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
    {
      NFC_Handler_AddCardInfoToWorking(&iHandlerData, iCardInfo);
      ChangeID(&iHandlerData.sWorkingCardInfo, 1); // Nastaveni ID
      NFC_Handler_Sync(&iHandlerData);
      xSemaphoreGive(Parametry->xNFCReader);
    }
    ...
    RAF = State_Mimo_Polozena;
  }
  ```  

#### 8.4 State machine: “New recipe” after completion

- When a recipe is completed (`State_KonecReceptu`), the state machine transitions to `State_NovyRecept` and cycles through pre-defined recipe templates (0..3) using `GetCardInfoByNumber`, while preserving local data like `ID`, `NumOfDrinks`, `RightNumber`:  
  ```980:1013:ESP32_Firmware_ASS_Interpreter/main/app.c
  case State_NovyRecept:
  {
    if (ReceiptCounter > 3)
    {
      ReceiptCounter = 0;
    }
    TCardInfo iCardInfo = GetCardInfoByNumber(ReceiptCounter++);
    iCardInfo = SaveLocalsData(&iHandlerData, iCardInfo);
    ...
    if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
    {
      NFC_Handler_AddCardInfoToWorking(&iHandlerData, iCardInfo);
      NFC_Handler_Sync(&iHandlerData);
      xSemaphoreGive(Parametry->xNFCReader);
    }
    ...
    RAF = State_Mimo_Polozena;
  }
  ```  

#### 8.5 On-the-fly transport step insertion

- When a glass arrives at the wrong cell, the state machine dynamically **inserts a transport step** before the current step:  
  ```851:870:ESP32_Firmware_ASS_Interpreter/main/app.c
  if (iHandlerData.sIntegrityCardInfo.sRecipeStep[iHandlerData.sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID != MyCellInfo.IDofCell)
  {
    uint8_t Last = 0;
    Error = GetMinule(&iHandlerData, iHandlerData.sIntegrityCardInfo.sRecipeStep[...].ID, &Last);
    AddRecipe(&iHandlerData, GetRecipeStepByNumber(10, iHandlerData.sIntegrityCardInfo.sRecipeStep[...].ProcessCellID),
              Last, &Parametry->xNFCReader, Last == 0 && ...);
    ...
    NFC_Handler_GetRecipeStep(&iHandlerData, &tempStep, Last);
    tempStep.NextID = iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeSteps;
    ...
    tempInfo.ActualRecipeStep = tempInfo.RecipeSteps - 1;
    NFC_Handler_WriteInfo(&iHandlerData, &tempInfo);
    NFC_Handler_Sync(&iHandlerData);
    ...
    RAF = State_Mimo_Polozena;
  }
  ```  

- `AddRecipe` uses `NFC_ChangeRecipeStepsSize`, `NFC_Handler_GetRecipeStep`, and `NFC_Handler_WriteStep` to:  
  - Increase `RecipeSteps` by 1.  
  - Allocate extra space in the working card.  
  - Insert the new step at the end and fix the `NextID` pointers (either linking from a predecessor or inserting at front).  

These behaviors mean that tags written by the ESP32 may differ from any static “design-time” layout; Android must interpret steps purely via the binary struct layout and header fields, not via assumptions about initial templates.

---

### 9. Unclear or Ambiguous Parts

Based strictly on the firmware:

- **Application-level semantics of `Parameters` (header)**:  
  - The NFC layer treats this as an opaque byte. The state machine does not obviously reference it for decision-making; its exact meaning is domain-specific and must be inferred from PLC logic, not NFC code.  
- **Exact interpretation of `ParameterProcess1`/`ParameterProcess2` per `TypeOfProcess`**:  
  - `NFC_recipes.c`’s `GetRecipeStepByNumber` indicates they represent drink volumes and timing for shaker/cleaner, but higher-level PLC semantics are outside this firmware.  
- **`UA_DateTime` semantics for `TimeOfProcess` and `TimeOfTransport`**:  
  - Stored as raw 64-bit values; treated as absolute times (`GetTime()` is compared to `TimeOfTransport` in the state machine) but the NFC code does not define their unit beyond using `UA_DateTime`.  
- **NTAG2xx support**:  
  - PN532 driver exposes NTAG2xx helpers, but the recipe code never calls them; in practice only UID length 4 and 7 paths are used. It is ambiguous whether NTAG2xx tags are expected in deployment.  

All other aspects (layout, offsets, checksums, step iteration, “empty tag” rules) are explicit in the code paths above.

---

### 10. Android App Implementation Requirements (Firmware-Exact Behavior)

Based only on the firmware, an Android implementation that wants to behave **exactly like the ESP32 reader** must follow these rules:

- **Tag detection and type handling**  
  - Treat any ISO14443A tag with UID length 4 as **MIFARE Classic** and 7 as **MIFARE Ultralight**.  
  - For Classic, treat memory as a linear array of 16-byte data blocks, skipping sector trailers exactly as `NFC_GetMifareClassicIndex` does.  
  - For Ultralight/NTAG-like tags, read/write 16-byte blocks mapped to 4-page groups starting at page `OFFSETDATA_ULTRALIGHT = 8`.  

- **Binary layout and endianness**  
  - Store `TRecipeInfo` and `TRecipeStep` as packed structs with the exact byte layouts described in section 3.  
  - Use **little-endian** encoding for all multi-byte integers (`uint16_t`, `UA_DateTime`), matching ESP32.  
  - Place header at offset 0, followed by `RecipeSteps` steps back-to-back.  

- **Reading recipes**  
  - Read header bytes and reconstitute `TRecipeInfo` exactly as in `NFC_LoadTRecipeInfoStructure`.  
  - Recompute `ID + RightNumber` and reject tags where this sum is not 255.  
  - Read `RecipeSteps` and allocate a contiguous `TRecipeStep` array of length `RecipeSteps`.  
  - Read step bytes beginning at offset `TRecipeInfo_Size`, mapping exactly as `NFC_LoadTRecipeSteps` does (including partial-block offset logic).  
  - Apply “empty tag” detection identical to `NFC_IsRecipeEmpty`:  
    - `RecipeSteps == 0` or `RecipeSteps > MAX_RECIPE_STEPS (64)` → empty.  
    - Steps NULL while `RecipeSteps > 0` → empty.  
    - `ActualRecipeStep >= RecipeSteps` and `RecipeDone == false` → empty.  
    - First step and core header fields all zero → empty.  

- **Checksum**  
  - When parsing:  
    - Optionally recompute checksum using the exact formula in `NFC_GetCheckSum` and compare against the stored `CheckSum` to decide if step data is valid.  
  - When writing:  
    - Always recompute `CheckSum` from all step bytes before writing; write it into header at offset 10–11.  
    - Never include header bytes in the checksum.  

- **Writing recipes**  
  - Construct `TRecipeInfo`/`TRecipeStep` in memory as packed little-endian structs.  
  - Ensure `ID + RightNumber == 255` for a valid tag.  
  - Use the same linear indexing and block-filling strategy as `NFC_WriteStructRange`:  
    - For each block, fill from header (offset 0..TRecipeInfo_Size-1) then from step bytes, then 0s for any remaining bytes within that block.  
  - For steps beyond the logical end of recipe memory, write 0 into any unused bytes in partially filled blocks; blocks fully beyond `konec` can be left unchanged, but zeroing them matches ESP32 behavior more closely.  

- **Step iteration and recipe end**  
  - Use `RecipeSteps` as array size and `ActualRecipeStep` as current index, but treat recipe as finished if any of these is true:  
    - `RecipeDone == true`.  
    - `ActualRecipeStep >= RecipeSteps`.  
    - Current step’s `NextID == ID` and `IsStepDone == true`.  
  - Use `NextID` to determine the next step; if `NextID == ID`, this step is terminal.  

- **Flags and times**  
  - Respect and preserve the four 1-bit flags in byte 30 of each step (`NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`).  
  - Treat `TimeOfProcess` and `TimeOfTransport` as 64-bit `UA_DateTime` values (little-endian); do not reinterpret or compress them.  

- **Reader-generated recipes compatibility**  
  - Recognize that some tags will have been generated or modified by the ESP32 using:  
    - Template recipes `GetCardInfoByNumber(0..3)`.  
    - Auto-inserted transport steps via `AddRecipe` and `GetRecipeStepByNumber(10, ...)`.  
    - “Return to storage” recipe created in `State_Mimo_NastaveniNaPresunDoSkladu`.  
  - Android must **not** assume a particular pattern of process types or step ordering beyond what is encoded in `TypeOfProcess`, `ParameterProcess*`, `ID`, and `NextID`.  

- **UID and sr_id**  
  - The ESP32 stores the UID bytes and length in `TCardInfo`; PLC-side `sr_id` strings are derived from this UID.  
  - Android should likewise interpret the UID directly from the tag (not from stored recipe fields) when building sr_id-equivalents, if it needs to match PLC behavior; this logic is not in the NFC layer but is strongly implied by `NFC_saveUID` and AAS integration in `main/app.c`.

By adhering strictly to these rules—struct layout, byte ordering, checksum algorithm, tag-type-specific memory mapping, and “empty recipe” detection—the Android app can read and write NFC recipe tags with behavior that matches the ESP32 firmware’s NFC reader exactly.

