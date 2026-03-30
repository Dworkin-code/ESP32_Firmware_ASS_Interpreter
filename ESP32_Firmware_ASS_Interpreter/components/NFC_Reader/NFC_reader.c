#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "NFC_reader.h"
#include "pn532.h"
#include "esp_system.h"
#include "esp_log.h"

#define OFFSETDATA_ULTRALIGHT 8 // Offset paměti na mifare ultralight NFC tagu
#define OFFSETDATA_CLASSIC 1    // Offset paměti na mifare classic NFC tagu
#define PAGESIZE_ULTRALIGHT 4   // Velikost stránky mifare ultralight
#define PAGESIZE_CLASSIC 16     // Velikost stránky mifare classic
#define MAXERRORREADING 5       // Maximalní počet opakování
#define TIMEOUTCHECKCARD 200   // Timeout pro dotaz na přitomnost karty
#define MAXTIMEOUT 5000         // Timeout pro zapis/čtení

//#define NFC_READER_ALL_DEBUG_EN 1 // Všechno debugovaní
//#define NFC_READER_DEBUG_EN 1     // Lehké debugování


#define Pozice 0
#define SvetloR 255
#define SvetloG 0
#define SvetloB 0
/*!
Zajištění výpisu všeho debugování
*/
#ifdef NFC_READER_ALL_DEBUG_EN
#define NFC_READER_ALL_DEBUG(tag, fmt, ...)                      \
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
#define NFC_READER_ALL_DEBUG(fmt, ...)
#endif

/*!
Zajištění výpisu lehkého debugování
*/
#ifdef NFC_READER_DEBUG_EN
#define NFC_READER_DEBUG(tag, fmt, ...)                         \
  do                                                            \
  {                                                             \
    if (tag && *tag)                                            \
    {                                                           \
      printf("\x1B[36m[%s]D:\x1B[0m " fmt, tag, ##__VA_ARGS__); \
      fflush(stdout);                                           \
    }                                                           \
    else                                                        \
    {                                                           \
      printf(fmt, ##__VA_ARGS__);                               \
    }                                                           \
  } while (0)
#else
#define NFC_READER_DEBUG(fmt, ...)
#endif

/*!
Možnost tisknout nazev
*/
#define _STRINGIFY(s) #s
#define STRINGIFY(s) _STRINGIFY(s)

static uint8_t NFC_Reader_GetWriteCheckOuterRetries(const TCardInfo *aCardInfo)
{
  if ((aCardInfo != NULL) && (aCardInfo->sUidLength == 7U))
  {
    return 1U;
  }
  return MAXERRORREADING;
}

typedef enum
{
  NFC_VERIFY_MISMATCH_NONE = 0,
  NFC_VERIFY_MISMATCH_INFO = 1,
  NFC_VERIFY_MISMATCH_STEP = 2
} nfc_verify_mismatch_kind_t;

typedef struct
{
  bool valid;
  nfc_verify_mismatch_kind_t kind;
  uint16_t rangeStart;
  uint16_t rangeEnd;
  uint16_t structureIndex;
  uint16_t byteIndex;
  uint8_t expectedByte;
  uint8_t actualByte;
  uint8_t expectedRecipeSteps;
  uint8_t actualRecipeSteps;
  uint16_t expectedCheckSum;
  uint16_t actualCheckSum;
  bool mismatchInCheckSum;
  uint16_t stepIndex;
  uint16_t stepByteIndex;
} nfc_verify_mismatch_info_t;

static nfc_verify_mismatch_info_t s_verifyMismatchInfo;

static void NFC_VerifyMismatch_Reset(void)
{
  memset(&s_verifyMismatchInfo, 0, sizeof(s_verifyMismatchInfo));
}

static void NFC_VerifyMismatch_SetInfo(uint16_t rangeStart, uint16_t rangeEnd,
                                       uint16_t structureIndex, uint16_t byteIndex,
                                       uint8_t expectedByte, uint8_t actualByte,
                                       uint8_t expectedRecipeSteps, uint8_t actualRecipeSteps,
                                       uint16_t expectedCheckSum, uint16_t actualCheckSum,
                                       bool mismatchInCheckSum)
{
  if (s_verifyMismatchInfo.valid)
    return;
  s_verifyMismatchInfo.valid = true;
  s_verifyMismatchInfo.kind = NFC_VERIFY_MISMATCH_INFO;
  s_verifyMismatchInfo.rangeStart = rangeStart;
  s_verifyMismatchInfo.rangeEnd = rangeEnd;
  s_verifyMismatchInfo.structureIndex = structureIndex;
  s_verifyMismatchInfo.byteIndex = byteIndex;
  s_verifyMismatchInfo.expectedByte = expectedByte;
  s_verifyMismatchInfo.actualByte = actualByte;
  s_verifyMismatchInfo.expectedRecipeSteps = expectedRecipeSteps;
  s_verifyMismatchInfo.actualRecipeSteps = actualRecipeSteps;
  s_verifyMismatchInfo.expectedCheckSum = expectedCheckSum;
  s_verifyMismatchInfo.actualCheckSum = actualCheckSum;
  s_verifyMismatchInfo.mismatchInCheckSum = mismatchInCheckSum;
}

static void NFC_VerifyMismatch_SetStep(uint16_t rangeStart, uint16_t rangeEnd,
                                       uint16_t structureIndex, uint16_t stepIndex,
                                       uint16_t stepByteIndex, uint8_t expectedByte, uint8_t actualByte)
{
  if (s_verifyMismatchInfo.valid)
    return;
  s_verifyMismatchInfo.valid = true;
  s_verifyMismatchInfo.kind = NFC_VERIFY_MISMATCH_STEP;
  s_verifyMismatchInfo.rangeStart = rangeStart;
  s_verifyMismatchInfo.rangeEnd = rangeEnd;
  s_verifyMismatchInfo.structureIndex = structureIndex;
  s_verifyMismatchInfo.stepIndex = stepIndex;
  s_verifyMismatchInfo.stepByteIndex = stepByteIndex;
  s_verifyMismatchInfo.expectedByte = expectedByte;
  s_verifyMismatchInfo.actualByte = actualByte;
}

static void NFC_VerifyMismatch_Print(const char *compareFn)
{
  if (!s_verifyMismatchInfo.valid)
  {
    printf("[NFC_VERIFY_MISMATCH] compareFn=%s range=%u-%u detail=not-captured\n",
           compareFn ? compareFn : "(null)",
           (unsigned)s_verifyMismatchInfo.rangeStart,
           (unsigned)s_verifyMismatchInfo.rangeEnd);
    return;
  }

  if (s_verifyMismatchInfo.kind == NFC_VERIFY_MISMATCH_INFO)
  {
    printf("[NFC_VERIFY_MISMATCH] compareFn=%s range=%u-%u struct=%u firstMismatchIndex=%u expectedByte=%u actualByte=%u\n",
           compareFn ? compareFn : "(null)",
           (unsigned)s_verifyMismatchInfo.rangeStart,
           (unsigned)s_verifyMismatchInfo.rangeEnd,
           (unsigned)s_verifyMismatchInfo.structureIndex,
           (unsigned)s_verifyMismatchInfo.byteIndex,
           (unsigned)s_verifyMismatchInfo.expectedByte,
           (unsigned)s_verifyMismatchInfo.actualByte);
    printf("[NFC_VERIFY_MISMATCH] expectedRecipeSteps=%u actualRecipeSteps=%u expectedCheckSum=%u actualCheckSum=%u mismatchInCheckSum=%u\n",
           (unsigned)s_verifyMismatchInfo.expectedRecipeSteps,
           (unsigned)s_verifyMismatchInfo.actualRecipeSteps,
           (unsigned)s_verifyMismatchInfo.expectedCheckSum,
           (unsigned)s_verifyMismatchInfo.actualCheckSum,
           (unsigned)s_verifyMismatchInfo.mismatchInCheckSum);
    return;
  }

  printf("[NFC_VERIFY_MISMATCH] compareFn=%s range=%u-%u struct=%u stepIndex=%u stepByteIndex=%u expectedByte=%u actualByte=%u\n",
         compareFn ? compareFn : "(null)",
         (unsigned)s_verifyMismatchInfo.rangeStart,
         (unsigned)s_verifyMismatchInfo.rangeEnd,
         (unsigned)s_verifyMismatchInfo.structureIndex,
         (unsigned)s_verifyMismatchInfo.stepIndex,
         (unsigned)s_verifyMismatchInfo.stepByteIndex,
         (unsigned)s_verifyMismatchInfo.expectedByte,
         (unsigned)s_verifyMismatchInfo.actualByte);
}

 /**************************************************************************/
/*!
    @brief  Inicializace PN532 desky

    @param  aNFC      Pointer na NFC strukturu
    @param  clk       CLK GPIO Výstup
    @param  miso      MISO GPIO Výstup
    @param  mosi      MOSI GPIO Výstup
    @param  ss        SS GPIO Výstup

    @returns 0 - NFC ctecka se nainicializovala, 1 - Nelze najít NFC čtečku PN53x
*/
/**************************************************************************/
uint8_t NFC_Reader_Init(pn532_t *aNFC, uint8_t aClk, uint8_t aMiso, uint8_t aMosi, uint8_t aSs)
{
  static const char *TAGin = "NFC_Reader_Init";
  NFC_READER_DEBUG(TAGin, "Inicializuji NFC ctecku.\n");
  pn532_spi_init(aNFC, aClk, aMiso, aMosi, aSs);
  pn532_begin(aNFC);
  uint32_t versiondata = pn532_getFirmwareVersion(aNFC);
  if (!versiondata)
  {
    NFC_READER_DEBUG(TAGin, "Nelze najít PN53x desku.\n");
    return 1;
  }
  NFC_READER_DEBUG(TAGin, "Našla se deska PN5 %lu.\n", (versiondata >> 24) & 0xFF);
  NFC_READER_ALL_DEBUG(TAGin, "Firmware ver. %lu.%lu. \n", (versiondata >> 16) & 0xFF, (versiondata >> 8) & 0xFF);
  pn532_SAMConfig(aNFC);
  return 0;
}

/**************************************************************************/
/*!
    @brief  Vypis vsech hodnot co mají být na NFC tagu

    @param  aCardInfo      Pointer na TCardInfo strukturu

*/
/**************************************************************************/
void NFC_Print(TCardInfo aCardInfo)
{
  static const char *TAGin = "NFC_Print";
  printf("\nInfo tagu: ");
  for (int i = 0; i < TRecipeInfo_Size; ++i)
  {
    printf("%d ", *(((uint8_t *)&aCardInfo.sRecipeInfo) + i));
  }
  if (aCardInfo.sRecipeInfo.RecipeSteps > 0 && aCardInfo.sRecipeStep != NULL)
  {
    printf("\nKroky Receptu:");
    for (int j = 0; j < aCardInfo.sRecipeInfo.RecipeSteps; j++)
    {
      printf("\n%d: ", j);
      for (int i = 0; i < TRecipeStep_Size; i++)
      {
        printf("%d ", *(((uint8_t *)aCardInfo.sRecipeStep) + j * TRecipeStep_Size + i));
      }
    }
  }
  printf("\n");
  fflush(stdout);
}

/**************************************************************************/
/*!
    @brief  Zapsaní jedné struktury paměti do NFC tagu

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo      aCardInfo struktura
    @param  NumOfStructure Číslo struktury(0- info, 1-end - recipe)

    @returns 0 - Data se na NFC tag zapsala, 1 - NumOfStructure je mimo rozsah, 2 - Data se nezapsala, 3 - Nelze autentizovat NFC tag, 4 - jiná chyba
*/
/**************************************************************************/
uint8_t NFC_WriteStruct(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t NumOfStructure)
{
  static const char *TAGin = "NFC_WriteStruct";
  NFC_READER_ALL_DEBUG(TAGin, "Zapisuji na kartu jednu struktu\n");
  
  uint8_t Error;
  for (size_t i = 0; i < MAXERRORREADING; ++i)
  {
    Error = NFC_WriteStructRange(aNFC, aCardInfo, NumOfStructure, NumOfStructure);
    if (Error == 0 || Error == 1)
      break;
  }
  switch (Error)
  {
  case 0:
    NFC_READER_ALL_DEBUG(TAGin, "Data do struktury %d se uspesne zapsala.\n", NumOfStructure);
    return 0;
    break;
  case 1:
    NFC_READER_ALL_DEBUG(TAGin, "NumOfStructure %d je mimo rozsah.\n", NumOfStructure);
    return 1;
    break;
  case 2:
    NFC_READER_ALL_DEBUG(TAGin, "Data se nezapsala.\n");
    return 2;
    break;
  case 3:
    NFC_READER_ALL_DEBUG(TAGin, "Nelze autentizovat NFC tag.\n");
    return 3;
    break;
  default:
    NFC_READER_ALL_DEBUG(TAGin, "Jina chyba.\n");
    return 4;
    break;
  }
  return 4;
}

/**************************************************************************/
/*!
    @brief  Zapsaní rozsahu struktur paměti do NFC tagu

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo      aCardInfo struktura
    @param  NumOfStructure Číslo struktury(0- info, 1-end - recipe)

    @returns 0 - Data se na NFC tag zapsala, 1 - NumOfStructure je mimo rozsah, 2 - Data se nezapsala, 3 - Nelze autentizovat NFC tag, 4 - Posledni struktura je mensi jak prvni struktura
*/
/**************************************************************************/
uint8_t NFC_WriteStructRange(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t NumOfStructureStart, uint16_t NumOfStructureEnd)
{
  static const char *TAGin = "NFC_WriteStructRange";
  uint32_t ws_start_ms = esp_log_timestamp();
  NFC_READER_DEBUG(TAGin, "Zapisuji na kartu\n");
  tNeopixel Svetlo = { Pozice, NP_RGB(SvetloR, SvetloG,  SvetloB) };
  neopixel_SetPixel(*Light,&Svetlo,1);
  if (NumOfStructureStart > NumOfStructureEnd)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Posledni struktura je mensi jak prvni struktura!\n");

    Svetlo.rgb = NP_RGB(0, 0,  0);
  neopixel_SetPixel(*Light,&Svetlo,1);
    return 4;
  }

  if (NumOfStructureStart > aCardInfo->sRecipeInfo.RecipeSteps || NumOfStructureEnd > aCardInfo->sRecipeInfo.RecipeSteps)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Index je mimo rozsah!!\n");

    Svetlo.rgb = NP_RGB(0, 0,  0);
  neopixel_SetPixel(*Light,&Svetlo,1);
    return 1;
  }
  NFC_READER_DEBUG(TAGin, "Od indexu: %d do %d.\n", NumOfStructureStart, NumOfStructureEnd);

  size_t zacatek = 0;
  size_t konec = TRecipeInfo_Size - 1;

  if (NumOfStructureStart > 0)
  {
    zacatek = TRecipeInfo_Size + (NumOfStructureStart - 1) * TRecipeStep_Size;
  }
  if (NumOfStructureEnd > 0)
  {
    konec = TRecipeInfo_Size + (NumOfStructureStart - 1) * TRecipeStep_Size + TRecipeStep_Size * (NumOfStructureEnd - NumOfStructureStart + 1) - 1;
  }

  uint16_t CheckSumOld = aCardInfo->sRecipeInfo.CheckSum;
  NFC_READER_ALL_DEBUG(TAGin,
                       "DBG checksum precompute range=%u-%u RecipeSteps=%u oldCheckSum=%u\n",
                       (unsigned)NumOfStructureStart,
                       (unsigned)NumOfStructureEnd,
                       (unsigned)aCardInfo->sRecipeInfo.RecipeSteps,
                       (unsigned)CheckSumOld);
  uint16_t CheckSumNew = NFC_GetCheckSum(*aCardInfo);
  NFC_READER_ALL_DEBUG(TAGin, "DBG checksum computed newCheckSum=%u\n", (unsigned)CheckSumNew);
  if (CheckSumNew != aCardInfo->sRecipeInfo.CheckSum)
  {
    aCardInfo->sRecipeInfo.CheckSum = CheckSumNew;
    NFC_READER_ALL_DEBUG(TAGin, "CheckSum se lisi, novy checksum: %d\n", CheckSumNew);
    if (NumOfStructureStart != 0)
    {
      NFC_READER_ALL_DEBUG(TAGin, "Pridavam do zapisu SRecipeInfo strukturu.\n");
      NFC_WriteStruct(aNFC, aCardInfo, 0);
    }
  }
  else
  {
    NFC_READER_ALL_DEBUG(TAGin, "CheckSum sedi.\n");
  }

  NFC_READER_ALL_DEBUG(TAGin, "Zacatek zapisu: %d, Konec: %d\n", zacatek, konec);

  uint8_t iuid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
  uint8_t iuidLength = 0;
  printf("[NFC_TIMING][%s] readPassiveTargetID start timeout=%u range=%u-%u t=%lldms\n",
         TAGin, (unsigned)MAXTIMEOUT, (unsigned)NumOfStructureStart, (unsigned)NumOfStructureEnd, (long long)esp_log_timestamp());
  uint32_t read_start_ms = esp_log_timestamp();
  uint8_t PrilozenaKarta = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, iuid, &iuidLength, MAXTIMEOUT);
  uint32_t read_end_ms = esp_log_timestamp();
  printf("[NFC_TIMING][%s] readPassiveTargetID end ret=%u uidLen=%u dt=%lldms t=%lldms\n",
         TAGin, (unsigned)PrilozenaKarta, (unsigned)iuidLength,
         (long long)(read_end_ms - read_start_ms), (long long)read_end_ms);
  if (PrilozenaKarta == 1)
  {
    // NFC MIFARE CLASSIC
    if (iuidLength == 4)
    {

      NFC_READER_ALL_DEBUG(TAGin, "NFC classic\n");

      uint8_t iData[PAGESIZE_CLASSIC];
      size_t PrvniBunka = zacatek / PAGESIZE_CLASSIC;
      size_t PosledniBunka = konec / PAGESIZE_CLASSIC;
      for (int i = PrvniBunka; i <= PosledniBunka; ++i)
      {
        NFC_READER_ALL_DEBUG(TAGin, "Bunka c.%d:", i);
        for (size_t k = 0; k < PAGESIZE_CLASSIC; k++)
        {

          if (i * PAGESIZE_CLASSIC + k < TRecipeInfo_Size)
          {
            iData[k] = *(((uint8_t *)&(aCardInfo->sRecipeInfo)) + i * PAGESIZE_CLASSIC + k);
            NFC_READER_ALL_DEBUG("", "%d ", *(((uint8_t *)&(aCardInfo->sRecipeInfo)) + i * PAGESIZE_CLASSIC + k));
          }
          else if (i * PAGESIZE_CLASSIC + k < TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size)
          {
            NFC_READER_ALL_DEBUG("", "%d ", *(((uint8_t *)aCardInfo->sRecipeStep) + i * PAGESIZE_CLASSIC + k - TRecipeInfo_Size));
            iData[k] = *(((uint8_t *)aCardInfo->sRecipeStep) + i * PAGESIZE_CLASSIC + k - TRecipeInfo_Size);
          }
          else
          {
            iData[k] = 0;
            NFC_READER_ALL_DEBUG("", "%d ", 0);
          }
        }
        NFC_READER_ALL_DEBUG("", "\n");

        // write
        size_t index = NFC_GetMifareClassicIndex(i);
        uint8_t keyuniversal[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t autorizovano = pn532_mifareclassic_AuthenticateBlock(aNFC, iuid, iuidLength, index, 1, keyuniversal);
        NFC_READER_ALL_DEBUG(TAGin, "autorizovano: %d\n", autorizovano);
        if (autorizovano)
        {
          NFC_READER_ALL_DEBUG("", "data: %d na index: %d\n", i, index);
          uint8_t Zapsano = pn532_mifareclassic_WriteDataBlock(aNFC, index, iData);
          NFC_READER_ALL_DEBUG("", "Navratova hodnota: %d\n", Zapsano);
          if (!Zapsano)
          {
            NFC_READER_ALL_DEBUG(TAGin, "LOWLEVEL WRITE FAIL medium=Classic block=%d chunk_i=%d\n", (int)index, i);
            Svetlo.rgb = NP_RGB(0, 0,  0);
            neopixel_SetPixel(*Light,&Svetlo,1);
            return 2;
          }
          uint8_t readBackData[PAGESIZE_CLASSIC];
          uint8_t readBackOk = pn532_mifareclassic_ReadDataBlock(aNFC, index, readBackData);
          if (!readBackOk)
          {
            NFC_READER_ALL_DEBUG(TAGin, "IMMEDIATE READBACK FAIL medium=Classic block=%d chunk_i=%d\n", (int)index, i);
          }
          else
          {
            for (size_t rb = 0; rb < PAGESIZE_CLASSIC; ++rb)
            {
              if (readBackData[rb] != iData[rb])
              {
                NFC_READER_ALL_DEBUG(TAGin,
                                     "IMMEDIATE READBACK MISMATCH medium=Classic block=%d byte_offset=%d expected=0x%02X actual=0x%02X\n",
                                     (int)index, (int)rb, (unsigned)iData[rb], (unsigned)readBackData[rb]);
              }
            }
          }
          NFC_READER_ALL_DEBUG(TAGin, "LOWLEVEL WRITE OK medium=Classic block=%d chunk_i=%d\n", (int)index, i);
        }
        else
        {
          NFC_READER_ALL_DEBUG(TAGin, "\nNelze autentifikovat.");

         Svetlo.rgb = NP_RGB(0, 0,  0);
          neopixel_SetPixel(*Light,&Svetlo,1);
          return 3;
        }
      }
    }
    else if ((iuidLength == 7))
    { // NFC MIFARE ULTRALIGHT
      uint8_t iData[PAGESIZE_ULTRALIGHT];
      size_t PrvniBunka = zacatek / PAGESIZE_ULTRALIGHT;
      size_t PosledniBunka = konec / PAGESIZE_ULTRALIGHT;
      for (int i = PrvniBunka; i <= PosledniBunka; ++i)
      {
        NFC_READER_ALL_DEBUG(TAGin, "Bunka c.%d:", i);
        for (size_t k = 0; k < PAGESIZE_ULTRALIGHT; k++)
        {

          if (i * PAGESIZE_ULTRALIGHT + k < TRecipeInfo_Size)
          {
            iData[k] = *(((uint8_t *)&(aCardInfo->sRecipeInfo)) + i * PAGESIZE_ULTRALIGHT + k);
            NFC_READER_ALL_DEBUG("", "%d ", *(((uint8_t *)&(aCardInfo->sRecipeInfo)) + i * PAGESIZE_ULTRALIGHT + k));
          }
          else if (i * PAGESIZE_ULTRALIGHT + k < TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size)
          {
            NFC_READER_ALL_DEBUG("", "%d ", *(((uint8_t *)aCardInfo->sRecipeStep) + i * PAGESIZE_ULTRALIGHT + k - TRecipeInfo_Size));
            iData[k] = *(((uint8_t *)aCardInfo->sRecipeStep) + i * PAGESIZE_ULTRALIGHT + k - TRecipeInfo_Size);
          }
          else
          {
            iData[k] = 0;
            NFC_READER_ALL_DEBUG("", "%d ", 0);
          }
        }
        NFC_READER_ALL_DEBUG("", "\n");
        uint8_t Zapsano = pn532_mifareultralight_WritePage(aNFC, i + OFFSETDATA_ULTRALIGHT, iData);
        if (!Zapsano)
        {
          NFC_READER_ALL_DEBUG(TAGin, "LOWLEVEL WRITE FAIL medium=Ultralight page=%d chunk_i=%d\n", i + OFFSETDATA_ULTRALIGHT, i);
          Svetlo.rgb = NP_RGB(0, 0,  0);
          neopixel_SetPixel(*Light,&Svetlo,1);
          return 2;
        }
        uint8_t readBackData[PAGESIZE_CLASSIC];
        uint8_t readBackOk = pn532_mifareultralight_ReadPage(aNFC, i + OFFSETDATA_ULTRALIGHT, readBackData);
        if (!readBackOk)
        {
          NFC_READER_ALL_DEBUG(TAGin, "IMMEDIATE READBACK FAIL medium=Ultralight page=%d chunk_i=%d\n", i + OFFSETDATA_ULTRALIGHT, i);
        }
        else
        {
          for (size_t rb = 0; rb < PAGESIZE_ULTRALIGHT; ++rb)
          {
            if (readBackData[rb] != iData[rb])
            {
              NFC_READER_ALL_DEBUG(TAGin,
                                   "IMMEDIATE READBACK MISMATCH medium=Ultralight page=%d byte_offset=%d expected=0x%02X actual=0x%02X\n",
                                   i + OFFSETDATA_ULTRALIGHT, (int)rb, (unsigned)iData[rb], (unsigned)readBackData[rb]);
            }
          }
        }
        NFC_READER_ALL_DEBUG(TAGin, "LOWLEVEL WRITE OK medium=Ultralight page=%d chunk_i=%d\n", i + OFFSETDATA_ULTRALIGHT, i);
      }
    }

    printf("\n");
  }
  else
  {
    NFC_READER_DEBUG(TAGin, "Na kartu nelze zapsat\n");

   Svetlo.rgb = NP_RGB(0, 0,  0);
    neopixel_SetPixel(*Light,&Svetlo,1);

    return 2;
  }
  Svetlo.rgb = NP_RGB(0, 0,  0);
  neopixel_SetPixel(*Light,&Svetlo,1);
  printf("[NFC_TIMING][%s] exit res=0 dt_total=%ums\n", TAGin, (unsigned)(esp_log_timestamp() - ws_start_ms));
  return 0;
}

/**************************************************************************/
/*!
    @brief  Zapsaní všech struktur paměti do NFC tagu

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo      aCardInfo struktura

    @returns 0 - Data se na NFC tag zapsala, 2 - Data se nezapsala, 3 - Nelze autentizovat NFC tag, 4 - jina chyba
*/
/**************************************************************************/
uint8_t NFC_WriteAllData(pn532_t *aNFC, TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_WriteAllData";
  NFC_READER_ALL_DEBUG(TAGin, "Zapisuji na kartu vsechna data\n");
  uint8_t Error;
  for (size_t i = 0; i < MAXERRORREADING; ++i)
  {
    Error = NFC_WriteStructRange(aNFC, aCardInfo, 0, aCardInfo->sRecipeInfo.RecipeSteps);
    if (Error == 0)
      break;
  }
  switch (Error)
  {
  case 0:
    NFC_READER_ALL_DEBUG(TAGin, "Vsechna data do struktury se uspesne zapsala.\n");
    return 0;
    break;
  case 2:
    NFC_READER_ALL_DEBUG(TAGin, "Data se nezapsala.\n");
    return 2;
    break;
  case 3:
    NFC_READER_ALL_DEBUG(TAGin, "Nelze autentizovat NFC tag.\n");
    return 3;
    break;
  default:
    NFC_READER_ALL_DEBUG(TAGin, "Jina chyba.\n");
    return 4;
    break;
  }
  return 4;
}

/**************************************************************************/
/*!
    @brief  Převod i pozice na pozici na Mifare Classic čipu na DATA paměťové místa

    @param  i      Původní pozice

    @returns Přepočítaná pozice
*/
/**************************************************************************/
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

/**************************************************************************/
/*!
    @brief  Nacteni vsech dat z NFC tagu do struktury aCardInfo

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo      aCardInfo struktura

    @returns 0 - Data byla nahrána, 1 - Data nelze nacist/nebyla prilozena karta, 2- Nelze autentizovat NFC Tag, 3 - Nebyla nactena struktura TRecipeInfo, 4 - Nelze Alokovat pole, 5 - Nebylo vytvoreno pole pro data ,20 - Neocekavana chyba
*/
/**************************************************************************/
uint8_t NFC_LoadAllData(pn532_t *aNFC, TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_WriteAllData";
  NFC_READER_DEBUG(TAGin, "Nacitam vsechny data z NFC Tagu\n");
  if (aCardInfo->TRecipeStepArrayCreated)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Uvolnuji pole\n");
    NFC_DeAllocTRecipeStepArray(aCardInfo);
  }
  uint8_t Error = 0;
  for (int i = 0; i < MAXERRORREADING; ++i)
  {
    NFC_InitTCardInfo(aCardInfo);
    Error = NFC_LoadTRecipeInfoStructure(aNFC, aCardInfo);
    if (Error == 0)
    {
      break;
    }
    NFC_READER_DEBUG(TAGin, "Pokousim se znovu nacist TRecipeInfo strukturu.\n");
  }
  switch (Error)
  {
  case 0:
    break;
  case 2:
    NFC_READER_DEBUG(TAGin, "Data nelze nacist/nebyla prilozena karta.\n");
    return 1;
    break;
  case 3:
    NFC_READER_DEBUG(TAGin, "Nelze autentizovat NFC Tag.\n");
    return 2;
    break;
  default:
    NFC_READER_DEBUG(TAGin, "Neocekavana chyba.\n");
    return 20;
    break;
  }

  for (int i = 0; i < MAXERRORREADING; ++i)
  {
    Error = NFC_AllocTRecipeStepArray(aCardInfo);
    if (Error == 0)
    {
      break;
    }
    NFC_DeAllocTRecipeStepArray(aCardInfo);
    NFC_READER_DEBUG(TAGin, "Pokousim se znovu nacist strukturu.\n");
  }
  switch (Error)
  {
  case 0:
    break;
  case 2:
    NFC_READER_DEBUG(TAGin, "Nebyla nactena struktura TRecipeInfo.\n");
    return 3;
    break;
  case 3:
    NFC_READER_DEBUG(TAGin, "Nelze Alokovat pole.\n");
    return 4;
    break;
  default:
    NFC_READER_DEBUG(TAGin, "Neocekavana chyba.\n");
    return 20;
    break;
  }

  for (int i = 0; i < MAXERRORREADING; ++i)
  {
    Error = NFC_LoadTRecipeSteps(aNFC, aCardInfo);
    if (Error == 0)
    {
      break;
    }
    NFC_READER_DEBUG(TAGin, "Pokousim se znovu nacist TRecipeSteps strukturu.\n");
  }
  switch (Error)
  {
  case 0:
    break;
  case 2:
    NFC_READER_DEBUG(TAGin, "Data nelze nacist/nebyla prilozena karta.\n");
    return 1;
    break;
  case 3:
    NFC_READER_DEBUG(TAGin, "Nelze autentizovat NFC Tag.\n");
    return 2;
    break;
  case 4:
    NFC_READER_DEBUG(TAGin, "Nebylo vytvoreno pole struktur.\n");
    return 5;
    break;
  default:
    NFC_READER_DEBUG(TAGin, "Neocekavana chyba.\n");
    return 20;
    break;
  }
  return 0;
}

/**************************************************************************/
/*!
    @brief  Přečtení struktury TRecipeInfo z NFC tagu

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo      aCardInfo struktura


    @returns 0 - Data se z NFC tag precetla, 2 - Data se neprecetla/nebyla prilozena karta, 3 - Nelze autentizovat NFC tag
*/
/**************************************************************************/
uint8_t NFC_LoadTRecipeInfoStructure(pn532_t *aNFC, TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_LoadTRecipeInfoStructure";
  NFC_READER_DEBUG(TAGin, "Nacitam strukturu TRecipeInfo.\n");
  memset(&aCardInfo->sRecipeInfo, 0, sizeof(aCardInfo->sRecipeInfo));
  aCardInfo->TRecipeInfoLoaded = false;
  uint8_t iuid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
  uint8_t iuidLength;
  size_t bytesLoaded = 0;
  tNeopixel Svetlo = { Pozice, NP_RGB(SvetloG, SvetloR,  SvetloB) };
  neopixel_SetPixel(*Light,&Svetlo,1);
  uint8_t PrilozenaKarta = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, iuid, &iuidLength, MAXTIMEOUT);
  if (PrilozenaKarta == 1)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Karta byla prilozena.\n");
    if (iuidLength == 4)
    {
      NFC_READER_ALL_DEBUG(TAGin, "NFC classic\n");
      size_t konec = TRecipeInfo_Size - 1;
      uint8_t iData[PAGESIZE_CLASSIC];
      size_t PosledniBunka = (TRecipeInfo_Size - 1) / PAGESIZE_CLASSIC;
      for (int i = 0; i <= PosledniBunka; ++i)
      {
        size_t index = NFC_GetMifareClassicIndex(i);

        uint8_t keyuniversal[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t autorizovano = pn532_mifareclassic_AuthenticateBlock(aNFC, iuid, iuidLength, index, 1, keyuniversal);
        NFC_READER_ALL_DEBUG("", "Autorizovano: %d\n", autorizovano);
        if (autorizovano)
        {
          uint8_t success = pn532_mifareclassic_ReadDataBlock(aNFC, index, iData);
          if (success)
          {
            // Data seems to have been read ... spit it out
            NFC_READER_ALL_DEBUG(TAGin, "Ctu Block %d: ", i);
            for (int k = 0; k < PAGESIZE_CLASSIC; ++k)
            {
              NFC_READER_ALL_DEBUG("", "%d ", iData[k]);
            }
            NFC_READER_ALL_DEBUG("", "\n");

            for (int k = 0; k < PAGESIZE_CLASSIC; ++k)
            {
              if (k + i * PAGESIZE_CLASSIC < TRecipeInfo_Size)
              {

                *(((uint8_t *)&aCardInfo->sRecipeInfo) + k + i * PAGESIZE_CLASSIC) = iData[k];
                bytesLoaded++;
                // NFC_READER_ALL_DEBUG("", "%d: %d, ", k, *(((uint8_t *)&aCardInfo->sRecipeInfo) + k + i * PAGESIZE_CLASSIC));
              }
              else
              {
                break;
              }
            }
          }
          else
          {
            NFC_READER_DEBUG(TAGin, "Nelze precist. Chyba %d, index: %d\n", success, index);
            Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
            return 2;
          }
        }
        else
        {
          NFC_READER_DEBUG(TAGin, "Nelze autentifikovat.\n");
          Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
          return 3;
        }
      }
    }
    else if ((iuidLength == 7))
    {
      NFC_READER_ALL_DEBUG(TAGin, "NFC ultralight\n");

      uint8_t iData[16];
      size_t PosledniBunka = (TRecipeInfo_Size - 1) / 16;
      for (int i = 0; i <= PosledniBunka; ++i)
      {
        uint8_t success = pn532_mifareultralight_ReadPage(aNFC, (i * 4) + OFFSETDATA_ULTRALIGHT, iData);
        if (success)
        {
          // Data seems to have been read ... spit it out
          NFC_READER_ALL_DEBUG(TAGin, "\nCtu Block %d: ", i);
          for (int k = 0; k < 16; ++k)
          {
            NFC_READER_ALL_DEBUG("", "%d ", iData[k]);
          }
          NFC_READER_ALL_DEBUG("", "\n");
          for (int k = 0; k < 16; ++k)
          {
            if (k + i * 16 < TRecipeInfo_Size)
            {
              *(((uint8_t *)&aCardInfo->sRecipeInfo) + k + i * 16) = iData[k];
              bytesLoaded++;
              // NFC_READER_ALL_DEBUG("", "%d: %d, ", k, *(((uint8_t *)&aCardInfo->sRecipeInfo) + k + i * 16));
            }
            else
            {
              break;
            }
          }
        }
        else
        {
          NFC_READER_ALL_DEBUG(TAGin, "\nNelze precist. Chyba %d\n", success);
          Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
          return 2;
        }
      }
    }
    else
    {
      NFC_READER_DEBUG(TAGin, "Unsupported UID length: %d\n", iuidLength);
      NFC_READER_DEBUG(TAGin, "Read failure while loading TRecipeInfo.\n");
      Svetlo.rgb = 0;
      neopixel_SetPixel(*Light,&Svetlo,1);
      return 2;
    }
  }
  else
  {
    NFC_READER_DEBUG(TAGin, "Karta nebyla prilozena.\n");
    NFC_READER_DEBUG(TAGin, "Read failure while loading TRecipeInfo.\n");
    Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
    return 2;
  }
  if (bytesLoaded != TRecipeInfo_Size)
  {
    NFC_READER_DEBUG(TAGin, "Read failure while loading TRecipeInfo.\n");
    Svetlo.rgb = 0;
    neopixel_SetPixel(*Light,&Svetlo,1);
    return 2;
  }
  /* AAS fix: persist UID into TCardInfo so sr_id and ReportProduct use real tag identity */
  NFC_saveUID(aCardInfo, iuid, iuidLength);
  aCardInfo->TRecipeInfoLoaded = true;
  NFC_READER_DEBUG(TAGin, "TRecipeInfo fully loaded (%d bytes).\n", TRecipeInfo_Size);
  Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
  return 0;
}

/**************************************************************************/
/*!
    @brief  Přečtení všech struktur TRecipeStep z NFC tagu

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo      aCardInfo struktura


    @returns 0 - Data se z NFC tag precetla, 2 - Data se neprecetla/nebyla prilozena karta, 3 - Nelze autentizovat NFC tag 4 - Není vytvořeno pole pro pole struktur
*/
/**************************************************************************/
uint8_t NFC_LoadTRecipeSteps(pn532_t *aNFC, TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_LoadTRecipeSteps";
  NFC_READER_DEBUG(TAGin, "Nacitam vsechny strukturu TRecipeSteps.\n");
  if (!aCardInfo->TRecipeStepArrayCreated)
  {
    NFC_READER_DEBUG(TAGin, "Neni vytvoreno pole pro hodnoty!.\n");
    return 4;
  }
  uint8_t iuid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
  uint8_t iuidLength;
  tNeopixel Svetlo = { Pozice, NP_RGB(SvetloG, SvetloR,  SvetloB) };
  neopixel_SetPixel(*Light,&Svetlo,1);
  uint8_t PrilozenaKarta = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, iuid, &iuidLength, MAXTIMEOUT);
  if (PrilozenaKarta == 1)
  {
    if (iuidLength == 4)
    {
      NFC_READER_ALL_DEBUG(TAGin, "NFC classic\n");
      size_t zacatek = TRecipeInfo_Size;
      size_t konec = TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size - 1;
      uint8_t iData[PAGESIZE_CLASSIC];
      size_t PrvniBunka = zacatek / PAGESIZE_CLASSIC;
      size_t PosledniBunka = konec / PAGESIZE_CLASSIC;
      for (int i = PrvniBunka; i <= PosledniBunka; ++i)
      {
        size_t index = NFC_GetMifareClassicIndex(i);

        uint8_t keyuniversal[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t autorizovano = pn532_mifareclassic_AuthenticateBlock(aNFC, iuid, iuidLength, index, 1, keyuniversal);
        NFC_READER_ALL_DEBUG("", "Autorizovano: %d\n", autorizovano);
        if (autorizovano)
        {
          uint8_t success = pn532_mifareclassic_ReadDataBlock(aNFC, index, iData);
          if (success)
          {
            // Data seems to have been read ... spit it out
            NFC_READER_ALL_DEBUG(TAGin, "Ctu Block %d: ", i);
            for (int k = 0; k < PAGESIZE_CLASSIC; ++k)
            {
              NFC_READER_ALL_DEBUG("", "%d ", iData[k]);
            }
            NFC_READER_ALL_DEBUG("", "\n");
            size_t Propocet = 0;
            size_t IndexovaPosun = 0;
            size_t Posun = +zacatek % PAGESIZE_CLASSIC;
            for (size_t k = 0; k < PAGESIZE_CLASSIC; ++k)
            {
              if (k + i * PAGESIZE_CLASSIC < TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size)
              {
                Propocet = k;
                IndexovaPosun = k + (i - PrvniBunka) * PAGESIZE_CLASSIC;
                if (i == PrvniBunka)
                {
                  Propocet += Posun;
                  if (Propocet == PAGESIZE_CLASSIC)
                  {
                    break;
                  }
                }
                else
                {
                  IndexovaPosun = IndexovaPosun - Posun;
                }

                *(((uint8_t *)aCardInfo->sRecipeStep) + IndexovaPosun) = iData[Propocet];
                // NFC_READER_ALL_DEBUG("", "%d: %p+%d-%d, ", k,aCardInfo->sRecipeStep,IndexovaPosun,iData[Propocet]);
              }
              else
              {
                break;
              }
            }
          }
          else
          {
            NFC_READER_DEBUG(TAGin, "Nelze precist. Chyba %d, index: %d\n", success, index);
            Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
            return 2;
          }
        }
        else
        {
          NFC_READER_DEBUG(TAGin, "Nelze autentifikovat.\n");
          Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
          return 3;
        }
      }
    }
    else if ((iuidLength == 7))
    {
      NFC_READER_ALL_DEBUG(TAGin, "NFC ultralight\n");

      uint8_t iData[16];
      size_t zacatek = TRecipeInfo_Size;
      size_t konec = TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size - 1;
      size_t PrvniBunka = zacatek / PAGESIZE_CLASSIC;
      size_t PosledniBunka = konec / PAGESIZE_CLASSIC;
      for (int i = PrvniBunka; i <= PosledniBunka; ++i)
      {
        uint8_t success = pn532_mifareultralight_ReadPage(aNFC, (i * 4) + OFFSETDATA_ULTRALIGHT, iData);
        if (success)
        {
          // Data seems to have been read ... spit it out
          NFC_READER_ALL_DEBUG(TAGin, "\nCtu Block %d: ", i);
          for (int k = 0; k < 16; ++k)
          {
            NFC_READER_ALL_DEBUG("", "%d ", iData[k]);
          }
          NFC_READER_ALL_DEBUG("", "\n");

          size_t Propocet = 0;
          size_t IndexovaPosun = 0;
          size_t Posun = +zacatek % 16;

          for (int k = 0; k < 16; ++k)
          {
            if (k + i * 16 < TRecipeInfo_Size + aCardInfo->sRecipeInfo.RecipeSteps * TRecipeStep_Size)
            {
              Propocet = k;
              IndexovaPosun = k + (i - PrvniBunka) * 16;
              if (i == PrvniBunka)
              {
                Propocet += Posun;
                if (Propocet == 16)
                {
                  break;
                }
              }
              else
              {
                IndexovaPosun = IndexovaPosun - Posun;
              }

              *(((uint8_t *)aCardInfo->sRecipeStep) + IndexovaPosun) = iData[Propocet];
              // NFC_READER_ALL_DEBUG("", "%d: %p+%d-%d, ", k, aCardInfo->sRecipeStep, IndexovaPosun, iData[Propocet]);
            }
            else
            {
              break;
            }
          }
        }
        else
        {
          NFC_READER_ALL_DEBUG(TAGin, "\nNelze precist. Chyba %d\n", success);
          return 2;
        }
      }
    }
    else
    {
      NFC_READER_DEBUG(TAGin, "Na kartu z karty precist hodnoty.\n");
    }
  }
  else
  {
    NFC_READER_DEBUG(TAGin, "Karta nebyla prilozena.\n");
    Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
    return 2;
  }
  aCardInfo->TRecipeStepLoaded = true;
  Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
  return 0;
}

/**************************************************************************/
/*!
    @brief  Přečtení jedne struktur TRecipeStep z NFC tagu

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo      aCardInfo struktura
    @param  NumOfStructure      aCardInfo struktura od 0 index

    @returns 0 - Data se z NFC tag precetla, 2 - Data se neprecetla/nebyla prilozena karta, 3 - Nelze autentizovat NFC tag 4 - Není vytvořeno pole pro pole struktur, 5 - NumOfStructure je mimo rozsah kroků
*/
/**************************************************************************/
uint8_t NFC_LoadTRecipeStep(pn532_t *aNFC, TCardInfo *aCardInfo, size_t NumOfStructure)
{
  static const char *TAGin = "NFC_LoadTRecipeStep";
  NFC_READER_DEBUG(TAGin, "Nacitam jednu strukturu TRecipeSteps.\n");
  tNeopixel Svetlo = { Pozice, NP_RGB(SvetloG, SvetloR,  SvetloB) };
  neopixel_SetPixel(*Light,&Svetlo,1);
  if (!aCardInfo->TRecipeStepArrayCreated)
  {
    NFC_READER_DEBUG(TAGin, "Neni vytvoreno pole pro hodnoty!.\n");
    Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
    return 4;
  }
  if (NumOfStructure >= aCardInfo->sRecipeInfo.RecipeSteps)
  {
    NFC_READER_DEBUG(TAGin, "NumOfStructure je mimo rozsah kroků!.\n");
    Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
    return 5;
  }
  uint8_t iuid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
  uint8_t iuidLength;
  size_t DataCounter = 0;
  uint8_t PrilozenaKarta = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, iuid, &iuidLength, MAXTIMEOUT);
  if (PrilozenaKarta == 1)
  {
    if (iuidLength == 4)
    {
      NFC_READER_ALL_DEBUG(TAGin, "NFC classic\n");
      size_t zacatek = TRecipeInfo_Size + NumOfStructure * TRecipeStep_Size;
      size_t konec = zacatek + TRecipeStep_Size - 1;
      uint8_t iData[PAGESIZE_CLASSIC];
      size_t PrvniBunka = zacatek / PAGESIZE_CLASSIC;
      size_t PosledniBunka = konec / PAGESIZE_CLASSIC;
      NFC_READER_ALL_DEBUG(TAGin, "PrvniBunka: %d(%d), PosledniBunka: %d(%d)\n", PrvniBunka, zacatek, PosledniBunka, konec);
      for (int i = PrvniBunka; i <= PosledniBunka; ++i)
      {
        size_t index = NFC_GetMifareClassicIndex(i);

        uint8_t keyuniversal[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t autorizovano = pn532_mifareclassic_AuthenticateBlock(aNFC, iuid, iuidLength, index, 1, keyuniversal);
        NFC_READER_ALL_DEBUG("", "Autorizovano: %d\n", autorizovano);
        if (autorizovano)
        {
          uint8_t success = pn532_mifareclassic_ReadDataBlock(aNFC, index, iData);
          if (success)
          {
            // Data seems to have been read ... spit it out
            NFC_READER_ALL_DEBUG(TAGin, "Ctu Block %d: ", i);
            for (int k = 0; k < PAGESIZE_CLASSIC; ++k)
            {
              NFC_READER_ALL_DEBUG("", "%d ", iData[k]);
            }
            NFC_READER_ALL_DEBUG("", "\n");
            size_t Propocet = 0;
            size_t IndexovaPosun = 0;
            size_t Posun = +zacatek % PAGESIZE_CLASSIC;
            for (size_t k = 0; k < PAGESIZE_CLASSIC; ++k)
            {
              if (k + i * PAGESIZE_CLASSIC < konec + 1)
              {
                Propocet = k;
                IndexovaPosun = k + (i - PrvniBunka) * PAGESIZE_CLASSIC + (NumOfStructure)*TRecipeStep_Size;
                if (i == PrvniBunka)
                {
                  Propocet += Posun;
                  if (Propocet == PAGESIZE_CLASSIC)
                  {
                    break;
                  }
                }
                else
                {
                  IndexovaPosun = IndexovaPosun - Posun;
                }

                *(((uint8_t *)aCardInfo->sRecipeStep) + IndexovaPosun) = iData[Propocet];

                // NFC_READER_ALL_DEBUG("", "%d: %d-%d, ", k + i * PAGESIZE_CLASSIC, IndexovaPosun, iData[Propocet]);
                if (++DataCounter == TRecipeStep_Size)
                {
                  break;
                }
              }
              else
              {
                break;
              }
            }
          }
          else
          {
            NFC_READER_DEBUG(TAGin, "Nelze precist. Chyba %d, index: %d\n", success, index);
            Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
            return 2;
          }
        }
        else
        {
          NFC_READER_DEBUG(TAGin, "Nelze autentifikovat.\n");
          Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
          return 3;
        }
      }
    }
    else if ((iuidLength == 7))
    {
      NFC_READER_ALL_DEBUG(TAGin, "NFC ultralight\n");

      uint8_t iData[16];
      size_t zacatek = TRecipeInfo_Size + NumOfStructure * TRecipeStep_Size;
      size_t konec = zacatek + TRecipeStep_Size - 1;
      size_t PrvniBunka = zacatek / PAGESIZE_CLASSIC;
      size_t PosledniBunka = konec / PAGESIZE_CLASSIC;
      for (int i = PrvniBunka; i <= PosledniBunka; ++i)
      {
        uint8_t success = pn532_mifareultralight_ReadPage(aNFC, (i * 4) + OFFSETDATA_ULTRALIGHT, iData);
        if (success)
        {
          // Data seems to have been read ... spit it out
          NFC_READER_ALL_DEBUG(TAGin, "\nCtu Block %d: ", i);
          for (int k = 0; k < 16; ++k)
          {
            NFC_READER_ALL_DEBUG("", "%d ", iData[k]);
          }
          NFC_READER_ALL_DEBUG("", "\n");

          size_t Propocet = 0;
          size_t IndexovaPosun = 0;
          size_t Posun = +zacatek % 16;

          for (int k = 0; k < 16; ++k)
          {
            if (k + i * 16 < konec + 1)
            {
              Propocet = k;
              IndexovaPosun = k + (i - PrvniBunka) * 16 + NumOfStructure * TRecipeStep_Size;
              if (i == PrvniBunka)
              {
                Propocet += Posun;
                if (Propocet == 16)
                {
                  break;
                }
              }
              else
              {
                IndexovaPosun = IndexovaPosun - Posun;
              }

              *(((uint8_t *)aCardInfo->sRecipeStep) + IndexovaPosun) = iData[Propocet];
              // NFC_READER_ALL_DEBUG("", "%d: %p+%d-%d, ", k, aCardInfo->sRecipeStep, IndexovaPosun, iData[Propocet]);
              if (++DataCounter == TRecipeStep_Size)
              {
                break;
              }
            }
            else
            {
              break;
            }
          }
        }
        else
        {
          NFC_READER_ALL_DEBUG(TAGin, "\nNelze precist. Chyba %d\n", success);
          Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
          return 2;
        }
      }
    }
    else
    {
      NFC_READER_DEBUG(TAGin, "Na kartu z karty precist hodnoty.\n");
    }
  }
  else
  {
    NFC_READER_DEBUG(TAGin, "Karta nebyla prilozena.\n");
    Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
    return 2;
  }
  aCardInfo->TRecipeStepLoaded = true;
  Svetlo.rgb = 0;
            neopixel_SetPixel(*Light,&Svetlo,1);
  return 0;
}

/**************************************************************************/
/*!
    @brief  Alokace pole pro strukturu TRecipeStep


    @param  aCardInfo      aCardInfo struktura


    @returns 0 - Pole se vytvořilo, 1 - Pole je již vytvořeno, 2 - Nebyla načtena struktura TRecipeInfo, 3 - Nelze alokovat pole
*/
/**************************************************************************/
uint8_t NFC_AllocTRecipeStepArray(TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_AllocTRecipeStepArray";
  NFC_READER_ALL_DEBUG(TAGin, "Alokuji TRecipeStep\n");
  if (!aCardInfo->TRecipeInfoLoaded)
  {
    NFC_READER_DEBUG(TAGin, "Nebyla nactena TRecipeInfo struktura.\n");
    return 2;
  }
  if (aCardInfo->TRecipeStepArrayCreated)
  {
    NFC_READER_DEBUG(TAGin, "Pole pro TRecipeStepArray je jiz vytvoreno.\n");
    return 1;
  }
  aCardInfo->sRecipeStep = (TRecipeStep *)malloc(TRecipeStep_Size * aCardInfo->sRecipeInfo.RecipeSteps);
  if (!aCardInfo->sRecipeStep)
  {
    NFC_READER_DEBUG(TAGin, "Nelze vytvorit pole dat.\n");
    return 3;
  }
  NFC_READER_ALL_DEBUG(TAGin, "Pole bylo vytvoreno.\n");
  aCardInfo->TRecipeStepArrayCreated = true;
  return 0;
}

/**************************************************************************/
/*!
    @brief  Dealokování pole pro strukturu TRecipeStep


    @param  aCardInfo      aCardInfo struktura


    @returns 0 - Pole se dealokovalo, 1 - Pole je již null
*/
/**************************************************************************/
uint8_t NFC_DeAllocTRecipeStepArray(TCardInfo *aCardInfo)
{
  static const char *TAGin = "NFC_DeAllocTRecipeStepArray";
  NFC_READER_ALL_DEBUG(TAGin, "Odalokovavam TRecipeStep\n");
  if ((aCardInfo->sRecipeStep) == NULL)
  {
    NFC_READER_ALL_DEBUG(TAGin, "TRecipeStep je již null\n");
    return 1;
  }
  free(aCardInfo->sRecipeStep);
  aCardInfo->sRecipeStep = NULL;
  aCardInfo->TRecipeStepArrayCreated = aCardInfo->TRecipeStepLoaded = false;
  NFC_READER_ALL_DEBUG(TAGin, "Pole se odalokovalo\n");
  return 0;
}

/**************************************************************************/
/*!
    @brief  Nainicializovani hodnot aCardInfo


    @param  aCardInfo      aCardInfo struktura

*/
/**************************************************************************/
void NFC_InitTCardInfo(TCardInfo *aCardInfo)
{
  aCardInfo->sRecipeStep = NULL;
  aCardInfo->TRecipeInfoLoaded = aCardInfo->TRecipeStepArrayCreated = aCardInfo->TRecipeStepLoaded = false;
  aCardInfo->sUidLength = 7;
  for (size_t i = 0; i < aCardInfo->sUidLength; ++i)
  {
    aCardInfo->sUid[i] = 0;
  }
}

/**************************************************************************/
/*!
    @brief  Ověří jestli je karta přítomna na čtečce

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo Pointer na TCardInfo strukturu

    @returns    true - Pokud je přítomna, false - Pokud neni přitomna
*/
/**************************************************************************/
bool NFC_isCardReady(pn532_t *aNFC)
{
  static const char *TAGin = "NFC_isCardReadyToRead";
  uint8_t iuid[] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t iuidLength;
  NFC_READER_ALL_DEBUG(TAGin, "Zkousím jestli je karta přítomna.\n");
  bool iStatus = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, iuid, &iuidLength, TIMEOUTCHECKCARD);
  if (iStatus)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Je pritomna.\n");
  }
  else
  {
    NFC_READER_ALL_DEBUG(TAGin, "Neni pritomna.\n");
  }
  return iStatus;
}

/**************************************************************************/
/*!
    @brief  Získá UID a délku UID karty

    @param  aNFC      Pointer na NFC strukturu
    @param  aUid      Pointer na Uid pole
     @param  aUidLength      Pointer na Uid délku

    @returns    true - Pokud se správně načetlo, false - Pokud se špatně načetlo
*/
/**************************************************************************/
bool NFC_getUID(pn532_t *aNFC, uint8_t *aUid, uint8_t *aUidLength)
{
  static const char *TAGin = "NFC_getUID";
  NFC_READER_ALL_DEBUG(TAGin, "Ziskavam UID.\n");
  bool iSuccess = pn532_readPassiveTargetID(aNFC, PN532_MIFARE_ISO14443A, aUid, aUidLength, MAXTIMEOUT);
  if (!iSuccess)
    return false;
  NFC_READER_ALL_DEBUG(TAGin, "UID se nacetlo: ");
  for (size_t i = 0; i < *aUidLength; i++)
  {
    NFC_READER_ALL_DEBUG("", "%x ", aUid[i]);
  }
  NFC_READER_ALL_DEBUG("", ", s delkou: %zu. \n", *aUidLength);
  return true;
}
/**************************************************************************/
/*!
    @brief  Uloží UID a délku UID do TCardInfo struktury

    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  aUid      Pointer na Uid pole
     @param  aUidLength      Délka UID

    @returns    true - Pokud se správně uložilo, false - Pokud se stala chyba při ukládání
*/
/**************************************************************************/
bool NFC_saveUID(TCardInfo *aCardInfo, uint8_t *aUid, uint8_t aUidLength)
{
  static const char *TAGin = "NFC_saveUID";
  NFC_READER_ALL_DEBUG(TAGin, "Ukladam UID:");
  for (size_t i = 0; i < aUidLength; i++)
  {
    aCardInfo->sUid[i] = aUid[i];
    NFC_READER_ALL_DEBUG("", "%x ", aCardInfo->sUid[i]);
  }
  NFC_READER_ALL_DEBUG("", ", s delkou: %zu. \n", aUidLength);
  aCardInfo->sUidLength = aUidLength;
  return true;
}

/**************************************************************************/
/*!
    @brief  Zkontroluje jestli struktura TRecipeStep je stejná v zařízení jak v NFC čipu

    @param  aNFC      Pointer na NFC
    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  anumOfNFCStruct Číslo struktury(0- info, 1-end - recipe)

    @returns 0 - Pokud se Data načetla a jsou stejna, 1 - Pokud se struktura liší, 2 - Pokud je anumOfNFCStruct mimo rozsah,3 - Nelze cist z karty, 4- Nelze naalokovat pole pro hodnoty, 5 - NumOfStructureStart je vetsi jak NumOfStructureEnd, 6 - Nenactene informace o aCardInfo
*/
/**************************************************************************/
uint8_t NFC_CheckStructArrayIsSame(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t NumOfStructureStart, uint16_t NumOfStructureEnd)
{
  static const char *TAGin = "NFC_CheckStructArrayIsSame";
  NFC_READER_DEBUG(TAGin, "Porovnavam data v rozsahu %d - %d.\n", NumOfStructureStart, NumOfStructureEnd);
  if (NumOfStructureStart > NumOfStructureEnd)
  {
    NFC_READER_DEBUG(TAGin, "Startovni index je vetsi jak konecny!");
    return 5;
  }
  if (NumOfStructureStart > aCardInfo->sRecipeInfo.RecipeSteps || NumOfStructureEnd > aCardInfo->sRecipeInfo.RecipeSteps)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Index je mimo rozsah!!\n");
    return 2;
  }
  TCardInfo idataNFC1;
  uint8_t Error;
  NFC_InitTCardInfo(&idataNFC1);

  if (aCardInfo->TRecipeInfoLoaded != true)
  {
    NFC_READER_DEBUG(TAGin, "Nenactene info o sklenici!!\n");
    return 6;
  }
  idataNFC1.TRecipeInfoLoaded = true;
  idataNFC1.sRecipeInfo.RecipeSteps = NumOfStructureEnd;
  if (NumOfStructureEnd > 0)
  {
    if (NFC_AllocTRecipeStepArray(&idataNFC1) != 0)
      return 4;
  }
  for (size_t i = NumOfStructureStart; i <= NumOfStructureEnd; ++i)
  {
    if (i == 0)
    {

      for (size_t j = 0; j < MAXERRORREADING; ++j)
      {
        Error = NFC_LoadTRecipeInfoStructure(aNFC, &idataNFC1);
        if (Error == 0)
          break;
      }
      switch (Error)
      {
      case 0:
        break;

      default:
        if (idataNFC1.TRecipeStepArrayCreated == true)
        {
          NFC_DeAllocTRecipeStepArray(&idataNFC1);
        }
        return 3;
        break;
      }
      for (int j = 0; j < TRecipeInfo_Size; ++j)
      {
        if (*(((uint8_t *)&aCardInfo->sRecipeInfo) + j) != *(((uint8_t *)&idataNFC1.sRecipeInfo) + j))
        {
          uint8_t expectedByte = *(((uint8_t *)&aCardInfo->sRecipeInfo) + j);
          uint8_t actualByte = *(((uint8_t *)&idataNFC1.sRecipeInfo) + j);
          size_t checkSumOffset = (size_t)((uint8_t *)&aCardInfo->sRecipeInfo.CheckSum - (uint8_t *)&aCardInfo->sRecipeInfo);
          bool mismatchInCheckSum = (j >= (int)checkSumOffset) &&
                                    (j < (int)(checkSumOffset + sizeof(aCardInfo->sRecipeInfo.CheckSum)));
          NFC_VerifyMismatch_SetInfo(NumOfStructureStart, NumOfStructureEnd, i, j,
                                     expectedByte, actualByte,
                                     aCardInfo->sRecipeInfo.RecipeSteps,
                                     idataNFC1.sRecipeInfo.RecipeSteps,
                                     aCardInfo->sRecipeInfo.CheckSum,
                                     idataNFC1.sRecipeInfo.CheckSum,
                                     mismatchInCheckSum);
          NFC_READER_ALL_DEBUG(TAGin, "Struktura %d na pozici %d jsou rozdilne.\n", i, j);
          NFC_READER_ALL_DEBUG(TAGin,
                               "DBG info mismatch j=%d expectedByte=%u actualByte=%u expectedRecipeSteps=%u actualRecipeSteps=%u expectedCheckSum=%u actualCheckSum=%u mismatchInCheckSum=%u\n",
                               j,
                               (unsigned)expectedByte,
                               (unsigned)actualByte,
                               (unsigned)aCardInfo->sRecipeInfo.RecipeSteps,
                               (unsigned)idataNFC1.sRecipeInfo.RecipeSteps,
                               (unsigned)aCardInfo->sRecipeInfo.CheckSum,
                               (unsigned)idataNFC1.sRecipeInfo.CheckSum,
                               (unsigned)mismatchInCheckSum);
          if (idataNFC1.TRecipeStepArrayCreated == true)
            NFC_DeAllocTRecipeStepArray(&idataNFC1);
          return 1;
        }
      }
      NFC_READER_ALL_DEBUG(TAGin, "Struktura %d je stejna.\n", i);
    }
    else
    {
      for (size_t j = 0; j < MAXERRORREADING; ++j)
      {
        Error = NFC_LoadTRecipeStep(aNFC, &idataNFC1, i - 1);
        if (Error == 0)
          break;
      }
      switch (Error)
      {
      case 0:
        break;

      default:
        NFC_DeAllocTRecipeStepArray(&idataNFC1);
        return 3;
        break;
      }
      for (int j = 0; j < TRecipeStep_Size; ++j)
      {
        if (*(((uint8_t *)aCardInfo->sRecipeStep) + j + (i - 1) * TRecipeStep_Size) != *(((uint8_t *)idataNFC1.sRecipeStep) + j + (i - 1) * TRecipeStep_Size))
        {
          uint8_t expectedByte = *(((uint8_t *)aCardInfo->sRecipeStep) + j + (i - 1) * TRecipeStep_Size);
          uint8_t actualByte = *(((uint8_t *)idataNFC1.sRecipeStep) + j + (i - 1) * TRecipeStep_Size);
          NFC_VerifyMismatch_SetStep(NumOfStructureStart, NumOfStructureEnd, i, (uint16_t)(i - 1), (uint16_t)j, expectedByte, actualByte);
          NFC_READER_ALL_DEBUG(TAGin, "Struktura %d na pozici %d jsou rozdílne.\n", i, j);
          NFC_DeAllocTRecipeStepArray(&idataNFC1);
          return 1;
        }
      }
      NFC_READER_ALL_DEBUG(TAGin, "Struktura %d jsou stejne.\n", i);
    }
  }
  NFC_READER_ALL_DEBUG(TAGin, "Cely rozsah je stejny.\n");
  if (idataNFC1.TRecipeStepArrayCreated == true)
  {
    NFC_DeAllocTRecipeStepArray(&idataNFC1);
  }
  return 0;
}

/**************************************************************************/
/*!
    @brief  Zapíše strukturu a zkontroluje

    @param  aNFC      Pointer na NFC strukturu
    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  NumOfStructureStart Číslo 1. struktury(0- info, 1-end - recipe)
    @param  NumOfStructureEnd Číslo 1. struktury(0- info, 1-end - recipe)

    @returns    0 - Hodnoty na kartě sedí se zapsanými, 1- Data se liší, 2 - Index anumOfNFCStruct je mimo rozsah struktury, 3 - Na kartu nelze zapsat, 4 - Kartu nelze autentifikovat 5 - neocekavana chyba, 6 - Nelze naalokovat pole pro porovnavaci hodnoty, 7 - Spatne zadane prvni a posledni prvky, 8 - Nenactene informace o NFC tagu
*/
/**************************************************************************/
uint8_t NFC_WriteCheck(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t NumOfStructureStart, uint16_t NumOfStructureEnd)
{
  static const char *TAGin = "NFC_WriteCheck";
  uint32_t wc_start_ms = esp_log_timestamp();
  uint8_t outerRetryCount = NFC_Reader_GetWriteCheckOuterRetries(aCardInfo);
  NFC_READER_DEBUG(TAGin, "Zapisuji hodnoty a kontroluji jestli jsou stejne od %d do %d.\n", NumOfStructureStart, NumOfStructureEnd);
  printf("[NFC_TIMING][%s] enter range=%u-%u t=%lldms\n",
         TAGin, (unsigned)NumOfStructureStart, (unsigned)NumOfStructureEnd, (long long)wc_start_ms);
  NFC_VerifyMismatch_Reset();
  uint8_t Error = 0;
  for (int k = 0; k < outerRetryCount; ++k)
  {
    for (int i = 0; i < MAXERRORREADING; ++i)
    {
      uint32_t wr_start_ms = esp_log_timestamp();
      printf("[NFC_TIMING][%s] write outer=%d inner=%d start t=%lldms\n", TAGin, k, i, (long long)wr_start_ms);
      Error = NFC_WriteStructRange(aNFC, aCardInfo, NumOfStructureStart, NumOfStructureEnd);
      uint32_t wr_end_ms = esp_log_timestamp();
      printf("[NFC_TIMING][%s] write outer=%d inner=%d end res=%u dt=%lldms t=%lldms\n",
             TAGin, k, i, (unsigned)Error, (long long)(wr_end_ms - wr_start_ms), (long long)wr_end_ms);
      if (Error == 0)
      {
        break;
      }
    }
    switch (Error)
    {
    case 0:
      break;
    case 1:
      NFC_READER_DEBUG(TAGin, "Index anumOfNFCStruct je mimo rozsah struktury.\n");
      return 2;
      break;
    case 2:
      NFC_READER_DEBUG(TAGin, "Nelze zapsat do NFC tagu.\n");
      return 3;
      break;
    case 3:
      NFC_READER_DEBUG(TAGin, "Kartu nelze autentifikovat.\n");
      return 4;
      break;
    case 4:
      NFC_READER_DEBUG(TAGin, "Spatne zadane prvni a posledni prvky.\n");
      return 7;
    default:
      NFC_READER_DEBUG(TAGin, "Jina chyba.\n");
      return 5;
      break;
    }
    for (int i = 0; i < MAXERRORREADING; ++i)
    {
      uint32_t vc_start_ms = esp_log_timestamp();
      printf("[NFC_TIMING][%s] verify outer=%d inner=%d start t=%lldms\n", TAGin, k, i, (long long)vc_start_ms);
      Error = NFC_CheckStructArrayIsSame(aNFC, aCardInfo, NumOfStructureStart, NumOfStructureEnd);
      uint32_t vc_end_ms = esp_log_timestamp();
      printf("[NFC_TIMING][%s] verify outer=%d inner=%d end res=%u dt=%lldms t=%lldms\n",
             TAGin, k, i, (unsigned)Error, (long long)(vc_end_ms - vc_start_ms), (long long)vc_end_ms);
      if (Error <= 1)
      {
        break;
      }
    }
    switch (Error)
    {
    case 0:
      NFC_READER_DEBUG(TAGin, "Data se zapsala správne.\n");
      printf("[NFC_TIMING][%s] exit res=0 dt_total=%ums\n", TAGin, (unsigned)(esp_log_timestamp() - wc_start_ms));
      return 0;
    case 1:
      NFC_READER_DEBUG(TAGin, "Data se nezapsala spravne, zkusim znovu.\n");
      break;
    case 2:
      NFC_READER_DEBUG(TAGin, "Index anumOfNFCStruct je mimo rozsah struktury.\n");
      return 2;
      break;
    case 3:
      NFC_READER_DEBUG(TAGin, "Nelze z karty cist.\n");
      return 4;
      break;
    case 4:
      NFC_READER_DEBUG(TAGin, "Nelze znaalokovat pole pro hodnoty porovnani.\n");
      return 6;
      break;
    case 5:
      NFC_READER_DEBUG(TAGin, "Spatne zadane prvni a posledni prvky.\n");
      return 7;
      break;
    case 6:
      NFC_READER_DEBUG(TAGin, "Nenactene informace o NFC tagu.\n");
      return 8;
    default:
      NFC_READER_DEBUG(TAGin, "Jina chyba.\n");
      return 5;
      break;
    }
  }
  NFC_READER_DEBUG(TAGin, "Data se nezapsala spravne ani po %u pokusech.\n", (unsigned)outerRetryCount);
  NFC_VerifyMismatch_Print("NFC_CheckStructArrayIsSame");
  fflush(stdout);
  printf("[NFC_TIMING][%s] exit res=1 dt_total=%ums\n", TAGin, (unsigned)(esp_log_timestamp() - wc_start_ms));
  return 1;
}

/**************************************************************************/
/*!
    @brief  Vypočítá CheckSum

    @param  aCardInfo Pointer na TCardInfo strukturu

    @returns    hodnota CheckSum(hodnota bytu*(pozice bytu%4+1))
*/
/**************************************************************************/
uint16_t NFC_GetCheckSum(TCardInfo aCardInfo)
{
  static const char *TAGin = "NFC_GetCheckSum";
  NFC_READER_ALL_DEBUG(TAGin, "Pocitam checksum.\n");
  if (aCardInfo.sRecipeInfo.RecipeSteps == 0)
  {
    aCardInfo.sRecipeInfo.CheckSum = 0;
    NFC_READER_ALL_DEBUG(TAGin, "Počet receptů je 0 -> Checksum = 0.\n");
    return 0;
  }
  uint16_t CheckSum = 0;
  NFC_READER_ALL_DEBUG(TAGin, "Prubeh CheckSumu: ");
  for (size_t i = 0; i < TRecipeStep_Size * aCardInfo.sRecipeInfo.RecipeSteps; ++i)
  {
    CheckSum += *((uint8_t *)aCardInfo.sRecipeStep + i) * (i % 4 + 1);
    NFC_READER_ALL_DEBUG("", " %d,", CheckSum);
  }
  NFC_READER_ALL_DEBUG("", "\n");
  NFC_READER_ALL_DEBUG(TAGin, "Checksum je %d.\n", CheckSum);
  return CheckSum;
}

/**************************************************************************/
/*!
    @brief  Vytvoření TCardInfo struktury z TRecipeInfo struktury

    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  aRecipeInfo Struktura aRecipeInfo

    @returns    0 - Uspesne vytvoreni, 1 - Nelze alokovat pole
*/
/**************************************************************************/
uint8_t NFC_CreateCardInfoFromRecipeInfo(TCardInfo *aCardInfo, TRecipeInfo aRecipeInfo)
{
  static const char *TAGin = "NFC_CreateCardInfoFromRecipeInfo";
  NFC_READER_DEBUG(TAGin, "Vytvarim CardInfo z RecipeStepu.\n");
  NFC_InitTCardInfo(aCardInfo);
  for (size_t i = 0; i < TRecipeInfo_Size; ++i)
  {
    *(((uint8_t *)(&aCardInfo->sRecipeInfo)) + i) = *(((uint8_t *)(&aRecipeInfo)) + i);
  }
  NFC_READER_ALL_DEBUG(TAGin, "Data se prekopirovala.\n");
  uint8_t Error = 0;
  aCardInfo->TRecipeInfoLoaded = true;
  if (aRecipeInfo.RecipeSteps > 0)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Alokuji pole.\n");
    for (size_t i = 0; i < MAXERRORREADING; ++i)
    {
      Error = NFC_AllocTRecipeStepArray(aCardInfo);
      if (Error == 0)
        break;
    }
    switch (Error)
    {
    case 0:
      for (size_t i = 0; i < TRecipeStep_Size * aCardInfo->sRecipeInfo.RecipeSteps; ++i)
      {
        *((uint8_t *)aCardInfo->sRecipeStep + i) = 0;
      }
      aCardInfo->sRecipeInfo.CheckSum = 0;
      aCardInfo->TRecipeStepArrayCreated = true;
      break;

    default:
      NFC_READER_DEBUG(TAGin, "Chyba vytvareni pole.\n");
      return 1;
      break;
    }
  }
  NFC_READER_DEBUG(TAGin, "Vytvareno uspesne.\n");
  return 0;
}

/**************************************************************************/
/*!
    @brief  VPridani struktur TRecipeStep do TCardInfo struktury

    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  aRecipeStep Pointer na pole Struktur aRecipeStep
    @param  SizeOfRecipeSteps Počet struktur
    @param  DeAlloc Ma se odalokovat pole?

    @returns    0 - Uspesne pridani, 1 - Neni nactena TRecipeInfo struktura, 2 - SizeOfRecipeSteps je nulove, 3 - Chyba vytvareni pole, 4 - aRecipeStep je NULL
*/
/**************************************************************************/
uint8_t NFC_AddRecipeStepsToCardInfo(TCardInfo *aCardInfo, TRecipeStep *aRecipeStep, size_t SizeOfRecipeSteps, bool DeAlloc)
{
  static const char *TAGin = "NFC_AddRecipeStepsToCardInfo";
  NFC_READER_DEBUG(TAGin, "Pridavam TRecipeStep do TCardInfo struktury.\n");
  if (aCardInfo->TRecipeInfoLoaded != true)
  {
    NFC_READER_DEBUG(TAGin, "Neni nactena TRecipeInfo struktura.\n");
    return 1;
  }
  if (SizeOfRecipeSteps == 0)
  {
    NFC_READER_DEBUG(TAGin, "SizeOfRecipeSteps je nulove.\n");
    return 2;
  }
  uint8_t Error;

  if (aCardInfo->sRecipeInfo.RecipeSteps != SizeOfRecipeSteps && aCardInfo->TRecipeStepArrayCreated)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Odalokovavam pole(Rozdilna velikost).\n");
    NFC_DeAllocTRecipeStepArray(aCardInfo);
    aCardInfo->TRecipeStepArrayCreated = false;
  }
  if (aCardInfo->TRecipeStepArrayCreated == false)
  {
    aCardInfo->sRecipeInfo.RecipeSteps = SizeOfRecipeSteps;
    NFC_READER_ALL_DEBUG(TAGin, "Alokuji pole.\n");
    for (size_t i = 0; i < MAXERRORREADING; ++i)
    {
      Error = NFC_AllocTRecipeStepArray(aCardInfo);
      if (Error == 0)
        break;
    }
    switch (Error)
    {
    case 0:
      aCardInfo->TRecipeStepArrayCreated = true;
      break;

    default:
      NFC_READER_DEBUG(TAGin, "Chyba vytvareni pole.\n");
      return 3;
      break;
    }
    aCardInfo->sRecipeInfo.RecipeSteps = SizeOfRecipeSteps;
  }
  for (size_t i = 0; i < TRecipeStep_Size * aCardInfo->sRecipeInfo.RecipeSteps; ++i)
  {
    *((uint8_t *)aCardInfo->sRecipeStep + i) = *((uint8_t *)aRecipeStep + i);
  }
  if (DeAlloc)
  {
    free(aRecipeStep);
    aRecipeStep = NULL;
  }
  aCardInfo->sRecipeInfo.CheckSum = NFC_GetCheckSum(*aCardInfo);
  return 0;
}

/**************************************************************************/
/*!
    @brief  Funkce ke zmene velikosti pole s RecipeSteps hodnotami

    @param  aCardInfo Pointer na TCardInfo strukturu
    @param  NewSize Pointer na pole Struktur aRecipeStep


    @returns    0 - uspesna zmena, 1 - Nejsou nactena RecipeInfo, 2 - Nelze Alokovat pole 20 - Neocekavana chyba
*/
/**************************************************************************/
uint8_t NFC_ChangeRecipeStepsSize(TCardInfo *aCardInfo, uint8_t NewSize)
{
  static const char *TAGin = "NFC_ChangeRecipeStepsSize";
  NFC_READER_DEBUG(TAGin, "Menim hodnoty velikosti pole\n");
  if (aCardInfo->TRecipeInfoLoaded == 0)
  {
    NFC_READER_DEBUG(TAGin, "Nejsou nactena RecipeInfo\n");
    return 1;
  }

  if (NewSize == aCardInfo->sRecipeInfo.RecipeSteps)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Pole jsou stejne velke.\n");
    return 0;
  }
  uint8_t Error;
  if(NewSize == 0)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Odalokovavam pole.\n");
    NFC_READER_DEBUG(TAGin, "Pole zmenilo svou velikost na %d prvku.\n", NewSize);
    NFC_DeAllocTRecipeStepArray(aCardInfo);
    aCardInfo->sRecipeInfo.RecipeSteps = NewSize;
    return 0;
  }
  if (aCardInfo->TRecipeStepArrayCreated)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Vytvarim pole o velikosti %d bytu.\n", TRecipeStep_Size * NewSize);

    TRecipeStep *NoveRecepty = (TRecipeStep *)malloc(TRecipeStep_Size * NewSize);
    if (!NoveRecepty)
    {
      NFC_READER_DEBUG(TAGin, "Nelze vytvorit pole dat.\n");
      return 2;
    }
    NFC_READER_ALL_DEBUG(TAGin, "Pole bylo vytvoreno.\n");
    fflush(stdout);
    NFC_READER_ALL_DEBUG(TAGin, "Kopiruji data.\n");
    for (size_t i = 0; i < NewSize; ++i)
    {NFC_READER_ALL_DEBUG("", "%d: ",i);
      for (size_t j = 0; j < TRecipeStep_Size; ++j)
      {
        if (i < aCardInfo->sRecipeInfo.RecipeSteps)
        {
          *((uint8_t *)NoveRecepty + i * TRecipeStep_Size + j) = *((uint8_t *)aCardInfo->sRecipeStep + i * TRecipeStep_Size + j);
          NFC_READER_ALL_DEBUG("", "%d: ",*((uint8_t *)NoveRecepty + i * TRecipeStep_Size + j));
        }
        else
        {
          *((uint8_t *)NoveRecepty + i * TRecipeStep_Size + j) = 0;
          NFC_READER_ALL_DEBUG("", "%d: ",*((uint8_t *)NoveRecepty + i * TRecipeStep_Size + j));
        }
      }
      NFC_READER_ALL_DEBUG("", "\n");
    }
    NFC_READER_ALL_DEBUG(TAGin, "Odalokovavam.\n");
    fflush(stdout);
    NFC_DeAllocTRecipeStepArray(aCardInfo);
    NFC_READER_ALL_DEBUG(TAGin, "Odalokovano.\n");
    aCardInfo->sRecipeStep = NoveRecepty;
    aCardInfo->TRecipeStepArrayCreated = aCardInfo->TRecipeStepLoaded = true;
    aCardInfo->sRecipeInfo.RecipeSteps = NewSize;
    NFC_READER_ALL_DEBUG(TAGin, "Udaje zmeneny.\n");
  }
  else
  {
    aCardInfo->sRecipeInfo.RecipeSteps = NewSize;
    for (int i = 0; i < MAXERRORREADING; ++i)
    {
      Error = NFC_AllocTRecipeStepArray(aCardInfo);
      if (Error == 0)
      {
        break;
      }
      NFC_DeAllocTRecipeStepArray(aCardInfo);
    }
    switch (Error)
    {
    case 0:
      break;
    case 2:
      NFC_READER_DEBUG(TAGin, "Nebyla nactena struktura TRecipeInfo.\n");
      return 1;
      break;
    case 3:
      NFC_READER_DEBUG(TAGin, "Nelze Alokovat pole.\n");
      return 2;
      break;
    default:
      NFC_READER_DEBUG(TAGin, "Neocekavana chyba.\n");
      return 20;
      break;
    }
    for (size_t i = 0; i < TRecipeStep_Size * aCardInfo->sRecipeInfo.RecipeSteps; ++i)
    {
      *((uint8_t *)aCardInfo->sRecipeStep + i) = 0;
    }
  }
  NFC_READER_DEBUG(TAGin, "Pole zmenilo svou velikost na %d prvku.\n", NewSize);
  return 0;
}

/**************************************************************************/
/*!
    @brief  Funkce slouží k přesunutí dat z aCardInfoOrigin do aCardInfoNew
    @param  aCardInfoOrigin Pointer na původní TCardInfo strukturu
    @param  aCardInfoNew Pointer na novou TCardInfo strukturu


    @returns    0 - uspesne kopirovani, 1 -Data TRecipeInfo nejsou nahrana v původní struktuře,2 - Nelze Alokovat pole. ,3 -Nebyla nactena struktura TRecipeInfo, 20- Neocekavana chyba
*/
/**************************************************************************/
uint8_t NFC_CopyTCardInfo(TCardInfo *aCardInfoOrigin, TCardInfo *aCardInfoNew)
{
  static const char *TAGin = "NFC_CopyTCardInfo";
  NFC_READER_DEBUG(TAGin, "Kopiruji data CardInfo do nove struktury\n");
  if (!aCardInfoOrigin->TRecipeInfoLoaded)
  {
    NFC_READER_DEBUG(TAGin, "Data TRecipeInfo nejsou nahrana v puvodni strukture.\n");
    return 1;
  }
  NFC_READER_ALL_DEBUG(TAGin, "Kopiruji TRecipeInfoData.\n");
  aCardInfoNew->sRecipeInfo = aCardInfoOrigin->sRecipeInfo;
  NFC_READER_ALL_DEBUG(TAGin, "Kopiruji Structure data.\n");
  aCardInfoNew->sUidLength = aCardInfoOrigin->sUidLength;
  for (size_t i = 0; i < aCardInfoNew->sUidLength; ++i)
  {
    aCardInfoNew->sUid[i] = aCardInfoOrigin->sUid[i];
  }
  aCardInfoNew->TRecipeInfoLoaded = aCardInfoOrigin->TRecipeInfoLoaded;
  if (aCardInfoNew->TRecipeStepArrayCreated || aCardInfoNew->sRecipeStep != NULL)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Odalokavam puvodni pamet.\n");
    NFC_DeAllocTRecipeStepArray(aCardInfoNew);
  }

  uint8_t Error;
  if (aCardInfoOrigin->TRecipeStepArrayCreated)
  {
    NFC_READER_ALL_DEBUG(TAGin, "Alokuju novou pamet.\n");
    for (int i = 0; i < MAXERRORREADING; ++i)
    {
      Error = NFC_AllocTRecipeStepArray(aCardInfoNew);
      if (Error == 0)
      {
        break;
      }
      NFC_DeAllocTRecipeStepArray(aCardInfoNew);
    }
    switch (Error)
    {
    case 0:
      break;
    case 2:
      NFC_READER_DEBUG(TAGin, "Nebyla nactena struktura TRecipeInfo.\n");
      return 3;
      break;
    case 3:
      NFC_READER_DEBUG(TAGin, "Nelze Alokovat pole.\n");
      return 2;
      break;
    default:
      NFC_READER_DEBUG(TAGin, "Neocekavana chyba.\n");
      return 20;
      break;
    }
    for (size_t i = 0; i < TRecipeStep_Size * aCardInfoNew->sRecipeInfo.RecipeSteps; ++i)
    {
      *((uint8_t *)aCardInfoNew->sRecipeStep + i) = *((uint8_t *)aCardInfoOrigin->sRecipeStep + i);
    }

    aCardInfoNew->TRecipeStepArrayCreated = aCardInfoOrigin->TRecipeStepArrayCreated;
    NFC_READER_ALL_DEBUG(TAGin, "Pole TRecipeStep se prekopirovalo.\n");
  }
  else
  {
    aCardInfoNew->sRecipeStep = NULL;
  }
  aCardInfoNew->TRecipeStepLoaded = aCardInfoOrigin->TRecipeStepLoaded;

  NFC_READER_DEBUG(TAGin, "Data se prekopirovala.\n");
  return 0;
}

void setLight(tNeopixelContext* aLight)
{
  Light = aLight;
}
