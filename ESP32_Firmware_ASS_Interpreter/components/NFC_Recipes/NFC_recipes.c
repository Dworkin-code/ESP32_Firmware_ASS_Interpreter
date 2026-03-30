#include "NFC_recipes.h"
#include "OPC_klient.h"

#define NFC_RECIPES_ALL_DEBUG_EN 1
#define NFC_RECIPES_DEBUG_EN 1
/*!
Zajištění výpisu všeho debugování
*/
#ifdef NFC_RECIPES_ALL_DEBUG_EN
#define NFC_RECIPES_ALL_DEBUG(tag, fmt, ...)                     \
  do                                                             \
  {                                                              \
    if (tag && *tag)                                             \
    {                                                            \
      printf("\x1B[35m[%s]DA:\x1B[0m " fmt, tag, ##__VA_ARGS__); \
      fflush(stdout);                                            \
    }                                                            \
    else                                                         \
    {                                                            \
      printf(fmt, ##__VA_ARGS__);                                \
    }                                                            \
  } while (0)
#else
#define NFC_RECIPESR_ALL_DEBUG(fmt, ...)
#endif

/*!
Zajištění výpisu lehkého debugování
*/
#ifdef NFC_RECIPES_DEBUG_EN
#define NFC_RECIPES_DEBUG(tag, fmt, ...)                        \
  do                                                            \
  {                                                             \
    if (tag && *tag)                                            \
    {                                                           \
      printf("\x1B[37m[%s]D:\x1B[0m " fmt, tag, ##__VA_ARGS__); \
      fflush(stdout);                                           \
    }                                                           \
    else                                                        \
    {                                                           \
      printf(fmt, ##__VA_ARGS__);                               \
    }                                                           \
  } while (0)
#else
#define NFC_RECIPES_DEBUG(fmt, ...)
#endif

TRecipeStep GetRecipeStepByNumber(uint8_t aNumOfRecipe, uint16_t aParam)
{
  static const char *TAGin = "GetRecipeStepByNumber";
  NFC_RECIPES_DEBUG(TAGin, "Generuji krok receptu c. %d\n", aNumOfRecipe);
  TRecipeStep tempRecipeStep = EmptyRecipeStep;

  /*
  1 - Vodka
  2 - Rum
  3 - Goralka
  4 - Water
  5 - Cola
  6 - Shaker
  7 - Cleaner
  8 - ToStorage
  9 - ToCustomer
  */
  switch (aNumOfRecipe)
  {
  case 1:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu Vodka o objemu %d\n", aParam);
    tempRecipeStep.TypeOfProcess = StorageAlcohol;
    tempRecipeStep.ParameterProcess1 = Vodka;
    tempRecipeStep.ParameterProcess2 = aParam;
    break;
  case 2:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu Rum o objemu %d\n", aParam);
    tempRecipeStep.TypeOfProcess = StorageAlcohol;
    tempRecipeStep.ParameterProcess1 = Rum;
    tempRecipeStep.ParameterProcess2 = aParam;
    break;
  case 3:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu Goralka o objemu %d\n", aParam);
    tempRecipeStep.TypeOfProcess = StorageAlcohol;
    tempRecipeStep.ParameterProcess1 = Goralka;
    tempRecipeStep.ParameterProcess2 = aParam;
    break;
  case 4:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu Voda o objemu %d\n", aParam);
    tempRecipeStep.TypeOfProcess = StorageNonAlcohol;
    tempRecipeStep.ParameterProcess1 = Water;
    tempRecipeStep.ParameterProcess2 = aParam;
    break;
  case 5:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu Cola o objemu %d\n", aParam);
    tempRecipeStep.TypeOfProcess = StorageNonAlcohol;
    tempRecipeStep.ParameterProcess1 = Cola;
    tempRecipeStep.ParameterProcess2 = aParam;
    break;
  case 6:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu Protrepani o dobe trvani %d s\n", aParam);
    tempRecipeStep.TypeOfProcess = Shaker;
    tempRecipeStep.ParameterProcess1 = aParam;
    break;
  case 7:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu Cisteni o dobe trvani %d s\n", aParam);
    tempRecipeStep.TypeOfProcess = Cleaner;
    tempRecipeStep.ParameterProcess1 = aParam;
    break;
  case 8:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu Navrat do skladu\n");
    tempRecipeStep.TypeOfProcess = ToStorageGlass;
    break;
  case 9:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji krok receptu DatDrinkZakaznikovi\n");
    tempRecipeStep.TypeOfProcess = ToCustomer;
    break;
  case 10:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji Pridat Transport\n");
    tempRecipeStep.TypeOfProcess = Transport;
    tempRecipeStep.ParameterProcess1 = aParam;
    break;
  default:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji prazdny recept\n");
    break;
  }

  return tempRecipeStep;
}

/*
0-
1 - Kola s Rumem(60+40)
2 - Voda s vodkou(80+20)
3 - navrat do skladu
*/
TCardInfo GetCardInfoByNumber(uint8_t aNumOfRecipe)
{
  TCardInfo tempCardInfo;

  TRecipeInfo tempRecipeInfo = EmptyRecipeInfo;
  TRecipeStep *tempRecipeSteps = NULL;
  static const char *TAGin = "GetCardInfoByNumber";
  switch (aNumOfRecipe)
  {
    case 0 :
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji recept na Vodku s vodou(20+80ml)\n");
    tempRecipeInfo.RecipeSteps = 5;
    tempRecipeInfo.ActualRecipeStep = 0;
    tempRecipeSteps = malloc(TRecipeStep_Size * tempRecipeInfo.RecipeSteps);
    tempRecipeSteps[0] = GetRecipeStepByNumber(7, 5);  // Cisteni(7) 5 sekund
    tempRecipeSteps[1] = GetRecipeStepByNumber(1, 20); // Vodka(1) 20ml
    tempRecipeSteps[2] = GetRecipeStepByNumber(9, 0);  // Drink k zakaznikovi(9)
    tempRecipeSteps[3] = GetRecipeStepByNumber(7, 20); // Cisteni(7) 20 sekund
    tempRecipeSteps[4] = GetRecipeStepByNumber(8, 0);  // Navrat do skladu(8)

    break;
  case 1:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji recept na Rum s colou(40+60ml)\n");
    tempRecipeInfo.RecipeSteps = 6;
    tempRecipeInfo.ActualRecipeStep = 0;
    tempRecipeInfo.ActualBudget = 200;
    tempRecipeSteps = malloc(TRecipeStep_Size * tempRecipeInfo.RecipeSteps);
    tempRecipeSteps[0] = GetRecipeStepByNumber(7, 5);  // Cisteni(7) 5 sekund
    tempRecipeSteps[1] = GetRecipeStepByNumber(2, 40); // Rum(2) 40ml
    tempRecipeSteps[2] = GetRecipeStepByNumber(5, 60); // Cola(5) 60ml
    tempRecipeSteps[3] = GetRecipeStepByNumber(9, 0);  // Drink k zakaznikovi(9)
    tempRecipeSteps[4] = GetRecipeStepByNumber(7, 20); // Cisteni(7) 20 sekund
    tempRecipeSteps[5] = GetRecipeStepByNumber(8, 0);  // Navrat do skladu(8)

    break;
  case 2:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji recept na Vodku s vodou(20+80ml)\n");
    tempRecipeInfo.RecipeSteps = 5;
    tempRecipeInfo.ActualRecipeStep = 0;
    tempRecipeSteps = malloc(TRecipeStep_Size * tempRecipeInfo.RecipeSteps);
    tempRecipeSteps[0] = GetRecipeStepByNumber(7, 5);  // Cisteni(7) 5 sekund
    tempRecipeSteps[1] = GetRecipeStepByNumber(1, 20); // Vodka(1) 20ml
    tempRecipeSteps[2] = GetRecipeStepByNumber(9, 0);  // Drink k zakaznikovi(9)
    tempRecipeSteps[3] = GetRecipeStepByNumber(7, 20); // Cisteni(7) 20 sekund
    tempRecipeSteps[4] = GetRecipeStepByNumber(8, 0);  // Navrat do skladu(8)

    break;
  case 3:
    NFC_RECIPES_ALL_DEBUG(TAGin, "Generuji recept na Návrat do skladu\n");
    tempRecipeInfo.RecipeSteps = 1;
    tempRecipeInfo.ActualRecipeStep = 0;
    tempRecipeSteps = malloc(TRecipeStep_Size * tempRecipeInfo.RecipeSteps);
    tempRecipeSteps[0] = GetRecipeStepByNumber(8, 0); // Navrat do skladu(8)

    break;
  default:
    NFC_RECIPES_ALL_DEBUG(TAGin, "GenerujiPrazdne Card info\n");

    break;
  }
  NFC_CreateCardInfoFromRecipeInfo(&tempCardInfo, tempRecipeInfo);
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
  if (tempRecipeSteps != NULL)
  {
    NFC_AddRecipeStepsToCardInfo(&tempCardInfo, tempRecipeSteps, tempRecipeInfo.RecipeSteps, true);
  }
  return tempCardInfo;
}

uint8_t ChangeID(TCardInfo *aCardInfo, uint16_t aNewID)
{
  static const char *TAGin = "ChangeID";
  NFC_RECIPES_ALL_DEBUG(TAGin, "Menim ID z %d na %d\n", aCardInfo->sRecipeInfo.ID, aNewID);
  aCardInfo->sRecipeInfo.ID = aNewID;
  aCardInfo->sRecipeInfo.RightNumber = 255 - aNewID;
  return 0;
}

CellInfo *GetCellInfoFromLDS(uint8_t aType, uint16_t *aNumberOfCells)
{
  static const char *TAGin = "GetCellInfoFromLDS";
  NFC_RECIPES_ALL_DEBUG(TAGin, "Ziskavam bunky transportni, skladovaci a typ %d\n", aType);

  uint8_t processTypes1[] = {1, 2,3};
  uint8_t processTypes2[] = {0,4, 5,6};
  uint8_t processTypes3[] = { 7};
  CellInfo Vsechny[] =
      {
          {1, "opc.tcp://Lubin-Laptop.local:20000\0", 25, processTypes1, 3},
          {2, "opc.tcp://Lubin-Laptop.local:20001\0", 25, processTypes2, 4},
          {3, "opc.tcp://Lubin-Laptop.local:20002\0", 25, processTypes3, 1}};

  size_t VsechnySize = 3;

  uint8_t aNumOfRequestedTypes[] = {Transport, ToStorageGlass, aType};
  size_t aNumOfRequestedTypesSize = 3;

  CellInfo *Vybrane = malloc(sizeof(CellInfo) * VsechnySize);

  *aNumberOfCells = 0;

  for (size_t i = 0; i < VsechnySize; i++)
  {

    if (ChooseCell(Vsechny[i].ProcessTypes, Vsechny[i].ProcessTypesLenght, aNumOfRequestedTypes, aNumOfRequestedTypesSize))
    {

      Vybrane[(*aNumberOfCells)++] = Vsechny[i];

      Vybrane[(*aNumberOfCells - 1)].ProcessTypes = malloc(sizeof(uint8_t) * Vybrane[(*aNumberOfCells - 1)].ProcessTypesLenght);
      for (size_t j = 0; j < Vybrane[(*aNumberOfCells - 1)].ProcessTypesLenght; j++)
      {

        Vybrane[(*aNumberOfCells - 1)].ProcessTypes[j] = Vsechny[i].ProcessTypes[j];
      }
    }
  }

  Vybrane = realloc(Vybrane, *aNumberOfCells * sizeof(CellInfo));
  return Vybrane;
}

bool ChooseCell(uint8_t *aArrayOfProcess, size_t aArrayOfProcessSize, uint8_t *aArrayOfReqProcess, size_t aArrayOfReqProcessSize)
{
  for (size_t i = 0; i < aArrayOfProcessSize; i++)
  {
    for (size_t j = 0; j < aArrayOfReqProcessSize; j++)
    {
      if (aArrayOfProcess[i] == aArrayOfReqProcess[j])
      {
        return true;
      }
    }
  }
  return false;
}
uint8_t DestroyCellInfoArray(CellInfo *aCellInfo, uint16_t aNumberOfCells)
{
  static const char *TAGin = "GetCellInfoFromLDS";
  NFC_RECIPES_ALL_DEBUG(TAGin, "Uvolnuji pamet po Cell Info poli\n");
  if (aCellInfo == NULL)
  {
    NFC_RECIPES_ALL_DEBUG(TAGin, "Pole jiz je NULL\n");
    return 1;
  }
  for (size_t i = 0; i < aNumberOfCells; i++)
  {
    NFC_RECIPES_ALL_DEBUG(TAGin, "Uvolnuji pole %d.\n", i);

    if (aCellInfo[i].ProcessTypes != NULL)
    {
      free(aCellInfo[i].ProcessTypes);
    }
  }
  free(aCellInfo);
  aCellInfo = NULL;

  return 0;
}

uint8_t GetWinningCell(CellInfo *aCellInfo, uint16_t aNumberOfCells, uint16_t InterpreterID, uint8_t aProcessType, uint8_t param1, uint16_t param2, bool priority, Reservation *aReservation)
{
  static const char *TAGin = "GetWinningCell";
  uint16_t pocet = 0;
  uint8_t Error;
  Reservation tempRezervace[aNumberOfCells];
  NFC_RECIPES_ALL_DEBUG(TAGin, "Fiktivni vyherni bunky\n");
  for (size_t i = 0; i < aNumberOfCells; i++)
  {
    for (size_t j = 0; j < aCellInfo[i].ProcessTypesLenght; j++)
    {
      if (aCellInfo[i].ProcessTypes[j] == aProcessType)
      {

        Error = Inquire(aCellInfo[i], InterpreterID, aProcessType, priority, param1, param2, &tempRezervace[pocet]);
        if (Error != 0)
        {
          NFC_RECIPES_ALL_DEBUG(TAGin, "Chyba ziskani\n");
          return 1;
        }
        ++pocet;
      }
    }
  }
  if (pocet == 0)
  {
    NFC_RECIPES_ALL_DEBUG(TAGin, "Nenalezena zadna bunka\n");
    return 1;
  }
  float minValue = 0;
  int index = 0;
  for (size_t i = 0; i < pocet; i++)
  {
    if (i == 0)
    {
      minValue = tempRezervace[i].Price;
    }
    else if (minValue < tempRezervace[i].Price)
    {
      minValue = tempRezervace[i].Price;
      index = i;
    }
  }
  aReservation->IDofCell = tempRezervace[index].IDofCell;
  aReservation->IDofReservation = tempRezervace[index].IDofReservation;
  aReservation->ProcessType = tempRezervace[index].ProcessType;
  aReservation->TimeOfReservation = tempRezervace[index].TimeOfReservation;

  return 0;
}

bool ExistType(CellInfo *aCellInfo, uint16_t aNumberOfCells, uint8_t aProcessType)
{
  static const char *TAGin = "ExistType";
  NFC_RECIPES_ALL_DEBUG(TAGin, "Kontroluji jestli existuje typ v bunkach\n");
  for (size_t i = 0; i < aNumberOfCells; i++)
  {
    for (size_t j = 0; j < aCellInfo[i].ProcessTypesLenght; j++)
    {
      if (aCellInfo[i].ProcessTypes[j] == aProcessType)
      {
        return true;
        NFC_RECIPES_ALL_DEBUG(TAGin, "Existuje\n");
      }
    }
  }
  NFC_RECIPES_ALL_DEBUG(TAGin, "Neexistuje\n");
  return false;
}

uint8_t AskForValidOffer(THandlerData *aHandlerData, uint16_t *aLastGoodReserved, CellInfo *aCellInfo, uint16_t aNumberOfCells)
{
  //  - Poptani jestli plaati vsechny nabidky
  static const char *TAGin = "AskForValidOffer";

  NFC_RECIPES_ALL_DEBUG(TAGin, "Ptam se jestli poptavka plati //TODO\n");

  bool rezervovat = true;
  bool AllValid = true;
  for (size_t i = aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep; i < aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps; i++)
  {
    bool skip = false;
    if (aHandlerData->sIntegrityCardInfo.sRecipeStep[i].ProcessCellID != 0)
    {
      Reservation tempReservation;
      bool zmena = false;
      tempReservation.IDofReservation = aHandlerData->sIntegrityCardInfo.sRecipeStep[i].ProcessCellReservationID;
      CellInfo tempCell;
      for (size_t j = 0; j < aNumberOfCells; j++)
      {
        if (aHandlerData->sIntegrityCardInfo.sRecipeStep[i].ProcessCellID == aCellInfo[j].IDofCell)
        {

          tempCell = aCellInfo[j];
          break;
        }
      }

      GetInquireIsValid(tempCell, &tempReservation, &zmena);
      if (zmena)
      {
        AllValid = false;
        return 1;
      }
      else
      {
        *aLastGoodReserved = i * 2;
      }
    }
    else
    {
      skip = true;
    }
    if (aHandlerData->sIntegrityCardInfo.sRecipeStep[i].TransportCellID != 0)
    {
      Reservation tempReservation;
      bool zmena = false;
      tempReservation.IDofReservation = aHandlerData->sIntegrityCardInfo.sRecipeStep[i].TransportCellReservationID;
      CellInfo tempCell;
      for (size_t j= 0; j < aNumberOfCells; j++)
      {
        if (aHandlerData->sIntegrityCardInfo.sRecipeStep[i].TransportCellID == aCellInfo[j].IDofCell)
        {
          tempCell = aCellInfo[j];
          break;
        }
      }
      GetInquireIsValid(tempCell, &tempReservation, &zmena);
      if (zmena)
      {
        AllValid = false;
        return 1;
      }
      else
      {
        *aLastGoodReserved = i * 2 + 1;
      }
    }
    else if(skip)
    {
      break;
    }
  }

  return 0;
}

uint8_t ReserveAllOfferedReservation(THandlerData *aHandlerData, CellInfo *aCellInfo, uint16_t aNumberOfCells,SemaphoreHandle_t* aSemaforHandlerNFC)
{
  static const char *TAGin = "ReserveAllOfferedReservation";
  uint8_t Error = 0;
  NFC_RECIPES_ALL_DEBUG(TAGin, "Provedeni rezervace\n");
  for (size_t i = aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep; i < aHandlerData->sIntegrityCardInfo.sRecipeInfo.RecipeSteps; i++)
  {bool skip = false;
    if (aHandlerData->sIntegrityCardInfo.sRecipeStep[i].ProcessCellID != 0)
    {
      
      Reservation tempReservation;
      bool zarezervovano = false;
      tempReservation.IDofReservation = aHandlerData->sIntegrityCardInfo.sRecipeStep[i].ProcessCellReservationID;
      CellInfo tempCell;
      for (size_t j = 0; j < aNumberOfCells; j++)
      {
        if (aHandlerData->sIntegrityCardInfo.sRecipeStep[i].ProcessCellID == aCellInfo[j].IDofCell)
        {
          tempCell = aCellInfo[j];
          break;
        }
      }
      Error = Reserve(tempCell, &tempReservation, &zarezervovano, &tempReservation);
      if(Error != 0)
      {
        //TODO
        return 1;
      }
      if (xSemaphoreTake(*aSemaforHandlerNFC, (TickType_t)10000) == pdTRUE)
      { TRecipeStep tempStep;
      NFC_RECIPES_ALL_DEBUG(TAGin, "Zapisuji cas processu na pozici %d.\n",aHandlerData->sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        NFC_Handler_GetRecipeStep(aHandlerData,&tempStep,aHandlerData->sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        tempStep.TimeOfProcess = tempReservation.TimeOfReservation;
        Error = NFC_Handler_WriteStep(aHandlerData, &tempStep, aHandlerData->sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);

        xSemaphoreGive(*aSemaforHandlerNFC);
      }
    }
    else
    {
      skip = true;
    }
    if (aHandlerData->sIntegrityCardInfo.sRecipeStep[i].TransportCellID != 0)
    {
      Reservation tempReservation;
      bool zarezervovano = false;
      tempReservation.IDofReservation = aHandlerData->sIntegrityCardInfo.sRecipeStep[i].TransportCellReservationID;
      CellInfo tempCell;
      for (size_t j = 0; j < aNumberOfCells; j++)
      {
        if (aHandlerData->sIntegrityCardInfo.sRecipeStep[i].TransportCellID == aCellInfo[j].IDofCell)
        {
          tempCell = aCellInfo[j];
          break;
        }
      }
      Error = Reserve(tempCell, &tempReservation, &zarezervovano,  &tempReservation);
      if(Error != 0)
      {
        //TODO
        return 1;
      }
      if (xSemaphoreTake(*aSemaforHandlerNFC, (TickType_t)10000) == pdTRUE)
      { NFC_RECIPES_ALL_DEBUG(TAGin, "Zapisuji cas transportu na pozici %d.\n",aHandlerData->sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        TRecipeStep tempStep;
        NFC_Handler_GetRecipeStep(aHandlerData,&tempStep,aHandlerData->sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        tempStep.TimeOfTransport = tempReservation.TimeOfReservation;
        Error = NFC_Handler_WriteStep(aHandlerData, &tempStep, aHandlerData->sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        xSemaphoreGive(*aSemaforHandlerNFC);
      }
      else
      {
        NFC_RECIPES_DEBUG(TAGin, "Nelze prevzit semafor k NFC.\n");
        return 1;
      }
    }
    else if(skip)
    {
      break;
    }
  }
      if (xSemaphoreTake(*aSemaforHandlerNFC, (TickType_t)10000) == pdTRUE)
      {
        Error = NFC_Handler_Sync(aHandlerData);
        NFC_RECIPES_ALL_DEBUG(TAGin, "Zapisuji casy rezervaci.\n");
        xSemaphoreGive(*aSemaforHandlerNFC);
      }
      else
      {
        NFC_RECIPES_DEBUG(TAGin, "Nelze prevzit semafor k NFC.\n");
        return 1;
      }
  // TODO - Rezervace vsech bunek
  return 0;
}

uint8_t AskForValidReservation(THandlerData *aHandlerData, bool process, CellInfo *aCellInfo, uint16_t aNumberOfCells)
{
  // TODO

  return 0;
}

uint8_t OcupancyCell(CellInfo *aCellInfo, uint16_t aNumberOfCells, uint16_t aActualCellID, bool Occupancy)
{

  // TODO
  return 0;
}
uint8_t DoReservation(THandlerData *aHandlerData, CellInfo *aCellInfo, uint16_t aNumberOfCells, bool process)
{
    if (aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID != 0 && process)
    {
      Reservation tempReservation;
      bool zahajeno = false;
      tempReservation.IDofReservation = aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellReservationID;
      CellInfo tempCell;
      for (size_t j = 0; j < aNumberOfCells; j++)
      {
        if (aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID == aCellInfo[j].IDofCell)
        {
          tempCell = aCellInfo[j];
          break;
        }
      }
      DoReservation_klient(tempCell, &tempReservation, &zahajeno);
    }
    else if ((aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].TransportCellID != 0&&!process))
    {
      Reservation tempReservation;
      bool zahajeno = false;
      tempReservation.IDofReservation = aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].TransportCellReservationID;
      CellInfo tempCell;
      for (size_t j = 0; j < aNumberOfCells; j++)
      {
        if (aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].TransportCellID == aCellInfo[j].IDofCell)
        {
          tempCell = aCellInfo[j];
          break;
        }
      }
      DoReservation_klient(tempCell, &tempReservation, &zahajeno);
    }
    else
    {
      return 1;
    }
    
  
  // TODO
  return 0;
}

uint8_t IsDoneReservation(THandlerData *aHandlerData, CellInfo *aCellInfo, uint16_t aNumberOfCells, bool process)
{
  // 0 - Neni hotovo, 1- Hotov a správne, 2 - Hotovo a spatne
  Reservation tempReservation;
  bool finished = false;

  if (process)
  {
    tempReservation.IDofReservation = aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellReservationID;
    CellInfo tempCell;
    for (size_t i = 0; i < aNumberOfCells; i++)
    {
      if (aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID == aCellInfo[i].IDofCell)
      {
        tempCell = aCellInfo[i];
        break;
      }
    }
    IsFinished(tempCell, &tempReservation, &finished);
  }
  else
  {
    tempReservation.IDofReservation = aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].TransportCellReservationID;
    CellInfo tempCell;
    for (size_t i = 0; i < aNumberOfCells; i++)
    {
      if (aHandlerData->sIntegrityCardInfo.sRecipeStep[aHandlerData->sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].TransportCellID == aCellInfo[i].IDofCell)
      {
        tempCell = aCellInfo[i];
        break;
      }
    }
    IsFinished(tempCell, &tempReservation, &finished);
  }

  // TODO
  return finished;
}
char *GetRafName(uint8_t aRaf)
{
  char *Pole[] =
      {
          "State_Mimo_Polozena", // Sklenice se polozina na cteckou(default) //Written
          "State_Mimo_NastaveniNaPresunDoSkladu",
          "State_Inicializace_Podminky",     // Prvni cast inicializace
          "State_Inicializace_ZiskaniAdres", // Ziskani adres od ResearchServeru
          "State_Poptavka_Vyroba",
          "State_Poptavka_Skladovani",
          "State_Poptavka_Transporty",
          "State_Poptavka_AktualniBunkaPrioritne",
          "State_Rezervace",
          "State_Transport",
          "State_WaitUntilRemoved",
          "State_Vyroba_Objeveni",
          "State_Vyroba_OznameniOProvedeni",
          "State_Vyroba_SpravneProvedeni",
          "State_ZmizeniZeCtecky",
          "State_KonecReceptu",
          "State_NovyRecept",
          "NouzovyStav"};
  return Pole[aRaf];
}

TCardInfo SaveLocalsData(THandlerData *aHandlerData, TCardInfo aCardInfo)
{
  TCardInfo aTemp;
  aTemp = aCardInfo;
  aTemp.sRecipeInfo.ID = aHandlerData->sIntegrityCardInfo.sRecipeInfo.ID;
  aTemp.sRecipeInfo.RightNumber = aHandlerData->sIntegrityCardInfo.sRecipeInfo.RightNumber;
  aTemp.sRecipeInfo.NumOfDrinks = aHandlerData->sIntegrityCardInfo.sRecipeInfo.NumOfDrinks;
  return aTemp;
}
uint8_t AddRecipe(THandlerData *aHandlerData, TRecipeStep aRecipeStep, uint8_t aRecipeBefore, SemaphoreHandle_t *xSemaforNFC,bool First)
{
  static const char *TAGin = "AddRecipe";
  uint8_t Error = 0;
  NFC_RECIPES_DEBUG(TAGin, "Pridavani receptu\n");
  if (aHandlerData->sWorkingCardInfo.TRecipeInfoLoaded == false)
  {
    NFC_RECIPES_DEBUG(TAGin, "Recept nelze pridat, protoze neni nactene Info\n");
    return 1; // Neni nactene info
  }
  TRecipeInfo tempRecipeInfo;
  Error = NFC_Handler_GetRecipeInfo(aHandlerData, &tempRecipeInfo);
  if (tempRecipeInfo.RecipeSteps == 0)
  {
    ++tempRecipeInfo.RecipeSteps;
    NFC_RECIPES_DEBUG(TAGin, "Recept neobsahuje zadne kroky.\n");
    if (xSemaphoreTake(*xSemaforNFC, (TickType_t)10000) == pdTRUE)
    {
      Error = NFC_ChangeRecipeStepsSize(&aHandlerData->sWorkingCardInfo, tempRecipeInfo.RecipeSteps);
      Error = NFC_Handler_WriteInfo(aHandlerData, &tempRecipeInfo);
      aRecipeStep.ID = 0;
      aRecipeStep.NextID = 0;
      Error = NFC_Handler_WriteStep(aHandlerData, &aRecipeStep, tempRecipeInfo.RecipeSteps - 1);

      xSemaphoreGive(*xSemaforNFC);
    }
    else
    {
      NFC_RECIPES_DEBUG(TAGin, "Semafor ma nekdo jiny.\n");

      return 2; // Nelze vzit semafor
    }

    NFC_RECIPES_ALL_DEBUG(TAGin, "Krok se uspesne pridal.\n");
  }
  else
  {
    NFC_RECIPES_ALL_DEBUG(TAGin, "Recept ma jiz kroky.\n");
    if (aRecipeBefore == tempRecipeInfo.RecipeSteps)
    {
      NFC_RECIPES_DEBUG(TAGin, "Bunka je mimo rozsah.\n");
      return 3; // bunka je mimo rozsah
    }
    ++tempRecipeInfo.RecipeSteps;
    if (xSemaphoreTake(*xSemaforNFC, (TickType_t)10000) == pdTRUE)
    { NFC_RECIPES_ALL_DEBUG(TAGin, "Menim data.\n");
      Error = NFC_ChangeRecipeStepsSize(&aHandlerData->sWorkingCardInfo, tempRecipeInfo.RecipeSteps);
      TRecipeStep TempOrigoStep;
      Error = NFC_Handler_GetRecipeStep(aHandlerData, &TempOrigoStep, aRecipeBefore);
      aRecipeStep.ID = tempRecipeInfo.RecipeSteps - 1;
      
      if(!First)
      {
      if (TempOrigoStep.NextID == TempOrigoStep.ID)
      {
        aRecipeStep.NextID = aRecipeStep.ID;
      }
      else 
      {
        aRecipeStep.NextID = TempOrigoStep.NextID;
      }
      TempOrigoStep.NextID = aRecipeStep.ID;
      }
      else
      {
         aRecipeStep.NextID = TempOrigoStep.ID;
      }
      Error = NFC_Handler_WriteInfo(aHandlerData, &tempRecipeInfo);
      Error = NFC_Handler_WriteStep(aHandlerData, &TempOrigoStep, aRecipeBefore);
      Error = NFC_Handler_WriteStep(aHandlerData, &aRecipeStep, tempRecipeInfo.RecipeSteps - 1);
      xSemaphoreGive(*xSemaforNFC);
    }
    else
    {
      NFC_RECIPES_DEBUG(TAGin, "Semafor ma nekdo jiny.\n");

      return 2; // Nelze vzit semafor
    }
  }
  NFC_RECIPES_DEBUG(TAGin, "Recept se pridal.\n");
  return 0;
}

uint8_t GetMinule(THandlerData *aHandlerData,uint8_t ActualID,uint8_t* PreviousID)
{
  if(aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps>= ActualID)
  {
    return 1;//mimo rozsah
  }
  for (size_t i = 0; i < aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps; i++)
  {
    if(aHandlerData->sWorkingCardInfo.sRecipeStep[i].NextID == ActualID)
    {
      *PreviousID = aHandlerData->sWorkingCardInfo.sRecipeStep[i].ID;
      return 0;
    }
  }
  return 2;//Nenalezeno
}