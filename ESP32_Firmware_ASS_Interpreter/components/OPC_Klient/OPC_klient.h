#ifndef NFC_Klient_H
#define NFC_Klient_H

#ifdef __cplusplus
extern "C"
{
#endif
#include "NFC_recipes.h"
#include <stdio.h>
#include <sys/param.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_netif.h"
#include <esp_flash_encrypt.h>
#include <esp_task_wdt.h>
#include <esp_sntp.h>
#include "esp_event.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "lwip/ip_addr.h"
#include "sdkconfig.h"
#include <time.h>

#include "open62541.h"


#include "ethernet_init.h"
#include "esp_eth.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "ethernet_init.h"
#include "sdkconfig.h"
#include <lwip/sockets.h>

extern bool CasNastaven;

void time_sync_notification_cb(struct timeval *tv);
static void initialize_sntp(void);
static bool obtain_time(void);
static void opc_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data);
static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data);
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data);
void connection_scan();
bool ClientStart(UA_Client **iManagement_client, const char *IPAdress);
uint8_t Inquire(CellInfo aCellInfo, uint16_t IDInterpreter, uint8_t TypeOfProcess, UA_Boolean priority, uint8_t param1, uint16_t param2, Reservation *aRezervace); 
uint8_t GetInquireIsValid(CellInfo aCellInfo, Reservation *aRezervace, bool *Zmena);
uint8_t Reserve(CellInfo aCellInfo, Reservation *aRezervacePuvod, bool *Rezervovano,Reservation *aRezervaceNova);
uint8_t DoReservation_klient(CellInfo aCellInfo, Reservation *aRezervace, bool *Zahajeno);
uint8_t IsFinished(CellInfo aCellInfo, Reservation *aRezervace, bool *finished);
uint8_t Occupancy(CellInfo aCellInfo, bool Okupovani);     
uint64_t GetTime();

/* PLC AAS contract: CurrentId variable + AAS methods (ReportProduct, GetSupported, ReserveAction, FreeFromPosition) */
bool OPC_WriteCurrentId(const char *endpoint, const char *value);
/* ReportProduct: InputMessage = sr_id (decimal digits only, must not be 0) */
bool OPC_ReportProduct(const char *endpoint, const char *sr_id_decimal);
/* ReportProductEx: same as ReportProduct but returns OutputMessage in outBuf (e.g. "Success", "Error:8501"). Logs sr_id digits only. */
bool OPC_ReportProductEx(const char *endpoint, const char *sr_id_decimal, char *outBuf, size_t outSize);
/* Build sr_id decimal string from UID bytes; ensures non-zero (FNV-1a fallback if needed) */
bool OPC_BuildSrIdFromUid(const uint8_t *uid, uint8_t uidLen, char *outBuf, size_t outSize);
/* GetSupported / ReserveAction: InputMessage = "sr_id/priority/material/parameterA/parameterB" (5 fields). outBuf receives OutputMessage. */
bool OPC_GetSupported(const char *endpoint, const char *inputMessage_5field, char *outBuf, size_t outSize);
bool OPC_ReserveAction(const char *endpoint, const char *inputMessage_5field, char *outBuf, size_t outSize);
/* FreeFromPosition: InputMessage = "sr_id" (decimal only) */
bool OPC_FreeFromPosition(const char *endpoint, const char *sr_id_decimal, char *outBuf, size_t outSize);
/* Wait for AAS step completion (timeout-based; no PLC change). */
void OPC_AAS_WaitCompletion(uint32_t timeout_ms);
/* Poll GetStatus until finished / error / timeout; uses PLC AAS handshake. */
bool OPC_AAS_WaitCompletionPoll(const char *endpoint, const char *sr_id_decimal, uint32_t timeout_ms, uint32_t poll_interval_ms);

#ifdef __cplusplus
}
#endif

#endif //NFC_Klient_H