#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_log.h"

#include "NFC_handler.h"
#include "NFC_reader.h"

//#define NFC_HANDLER_DEBUG_EN 0
//#define NFC_HANDLER_DEBUG_ALL_EN 0

#define RETRYCOUNT 3

#define MAXERRORREADING 3

#ifdef NFC_HANDLER_DEBUG_EN
#define NFC_HANDLER_DEBUG(tag, fmt, ...)                        \
  do                                                            \
  {                                                             \
    if (tag && *tag)                                            \
    {                                                           \
      printf("\x1B[32m[%s]D:\x1B[0m " fmt, tag, ##__VA_ARGS__); \
      fflush(stdout);                                           \
    }                                                           \
    else                                                        \
    {                                                           \
      printf(fmt, ##__VA_ARGS__);                               \
    }                                                           \
  } while (0)
#else
#define NFC_HANDLER_DEBUG(fmt, ...)
#endif

#ifdef NFC_HANDLER_DEBUG_ALL_EN
#define NFC_HANDLER_ALL_DEBUG(tag, fmt, ...)                     \
  do                                                             \
  {                                                              \
    if (tag && *tag)                                             \
    {                                                            \
      printf("\x1B[31m[%s]DA:\x1B[0m " fmt, tag, ##__VA_ARGS__); \
      fflush(stdout);                                            \
    }                                                            \
    else                                                         \
    {                                                            \
      printf(fmt, ##__VA_ARGS__);                                \
    }                                                            \
  } while (0)
#else
#define NFC_HANDLER_ALL_DEBUG(fmt, ...)
#endif

#define _STRINGIFY(s) #s
#define STRINGIFY(s) _STRINGIFY(s)
#define MAX 5

static uint8_t NFC_Handler_GetRetryCountForUidLen(uint8_t aUidLength)
{
  return (aUidLength == 7U) ? 1U : MAXERRORREADING;
}

static void NFC_Handler_LogRetryProfile(const char *aTag, uint8_t aUidLength)
{
  uint8_t retries = NFC_Handler_GetRetryCountForUidLen(aUidLength);
  printf("[NFC_RETRY][%s] uid_len=%u write_safe_retries=%u sync_retries=%u writecheck_outer_retries=%u\n",
         aTag, (unsigned)aUidLength, (unsigned)retries, (unsigned)retries, (unsigned)retries);
}

/************************************************/
/*!
    @brief  Inicializace Handleru NFC struktury

    @param  aHandlerData  Pointer na THandlerData strukturu

    @returns 0 - NFC Handler se nainicializoval a nastavil
*/
/**************************************************************************/
uint8_t NFC_Handler_Init(THandlerData* aHandlerData)
{
  static const char *TAGin = "NFC_Handler_Init";
  NFC_HANDLER_DEBUG(TAGin, "Inicializuji data k Handleru.\n");
  aHandlerData->sRecipeStepIndexArray = NULL;
  aHandlerData->sRecipeStepIndexArraySize = 0;
  NFC_InitTCardInfo(&aHandlerData->sIntegrityCardInfo);
  NFC_InitTCardInfo(&aHandlerData->sWorkingCardInfo); 
  NFC_HANDLER_DEBUG(TAGin, "Data se nainicializovala.\n");
  return 0;
}

/************************************************/
/*!
    @brief  Funkce ke změně velikosti ahandler

    @param  aHandlerData  Pointer na THandlerData strukturu
    @param  aNewSize Nova velikost paměti k zapisu

    @returns 0 - Paměť zmenila velikost, 1 - Velikosti jsou stejne, 2 - Nelze vytvořit pole indexů,  
*/
/**************************************************************************/
uint8_t NFC_Handler_ResizeIndexArray(THandlerData *aHandlerData, size_t aNewSize)
{
  static const char *TAGin = "NFC_Handler_ResizeIndexArray";
  NFC_HANDLER_DEBUG(TAGin, "Menim velikost aHandlerData.\n");

  if (aNewSize == aHandlerData->sRecipeStepIndexArraySize)
  {
    NFC_HANDLER_DEBUG(TAGin, "Pole jsou stejne velke.\n");
    return 1;
  }
  //uint8_t Error;

  NFC_HANDLER_ALL_DEBUG(TAGin, "Vytvarim pole.\n");
  bool *NoveIndexy = (bool *)malloc(sizeof(bool) * aNewSize);
  if (!NoveIndexy)
  {
    NFC_HANDLER_DEBUG(TAGin, "Nelze vytvorit pole indexu.\n");
    return 2;
  }
  NFC_HANDLER_ALL_DEBUG(TAGin, "Pole bylo vytvoreno.\n");
  for (size_t i = 0; i < aNewSize; ++i)
  {
    if (i < aHandlerData->sRecipeStepIndexArraySize)
    {
      NoveIndexy[i] = aHandlerData->sRecipeStepIndexArray[i];
    }
    else
    {
      NoveIndexy[i] = 0;
    }
  }
  

  NFC_Handler_Dealoc(aHandlerData);

  
  aHandlerData->sRecipeStepIndexArray = NoveIndexy;
  aHandlerData->sRecipeStepIndexArraySize = aNewSize;
  NFC_HANDLER_DEBUG(TAGin, "Pole zmenilo svou velikost na %d.\n",aNewSize);
  //WorkingPrintPointers(aHandlerData);
  return 0;
}

/**************************************************************************/
/*!
    @brief  Dealokování pole pro indexy Handleru


    @param  aHandlerData      aHandlerData struktura


    @returns 0 - Pole se dealokovalo, 1 - Pole je již null
*/
/**************************************************************************/
uint8_t NFC_Handler_Dealoc(THandlerData* aHandlerData)
{
    static const char *TAGin = "NFC_Handler_Dealoc";
  NFC_HANDLER_DEBUG(TAGin, "Odalokovavam aHandlerData\n");
  if (aHandlerData->sRecipeStepIndexArray == NULL)
  {
    NFC_HANDLER_DEBUG(TAGin, "aHandlerData je již null\n");
    return 1;
  }
  free(aHandlerData->sRecipeStepIndexArray);
  aHandlerData->sRecipeStepIndexArray = NULL;
  aHandlerData->sRecipeStepIndexArraySize = 0;
  NFC_HANDLER_DEBUG(TAGin, "Pole se odalokovalo\n");
  return 0;
}

/**************************************************************************/
/*!
    @brief  Nastaveni Handleru

    @param  aHandlerData      aHandlerData struktura
    @param  sNFC      Data o NFC ctecte

    @returns 0 - Pole se dealokovalo, 1 - Neni nastaveno sNFC
*/
/**************************************************************************/
uint8_t NFC_Handler_SetUp(THandlerData* aHandlerData,pn532_t aNFC)
{
  static const char *TAGin = "NFC_Handler_SetUp";
  NFC_HANDLER_DEBUG(TAGin, "Nastavuji NFC reader.\n");
  if(aNFC._clk > 0 &&aNFC._clk  < 50)
  {
    aHandlerData->sNFC = aNFC;
    NFC_HANDLER_DEBUG(TAGin, "sNFC se priradilo\n");
  }
  else
  {
    NFC_HANDLER_DEBUG(TAGin, "Neni nastavena ctecka.\n");
    return 1;
  }
  return 0;
}


/**************************************************************************/
/*!
    @brief  Overeni jestli je stejny NFC tag a data

    @param  aHandlerData      aHandlerData struktura
    @param  aRecipeInfo      Navratova hodnota recipeInfo

    @returns 0 - Data a info jsou stejna, 1 - TRecipeStep jsou jine, 2 - TCardInfo je jine,´3-Obe struktury jsou jine,4- Jedna se o jinou kartu 5 - Nelze cist z karty
*/
/**************************************************************************/
uint8_t NFC_Handler_IsSameData(THandlerData* aHandlerData,TRecipeInfo *aRecipeInfo)
{
  static const char *TAGin = "NFC_Handler_IsSameData";
  NFC_HANDLER_DEBUG(TAGin, "Overuji jestli se jedna o stejny NFC tag a stejne data.\n");
  TCardInfo aTempData;
  NFC_InitTCardInfo(&aTempData);

  uint8_t Error = 1;
  for (size_t i = 0; i < MAXERRORREADING; ++i)
  {
    Error = NFC_LoadTRecipeInfoStructure(&aHandlerData->sNFC,&aTempData);
    if(Error == 0)
    break;
  }

  switch (Error)
  {
  case 0:
    aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded = true;
    NFC_HANDLER_DEBUG(TAGin, "Data nactene: ");
    for (size_t i = 0; i < TRecipeInfo_Size; i++)
    {
      NFC_HANDLER_DEBUG("", "%d ",*((uint8_t*)&aTempData.sRecipeInfo+i));
    }
    NFC_HANDLER_DEBUG("", "\n");
    if(aTempData.sRecipeInfo.ID + aTempData.sRecipeInfo.RightNumber != 255)
    {
      return 5;
    }
    break;
  
  default:
    NFC_HANDLER_DEBUG(TAGin, "Nelze cist z karty.\n");
    return 5;
    break;
  }
  bool SameInfo = true;
  bool SameData = false;
  bool SameCard = false;
  if(aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded)
  {
    if(aHandlerData->sIntegrityCardInfo.sRecipeInfo.ID == aTempData.sRecipeInfo.ID)
      {
      SameCard = true;
      NFC_HANDLER_ALL_DEBUG(TAGin, "Jedna se o stejnou kartu.\n");
      }
      else
      {
        NFC_HANDLER_DEBUG(TAGin, "Jedna se o jinou kartu.\n");
        *aRecipeInfo = aTempData.sRecipeInfo;
        NFC_HANDLER_ALL_DEBUG(TAGin, "Velikost noveho pole: %d\n",aRecipeInfo->RecipeSteps);
        return 4;
      }
    for (size_t i = 0; i < TRecipeInfo_Size - 2; ++i)
    {
      
      if (*((uint8_t*)(&aHandlerData->sIntegrityCardInfo.sRecipeInfo)+i)!= *((uint8_t*)&aTempData.sRecipeInfo+i))
      {
        SameInfo = false;
        NFC_HANDLER_ALL_DEBUG(TAGin, "RecipeInfo nesedi.index: %d(Rozdil: %d - %d)\n",i,*((uint8_t*)(&aHandlerData->sIntegrityCardInfo.sRecipeInfo)+i),*((uint8_t*)&aTempData.sRecipeInfo+i));
        break;
      }
    }
    if(NFC_GetCheckSum(aHandlerData->sIntegrityCardInfo) == aTempData.sRecipeInfo.CheckSum)
    {
      SameData = true;
      NFC_HANDLER_ALL_DEBUG(TAGin, "CheckSum sedi, data jsou stejna.(Original Checksum: %d).\n",aTempData.sRecipeInfo.CheckSum);
    }
    else
    {
      SameData = false;
      NFC_HANDLER_ALL_DEBUG(TAGin, "CheckSum nesedi, data nejsou stejna.(Original Checksum: %d).\n",aTempData.sRecipeInfo.CheckSum);
    }
    
  }
  else
  {
    SameInfo = false;
    NFC_HANDLER_ALL_DEBUG(TAGin, "Data nejsou nactena.\n");
  }
  NFC_HANDLER_ALL_DEBUG(TAGin, "SameInfo: %d, SameData: %d.\n",SameInfo,SameData);
  *aRecipeInfo = aTempData.sRecipeInfo;

  return (!SameInfo)+(!SameData)*2;
}


/**************************************************************************/
/*!
    @brief  Nacte data z NFC Tagu

    @param  aHandlerData      aHandlerData struktura
    @param  aRecipeInfo      Navratova hodnota recipeInfo

    @returns 0 - Data se spravne nacetla,4 - Recept je rozbity, nebo jej neobsahuje, 5 - Nelze cist z karty
*/
/**************************************************************************/
uint8_t NFC_Handler_LoadData(THandlerData* aHandlerData)
{
  static const char *TAGin = "NFC_Handler_LoadData";
  NFC_HANDLER_DEBUG(TAGin, "Nacitam data.\n");
  TRecipeInfo tempRecipeInfo;
  
  uint8_t Error = NFC_Handler_IsSameData(aHandlerData,&tempRecipeInfo);

  bool NeedLoadData = false;
  bool NeedLoadInfo = false;
  bool NewCard = false;
  NFC_HANDLER_DEBUG(TAGin, "Navrat z isSameData: %d.\n",Error);
  switch (Error)
  {
  case 0:
    break;
  case 1:NeedLoadInfo = true;break;
  case 2:NeedLoadData = true;break;
  case 3:NeedLoadInfo = true;NeedLoadData = true;break;
  case 4:NewCard = true; break;
  case 5:return 4; break;
  default:
    return 5;
    break;
  }
  if(NeedLoadInfo || NewCard)
  {
    NFC_HANDLER_DEBUG(TAGin, "Nacitam info a nebo nova karta.\n");
    if(tempRecipeInfo.RecipeSteps != aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps)
    {NFC_HANDLER_DEBUG(TAGin, "Menim velikost pole na velikost %d.\n",tempRecipeInfo.RecipeSteps);
      NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo,tempRecipeInfo.RecipeSteps);
      
    }
    
    aHandlerData->sIntegrityCardInfo.sRecipeInfo = tempRecipeInfo;
    
  }
  if(NeedLoadData || NewCard)
  {
    NFC_HANDLER_DEBUG(TAGin, "Nacitam data a nebo nova karta.\n");
    NFC_HANDLER_ALL_DEBUG(TAGin, "Menim data.\n");
    for (size_t i = 0; i < MAXERRORREADING; ++i)
    {
      Error = NFC_LoadTRecipeSteps(&aHandlerData->sNFC,&aHandlerData->sIntegrityCardInfo);
      if(Error == 0)
      {
        break;
      }
    }
    switch (Error)
      {
      case 0:
        aHandlerData->sIntegrityCardInfo.TRecipeStepLoaded = true;//Pridano
        NFC_HANDLER_DEBUG(TAGin, "Data se nacetla.\n");
        break;
      
      default:
        NFC_HANDLER_DEBUG(TAGin, "Jina chyba nacitani.\n");
        return 5;
        break;
      }
  }
  if(NewCard)
  {
    NeedLoadInfo = NeedLoadData = true;
  }
   NFC_Handler_ResizeIndexArray(aHandlerData,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
  NFC_Handler_CopyToWorking(aHandlerData,NeedLoadInfo,NeedLoadData);

  return 0;
}


/**************************************************************************/
/*!
    @brief  Prekopiruje data z Integrovanych dat do Working dat

    @param  aHandlerData      aHandlerData struktura
    @param  InfoData      Kopirovat info data
    @param  StepsData      Kopirovat StepsData data

    @returns 0 - Data se spravne prekopirovaly, 20 - Chyba vytvareni pole
*/
/**************************************************************************/
uint8_t NFC_Handler_CopyToWorking(THandlerData* aHandlerData,bool InfoData,bool StepsData)
{
  //WorkingPrintPointers(aHandlerData);
  static const char *TAGin = "NFC_Handler_CopyToWorking";
  NFC_HANDLER_DEBUG(TAGin, "Kopiruju do Working pameti. Velikost pole integrity: %d, working %d.\n",aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps,aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps);
  aHandlerData->sWorkingCardInfo.TRecipeInfoLoaded = aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded;
  if(!aHandlerData->sWorkingCardInfo.TRecipeStepArrayCreated)
  {
    NFC_HANDLER_ALL_DEBUG(TAGin, "Pole je vytvorene.\n");
    NFC_ChangeRecipeStepsSize(&aHandlerData->sWorkingCardInfo,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
  }

  aHandlerData->sWorkingCardInfo.TRecipeStepLoaded = aHandlerData->sIntegrityCardInfo.TRecipeStepLoaded;
  aHandlerData->sWorkingCardInfo.sUidLength = aHandlerData->sIntegrityCardInfo.sUidLength;
  for (size_t i = 0; i < 7; ++i)
  {
    aHandlerData->sWorkingCardInfo.sUid[i] = aHandlerData->sIntegrityCardInfo.sUid[i];
  }
  uint8_t Error = 0;
  if(aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps != aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps)
  {
    NFC_HANDLER_ALL_DEBUG(TAGin, "Menim velikost pole na %d.\n",aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
    Error = NFC_ChangeRecipeStepsSize(&aHandlerData->sWorkingCardInfo,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
    if(Error != 0)
    {
      NFC_HANDLER_ALL_DEBUG(TAGin, "Chyba vytvareni pole.\n");
      return 20;
    }
  }
  
   NFC_HANDLER_ALL_DEBUG(TAGin, "Pole bylo alokovano.\n");
  if(InfoData)
  {
     NFC_HANDLER_ALL_DEBUG(TAGin, "Kopiruju info.\n");
    aHandlerData->sWorkingCardInfo.sRecipeInfo = aHandlerData->sIntegrityCardInfo.sRecipeInfo;
  }
 
 if(aHandlerData->sRecipeStepIndexArraySize !=aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps)
 {
  NFC_Handler_ResizeIndexArray(aHandlerData,aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps);
 }
  if(StepsData)
  {
    //WorkingPrintPointers(aHandlerData);
    NFC_HANDLER_ALL_DEBUG(TAGin, "Kopiruji data:");
    for (size_t i = 0; i < aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps; ++i)
    {
      NFC_HANDLER_ALL_DEBUG("", "%d, ",i);
      if(aHandlerData->sRecipeStepIndexArray[i])
      {NFC_HANDLER_ALL_DEBUG("", "-Preskakuji, ");
        continue;
      }
      for (size_t j = 0; j < TRecipeStep_Size; j++)
      {
        *((uint8_t *)(aHandlerData->sWorkingCardInfo.sRecipeStep)+j+i*TRecipeStep_Size) = *((uint8_t *)(aHandlerData->sIntegrityCardInfo.sRecipeStep)+j+i*TRecipeStep_Size);
      }
      
  
    }
    NFC_HANDLER_ALL_DEBUG("", "\n");
  }
   //WorkingPrintPointers(aHandlerData);
   NFC_HANDLER_ALL_DEBUG(TAGin, "Data se prekopirovala.\n");
  return 0;
}


/**************************************************************************/
/*!
    @brief  Pridani indexu do pole k zapisu

    @param  aHandlerData      aHandlerData struktura
    @param  aIndex index zapisu

    @returns 0 - Data se spravne prekopirovali, 1 -Data jsou mimo rozsah
*/
/**************************************************************************/
uint8_t NFC_Handler_AddIndex(THandlerData* aHandlerData,size_t aIndex)
{
  static const char *TAGin = "NFC_Handler_AddIndex";
  NFC_HANDLER_DEBUG(TAGin, "Pridavam index do pole index %d.\n",aIndex);
  if(aIndex >= aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps)
  {
    NFC_HANDLER_DEBUG(TAGin, "a Index je vetsi jak velikost pole.\n");
    return 1;
  }
  aHandlerData->sRecipeStepIndexArray[aIndex] = true;
  return 0;
}

/**************************************************************************/
/*!
    @brief  Pridani indexu do pole k zapisu

    @param  aHandlerData      aHandlerData struktura
    @param  InfoData      Kopirovat info data
    @param  StepsData      Kopirovat StepsData data

    @returns 0 - Data se spravne prekopirovali, 1 -Data jsou mimo rozsah
*/
/**************************************************************************/
uint8_t NFC_Handler_RemoveIndex(THandlerData* aHandlerData,size_t aIndex)
{
  static const char *TAGin = "NFC_Handler_RemoveIndex";
  NFC_HANDLER_DEBUG(TAGin, "Odebiram index do pole index %d.\n",aIndex);
  if(aIndex >= aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps)
  {
    NFC_HANDLER_DEBUG(TAGin, "aIndex je vetsi jak velikost pole.\n");
    return 1;
  }
  aHandlerData->sRecipeStepIndexArray[aIndex] = false;
  return 0;
}


/**************************************************************************/
/*!
    @brief  Getter na recipe info

    @param  aHandlerData      aHandlerData struktura
    @param  aRecipeInfo      Pointer na vysledne recipe info


    @returns 0 - Data se spravne ziskala, 1 - data nebyla nactena
*/
/**************************************************************************/
uint8_t NFC_Handler_GetRecipeInfo(THandlerData* aHandlerData,TRecipeInfo *  aRecipeInfo)
{
  static const char *TAGin = "NFC_Handler_GetRecipeInfo";
  NFC_HANDLER_DEBUG(TAGin, "Ziskavam TrecipeInfo strukturu z working pole.\n");
  if(!aHandlerData->sWorkingCardInfo.TRecipeInfoLoaded)
  {
    NFC_HANDLER_DEBUG(TAGin, "Data nebyla nactena.\n");
    return 1;
  }
  *aRecipeInfo = aHandlerData->sWorkingCardInfo.sRecipeInfo;
  return 0;
}
/**************************************************************************/
/*!
    @brief  Getter na recipe info

    @param  aHandlerData      aHandlerData struktura
    @param  aRecipeStep      Pointer na vysledne recipe info
    @param  aIndex          index zapisu

    @returns 0 - Data se spravne ziskala, 2 - aIndex je mimo rozsah
*/
/**************************************************************************/
uint8_t NFC_Handler_GetRecipeStep(THandlerData* aHandlerData,TRecipeStep *  aRecipeStep, size_t aIndex)
{
  static const char *TAGin = "NFC_Handler_GetRecipeSteps";
  NFC_HANDLER_DEBUG(TAGin, "Ziskavam TRecipeStep strukturu z working pole index %d.\n",aIndex);
  if(aIndex >= aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps)
  {
    NFC_HANDLER_DEBUG(TAGin, "aIndex je vetsi jak velikost pole.\n");
    return 2;
  }
  if(!aHandlerData->sWorkingCardInfo.TRecipeStepLoaded)
  {
    NFC_HANDLER_DEBUG(TAGin, "Nejsou nactene data TrecipeStep.\n");
    return 1;
  }
  *aRecipeStep = aHandlerData->sWorkingCardInfo.sRecipeStep[aIndex];
  return 0;
}


/**************************************************************************/
/*!
    @brief  Pridani Stepu k zapisu

    @param  aHandlerData      aHandlerData struktura
    @param  aRecipeStep      Pointer na recipe step
    @param  aIndex        index zapisu


    @returns 0 - Data se spravne zapsala, 2 - aIndex je mimo rozsah
*/
/**************************************************************************/
uint8_t NFC_Handler_WriteStep(THandlerData* aHandlerData,TRecipeStep *  aRecipeStep, size_t aIndex)
{
  static const char *TAGin = "NFC_Handler_WriteStep";
  NFC_HANDLER_DEBUG(TAGin, "Pridani Stepu k zapisu index %d.\n",aIndex);
  if(aIndex >= aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps)
  {
    NFC_HANDLER_DEBUG(TAGin, "aIndex je vetsi jak velikost pole.\n");
    return 2;
  }
  aHandlerData->sWorkingCardInfo.sRecipeStep[aIndex] = *aRecipeStep;
  NFC_Handler_ResizeIndexArray(aHandlerData,aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps);
  NFC_Handler_AddIndex(aHandlerData,aIndex);
  return 0;
}
/**************************************************************************/
/*!
    @brief  Pridani Info k zapisu

    @param  aHandlerData      aHandlerData struktura
    @param  aRecipeInfo      Pointer na recipe info


    @returns 0 - Data se spravne zapsala
*/
/**************************************************************************/
uint8_t NFC_Handler_WriteInfo(THandlerData* aHandlerData,TRecipeInfo* aRecipeInfo)
{
  static const char *TAGin = "NFC_Handler_WriteInfo";
  NFC_HANDLER_DEBUG(TAGin, "Pridani Info k zapisu.\n");
  NFC_Handler_ResizeIndexArray(aHandlerData,aRecipeInfo->RecipeSteps);
  aHandlerData->sWorkingCardInfo.sRecipeInfo = *aRecipeInfo;
  return 0;
}


/**************************************************************************/
/*!
    @brief  Synchronizace dat z working bufferu do NFC tagu

    @param  aHandlerData      aHandlerData struktura


    @returns 0 - Data se spravne zapsala,1 - neni nic k zapisu, 2 - Nepodarilo se zapsat,9- Nepodarila se zmenit sIntegrity card, 10 - jina chyba
*/
/**************************************************************************/
uint8_t NFC_Handler_Sync(THandlerData* aHandlerData)
{
  static const char *TAGin = "NFC_Handler_Sync";
  uint8_t uidLength = aHandlerData->sWorkingCardInfo.sUidLength;
  if (uidLength == 0U)
  {
    uidLength = aHandlerData->sIntegrityCardInfo.sUidLength;
  }
  uint8_t syncRetryCount = NFC_Handler_GetRetryCountForUidLen(uidLength);
  NFC_Handler_LogRetryProfile(TAGin, uidLength);
  uint32_t sync_fn_start_ms = esp_log_timestamp();
  NFC_HANDLER_DEBUG(TAGin, "Synchronizuji working do NFC Tagu.\n");
  printf("[NFC_TIMING][%s] enter t=%ums\n", TAGin, (unsigned)sync_fn_start_ms);

  bool zapis = false;
  NFC_HANDLER_ALL_DEBUG(TAGin, "Data k zapsani: ");
  for (size_t i = 0; i < aHandlerData->sRecipeStepIndexArraySize; ++i)
  {
    NFC_HANDLER_ALL_DEBUG("", "%d:%d ,",i,aHandlerData->sRecipeStepIndexArray[i]);
    if(aHandlerData->sRecipeStepIndexArray[i])
      zapis = true;
    if(zapis)
      break;
  }
  NFC_HANDLER_ALL_DEBUG("", "\n");
  bool stejne = true;
  uint8_t Error = 0;
  if (!zapis)
  {
    Error = NFC_CheckStructArrayIsSame(&aHandlerData->sNFC, &aHandlerData->sWorkingCardInfo, 0, 0);
    switch (Error)
    {
    case 0:
      stejne = true;
      return 1;
      break;
    case 1:
      stejne = false;
       NFC_HANDLER_DEBUG(TAGin, "Zapisuju %d - %d\n", 0,0);
       for (size_t i = 0; i < syncRetryCount; ++i)
       {
        uint32_t wr_start_ms = esp_log_timestamp();
        printf("[NFC_TIMING][%s] NFC_WriteCheck start range=%u-%u retry=%u t=%lldms\n",
               TAGin, 0U, 0U, (unsigned)i, (long long)wr_start_ms);
        Error = NFC_WriteCheck(&aHandlerData->sNFC,&aHandlerData->sWorkingCardInfo,0,0);
        uint32_t wr_end_ms = esp_log_timestamp();
        printf("[NFC_TIMING][%s] NFC_WriteCheck end range=%u-%u retry=%u res=%u dt=%lldms t=%lldms\n",
               TAGin, 0U, 0U, (unsigned)i, (unsigned)Error,
               (long long)(wr_end_ms - wr_start_ms), (long long)wr_end_ms);
        if(Error == 0)
        {
          break;
        }
       }
       switch (Error)
       {
       case 0:
        NFC_HANDLER_DEBUG(TAGin, "Data se uspesne zapsala. Kopiruju do vnitrni pameti.\n");
        aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded = true;
        if(NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo,aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps)!= 0)
        {
          NFC_HANDLER_DEBUG(TAGin, "Nepodarilo se zmenit veliksot pole sIntegrityCard.\n");
          return 9;
        }
        aHandlerData->sIntegrityCardInfo.sRecipeInfo = aHandlerData->sWorkingCardInfo.sRecipeInfo;
        break;
       
       default:
        return 2;
        break;
       }
       
      break;
    default:
      return 10;
      break;
    }
  }
  else
  {
    
    uint8_t zacatek = 0;
    uint8_t konec = 0;
    bool zacatekSet = true;
    for (size_t i = 0; i < aHandlerData->sRecipeStepIndexArraySize; ++i)
    {
      if(aHandlerData->sRecipeStepIndexArray[i] == 0 && zacatekSet)
      {
        konec = i;
        NFC_HANDLER_DEBUG(TAGin, "Zapisuju %d - %d\n", zacatek,konec);
        zacatekSet = false;

        for (size_t j = 0; j < syncRetryCount; ++j)
        {
          uint32_t wr_start_ms = esp_log_timestamp();
          printf("[NFC_TIMING][%s] NFC_WriteCheck start range=%u-%u retry=%u t=%lldms\n",
                 TAGin, (unsigned)zacatek, (unsigned)konec, (unsigned)j, (long long)wr_start_ms);
          Error = NFC_WriteCheck(&aHandlerData->sNFC, &aHandlerData->sWorkingCardInfo, zacatek, konec);
          uint32_t wr_end_ms = esp_log_timestamp();
          printf("[NFC_TIMING][%s] NFC_WriteCheck end range=%u-%u retry=%u res=%u dt=%lldms t=%lldms\n",
                 TAGin, (unsigned)zacatek, (unsigned)konec, (unsigned)j, (unsigned)Error,
                 (long long)(wr_end_ms - wr_start_ms), (long long)wr_end_ms);
          if (Error == 0)
          {
            break;
          }
        }
        switch (Error)
        {
        case 0:
          NFC_HANDLER_DEBUG(TAGin, "Data se uspesne zapsala. Kopiruju do vnitrni pameti.\n");

          if(zacatek == 0)
          {
            aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded = true;
          NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo, aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps);
          aHandlerData->sIntegrityCardInfo.sRecipeInfo = aHandlerData->sWorkingCardInfo.sRecipeInfo;
          }
          
          NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
          for (size_t j = zacatek+(zacatek == 0); j <= konec; ++j)
          {NFC_HANDLER_ALL_DEBUG(TAGin, "Kopiruji %d misto.\n",j-1);
            for (size_t k = 0; k < TRecipeStep_Size; k++)
            { 
              *((uint8_t *)aHandlerData->sIntegrityCardInfo.sRecipeStep+(j-1)*TRecipeStep_Size + k) = *((uint8_t *)aHandlerData->sWorkingCardInfo.sRecipeStep+(j-1)*TRecipeStep_Size + k);
            }
            


            aHandlerData->sRecipeStepIndexArray[j-1] = false;
          }
          
          break;

        default:
          return 2;
          break;
        }
      }
      if(aHandlerData->sRecipeStepIndexArray[i] == 1 && !zacatekSet)
      {
       
        zacatek = i+1;
        zacatekSet = true;
        NFC_HANDLER_ALL_DEBUG(TAGin, "Nastavuju zacatek na %d\n", zacatek);
      }
    }
    if(zacatekSet)
    {
      konec = aHandlerData->sRecipeStepIndexArraySize;
      NFC_HANDLER_DEBUG(TAGin, "Zapisuju %d - %d\n", zacatek,konec);
      




      for (size_t j = 0; j < syncRetryCount; ++j)
        {
          uint32_t wr_start_ms = esp_log_timestamp();
          printf("[NFC_TIMING][%s] NFC_WriteCheck start range=%u-%u retry=%u t=%lldms\n",
                 TAGin, (unsigned)zacatek, (unsigned)konec, (unsigned)j, (long long)wr_start_ms);
          Error = NFC_WriteCheck(&aHandlerData->sNFC, &aHandlerData->sWorkingCardInfo, zacatek, konec);
          uint32_t wr_end_ms = esp_log_timestamp();
          printf("[NFC_TIMING][%s] NFC_WriteCheck end range=%u-%u retry=%u res=%u dt=%lldms t=%lldms\n",
                 TAGin, (unsigned)zacatek, (unsigned)konec, (unsigned)j, (unsigned)Error,
                 (long long)(wr_end_ms - wr_start_ms), (long long)wr_end_ms);
          if (Error == 0)
          {
            break;
          }
        }
        switch (Error)
        {
        case 0:
          NFC_HANDLER_DEBUG(TAGin, "Data se uspesne zapsala. Kopiruju do vnitrni pameti.\n");
          aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded = true;
          if(zacatek == 0)
          {
          
          NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo, aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps);
          aHandlerData->sIntegrityCardInfo.sRecipeInfo = aHandlerData->sWorkingCardInfo.sRecipeInfo;
          }
          
          NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
          for (size_t j = zacatek+(zacatek == 0); j <= konec; ++j)
          {NFC_HANDLER_ALL_DEBUG(TAGin, "Kopiruji %d misto.\n",j-1);
            for (size_t k = 0; k < TRecipeStep_Size; k++)
            { 
              *((uint8_t *)aHandlerData->sIntegrityCardInfo.sRecipeStep+(j-1)*TRecipeStep_Size + k) = *((uint8_t *)aHandlerData->sWorkingCardInfo.sRecipeStep+(j-1)*TRecipeStep_Size + k);
            }
            


            aHandlerData->sRecipeStepIndexArray[j-1] = false;
          }

        break;

        default:
          return 2;
          break;
        }

      zacatekSet = false;
    }
    
  }
  printf("[NFC_TIMING][%s] exit dt_total=%ums\n", TAGin, (unsigned)(esp_log_timestamp() - sync_fn_start_ms));
  return 0;
}


/**************************************************************************/
/*!
    @brief  Zapsani info struktury do NFC tagu

    @param  aHandlerData      THandlerData struktura
    @param  aRecipeInfo      TRecipeInfo struktura

    @returns 0 - Data se spravne zapsala, 1 - Nepodarilo se zapsat
*/
/**************************************************************************/
uint8_t NFC_Handler_WriteSafeInfo(THandlerData* aHandlerData,TRecipeInfo* aRecipeInfo)
{
  static const char *TAGin = "NFC_Handler_WriteSafeInfo";
  uint8_t uidLength = aHandlerData->sWorkingCardInfo.sUidLength;
  if (uidLength == 0U)
  {
    uidLength = aHandlerData->sIntegrityCardInfo.sUidLength;
  }
  uint8_t writeSafeRetryCount = NFC_Handler_GetRetryCountForUidLen(uidLength);
  NFC_Handler_LogRetryProfile(TAGin, uidLength);
  uint32_t info_fn_start_ms = esp_log_timestamp();
  NFC_HANDLER_DEBUG(TAGin, "Zapisuji hned info do NFC tagu.\n");
  printf("[NFC_TIMING][%s] enter t=%ums\n", TAGin, (unsigned)info_fn_start_ms);
  TRecipeInfo tempData = aHandlerData->sIntegrityCardInfo.sRecipeInfo;
  bool tempDataLoaded = aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded;
  aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded = true;
  aHandlerData->sIntegrityCardInfo.sRecipeInfo = *aRecipeInfo;
  uint8_t Error = 1;
  for (size_t i = 0; i < writeSafeRetryCount; i++)
  {
    uint32_t wr_start_ms = esp_log_timestamp();
    NFC_HANDLER_ALL_DEBUG(TAGin,
                          "DBG pre-write-check oldRecipeSteps=%u newRecipeSteps=%u stepArrayCreated=%u stepPtr=%p\n",
                          (unsigned)aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps,
                          (unsigned)aRecipeInfo->RecipeSteps,
                          (unsigned)aHandlerData->sIntegrityCardInfo.TRecipeStepArrayCreated,
                          (void *)aHandlerData->sIntegrityCardInfo.sRecipeStep);
    printf("[NFC_TIMING][%s] NFC_WriteCheck start retry=%u range=%u-%u t=%lldms\n",
           TAGin, (unsigned)i, 0U, 0U, (long long)wr_start_ms);
    Error = NFC_WriteCheck(&aHandlerData->sNFC,&aHandlerData->sIntegrityCardInfo,0,0);
    uint32_t wr_end_ms = esp_log_timestamp();
    printf("[NFC_TIMING][%s] NFC_WriteCheck end retry=%u res=%u dt=%lldms t=%lldms\n",
           TAGin, (unsigned)i, (unsigned)Error,
           (long long)(wr_end_ms - wr_start_ms), (long long)wr_end_ms);
    if(Error == 0)
      break;
  }
  switch (Error)
  {
  case 0:
    NFC_HANDLER_ALL_DEBUG(TAGin, "Data se uspesne nahrala.\n");
    break;
  
  default:
   NFC_HANDLER_DEBUG(TAGin, "Data se nenahrala, Davam zpet puvodni data, error: %d.\n",Error);
  aHandlerData->sIntegrityCardInfo.sRecipeInfo = tempData;
  tempDataLoaded = aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded;
    return 1;
    break;
  }
  if(tempData.RecipeSteps != aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps || (!tempDataLoaded && aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps > 0) || (tempDataLoaded &&aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps == 0))
  {
    NFC_HANDLER_ALL_DEBUG(TAGin, "Menim velikost pole. nova: %d\n",aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
    NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
  }
  NFC_HANDLER_ALL_DEBUG(TAGin, "Data se uspesne prepsala.\n");
  NFC_HANDLER_ALL_DEBUG(TAGin, "Zapisuji do working.\n");
  NFC_Handler_CopyToWorking(aHandlerData,1,0);
  printf("[NFC_TIMING][%s] exit dt_total=%ums\n", TAGin, (unsigned)(esp_log_timestamp() - info_fn_start_ms));

  return 0;
}

/**************************************************************************/
/*!
    @brief  Zapsani info struktury do NFC tagu

    @param  aHandlerData      THandlerData struktura
    @param  aRecipeStep      TRecipeStep struktura
    @param  aIndex      aIndex pole

    @returns 0 - Data se spravne zapsala, 1 - Nepodarilo se zapsat, 2 - aIndex je vetsi nez velikost pole
*/
/**************************************************************************/
uint8_t NFC_Handler_WriteSafeStep(THandlerData* aHandlerData,TRecipeStep* aRecipeStep, size_t aIndex)
{
  static const char *TAGin = "NFC_Handler_WriteSafeStep";
  NFC_HANDLER_DEBUG(TAGin, "Zapisuji step strukturu na pozici %d.\n",aIndex);
  if(aIndex >= aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps)
  {
    NFC_HANDLER_DEBUG(TAGin, "aIndex je vetsi jak velikost pole.\n");
    return 2;
  }
  TRecipeStep tempData = aHandlerData->sIntegrityCardInfo.sRecipeStep[aIndex];
  aHandlerData->sIntegrityCardInfo.sRecipeStep[aIndex] = *aRecipeStep;
  if(!aHandlerData->sIntegrityCardInfo.TRecipeStepArrayCreated)
  {
    NFC_HANDLER_DEBUG(TAGin, "Neni vytvoreno pole, vytvarim..\n");
    NFC_ChangeRecipeStepsSize(&aHandlerData->sIntegrityCardInfo,aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps);
  }
  uint8_t Error;
  for (size_t i = 0; i < MAXERRORREADING; i++)
  {
    Error = NFC_WriteCheck(&aHandlerData->sNFC,&aHandlerData->sIntegrityCardInfo,aIndex+1,aIndex+1);
    if(Error == 0)
      break;
  }
  switch (Error)
  {
  case 0:
    break;
  
  default:
    NFC_HANDLER_DEBUG(TAGin, "Data se nenahrala, Davam zpet puvodni data, error: %d.\n",Error);
    aHandlerData->sIntegrityCardInfo.sRecipeStep[aIndex] = tempData;
    if(!aHandlerData->sIntegrityCardInfo.TRecipeStepArrayCreated)
    {
      NFC_DeAllocTRecipeStepArray(&aHandlerData->sIntegrityCardInfo);
    }
    return 1;
    break;
  }
  if(!aHandlerData->sIntegrityCardInfo.TRecipeStepArrayCreated)
    {
      aHandlerData->sIntegrityCardInfo.TRecipeStepArrayCreated = 1;
    }
    NFC_HANDLER_ALL_DEBUG(TAGin, "Data se uspesne prepsala.\n");
  NFC_HANDLER_ALL_DEBUG(TAGin, "Zapisuji do working.\n");
  //WorkingPrintPointers(aHandlerData);
  NFC_Handler_CopyToWorking(aHandlerData,1,1);
  return 0;
}

uint8_t WorkingPrintPointers(THandlerData* aHandlerData)
{
  static const char *TAGin = "WorkingPrintPointers";
  NFC_HANDLER_DEBUG(TAGin, "Pointery integr.steps: %p, working.steps: %p, arraystowrite: %p.\n",aHandlerData->sIntegrityCardInfo.sRecipeStep,aHandlerData->sWorkingCardInfo.sRecipeStep,aHandlerData->sRecipeStepIndexArray);
  return 0;
}

uint8_t NFC_Handler_AddCardInfoToWorking(THandlerData* aHandlerData,TCardInfo aCardInfo)
{
  static const char *TAGin = "NFC_Handler_AddCardInfoToWorking";
  NFC_HANDLER_DEBUG(TAGin, "Pridavam celou strukturu TCardInfo do Working\n");
  if(aHandlerData->sIntegrityCardInfo.TRecipeInfoLoaded)
  {
    aCardInfo.sRecipeInfo.ID = aHandlerData->sIntegrityCardInfo.sRecipeInfo.ID;
    aCardInfo.sRecipeInfo.NumOfDrinks = aHandlerData->sIntegrityCardInfo.sRecipeInfo.NumOfDrinks;
    aCardInfo.sRecipeInfo.RightNumber = aHandlerData->sIntegrityCardInfo.sRecipeInfo.RightNumber;
  }
  if(aHandlerData->sWorkingCardInfo.TRecipeStepArrayCreated)
  {
    NFC_DeAllocTRecipeStepArray(&aHandlerData->sWorkingCardInfo);
  }
  aHandlerData->sWorkingCardInfo = aCardInfo;

  NFC_Handler_ResizeIndexArray(aHandlerData,aCardInfo.sRecipeInfo.RecipeSteps);

  for (size_t i = 0; i < aCardInfo.sRecipeInfo.RecipeSteps; i++)
  {
    NFC_Handler_AddIndex(aHandlerData,i);
  }
  
  return 0;

}