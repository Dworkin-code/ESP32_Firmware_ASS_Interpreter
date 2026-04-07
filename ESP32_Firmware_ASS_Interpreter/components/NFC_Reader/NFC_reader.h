/* ==========================================
    NFC_reader - Knihovna na synchronizaci dat z NFC Čipu
    Copyright (c) 2024 Luboš Chmelař
    [Licence]
========================================== */
#ifndef NFC_reader_H
#define NFC_reader_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "pn532.h"
#include "neopixel.h"
#include "open62541.h"

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

  extern tNeopixelContext* Light;
  static const size_t TRecipeInfo_Size = sizeof(TRecipeInfo);
  static const size_t TRecipeStep_Size = sizeof(TRecipeStep);

  
  uint8_t NFC_Reader_Init(pn532_t *aNFC,uint8_t aClk, uint8_t aMiso, uint8_t aMosi, uint8_t aSs);
  void NFC_Print(TCardInfo aCardInfo);
  uint8_t NFC_WriteStruct(pn532_t *aNFC, TCardInfo* aCardInfo, uint16_t NumOfStructure);
  uint8_t NFC_WriteStructRange(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t NumOfStructureStart, uint16_t NumOfStructureEnd);
  uint8_t NFC_LoadTRecipeInfoStructure(pn532_t *aNFC,TCardInfo *aCardInfo);
  uint8_t NFC_AllocTRecipeStepArray(TCardInfo *aCardInfo);
  uint8_t NFC_DeAllocTRecipeStepArray(TCardInfo *aCardInfo);
  void NFC_InitTCardInfo(TCardInfo *aCardInfo);
  uint8_t NFC_LoadTRecipeSteps(pn532_t *aNFC,TCardInfo *aCardInfo);
  uint8_t NFC_LoadTRecipeStep(pn532_t *aNFC,TCardInfo *aCardInfo,size_t NumOfStructure);
  bool NFC_isCardReady(pn532_t *aNFC);
  bool NFC_getUID(pn532_t *aNFC, uint8_t *aUid, uint8_t *aUidLength);
  bool NFC_saveUID(TCardInfo *aCardInfo, uint8_t *aUid, uint8_t aUidLength);
  uint8_t NFC_CheckStructArrayIsSame(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t NumOfStructureStart,uint16_t NumOfStructureEnd);
  uint8_t NFC_WriteAllData(pn532_t *aNFC, TCardInfo *aCardInfo);
  uint8_t NFC_GetMifareClassicIndex(size_t i);
  uint8_t NFC_LoadAllData(pn532_t *aNFC, TCardInfo *aCardInfo);
  uint8_t NFC_WriteCheck(pn532_t *aNFC,TCardInfo *aCardInfo,uint16_t NumOfStructureStart,uint16_t NumOfStructureEnd);
  uint16_t NFC_GetCheckSum(TCardInfo aCardInfo);
  uint8_t NFC_CreateCardInfoFromRecipeInfo(TCardInfo *aCardInfo,TRecipeInfo aRecipeInfo);
  uint8_t NFC_AddRecipeStepsToCardInfo(TCardInfo *aCardInfo,TRecipeStep *aRecipeStep, size_t SizeOfRecipeSteps,bool DeAlloc);
  uint8_t NFC_ChangeRecipeStepsSize(TCardInfo *aCardInfo,uint8_t NewSize);
    uint8_t NFC_CopyTCardInfo(TCardInfo *aCardInfoOrigin,TCardInfo *aCardInfoNew);
void setLight(tNeopixelContext* aLight);
  





#ifdef __cplusplus
}
#endif

#endif