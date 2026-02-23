#include "OPC_klient.h"
#include "NFC_recipes.h"
#include <inttypes.h>
#include <stdlib.h>

//#define OPC_KLIENT_ALL_DEBUG_EN 1
//#define OPC_KLIENT_DEBUG_EN 1
/*!
Zajištění výpisu všeho debugování
*/
#ifdef OPC_KLIENT_ALL_DEBUG_EN
#define OPC_KLIENT_ALL_DEBUG(tag, fmt, ...)                      \
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
#define OPC_KLIENT_ALL_DEBUG(fmt, ...)
#endif

/*!
Zajištění výpisu lehkého debugování
*/
#ifdef OPC_KLIENT_DEBUG_EN
#define OPC_KLIENT_DEBUG(tag, fmt, ...)                         \
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
#define OPC_KLIENT_DEBUG(fmt, ...)
#endif
bool CasNastaven = false;
#define SNTP_TAG "SNTP"
#define TAG "OPCUA_ESP32"

static struct tm timeinfo;
static UA_Boolean sntp_initialized = false;
static time_t now = 0;



void time_sync_notification_cb(struct timeval *tv)
{
    OPC_KLIENT_DEBUG(SNTP_TAG, "Byl Obdrzen cas\n");
}

static void initialize_sntp(void)
{
    OPC_KLIENT_DEBUG(SNTP_TAG, "Ziskavam SNTP cas\n");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "tik.cesnet.cz");
    sntp_setservername(1, "tak.cesnet.cz");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
    sntp_initialized = true;
}

uint64_t GetTime()
{
    setenv("TZ", "GMT", 1);
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);

  

     return UA_DateTime_fromUnixTime(now)+55150900000; //TODO opravit na cas
}



static bool obtain_time(void)
{
    initialize_sntp();
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    memset(&timeinfo, 0, sizeof(struct tm));
    int retry = 0;
    const int retry_count = 20;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= retry_count)
    {
        OPC_KLIENT_ALL_DEBUG(SNTP_TAG, "Cekam nez ziskam cas... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
    return timeinfo.tm_year > (2016 - 1900);
}

static void opc_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    OPC_KLIENT_DEBUG("opc event handler", "Handler opc\n");
    // Kontrola času
    if (sntp_initialized != true)
    {
        if (timeinfo.tm_year < (2016 - 1900))
        {
            OPC_KLIENT_ALL_DEBUG(SNTP_TAG, "Cas neni nastaven, nastavuji\n");
            if (!obtain_time())
            {
                OPC_KLIENT_DEBUG(SNTP_TAG, "Nemuzu ziskat cas. //TODO\n");
            }
            time(&now);
        }
        setenv("TZ", "GMT-2", 1);
        tzset();
        localtime_r(&now, &timeinfo);
        OPC_KLIENT_DEBUG(SNTP_TAG, "Aktualni cas: %d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        CasNastaven = true;
    }
}

static void disconnect_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        OPC_KLIENT_DEBUG(TAG, "Ethernet pripojen\n");
        OPC_KLIENT_ALL_DEBUG(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x\n",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        OPC_KLIENT_DEBUG(TAG, "Ethernet odpojen\n");
        break;
    case ETHERNET_EVENT_START:
        OPC_KLIENT_DEBUG(TAG, "Ethernet Start\n");
        break;
    case ETHERNET_EVENT_STOP:
        OPC_KLIENT_DEBUG(TAG, "Ethernet Stop\n");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    OPC_KLIENT_DEBUG(TAG, "Ethernet ziskal IP Adresu\n");
    OPC_KLIENT_DEBUG(TAG, "~~~~~~~~~~~\n");
    OPC_KLIENT_DEBUG(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    OPC_KLIENT_ALL_DEBUG(TAG, "\nETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    OPC_KLIENT_ALL_DEBUG(TAG, "\nETHGW:" IPSTR, IP2STR(&ip_info->gw));
    OPC_KLIENT_DEBUG(TAG, "\n~~~~~~~~~~~\n");
}

// moje uprava kodu start

void connection_scan()
{
    OPC_KLIENT_DEBUG(TAG, "Nastavuji Ethernet\n");
    // ESP Ethernet inicializace
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;
    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));

    // Netif inicializace
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create instance(s) of esp-netif for Ethernet(s)
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);

    // Attach Ethernet driver to TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));

    // ✱✱✱ STATICKÁ IP – HLAVNÍ ÚPRAVA ✱✱✱
    esp_err_t err = esp_netif_dhcpc_stop(eth_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        OPC_KLIENT_DEBUG(TAG, "Nemuzu zastavit DHCP klienta: %s\n", esp_err_to_name(err));
    }

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip,      192, 168, 0, 10);  // IP čtečky
    IP4_ADDR(&ip_info.gw,      192, 168, 0, 1);   // gateway (IP PLC nebo klidně 0.0.0.0)
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // maska

    ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &ip_info));
    OPC_KLIENT_DEBUG(TAG, "Nastavuji statickou IP: 192.168.0.10\n");

    // Register user defined event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++)
    {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

    // Start opc handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &opc_event_handler, NULL));
}


bool ClientStart(UA_Client **iManagement_client, const char *IPAdress)
{
    OPC_KLIENT_DEBUG(TAG, "Start OPC Klient\n");

    /* Složení úplného endpoint URL pro open62541:
       IPAdress = "192.168.0.1:4840"
       endpoint = "opc.tcp://192.168.0.1:4840"
    */
    char endpointUrl[128];
    snprintf(endpointUrl, sizeof(endpointUrl), "opc.tcp://%s", IPAdress);
    OPC_KLIENT_DEBUG(TAG, "Pokus o pripojeni na OPC UA server: %s\n", endpointUrl);

    int Pocet = 1;

    while (Pocet > 0)
    {
        UA_ClientConfig config = UA_ClientConfig_default;
        config.timeout = 1000;  // 1 s timeout
        *iManagement_client = UA_Client_new(config);

        UA_StatusCode retval = UA_Client_connect(*iManagement_client, endpointUrl);

        if (retval != UA_STATUSCODE_GOOD)
        {
            OPC_KLIENT_DEBUG(TAG,
                             "OPC UA connect failed (0x%08x), pokus %d\n",
                             retval, Pocet);

            UA_Client_delete(*iManagement_client);
            *iManagement_client = NULL;

            ++Pocet;
            if (Pocet > 5)
            {
                OPC_KLIENT_DEBUG(TAG,
                                 "OPC UA se nepodarilo pripojit ani po 5 pokusech, koncim.\n");
                return false;
            }

            vTaskDelay(1000 / portTICK_PERIOD_MS);  // 1 s pauza
            continue;
        }

        OPC_KLIENT_DEBUG(TAG, "OPC UA klient pripojen.\n");
        return true;
    }

    return false;
}

// moje uprava kodu stop
uint8_t Inquire(CellInfo aCellInfo, uint16_t IDInterpreter, uint8_t TypeOfProcess, UA_Boolean priority, uint8_t param1, uint16_t param2, Reservation *aRezervace)
{
    OPC_KLIENT_ALL_DEBUG(TAG, "Start OPC Klient\n");
    UA_Client *management_client = NULL;
    if (!ClientStart(&management_client, aCellInfo.IPAdress))
    {
        return 2;
    }

    UA_Variant input[5];
    UA_Variant_init(&input[0]);
    UA_Variant_setScalar(&input[0], &IDInterpreter, &UA_TYPES[UA_TYPES_UINT16]);

    UA_Variant_init(&input[1]);
    UA_Variant_setScalar(&input[1], &TypeOfProcess, &UA_TYPES[UA_TYPES_BYTE]);

    UA_Variant_init(&input[2]);
    UA_Variant_setScalar(&input[2], &priority, &UA_TYPES[UA_TYPES_BOOLEAN]);

    UA_Variant_init(&input[3]);
    UA_Variant_setScalar(&input[3], &param1, &UA_TYPES[UA_TYPES_BYTE]);

    UA_Variant_init(&input[4]);
    UA_Variant_setScalar(&input[4], &param2, &UA_TYPES[UA_TYPES_UINT16]);

    UA_Variant *output;
    size_t outputSize;
    OPC_KLIENT_ALL_DEBUG(TAG, "Volam sitovou metodu\n");
    UA_StatusCode retval = UA_Client_call(management_client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_STRING(1, "Inquire"), 5, input, &outputSize, &output);
    if (retval == UA_STATUSCODE_GOOD)
    {
        aRezervace->IDofReservation = *(UA_UInt16 *)output[0].data;
        aRezervace->Price = *(UA_Float *)output[1].data;
        aRezervace->TimeOfReservation = *(UA_DateTime *)output[2].data;
        aRezervace->ProcessType = TypeOfProcess;
        aRezervace->IDofCell = aCellInfo.IDofCell;
        UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    }
    else
    {
        OPC_KLIENT_ALL_DEBUG(TAG,"Volani metody bylo neuspesne\n");
        return 1;
    }

    UA_Client_disconnect(management_client);
    UA_Client_delete(management_client);

    return 0;
}

uint8_t GetInquireIsValid(CellInfo aCellInfo, Reservation *aRezervace, bool *Zmena)
{

    OPC_KLIENT_ALL_DEBUG(TAG, "Start OPC Klient\n");
    UA_Client *management_client = NULL;
    if (!ClientStart(&management_client, aCellInfo.IPAdress))
    {
        return 2;
    }

    UA_Variant input;
    UA_Variant_init(&input);
    UA_Variant_setScalar(&input, &(aRezervace->IDofReservation), &UA_TYPES[UA_TYPES_UINT16]);

    UA_Variant *output;
    size_t outputSize;
    OPC_KLIENT_ALL_DEBUG(TAG, "Volam sitovou metodu");
    UA_StatusCode retval = UA_Client_call(management_client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_STRING(1, "IsValid"), 1, &input, &outputSize, &output);
    if (retval == UA_STATUSCODE_GOOD)
    {
        *Zmena = *(UA_Boolean *)output[3].data;
        UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    }
    else
    {
        OPC_KLIENT_ALL_DEBUG(TAG,"Volani metody bylo neuspesne\n");
        return 1;
    }

    UA_Client_disconnect(management_client);
    UA_Client_delete(management_client);

    return 0;
}
uint8_t Reserve(CellInfo aCellInfo, Reservation *aRezervacePuvod, bool *Rezervovano,Reservation *aRezervaceNova)
{

    OPC_KLIENT_ALL_DEBUG(TAG, "Start OPC Klient\n");
    UA_Client *management_client = NULL;
    if (!ClientStart(&management_client, aCellInfo.IPAdress))
    {
        return 2;
    }

    UA_Variant input;
    UA_Variant_init(&input);
    UA_Variant_setScalar(&input, &(aRezervacePuvod->IDofReservation), &UA_TYPES[UA_TYPES_UINT16]);

    UA_Variant *output;
    size_t outputSize;
    OPC_KLIENT_ALL_DEBUG(TAG, "Volam sitovou metodu\n");
    UA_StatusCode retval = UA_Client_call(management_client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_STRING(1, "Rezervation"), 1, &input, &outputSize, &output);
    if (retval == UA_STATUSCODE_GOOD)
    {
        *Rezervovano = *(UA_Boolean *)output[3].data;
        if(aRezervaceNova != NULL)
        {
            aRezervaceNova->IDofCell = aRezervacePuvod->IDofCell;
            aRezervaceNova->IDofReservation = *(UA_UInt16 *)output[0].data;
            aRezervaceNova->Price = *(UA_Float *)output[1].data;
            aRezervaceNova->TimeOfReservation = *(UA_DateTime *)output[2].data;
            aRezervaceNova->ProcessType = aRezervacePuvod->ProcessType;
        }
        UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    }
    else
    {
        OPC_KLIENT_ALL_DEBUG(TAG,"Volani metody bylo neuspesne.\n");
        return 1;
    }

    UA_Client_disconnect(management_client);
    UA_Client_delete(management_client);

    return 0;
}
uint8_t DoReservation_klient(CellInfo aCellInfo, Reservation *aRezervace, bool *Zahajeno)
{

    OPC_KLIENT_ALL_DEBUG(TAG,"Start OPC Klient\n");
    UA_Client *management_client = NULL;
    if (!ClientStart(&management_client, aCellInfo.IPAdress))
    {
        return 2;
    }

    UA_Variant input;
    UA_Variant_init(&input);
    UA_Variant_setScalar(&input, &(aRezervace->IDofReservation), &UA_TYPES[UA_TYPES_UINT16]);

    UA_Variant *output;
    size_t outputSize;
    OPC_KLIENT_ALL_DEBUG(TAG, "Volam sitovou metodu\n");
    UA_StatusCode retval = UA_Client_call(management_client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_STRING(1, "DoProcess"), 1, &input, &outputSize, &output);
    if (retval == UA_STATUSCODE_GOOD)
    {
        *Zahajeno = *(UA_Boolean *)output->data;
        
        UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    }
    else
    {
        OPC_KLIENT_ALL_DEBUG(TAG,"Volani metody bylo neuspesne.\n\n");
        return 1;
    }

    UA_Client_disconnect(management_client);
    UA_Client_delete(management_client);

    return 0;
}
uint8_t IsFinished(CellInfo aCellInfo, Reservation *aRezervace, bool *finished)
{

    OPC_KLIENT_ALL_DEBUG(TAG, "Start OPC Klient\n");
    UA_Client *management_client = NULL;
    if (!ClientStart(&management_client, aCellInfo.IPAdress))
    {
        return 2;
    }

    UA_Variant input;
    UA_Variant_init(&input);
    UA_Variant_setScalar(&input, &(aRezervace->IDofReservation), &UA_TYPES[UA_TYPES_UINT16]);

    UA_Variant *output;
    size_t outputSize;
    OPC_KLIENT_ALL_DEBUG(TAG,"Volam sitovou metodu\n");
    UA_StatusCode retval = UA_Client_call(management_client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_STRING(1, "IsFinished"), 1, &input, &outputSize, &output);
    if (retval == UA_STATUSCODE_GOOD)
    {
        *finished = *(UA_Boolean *)output->data;
        
        UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    }
    else
    {
        OPC_KLIENT_ALL_DEBUG(TAG,"Volani metody bylo neuspesne.\n\n");
        return 1;
    }

    UA_Client_disconnect(management_client);
    UA_Client_delete(management_client);

    return 0;
}
uint8_t Occupancy(CellInfo aCellInfo, bool Okupovani)
{

    OPC_KLIENT_ALL_DEBUG(TAG, "Start OPC Klient\n");
    UA_Client *management_client = NULL;
    if (!ClientStart(&management_client, aCellInfo.IPAdress))
    {
        return 2;
    }

    UA_Variant input;
    UA_Variant_init(&input);
    UA_Variant_setScalar(&input, &Okupovani, &UA_TYPES[UA_TYPES_BOOLEAN]);

    UA_Variant *output;
    size_t outputSize;
    OPC_KLIENT_ALL_DEBUG(TAG,"Volam sitovou metodu\n");
    UA_StatusCode retval = UA_Client_call(management_client, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_STRING(1, "Occupancy"), 1, &input, &outputSize, &output);
    if (retval == UA_STATUSCODE_GOOD)
    {
        
        UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    }
    else
    {
        OPC_KLIENT_ALL_DEBUG(TAG,"Volani metody bylo neuspesne.\n\n");
        return 1;
    }

    UA_Client_disconnect(management_client);
    UA_Client_delete(management_client);

    return 0;
}

/* --- PLC AAS contract: correct namespace index (ns=4) per UAExpert --- */
/* CurrentId variable: ns=4;i=6101 */
#define PLC_NODEID_CURRENTID_NS 4
#define PLC_NODEID_CURRENTID_ID 6101
/* ReportProduct method: ns=4;i=7004 */
#define PLC_NODEID_REPORTPRODUCT_METHOD_NS 4
#define PLC_NODEID_REPORTPRODUCT_METHOD_ID 7004

bool OPC_WriteCurrentId(const char *endpoint, const char *value)
{
    if (!endpoint || !value)
    {
        ESP_LOGE(TAG, "OPC_WriteCurrentId: invalid args");
        return false;
    }
    UA_Client *client = NULL;
    if (!ClientStart(&client, endpoint))
    {
        ESP_LOGE(TAG, "OPC_WriteCurrentId: connect failed");
        return false;
    }
    UA_String uaStr = UA_String_fromChars(value);
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalar(&variant, &uaStr, &UA_TYPES[UA_TYPES_STRING]);
    UA_StatusCode ret = UA_Client_writeValueAttribute(client,
                                                       UA_NODEID_NUMERIC(PLC_NODEID_CURRENTID_NS, PLC_NODEID_CURRENTID_ID),
                                                       &variant);
    UA_String_deleteMembers(&uaStr);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    if (ret != UA_STATUSCODE_GOOD)
    {
        ESP_LOGE(TAG, "OPC_WriteCurrentId: write failed 0x%08" PRIx32, (uint32_t)ret);
        return false;
    }
    ESP_LOGI(TAG, "OPC_WriteCurrentId: write OK");
    return true;
}

bool OPC_ReportProduct(const char *endpoint, const char *uidStr)
{
    if (!endpoint || !uidStr)
    {
        ESP_LOGE(TAG, "OPC_ReportProduct: invalid args");
        return false;
    }
    size_t len = strlen(uidStr);
    ESP_LOGI(TAG, "OPC_ReportProduct: uidStr=\"%.*s\" strlen(uidStr)=%u", (int)(len > 32 ? 32 : len), uidStr, (unsigned)len);

    /* Derive last8 hex safely: len>=8 -> last 8 chars; else left-pad with '0' to 8 */
    char last8_buf[9];
    const char *last8;
    if (len >= 8)
    {
        last8 = uidStr + (len - 8);
    }
    else
    {
        memset(last8_buf, '0', 8);
        last8_buf[8] = '\0';
        if (len > 0)
            memcpy(last8_buf + (8 - len), uidStr, len);
        last8 = last8_buf;
    }

    char *endptr;
    uint32_t v = (uint32_t)strtoul(last8, &endptr, 16);
    if (endptr == last8 || (endptr && *endptr != '\0'))
    {
        ESP_LOGE(TAG, "OPC_ReportProduct: parse error last8=\"%s\"", last8);
        return false;
    }
    uint32_t id = v & 0x7FFFFFFF;
    if (id == 0)
        id = 1;
    ESP_LOGI(TAG, "OPC_ReportProduct: last8=\"%s\" hex=0x%08" PRIx32 " id=%" PRIu32, last8, v, id);

    UA_Client *client = NULL;
    if (!ClientStart(&client, endpoint))
    {
        ESP_LOGE(TAG, "OPC_ReportProduct: connect failed");
        return false;
    }

    char inputMessage[12];  /* decimal string of id, no JSON */
    int n = snprintf(inputMessage, sizeof(inputMessage), "%" PRIu32, id);
    if (n < 0 || n >= (int)sizeof(inputMessage))
    {
        ESP_LOGE(TAG, "OPC_ReportProduct: input message too long");
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        return false;
    }
    ESP_LOGI(TAG, "OPC_ReportProduct: InputMessage=%s", inputMessage);
    UA_String inputMsg = UA_String_fromChars(inputMessage);
    UA_Variant inputVar;
    UA_Variant_init(&inputVar);
    UA_Variant_setScalar(&inputVar, &inputMsg, &UA_TYPES[UA_TYPES_STRING]);

    UA_Variant *output = NULL;
    size_t outputSize = 0;
    /* objectId = methodId (method node); if server returns BadMethodInvalid, use parent object node id */
    UA_NodeId methodId = UA_NODEID_NUMERIC(PLC_NODEID_REPORTPRODUCT_METHOD_NS, PLC_NODEID_REPORTPRODUCT_METHOD_ID);
    UA_StatusCode ret = UA_Client_call(client, methodId, methodId, 1, &inputVar, &outputSize, &output);
    UA_String_deleteMembers(&inputMsg);

    if (ret != UA_STATUSCODE_GOOD)
    {
        ESP_LOGE(TAG, "OPC_ReportProduct: call failed 0x%08" PRIx32, (uint32_t)ret);
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        return false;
    }
    if (outputSize > 0 && output && output[0].data && output[0].type == &UA_TYPES[UA_TYPES_STRING])
    {
        UA_String *outStr = (UA_String *)output[0].data;
        if (outStr->length > 0 && outStr->data)
        {
            char buf[384];
            size_t copyLen = outStr->length < sizeof(buf) - 1 ? outStr->length : sizeof(buf) - 1;
            memcpy(buf, outStr->data, copyLen);
            buf[copyLen] = '\0';
            ESP_LOGI(TAG, "OPC_ReportProduct: OutputMessage=%s", buf);
        }
    }
    if (output)
        UA_Array_delete(output, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    UA_Client_disconnect(client);
    UA_Client_delete(client);
    ESP_LOGI(TAG, "OPC_ReportProduct: call OK");
    return true;
}