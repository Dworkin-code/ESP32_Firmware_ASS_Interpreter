#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_log_internal.h>
#include "opcua_esp32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"

#include "sdkconfig.h"
#include "mdns.h"

#include "pn532.h"
#include "NFC_reader.h"
#include "NFC_handler.h"
#include "NFC_recipes.h"
#include "OPC_klient.h"
#include "string.h"

#include "neopixel.h"
#include "nvs_flash.h"

#include "driver/gpio.h"
#include "libtelnet.h"
#include "telnet/server.h"
// #include "driver/adc.h"
// #include "driver/dac.h"

typedef unsigned char byte;

#define PN532_SCK 9
#define PN532_MOSI 11
#define PN532_SS 12   // CONFIG_PN532_SS
#define PN532_MISO 10 // CONFIG_PN532_MISO

#define CONFIG_FREERTOS_ENABLE_BACKWARD_COMPATIBILITY 1
#define IDOFINTERPRETTER 0

typedef struct
{
  SemaphoreHandle_t xNFCReader;
  SemaphoreHandle_t xEthernet;
  bool CardOnReader;
  pn532_t NFC_Reader;
  tNeopixelContext Svetelka;
  // další proměnné...
} TaskParams;
// static const char *TAG = "APP";
tNeopixelContext Svetelka;
telnet_server_config_t telnet_server_config = TELNET_SERVER_DEFAULT_CONFIG;
CellInfo MyCellInfo;

#define NFC_STATE_DEBUG_EN 1

#ifdef NFC_STATE_DEBUG_EN
#define NFC_STATE_DEBUG(tag, fmt, ...)                                                                                  \
  do                                                                                                                    \
  {                                                                                                                     \
    printf("\x1B[32m[Raf:%s]:\x1B[0m " fmt, tag, ##__VA_ARGS__);                                                        \
    struct user_t *users = GetUsers();                                                                                  \
    for (size_t i = 0; i != CONFIG_TELNET_SERVER_MAX_CONNECTIONS; ++i)                                                  \
    {                                                                                                                   \
      if (users[i].sock != -1 && users[i].name != 0)                                                                    \
      {                                                                                                                 \
        telnet_printf(users[i].telnet, "\x1B[32m[ID:%d Raf:%s]:\x1B[0m " fmt, MyCellInfo.IDofCell, tag, ##__VA_ARGS__); \
      }                                                                                                                 \
    }                                                                                                                   \
    fflush(stdout);                                                                                                     \
  } while (0)
#else
#define NFC_STATE_DEBUG(fmt, ...)
#endif

void State_Machine(void *pvParameter)
{
  uint8_t RAF = 0;
  uint8_t RAFnext = 0;

  TaskParams *Parametry = (TaskParams *)pvParameter;
  CellInfo *Bunky = NULL;
  uint16_t BunkyVelikost = 0;
  THandlerData iHandlerData;
  uint8_t Error = 0;
  uint8_t Counter = 0;
  uint8_t ReceiptCounter = 0;
  TRecipeStep tempStep;
  Reservation Process;
  TRecipeInfo tempInfo;
  uint64_t ActualTime = 20;
  // static esp_netif_t NetifHandler;
  NFC_Handler_Init(&iHandlerData);

  NFC_Handler_SetUp(&iHandlerData, Parametry->NFC_Reader);

  fflush(stdout);
  bool polozeno = false;
  while (true)
  {

    NFC_STATE_DEBUG(GetRafName(RAF), "\n\n\n");
    NFC_STATE_DEBUG(GetRafName(RAF), "Dalsi iterace\n");
    if (iHandlerData.sIntegrityCardInfo.TRecipeInfoLoaded && RAF != State_Mimo_Polozena)
    {
      NFC_STATE_DEBUG(GetRafName(RAF), "Aktualni krok receptu %d\n", iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
    }
    fflush(stdout);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    if (polozeno != Parametry->CardOnReader)
    {
      polozeno = Parametry->CardOnReader;
      NFC_STATE_DEBUG(GetRafName(RAF), "Oznameni bunce, ze je %s\n", polozeno ? "Obsazena" : "Uvolnena");
      OcupancyCell(Bunky, BunkyVelikost, MyCellInfo.IDofCell, polozeno);
    }

    if (!Parametry->CardOnReader && RAF != State_WaitUntilRemoved)
    {
      RAF = State_Mimo_Polozena;
      NFC_STATE_DEBUG(GetRafName(RAF), "Karta je odebrana\n");
      continue;
    }

    switch (RAF)
    {
    case State_Mimo_Polozena:
    {
      NFC_STATE_DEBUG(GetRafName(RAF), "Sklenice se objevila\n");
      if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Zacinam nacitat data\n");
        Error = NFC_Handler_LoadData(&iHandlerData);

        xSemaphoreGive(Parametry->xNFCReader);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Semafor ma nekdo sebrany\n");
        continue;
      }
      switch (Error)
      {
      case 0:
        break;
      case 4:
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Neplatny recept\n");
        RAF = State_Mimo_NastaveniNaPresunDoSkladu; // R
        continue;
        break;
      }
      default:
        NFC_STATE_DEBUG(GetRafName(RAF), "Chyba nacitani\n");
        continue;
        break;
      }
      NFC_Print(iHandlerData.sWorkingCardInfo);
      NFC_STATE_DEBUG(GetRafName(RAF), "Data se nacetla\n");
      if (iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep >= iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeSteps)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Recept je rozbity,(Pocet receptu: %d a aktualni recept: %d\n", iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeSteps, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        RAF = State_Mimo_NastaveniNaPresunDoSkladu; // G
        break;
      }
      else if (iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeDone == true)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Recept je hotov\n"); // G
        RAF = State_KonecReceptu;
        continue;
      }
      else if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].IsProcess == true)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Sklenice se objevila po processu\n");
        RAFnext = State_Vyroba_SpravneProvedeni; // H
        RAF = State_Inicializace_ZiskaniAdres;
        continue;
      }
      else if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].IsTransport == true)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Sklenice se objevila po transportu\n");
        RAF = State_Inicializace_ZiskaniAdres; // F
        RAFnext = State_Vyroba_Objeveni;
        continue;
      }
      else if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TimeOfProcess > 0 && MyCellInfo.IDofCell == iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Sklenice se objevila bez rezervovaneho transportu\n");
        RAF = State_Inicializace_ZiskaniAdres; // E
        RAFnext = State_Vyroba_Objeveni;
        continue;
      }
      else if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TimeOfTransport > 0 && MyCellInfo.IDofCell != iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Sklenice se objevila s rezervovanym transportem\n");
        RAF = State_Inicializace_ZiskaniAdres;
        RAFnext = State_Transport; // D
        continue;
      }
      else if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TransportCellReservationID || (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellReservationID && !iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].NeedForTransport))
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Jdu na rezervace\n");
        RAF = State_Inicializace_ZiskaniAdres;
        RAFnext = State_Rezervace; // C
        continue;
      }
      else if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellReservationID && iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].NeedForTransport)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Jdu na poptani Transportu\n");
        RAF = State_Inicializace_ZiskaniAdres;
        RAFnext = State_Poptavka_Transporty; // B - Poptavka vyroba
        continue;
      }
      RAF = State_Inicializace_ZiskaniAdres;
      RAFnext = State_Poptavka_Vyroba; // B - Poptavka vyroba
      continue;
      break;
    }
    case State_Mimo_NastaveniNaPresunDoSkladu:
    {

      NFC_Handler_Init(&iHandlerData);

      NFC_Handler_SetUp(&iHandlerData, Parametry->NFC_Reader);
      NFC_STATE_DEBUG(GetRafName(RAF), "Nahravam recept presun do skladu.\n");
      TCardInfo iCardInfo = GetCardInfoByNumber(3);

      NFC_STATE_DEBUG(GetRafName(RAF), "Vytvoren Novy recept.\n");
      if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
      {

        NFC_Handler_AddCardInfoToWorking(&iHandlerData, iCardInfo);
        ChangeID(&iHandlerData.sWorkingCardInfo, 1); // Nastaveni ID
        NFC_Handler_Sync(&iHandlerData);

        xSemaphoreGive(Parametry->xNFCReader);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vzit semafor\n");
      }

      NFC_STATE_DEBUG(GetRafName(RAF), "Novy recept navrat do skladu se nahral.\n");
      RAF = State_Mimo_Polozena; // I
      break;
    }
    case State_Inicializace_ZiskaniAdres:
    {
      NFC_STATE_DEBUG(GetRafName(RAF), "ZiskavamAdresy potrebnych bunek(operace %d)\n", iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TypeOfProcess);
      if (Bunky != NULL)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nicim vytvorene bunky\n");
        DestroyCellInfoArray(Bunky, BunkyVelikost);
      }
      NFC_STATE_DEBUG(GetRafName(RAF), "Ziskavam bunky\n");
      Bunky = GetCellInfoFromLDS(iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TypeOfProcess, &BunkyVelikost);

      // Existuje alespon 1Entita transport?
      bool existujeTransport = ExistType(Bunky, BunkyVelikost, Transport);
      if (!existujeTransport)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Neexistuje transportni bunka\n");
      }
      bool existujeTyp = ExistType(Bunky, BunkyVelikost, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TypeOfProcess);

      if (!existujeTyp)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Neexistuje typova bunka\n");
      }
      RAF = RAFnext;
      continue;
      break;
    }
    case State_Poptavka_Vyroba:
    {

      NFC_STATE_DEBUG(GetRafName(RAF), "Poptavam vyrobni jednotku\n");
      if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TypeOfProcess == Transport)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Preskakuji poptavku vyroby, protoze operace je transport\n");
        RAF = State_Poptavka_Transporty;
        break;
      }
      if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)20000) == pdTRUE)
      {
        Error = GetWinningCell(Bunky, BunkyVelikost, MyCellInfo.IDofCell, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TypeOfProcess, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ParameterProcess1, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ParameterProcess2, false, &Process);
        xSemaphoreGive(Parametry->xEthernet);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze ziskat semafor k Ethernetu\n");
        continue;
      }

      if (Error != 0)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vybrat vyherni bunku\n");
        // Nelze vyherni bunku
        continue;
      }

      NFC_Handler_GetRecipeStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
      tempStep.ProcessCellID = Process.IDofCell;
      tempStep.ProcessCellReservationID = Process.IDofReservation;
      tempStep.NeedForTransport = false;
      if (MyCellInfo.IDofCell != Process.IDofCell)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Je potreba transport(Aktualne %d- cil %d\n", MyCellInfo.IDofCell, Process.IDofCell);
        tempStep.NeedForTransport = true;
      }

      NFC_STATE_DEBUG(GetRafName(RAF), "Zapisuji data o vyherni procesni bunce\n");

      if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
      {
        Error = NFC_Handler_WriteSafeStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);

        xSemaphoreGive(Parametry->xNFCReader);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vzit semafor\n");
      }
      if (ExistType(Bunky, BunkyVelikost, Buffer))
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Existuje skladovaci bunka\n");
        // Existuje skladovaci bunka
      }
      RAF = State_Poptavka_Transporty; // 1
      continue;
      break;
    }
    case State_Poptavka_Skladovani:
    {
      NFC_STATE_DEBUG(GetRafName(RAF), "State_Poptavka_Skladovani\n");

      break;
    }
    case State_Poptavka_Transporty:
    {
      NFC_STATE_DEBUG(GetRafName(RAF), "Poptavka transportu\n");
      if (!iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].NeedForTransport && !(iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TypeOfProcess == Transport))
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Neni potreba transport\n");
        RAF = State_Rezervace; // C
        continue;
      }
      NFC_STATE_DEBUG(GetRafName(RAF), "Je potreba transport(%d)\n", iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].NeedForTransport);
      if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)20000) == pdTRUE)
      {
        if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID == Transport)
        {
          Error = GetWinningCell(Bunky, BunkyVelikost, MyCellInfo.IDofCell, Transport, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ParameterProcess1, 0, false, &Process);
        }
        else
        {
          Error = GetWinningCell(Bunky, BunkyVelikost, MyCellInfo.IDofCell, Transport, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID, 0, false, &Process);
        }
        xSemaphoreGive(Parametry->xEthernet);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze ziskat semafor k Ethernetu\n");
        continue;
      }

      if (Error != 0)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vybrat transportni bunku\n");
        continue;
        // Nelze vyherni transportni bunku
      }
      NFC_Handler_GetRecipeStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
      tempStep.NeedForTransport = true;
      tempStep.TransportCellID = Process.IDofCell;
      tempStep.TransportCellReservationID = Process.IDofReservation;
      NFC_STATE_DEBUG(GetRafName(RAF), "Zapisuji data o vyherni transportni bunce\n");
      if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
      {
        Error = NFC_Handler_WriteSafeStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);

        xSemaphoreGive(Parametry->xNFCReader);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vzit semafor\n");
      }

      RAF = State_Rezervace; // J
      continue;
      break;
    }
    case State_Rezervace:
    {
      NFC_STATE_DEBUG(GetRafName(RAF), "Provadim Rezervaci\n");
      uint16_t lastReserved = 0;

      if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)20000) == pdTRUE)
      {
        Error = AskForValidOffer(&iHandlerData, &lastReserved, Bunky, BunkyVelikost);
        xSemaphoreGive(Parametry->xEthernet);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze ziskat semafor k Ethernetu\n");
        continue;
      }

      if (Error != 0)
      {
        RAF = State_Poptavka_Vyroba; // K
        break;
      }
      NFC_STATE_DEBUG(GetRafName(RAF), "Vsechny nabidka plati\n");
      if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)20000) == pdTRUE)
      {
        Error = ReserveAllOfferedReservation(&iHandlerData, Bunky, BunkyVelikost, &Parametry->xNFCReader);
        xSemaphoreGive(Parametry->xEthernet);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze ziskat semafor k Ethernetu\n");
        continue;
      }
      if (Error != 0)
      {
        RAF = State_Mimo_Polozena;
      }
      NFC_STATE_DEBUG(GetRafName(RAF), "Vse se zarezervovalo\n");
      RAF = State_Transport; // L
      continue;
      break;
    }
    case State_Transport:
    {
      ActualTime = GetTime();
      NFC_STATE_DEBUG(GetRafName(RAF), "Stav transport\n");
      if (iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].NeedForTransport == 0)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Neni potreba transport\n");
        RAF = State_Vyroba_OznameniOProvedeni; // M
        break;
      }
      if (ActualTime < iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TimeOfTransport && false)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Jeste nezacal cas transportu Aktualni cas: %llu, Cas transportu: %llu\n", ActualTime, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].TimeOfTransport);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        break;
      }
      if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)20000) == pdTRUE)
      {
        if (AskForValidReservation(&iHandlerData, true, Bunky, BunkyVelikost) != 0 || AskForValidReservation(&iHandlerData, false, Bunky, BunkyVelikost) != 0)
        {
          //  Odregistrovat
          break;
        }
        xSemaphoreGive(Parametry->xEthernet);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze ziskat semafor k Ethernetu\n");
        continue;
      }

      NFC_STATE_DEBUG(GetRafName(RAF), "Ukladani dat\n");
      NFC_Handler_GetRecipeStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
      tempStep.IsTransport = 1;
      NFC_Handler_GetRecipeInfo(&iHandlerData, &tempInfo);
      tempInfo.ActualBudget = tempInfo.ActualBudget - tempStep.PriceForTransport;
      if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
      {
        Error = NFC_Handler_WriteInfo(&iHandlerData, &tempInfo);
        Error = NFC_Handler_WriteStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        Error = NFC_Handler_Sync(&iHandlerData);

        xSemaphoreGive(Parametry->xNFCReader);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vzit semafor\n");
      }
      NFC_STATE_DEBUG(GetRafName(RAF), "Moznost provedeni transportu\n");

      if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)20000) == pdTRUE)
      {
        Error = DoReservation(&iHandlerData, Bunky, BunkyVelikost, false);
        xSemaphoreGive(Parametry->xEthernet);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze ziskat semafor k Ethernetu\n");
        continue;
      }

      RAF = State_WaitUntilRemoved;
      break;
    }
    case State_WaitUntilRemoved:
    {

      NFC_STATE_DEBUG(GetRafName(RAF), "Cekam nez tag zmizi po odebrani transportem\n");

      if (!Parametry->CardOnReader)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Zmizel\n");

        RAF = State_Mimo_Polozena; // O
      }
      break;
    }
    case State_Vyroba_Objeveni:
    {
      if (iHandlerData.sIntegrityCardInfo.sRecipeStep[iHandlerData.sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].TypeOfProcess == Transport)
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Preskakuji vyrobu, protoze vyroba byla transport.\n");
        if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
        {

          Error = NFC_Handler_GetRecipeInfo(&iHandlerData, &tempInfo);

          NFC_Handler_GetRecipeStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
          NFC_STATE_DEBUG(GetRafName(RAF), "Nastavuji dalsi krok %d\n", tempStep.NextID);
          tempInfo.ActualRecipeStep = tempStep.NextID;
          tempStep.IsProcess = 0;
          tempStep.IsTransport = 0;
          tempStep.IsStepDone = 1;
          NFC_Handler_WriteStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
          NFC_Handler_WriteInfo(&iHandlerData, &tempInfo);
          NFC_Handler_Sync(&iHandlerData);
          xSemaphoreGive(Parametry->xNFCReader);
          RAF = State_Mimo_Polozena;
          break;
        }
        else
        {
          NFC_STATE_DEBUG(GetRafName(RAF), "Semafor ma nekdo jiny\n");

          break;
        }
      }
      if (iHandlerData.sIntegrityCardInfo.sRecipeStep[iHandlerData.sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID != MyCellInfo.IDofCell)
      {
        uint8_t Last = 0;
        NFC_STATE_DEBUG(GetRafName(RAF), "Jsme u spatne bunky, pridavam transport\n");
        Error = GetMinule(&iHandlerData, iHandlerData.sIntegrityCardInfo.sRecipeStep[iHandlerData.sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ID, &Last);
        AddRecipe(&iHandlerData, GetRecipeStepByNumber(10, iHandlerData.sIntegrityCardInfo.sRecipeStep[iHandlerData.sIntegrityCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID), Last, &Parametry->xNFCReader, Last == 0 && iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep == 0);
        NFC_Print(iHandlerData.sWorkingCardInfo);
        if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
        {

          NFC_Handler_GetRecipeStep(&iHandlerData, &tempStep, Last);
          tempStep.NextID = iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeSteps;
          TRecipeInfo tempInfo;
          Error = NFC_Handler_GetRecipeInfo(&iHandlerData, &tempInfo);
          tempInfo.ActualRecipeStep = tempInfo.RecipeSteps - 1;
          NFC_Handler_WriteInfo(&iHandlerData, &tempInfo);
          NFC_Handler_Sync(&iHandlerData);
          NFC_STATE_DEBUG(GetRafName(RAF), "Transport se zapsal\n");
          xSemaphoreGive(Parametry->xNFCReader);
          RAF = State_Mimo_Polozena;
          break;
        }
        else
        {
          NFC_STATE_DEBUG(GetRafName(RAF), "Semafor ma nekdo jiny\n");

          break;
        }
      }
      NFC_STATE_DEBUG(GetRafName(RAF), "NFC tag se objevil u vyrobni bunky\n");
      RAF = State_Vyroba_OznameniOProvedeni;
      break;
    }
    case State_Vyroba_OznameniOProvedeni:
    {
      NFC_STATE_DEBUG(GetRafName(RAF), "Muze se provest operace\n");
      NFC_Handler_GetRecipeStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
      NFC_Handler_GetRecipeInfo(&iHandlerData, &tempInfo);
      tempStep.IsTransport = 0;
      tempStep.IsProcess = 1;
      NFC_Handler_GetRecipeInfo(&iHandlerData, &tempInfo);
      tempInfo.ActualBudget = tempInfo.ActualBudget - tempStep.PriceForProcess;
      if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
      {
        Error = NFC_Handler_WriteInfo(&iHandlerData, &tempInfo);
        Error = NFC_Handler_WriteStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        Error = NFC_Handler_Sync(&iHandlerData);

        xSemaphoreGive(Parametry->xNFCReader);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vzit semafor\n");
        break;
      }
      Error = DoReservation(&iHandlerData, Bunky, BunkyVelikost, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID);
      RAF = State_Vyroba_SpravneProvedeni; // P
      break;
    }
    case State_Vyroba_SpravneProvedeni:
    {
      NFC_STATE_DEBUG(GetRafName(RAF), "Ziskavam jestli je hotovy process\n");
      switch (IsDoneReservation(&iHandlerData, Bunky, BunkyVelikost, iHandlerData.sWorkingCardInfo.sRecipeStep[iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep].ProcessCellID))
      {
      case 0:
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Process neni hotov\n");
        break;
      }
      case 1:
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Process je hotov\n");
        NFC_Handler_GetRecipeStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
        tempStep.IsProcess = 0;
        tempStep.IsStepDone = 1;

        if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
        {
          Error = NFC_Handler_WriteStep(&iHandlerData, &tempStep, iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
          xSemaphoreGive(Parametry->xNFCReader);
        }
        else
        {
          NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vzit semafor\n");
        }
        if (tempStep.NextID != tempStep.ID)
        {
          NFC_STATE_DEBUG(GetRafName(RAF), "Nastavuji dalsi krok %d\n", tempStep.NextID);
          tempInfo.ActualRecipeStep = tempStep.NextID;
        }
        else
        {
          tempInfo.RecipeDone = 1;
          ++tempInfo.NumOfDrinks;
          NFC_STATE_DEBUG(GetRafName(RAF), "Recept je hotov\n");
        }
        if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
        {
          Error = NFC_Handler_WriteInfo(&iHandlerData, &tempInfo);

          Error = NFC_Handler_Sync(&iHandlerData);

          xSemaphoreGive(Parametry->xNFCReader);
        }
        else
        {
          NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vzit semafor\n");
        }
        RAF = State_Mimo_Polozena;
        break;
      }
      case 2:
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Process je hotov, ale nedopadl\n");
        break;
      }
      default:
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Jina chyba\n");
        break;
      }
      }
      break;
    }
    case State_ZmizeniZeCtecky:
    {

      break;
    }
    case State_KonecReceptu:
    {

      NFC_STATE_DEBUG(GetRafName(RAF), "Recept je dokoncen, nahravam novy recept.\n");
      RAF = State_NovyRecept;
      break;
    }
    case State_NovyRecept:
    {

      NFC_STATE_DEBUG(GetRafName(RAF), "Nahravam dalsi recept.\n");
      if (ReceiptCounter > 3)
      {
        ReceiptCounter = 0;
      }
      TCardInfo iCardInfo = GetCardInfoByNumber(ReceiptCounter++);
      iCardInfo = SaveLocalsData(&iHandlerData, iCardInfo);
      NFC_STATE_DEBUG(GetRafName(RAF), "Vytvoren Novy recept.\n");

      if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
      {
        NFC_Handler_AddCardInfoToWorking(&iHandlerData, iCardInfo);
        NFC_Handler_Sync(&iHandlerData);

        xSemaphoreGive(Parametry->xNFCReader);
      }
      else
      {
        NFC_STATE_DEBUG(GetRafName(RAF), "Nelze vzit semafor\n");
      }

      NFC_STATE_DEBUG(GetRafName(RAF), "Novy recept se nahral.\n");
      RAF = State_Mimo_Polozena;

      break;
    }
    case NouzovyStav:
    {
      break;
    }
    default:
      break;
    }
  }
}

void Is_Card_On_Reader(void *pvParameter)
{
  TaskParams *Parametry = (TaskParams *)pvParameter;
  tNeopixel Svetlo = {3, NP_RGB(0, 0, 0)};
  bool minulyStav = !Parametry->CardOnReader;
  while (true)
  {

    if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
    {
      Parametry->CardOnReader = NFC_isCardReady(&Parametry->NFC_Reader);

      xSemaphoreGive(Parametry->xNFCReader);
    }

    if (minulyStav != Parametry->CardOnReader)
    {
      minulyStav = Parametry->CardOnReader;
      if (minulyStav)
      {
        Svetlo.rgb = NP_RGB(0, 150, 0);
        neopixel_SetPixel(Parametry->Svetelka, &Svetlo, 1);
      }
      else
      {
        Svetlo.rgb = NP_RGB(100, 0, 30);
        neopixel_SetPixel(Parametry->Svetelka, &Svetlo, 1);
      }
    }

    if (Parametry->CardOnReader)
    {
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    else
    {
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }
}

//moje uprava start
void OPC_Permanent_Test(void *pvParameter)
{
  TaskParams *Parametry = (TaskParams *)pvParameter;

  while (true)
  {
    if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)10000) == pdTRUE)
    {
      UA_Client *client = NULL;
      bool ok = ClientStart(&client, MyCellInfo.IPAdress);

      if (ok && client != NULL)
      {
        printf("OPC_TEST: Pripojeni na %s USPELO.\n", MyCellInfo.IPAdress);
        /* případně jednoduchý dotaz na server by šel sem */
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        client = NULL;
      }
      else
      {
        printf("OPC_TEST: Pripojeni na %s SELHALO.\n", MyCellInfo.IPAdress);
        if (client != NULL)
        {
          UA_Client_delete(client);
          client = NULL;
        }
      }

      xSemaphoreGive(Parametry->xEthernet);
    }
    else
    {
      printf("OPC_TEST: Nelze ziskat semafor k Ethernetu.\n");
    }

    // pauza mezi pokusy (5 s)
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

//moje uprava stop

void app_main()
{
  uint8_t processTypes1[] = {0, 1, 2};
  nvs_handle_t nvs_handle;
  nvs_flash_init();

  if (IDOFINTERPRETTER > 0)
  {
    uint8_t IDnew = IDOFINTERPRETTER;
    nvs_open("DataNFC", NVS_READWRITE, &nvs_handle);
    nvs_set_u8(nvs_handle, "ID_Interpretter", IDnew);
    printf("New Value of ID interpretter: %d \n", IDnew);
    nvs_commit(nvs_handle);
  }
  else
  {
    nvs_open("DataNFC", NVS_READONLY, &nvs_handle);
  }
  nvs_get_u8(nvs_handle, "ID_Interpretter", &MyCellInfo.IDofCell);
  nvs_close(nvs_handle);
  printf("ID_Of_Interpretter: %d\n", MyCellInfo.IDofCell);

  MyCellInfo.IPAdress = "192.168.0.1:4840";
  MyCellInfo.IPAdressLenght = strlen(MyCellInfo.IPAdress);
  MyCellInfo.ProcessTypes = processTypes1;
  MyCellInfo.ProcessTypesLenght = 3;

  TaskParams Parametry;
  Parametry.xEthernet = xSemaphoreCreateBinary();
  Parametry.xNFCReader = xSemaphoreCreateBinary();
  xSemaphoreGive(Parametry.xNFCReader);
  xSemaphoreGive(Parametry.xEthernet);
  Parametry.CardOnReader = false;
  Parametry.Svetelka = neopixel_Init(4, 3);
  setLight(&Parametry.Svetelka);
  NFC_Reader_Init(&Parametry.NFC_Reader, PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
  xTaskCreate(&Is_Card_On_Reader, "NFC_Zjistovani_pritomnosti_karty", 2048, (void *)&Parametry, 4, NULL);
  // xTaskCreate(&nfc_task, "nfc_task", 4096, (void*)&Parametry, 4, NULL);

  if (xSemaphoreTake(Parametry.xEthernet, (TickType_t)10000) == pdTRUE)
  {
    connection_scan();

    xSemaphoreGive(Parametry.xEthernet);
  }
// moje upravda kodu start

 int retries = 0;
while (!CasNastaven && retries < 5) 
{
    printf("Cekam na ziskani casu (pokus %d)\n", retries);
    retries++;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

if (!CasNastaven) {
    printf("Cas se neziskal, pokracuju dale bez casu.\n");
    CasNastaven = true;  // odblokovani stavu
} else {
    printf("Cas ziskan, pokracuju dale.\n");
}
// moje upravda kodu stop

// moje upravda kodu start
 xTaskCreate(&OPC_Permanent_Test, "OPC_Test", 4096, (void *)&Parametry, 6, NULL);
// moje upravda kodu stop
  esp_err_t err = mdns_init();
  if (err)
  {
    printf("MDNS Init failed: %d\n", err);
  }
  char result[20];
  sprintf(result, "interpreter_%d", MyCellInfo.IDofCell);
  mdns_hostname_set(result);
  mdns_instance_name_set("ESP32 Telnet Server");
  mdns_service_add(NULL, "_telnet", "_tcp", 23, NULL, 0);
  telnet_server_create(&telnet_server_config);

  xTaskCreate(&State_Machine, "Stavovy_automat_Interpretteru", 4096, (void *)&Parametry, 7, NULL);

  while (true)
  {

    vTaskDelay(20000 / portTICK_PERIOD_MS);
  }
}
