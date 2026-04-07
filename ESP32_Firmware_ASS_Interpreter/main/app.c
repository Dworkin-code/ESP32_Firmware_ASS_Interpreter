#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_log_internal.h>
#include "esp_timer.h"
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

/* Set to 1 to use PLC AAS flow (ReportProduct, GetSupported, ReserveAction, write-back). Undefine LEGACY_FLOW. */
#ifndef LEGACY_FLOW
#define USE_PLC_AAS_FLOW 1
#endif
/* AAS completion: wait time after ReserveAction returns Success (ms); no PLC status poll. */
#define AAS_COMPLETION_TIMEOUT_MS 30000
/* Re-scan guard: same sr_id within this window (ms) skips ReserveAction to avoid double call. */
#define AAS_RESCAN_GUARD_MS 5000
/* Cross-cell handover polling config */
#define CROSS_CELL_STATUS_POLLS 3
#define CROSS_CELL_STATUS_POLL_INTERVAL_MS 500

/* Deterministic process-owner cell routing constants. */
#define CELL_UNKNOWN 0
#define CELL_SKLAD_KAPALIN 1
#define CELL_SODAMAKER 2
#define CELL_SHAKER 3
#define CELL_SKLAD_ALKOHOLU 4
#define CELL_SKLAD_SKLENICEK 5
#define CELL_OUTPUT 6

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

/* Re-scan guard: last sr_id we called ReserveAction for and when (ms). Reader-side only. */
static char s_lastSeenSrId[16];
static uint32_t s_lastActionTimestampMs;

/* Empty-tag: print diagnostic block only once per tag-present; reset when card removed. */
static bool s_emptyTagLogged;

/* Upper bound for RecipeSteps; tags with more are treated as empty/uninitialized. */
#define MAX_RECIPE_STEPS 64

static const char *normalize_cell_endpoint(const char *rawEndpoint, char *buffer, size_t bufferSize)
{
  if (!rawEndpoint || !buffer || bufferSize == 0)
    return NULL;
  if (strncmp(rawEndpoint, "opc.tcp://", 10) == 0)
  {
    snprintf(buffer, bufferSize, "%s", rawEndpoint + 10);
  }
  else
  {
    snprintf(buffer, bufferSize, "%s", rawEndpoint);
  }
  buffer[bufferSize - 1] = '\0';
  return buffer;
}

static bool parse_supported_positive(const char *response)
{
  if (!response || response[0] == '\0')
    return false;
  if (strncmp(response, "Error:", 6) == 0 || strncmp(response, "error:", 6) == 0)
    return false;
  if (strncmp(response, "Support:", 8) == 0)
  {
    int value = atoi(response + 8);
    return value > 0;
  }
  /* Simulation tolerance: non-error non-empty response considered supported. */
  return true;
}

static const char *assign_local_endpoint_from_cell_id(uint16_t cellId)
{
  switch (cellId)
  {
  case 1:
    return "192.168.168.66:4840";  // Skladkapalin
  case 2:
    return "192.168.168.102:4840"; // SodaMaker
  case 3:
    return "192.168.168.150:4840"; // Shaker
  case 4:
    return "192.168.168.88:4840";  // SkladAlkoholu
  case 5:
    return "192.168.168.63:4840";  // SkladSklenicek
  case 6:
    return "192.168.168.203:4840"; // DrticLedu
  default:
    return NULL;
  }
}

static uint16_t resolve_owner_cell_id_from_process_type(uint8_t typeOfProcess)
{
  switch (typeOfProcess)
  {
  case ToStorageGlass:
    return CELL_SKLAD_SKLENICEK;
  case StorageAlcohol:
    return CELL_SKLAD_ALKOHOLU;
  case StorageNonAlcohol:
    return CELL_SKLAD_KAPALIN;
  case Shaker:
    return CELL_SHAKER;
  case Cleaner:
    return CELL_SHAKER; /* Cleaning is handled by shaker-capable cell. */
  case SodaMake:
    return CELL_SODAMAKER;
  case ToCustomer:
    return CELL_OUTPUT;
  case Transport:
    return CELL_UNKNOWN; /* Transport is handled separately (no fixed owner). */
  default:
    return CELL_UNKNOWN;
  }
}

static bool resolve_next_target_cell(const THandlerData *handler, uint16_t myCellId,
                                     CellInfo *resolvedCell, uint8_t *resolvedStepIndex)
{
  if (!handler || !resolvedCell || !resolvedStepIndex || !handler->sWorkingCardInfo.TRecipeInfoLoaded ||
      !handler->sWorkingCardInfo.TRecipeStepLoaded)
    return false;
  const TRecipeInfo *info = &handler->sWorkingCardInfo.sRecipeInfo;
  uint8_t current = info->ActualRecipeStep;
  if (current >= info->RecipeSteps)
    return false;
  const TRecipeStep *currentStep = &handler->sWorkingCardInfo.sRecipeStep[current];
  uint8_t nextIndex = currentStep->NextID;
  if (nextIndex >= info->RecipeSteps)
  {
    if ((current + 1U) < info->RecipeSteps)
      nextIndex = current + 1U;
    else
      return false;
  }
  const TRecipeStep *nextStep = &handler->sWorkingCardInfo.sRecipeStep[nextIndex];
  uint16_t candidateCount = 0;
  CellInfo *cells = GetCellInfoFromLDS(nextStep->TypeOfProcess, &candidateCount);
  if (!cells || candidateCount == 0)
    return false;

  bool found = false;
  uint16_t preferredId = nextStep->ProcessCellID;
  for (uint16_t i = 0; i < candidateCount; i++)
  {
    if (cells[i].IDofCell == myCellId)
      continue;
    if (preferredId != 0 && cells[i].IDofCell != preferredId)
      continue;
    *resolvedCell = cells[i];
    found = true;
    break;
  }
  if (!found)
  {
    for (uint16_t i = 0; i < candidateCount; i++)
    {
      if (cells[i].IDofCell == myCellId)
        continue;
      *resolvedCell = cells[i];
      found = true;
      break;
    }
  }
  if (found)
    *resolvedStepIndex = nextIndex;
  DestroyCellInfoArray(cells, candidateCount);
  return found;
}

static bool build_target_action_message(const TRecipeStep *step, const char *sr_id, uint16_t localCellId, char *outMsg, size_t outMsgSize)
{
  if (!step || !sr_id || !outMsg || outMsgSize == 0)
    return false;
  int n = snprintf(outMsg, outMsgSize, "%s/%u/%u/%u/%u", sr_id,
                   (unsigned)localCellId,
                   (unsigned)step->TypeOfProcess,
                   (unsigned)step->ParameterProcess1,
                   (unsigned)step->ParameterProcess2);
  return (n > 0) && ((size_t)n < outMsgSize);
}

static bool build_local_aas_action_message(const TRecipeStep *step, const char *sr_id, char *outMsg, size_t outMsgSize)
{
  if (!step || !sr_id || !outMsg || outMsgSize == 0)
    return false;
  /* PLC payload contract: token0=id token1=priority token2=material/classifier token3=parameterA token4=parameterB */
  const unsigned priority = 0U;
  int n = snprintf(outMsg, outMsgSize, "%s/%u/%u/%u/%u", sr_id,
                   priority,
                   (unsigned)step->TypeOfProcess,
                   (unsigned)step->ParameterProcess1,
                   (unsigned)step->ParameterProcess2);
  return (n > 0) && ((size_t)n < outMsgSize);
}

static bool poll_remote_target_status(const char *endpoint, const char *sr_id)
{
  if (!endpoint || !sr_id)
    return false;
  char statusBuf[128];
  for (int i = 0; i < CROSS_CELL_STATUS_POLLS; i++)
  {
    statusBuf[0] = '\0';
    if (OPC_GetStatus(endpoint, sr_id, statusBuf, sizeof(statusBuf)))
    {
      printf("[CROSS_CELL] remote GetStatus poll=%d sr_id=%s status=%s\n", i + 1, sr_id, statusBuf);
      fflush(stdout);
      return true;
    }
    vTaskDelay(CROSS_CELL_STATUS_POLL_INTERVAL_MS / portTICK_PERIOD_MS);
  }
  return false;
}

static bool reserve_remote_target(THandlerData *handler, const char *sr_id, uint16_t myCellId, SemaphoreHandle_t xEthernet)
{
  if (!handler || !sr_id || !xEthernet)
    return false;

  CellInfo targetCell;
  uint8_t nextIndex = 0;
  if (!resolve_next_target_cell(handler, myCellId, &targetCell, &nextIndex))
  {
    printf("[CROSS_CELL] local finish: next target not resolved or no remote candidate\n");
    fflush(stdout);
    return false;
  }
  const TRecipeStep *nextStep = &handler->sWorkingCardInfo.sRecipeStep[nextIndex];
  if (targetCell.IDofCell == myCellId)
  {
    printf("[CROSS_CELL] skip remote reservation: resolved next cell is local (%u)\n", (unsigned)myCellId);
    fflush(stdout);
    return false;
  }

  char endpointBuf[96];
  if (!normalize_cell_endpoint(targetCell.IPAdress, endpointBuf, sizeof(endpointBuf)))
    return false;
  char inputMsg[96];
  if (!build_target_action_message(nextStep, sr_id, myCellId, inputMsg, sizeof(inputMsg)))
  {
    printf("[CROSS_CELL] failed to build InputMessage for next step\n");
    fflush(stdout);
    return false;
  }

  printf("[CROSS_CELL] local cell finished, localCell=%u nextCell=%u endpoint=%s step=%u type=%u\n",
         (unsigned)myCellId, (unsigned)targetCell.IDofCell, endpointBuf, (unsigned)nextIndex, (unsigned)nextStep->TypeOfProcess);
  printf("[CROSS_CELL] remote GetSupported request InputMessage=%s\n", inputMsg);
  fflush(stdout);

  if (xSemaphoreTake(xEthernet, (TickType_t)10000) != pdTRUE)
  {
    printf("[CROSS_CELL] failed: ethernet semaphore unavailable\n");
    fflush(stdout);
    return false;
  }

  bool success = false;
  char outBuf[128];
  outBuf[0] = '\0';
  bool supportedCallOk = OPC_GetSupported(endpointBuf, inputMsg, outBuf, sizeof(outBuf));
  bool supported = supportedCallOk && parse_supported_positive(outBuf);
  printf("[CROSS_CELL] remote GetSupported callOk=%u response=%s\n",
         (unsigned)supportedCallOk, outBuf[0] ? outBuf : "(empty)");
  if (supported)
  {
    outBuf[0] = '\0';
    printf("[CROSS_CELL] remote ReserveAction request InputMessage=%s\n", inputMsg);
    bool reserveCallOk = OPC_ReserveAction(endpointBuf, inputMsg, outBuf, sizeof(outBuf));
    bool reserveAccepted = reserveCallOk && !(strncmp(outBuf, "Error:", 6) == 0 || strncmp(outBuf, "error:", 6) == 0);
    printf("[CROSS_CELL] remote ReserveAction callOk=%u response=%s\n",
           (unsigned)reserveCallOk, outBuf[0] ? outBuf : "(empty)");
    if (reserveAccepted)
    {
      poll_remote_target_status(endpointBuf, sr_id);
      printf("[CROSS_CELL] remote reservation SUCCESS targetCell=%u sr_id=%s\n",
             (unsigned)targetCell.IDofCell, sr_id);
      success = true;
    }
    else
    {
      printf("[CROSS_CELL] remote reservation FAILED targetCell=%u sr_id=%s\n",
             (unsigned)targetCell.IDofCell, sr_id);
    }
  }
  else
  {
    printf("[CROSS_CELL] remote target not supported targetCell=%u sr_id=%s\n",
           (unsigned)targetCell.IDofCell, sr_id);
  }
  fflush(stdout);
  xSemaphoreGive(xEthernet);
  return success;
}

/*
 * Returns true if the loaded recipe is considered "empty" (no recipe).
 * Fills reasonBuf with a short reason (e.g. "RecipeSteps=0", "Step0 all zeros").
 */
static bool NFC_IsRecipeEmpty(const TRecipeInfo *info, const TRecipeStep *steps, int stepCount,
                              char *reasonBuf, size_t reasonBufSize)
{
  if (reasonBufSize > 0)
    reasonBuf[0] = '\0';
  if (!info)
  {
    if (reasonBufSize > 0)
      snprintf(reasonBuf, reasonBufSize, "InfoNull");
    return true;
  }
  /* B) RecipeSteps == 0 or > MAX_RECIPE_STEPS */
  if (info->RecipeSteps == 0)
  {
    if (reasonBufSize > 0)
      snprintf(reasonBuf, reasonBufSize, "RecipeSteps=0");
    return true;
  }
  if (info->RecipeSteps > MAX_RECIPE_STEPS)
  {
    if (reasonBufSize > 0)
      snprintf(reasonBuf, reasonBufSize, "RecipeSteps>%d", MAX_RECIPE_STEPS);
    return true;
  }
  /* steps NULL with positive step count is invalid (e.g. load failure) */
  if (steps == NULL && stepCount > 0)
  {
    if (reasonBufSize > 0)
      snprintf(reasonBuf, reasonBufSize, "StepsNull");
    return true;
  }
  /* C) ActualRecipeStep out of bounds and not a valid "done" state */
  if (stepCount > 0 && (unsigned)info->ActualRecipeStep >= (unsigned)stepCount && !info->RecipeDone)
  {
    if (reasonBufSize > 0)
      snprintf(reasonBuf, reasonBufSize, "ActualStepOutOfBounds");
    return true;
  }
  /* D) First step and recipe info look uninitialized (all zeros) */
  if (stepCount > 0 && steps != NULL)
  {
    const TRecipeStep *s0 = &steps[0];
    if (s0->TypeOfProcess == 0 && s0->ParameterProcess1 == 0 && s0->ParameterProcess2 == 0
        && s0->ProcessCellID == 0 && s0->TransportCellID == 0
        && info->ID == 0 && info->NumOfDrinks == 0 && info->ActualBudget == 0
        && info->Parameters == 0 && info->RightNumber == 0)
    {
      if (reasonBufSize > 0)
        snprintf(reasonBuf, reasonBufSize, "Step0 and info zeros");
      return true;
    }
  }
  return false;
}

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
      s_emptyTagLogged = false;
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
      /* Empty-tag check: skip PLC calls and print diagnostic once per tag-present */
      {
        char reasonBuf[64];
        if (NFC_IsRecipeEmpty(&iHandlerData.sWorkingCardInfo.sRecipeInfo,
                              iHandlerData.sWorkingCardInfo.sRecipeStep,
                              (int)iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeSteps,
                              reasonBuf, sizeof(reasonBuf)))
        {
          if (!s_emptyTagLogged)
          {
            char uidHex[20] = "(none)";
            char sr_idStr[16] = "(none)";
            if (iHandlerData.sWorkingCardInfo.sUidLength > 0)
            {
              size_t i, n = (size_t)iHandlerData.sWorkingCardInfo.sUidLength;
              if (n > 7)
                n = 7;
              for (i = 0; i < n; i++)
                sprintf(uidHex + 2 * i, "%02X", iHandlerData.sWorkingCardInfo.sUid[i]);
              uidHex[2 * n] = '\0';
              if (OPC_BuildSrIdFromUid(iHandlerData.sWorkingCardInfo.sUid, iHandlerData.sWorkingCardInfo.sUidLength, sr_idStr, sizeof(sr_idStr)))
                /* sr_idStr already set */;
              else
              {
                strncpy(sr_idStr, "(build failed)", sizeof(sr_idStr) - 1);
                sr_idStr[sizeof(sr_idStr) - 1] = '\0';
              }
            }
            printf("[NFC] EMPTY TAG / NO RECIPE DETECTED\n");
            printf("[NFC] UID=%s  sr_id=%s\n", uidHex, sr_idStr);
            printf("[NFC] Reason=%s\n", reasonBuf[0] ? reasonBuf : "(unknown)");
            printf("[NFC] Action=SKIP_PLC_CALLS\n");
            fflush(stdout);
            s_emptyTagLogged = true;
          }
          RAF = State_WaitUntilRemoved;
          continue;
        }
      }
      NFC_Print(iHandlerData.sWorkingCardInfo);
      NFC_STATE_DEBUG(GetRafName(RAF), "Data se nacetla\n");
      /* PLC AAS: build sr_id from UID (now stored in TCardInfo), write CurrentId, call ReportProductEx to get OutputMessage */
      char sr_id_buf[16];
      char uidStr[15];
      uidStr[0] = '\0';
      bool have_sr_id = false;
      char reportOutBuf[128];
      reportOutBuf[0] = '\0';
      bool report_call_ok = false;
      if (iHandlerData.sWorkingCardInfo.sUidLength > 0)
      {
        size_t i;
        for (i = 0; i < (size_t)iHandlerData.sWorkingCardInfo.sUidLength && i < 7; i++)
          sprintf(uidStr + 2 * i, "%02X", iHandlerData.sWorkingCardInfo.sUid[i]);
        uidStr[2 * i] = '\0';
        have_sr_id = OPC_BuildSrIdFromUid(iHandlerData.sWorkingCardInfo.sUid, iHandlerData.sWorkingCardInfo.sUidLength, sr_id_buf, sizeof(sr_id_buf));
        NFC_STATE_DEBUG(GetRafName(RAF), "UID bytes -> sr_id=%s\n", have_sr_id ? sr_id_buf : "(none)");
        if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)5000) == pdTRUE)
        {
          OPC_WriteCurrentId(MyCellInfo.IPAdress, uidStr);
          if (have_sr_id)
            report_call_ok = OPC_ReportProductEx(MyCellInfo.IPAdress, sr_id_buf, reportOutBuf, sizeof(reportOutBuf));
          xSemaphoreGive(Parametry->xEthernet);
        }
      }
#if defined(USE_PLC_AAS_FLOW) && USE_PLC_AAS_FLOW
      /* Idempotent ReportProduct: Success or Error:8501 (already reported) -> proceed; other Error -> write tag and abort */
      if (have_sr_id && iHandlerData.sWorkingCardInfo.TRecipeInfoLoaded)
      {
        bool report_ok = report_call_ok && (strcmp(reportOutBuf, "Success") == 0 || strcmp(reportOutBuf, "Error:8501") == 0);
        if (report_call_ok && !report_ok && strncmp(reportOutBuf, "Error:", 6) == 0)
        {
          NFC_STATE_DEBUG(GetRafName(RAF), "AAS: ReportProduct returned %s -> treat as failure\n", reportOutBuf);
          iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeDone = true;
          if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
          {
            NFC_Handler_WriteSafeInfo(&iHandlerData, &iHandlerData.sWorkingCardInfo.sRecipeInfo);
            NFC_Handler_Sync(&iHandlerData);
            xSemaphoreGive(Parametry->xNFCReader);
          }
          RAF = State_Mimo_Polozena;
          continue;
        }
        if (!report_ok)
        {
          /* Call failed or no Success/8501: fall through to legacy */
        }
        else
        {
          /* report_ok: always run step/route check first. */
          TRecipeInfo *info = &iHandlerData.sWorkingCardInfo.sRecipeInfo;
          uint8_t curStep = info->ActualRecipeStep;
          uint8_t numSteps = info->RecipeSteps;
          bool recipeFinished = (info->RecipeDone == true) || (curStep >= numSteps);

          if (recipeFinished)
          {
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS_DECISION: step %u / %u -> RECIPE_FINISHED\n",
                            (unsigned)curStep, (unsigned)numSteps);
            /* If not yet flagged as done on tag, mark and persist once. */
            if (!info->RecipeDone)
            {
              info->RecipeDone = true;
              if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
              {
                NFC_Handler_WriteSafeInfo(&iHandlerData, info);
                NFC_Handler_Sync(&iHandlerData);
                xSemaphoreGive(Parametry->xNFCReader);
              }
            }
            RAF = State_KonecReceptu;
            continue;
          }

          /* Re-scan guard: same sr_id within window -> do not call ReserveAction again */
          uint32_t now_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
          if (strcmp(sr_id_buf, s_lastSeenSrId) == 0 && (now_ms - s_lastActionTimestampMs) < (uint32_t)AAS_RESCAN_GUARD_MS)
          {
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS: re-scan same sr_id within %d ms, skip ReserveAction\n", AAS_RESCAN_GUARD_MS);
            RAF = State_WaitUntilRemoved;
            continue;
          }

          TRecipeStep *step = &iHandlerData.sWorkingCardInfo.sRecipeStep[curStep];
          NFC_STATE_DEBUG(GetRafName(RAF),
                          "AAS_DECISION: ActualRecipeStep=%u RecipeSteps=%u TypeOfProcess=%u P1=%u P2=%u\n",
                          (unsigned)curStep, (unsigned)numSteps,
                          (unsigned)step->TypeOfProcess,
                          (unsigned)step->ParameterProcess1,
                          (unsigned)step->ParameterProcess2);

          uint16_t ownerCellId = resolve_owner_cell_id_from_process_type(step->TypeOfProcess);
          bool localProcess = (ownerCellId != 0U) && (ownerCellId == MyCellInfo.IDofCell);
          NFC_STATE_DEBUG(GetRafName(RAF),
                          "AAS_DECISION: TypeOfProcess=%u owner_cell_id=%u local_cell_id=%u => %s\n",
                          (unsigned)step->TypeOfProcess,
                          (unsigned)ownerCellId,
                          (unsigned)MyCellInfo.IDofCell,
                          localProcess ? "LOCAL_PROCESS" : "REQUEST_TRANSPORT");

          if (localProcess)
          {
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS_DECISION: LOCAL_PROCESS\n");
            /* Build 5-field message: sr_id/priority/material/parameterA/parameterB (priority=0) */
            char msg5[80];
            if (!build_local_aas_action_message(step, sr_id_buf, msg5, sizeof(msg5)))
            {
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: message build failed\n");
              RAF = State_Mimo_Polozena;
              continue;
            }
            NFC_STATE_DEBUG(GetRafName(RAF),
                            "AAS: local InputMessage=%s [id=%s priority=0 material=%u pA=%u pB=%u]\n",
                            msg5, sr_id_buf,
                            (unsigned)step->TypeOfProcess,
                            (unsigned)step->ParameterProcess1,
                            (unsigned)step->ParameterProcess2);
            char outBuf[128];
            outBuf[0] = '\0';
            if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)10000) != pdTRUE)
            {
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: no ethernet semaphore\n");
              continue;
            }
            /* Optional: GetSupported; if response starts with "Error:" abort step */
            bool ok_supported = OPC_GetSupported(MyCellInfo.IPAdress, msg5, outBuf, sizeof(outBuf));
            if (ok_supported && outBuf[0] != '\0' && strncmp(outBuf, "Error:", 6) == 0)
            {
              NFC_STATE_DEBUG(GetRafName(RAF),
                              "AAS_FAIL_LOCAL: GetSupported Error response=%s -> keep step=%u pending (RecipeDone=%u)\n",
                              outBuf,
                              (unsigned)curStep,
                              (unsigned)iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeDone);
              xSemaphoreGive(Parametry->xEthernet);
              /* Do not write completion flags on local AAS request failure. Retry on next iteration. */
              RAF = State_Mimo_Polozena;
              continue;
            }
            /* ReserveAction */
            outBuf[0] = '\0';
            bool ok_reserve = OPC_ReserveAction(MyCellInfo.IPAdress, msg5, outBuf, sizeof(outBuf));
            if (!ok_reserve || (outBuf[0] != '\0' && strncmp(outBuf, "Error:", 6) == 0))
            {
              NFC_STATE_DEBUG(GetRafName(RAF),
                              "AAS_FAIL_LOCAL: ReserveAction callOk=%u response=%s -> keep step=%u pending (RecipeDone=%u)\n",
                              (unsigned)ok_reserve,
                              outBuf[0] ? outBuf : "(empty)",
                              (unsigned)curStep,
                              (unsigned)iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeDone);
              xSemaphoreGive(Parametry->xEthernet);
              /* Do not write completion flags on local AAS request failure. Retry on next iteration. */
              RAF = State_Mimo_Polozena;
              continue;
            }
            /* Success: record for re-scan guard, then poll GetStatus until finished or error/timeout */
            (void)strncpy(s_lastSeenSrId, sr_id_buf, sizeof(s_lastSeenSrId) - 1);
            s_lastSeenSrId[sizeof(s_lastSeenSrId) - 1] = '\0';
            s_lastActionTimestampMs = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
            bool completion_ok = OPC_AAS_WaitCompletionPoll(MyCellInfo.IPAdress, sr_id_buf, (uint32_t)AAS_COMPLETION_TIMEOUT_MS, 500);
            xSemaphoreGive(Parametry->xEthernet);
            if (!completion_ok)
            {
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: completion error or timeout -> RecipeDone\n");
              iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeDone = true;
              if (xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000) == pdTRUE)
              {
                NFC_Handler_WriteSafeInfo(&iHandlerData, &iHandlerData.sWorkingCardInfo.sRecipeInfo);
                NFC_Handler_Sync(&iHandlerData);
                xSemaphoreGive(Parametry->xNFCReader);
              }
              RAF = State_Mimo_Polozena;
              continue;
            }
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS: completion poll SUCCESS, entering write-back path\n");
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS: current step index=%u\n", (unsigned)curStep);
            step->IsStepDone = 1;
            iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep = curStep + 1;
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS: new ActualRecipeStep=%u\n",
                            (unsigned)iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep);
            if (iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep >= numSteps)
              iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeDone = true;
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS: RecipeDone=%u\n",
                            (unsigned)iHandlerData.sWorkingCardInfo.sRecipeInfo.RecipeDone);
            BaseType_t nfc_take_res = xSemaphoreTake(Parametry->xNFCReader, (TickType_t)10000);
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS: xSemaphoreTake(xNFCReader,10000)=%ld\n", (long)nfc_take_res);
            if (nfc_take_res == pdTRUE)
            {
              int64_t wb_step_start_ms = esp_timer_get_time() / 1000;
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: WRITEBACK start NFC_Handler_WriteStep t=%lldms\n", (long long)wb_step_start_ms);
              uint8_t write_step_res = NFC_Handler_WriteStep(&iHandlerData, step, curStep);
              int64_t wb_step_end_ms = esp_timer_get_time() / 1000;
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: WRITEBACK end NFC_Handler_WriteStep res=%u dt=%lldms t=%lldms\n",
                              (unsigned)write_step_res,
                              (long long)(wb_step_end_ms - wb_step_start_ms),
                              (long long)wb_step_end_ms);

              int64_t wb_info_start_ms = esp_timer_get_time() / 1000;
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: WRITEBACK start NFC_Handler_WriteSafeInfo t=%lldms\n", (long long)wb_info_start_ms);
              uint8_t write_info_res = NFC_Handler_WriteSafeInfo(&iHandlerData, &iHandlerData.sWorkingCardInfo.sRecipeInfo);
              int64_t wb_info_end_ms = esp_timer_get_time() / 1000;
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: WRITEBACK end NFC_Handler_WriteSafeInfo res=%u dt=%lldms t=%lldms\n",
                              (unsigned)write_info_res,
                              (long long)(wb_info_end_ms - wb_info_start_ms),
                              (long long)wb_info_end_ms);

              int64_t wb_sync_start_ms = esp_timer_get_time() / 1000;
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: WRITEBACK start NFC_Handler_Sync t=%lldms\n", (long long)wb_sync_start_ms);
              uint8_t sync_res = NFC_Handler_Sync(&iHandlerData);
              int64_t wb_sync_end_ms = esp_timer_get_time() / 1000;
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: WRITEBACK end NFC_Handler_Sync res=%u dt=%lldms t=%lldms\n",
                              (unsigned)sync_res,
                              (long long)(wb_sync_end_ms - wb_sync_start_ms),
                              (long long)wb_sync_end_ms);
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: NFC_Handler_WriteStep=%u\n", (unsigned)write_step_res);
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: NFC_Handler_WriteSafeInfo=%u\n", (unsigned)write_info_res);
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: NFC_Handler_Sync=%u\n", (unsigned)sync_res);
              xSemaphoreGive(Parametry->xNFCReader);
            }
            else
            {
              NFC_STATE_DEBUG(GetRafName(RAF), "AAS: NFC write-back skipped, xNFCReader semaphore not acquired\n");
            }
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS: step %u done, write-back OK\n", (unsigned)curStep);
            NFC_STATE_DEBUG(GetRafName(RAF), "AAS: transitioning to State_WaitUntilRemoved\n");
            RAF = State_WaitUntilRemoved;
            continue;
          }
          else
          {
            NFC_STATE_DEBUG(GetRafName(RAF),
                            "AAS_DECISION: REQUEST_TRANSPORT (TypeOfProcess=%u owner_cell_id=%u local_cell_id=%u)\n",
                            (unsigned)step->TypeOfProcess,
                            (unsigned)ownerCellId,
                            (unsigned)MyCellInfo.IDofCell);
            /* Route to existing transport / production routing logic. */
            RAF = State_Inicializace_ZiskaniAdres;
            RAFnext = State_Poptavka_Vyroba;
            continue;
          }
        }
      }
      /* Fall-through to legacy branch if not AAS or no sr_id */
#endif
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

      NFC_STATE_DEBUG(GetRafName(RAF), "State_WaitUntilRemoved entered\n");
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
        char finishedSrId[16];
        bool finishedHasSrId = OPC_BuildSrIdFromUid(iHandlerData.sWorkingCardInfo.sUid,
                                                    iHandlerData.sWorkingCardInfo.sUidLength,
                                                    finishedSrId, sizeof(finishedSrId));
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

        if (!tempInfo.RecipeDone && finishedHasSrId)
        {
          (void)reserve_remote_target(&iHandlerData, finishedSrId, MyCellInfo.IDofCell, Parametry->xEthernet);
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
        /* Debug session: suppress repetitive success spam to keep AAS path visible. */
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
  uint8_t id_interpretter = 0;
  nvs_get_u8(nvs_handle, "ID_Interpretter", &id_interpretter);
  MyCellInfo.IDofCell = id_interpretter;
  nvs_close(nvs_handle);
  printf("[BOOT] NVS loaded ID_Interpretter=%u\n", (unsigned)MyCellInfo.IDofCell);

  const char *localEndpoint = assign_local_endpoint_from_cell_id(MyCellInfo.IDofCell);
  if (localEndpoint == NULL)
  {
    localEndpoint = "192.168.168.102:4840";
    printf("[BOOT] WARN unknown cell ID=%u, fallback endpoint=%s\n",
           (unsigned)MyCellInfo.IDofCell, localEndpoint);
  }
  MyCellInfo.IPAdress = localEndpoint;
  MyCellInfo.IPAdressLenght = strlen(MyCellInfo.IPAdress);
  printf("[BOOT] Local endpoint assigned from cell ID: ID=%u endpoint=%s\n",
         (unsigned)MyCellInfo.IDofCell, MyCellInfo.IPAdress);
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
  xTaskCreate(&Is_Card_On_Reader, "NFC_Zjistovani_pritomnosti_karty", 3072, (void *)&Parametry, 4, NULL);
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
 xTaskCreate(&OPC_Permanent_Test, "OPC_Test", 6144, (void *)&Parametry, 6, NULL);
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

  xTaskCreate(&State_Machine, "Stavovy_automat_Interpretteru", 8192, (void *)&Parametry, 7, NULL);

  while (true)
  {

    vTaskDelay(20000 / portTICK_PERIOD_MS);
  }
}
