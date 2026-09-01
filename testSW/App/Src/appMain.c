#include "appMain.h"
#include "debug_console.h"
#include "greenpak_fixture.h"
#include "greenpak_host.h"
#include "greenpak_ram.h"
#include "i2c_scan.h"
#include "manual_control.h"
#include "test_runner.h"
#include "main.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

extern I2C_HandleTypeDef hi2c1;
extern DAC_HandleTypeDef hdac1;

#define TERMINAL_FLUSH_MS 10u
#define PANEL_WIDTH       66u
#define PANEL_INNER_FMT   "%-66.66s"

#define ADC_RAW_MAX              16383u
#define ADC_VREF_MV              1620u
#define RELAY_DIVIDER_NUMERATOR  11u
#define TEMP_SENSOR_25C_UV       753800u
#define TEMP_SENSOR_SLOPE_UV_C   1830u
#define VOLTAGE_INPUT_BUFFER_SIZE 16u

typedef enum
{
    APP_MENU_MAIN = 0,
    APP_MENU_MANUAL,
    APP_MENU_TEST
} AppMenu_t;

static AppMenu_t s_currentMenu = APP_MENU_MAIN;

#define TEST_NUMBER_BUFFER_SIZE 6u
static char s_testNumberBuffer[TEST_NUMBER_BUFFER_SIZE];
static uint32_t s_testNumberLength = 0u;
static bool s_manualVoltageInputActive = false;
static char s_voltageInputBuffer[VOLTAGE_INPUT_BUFFER_SIZE];
static uint32_t s_voltageInputLength = 0u;

static const char *levelText(GPIO_PinState state)
{
    return (state == GPIO_PIN_SET) ? "HIGH" : "LOW";
}

static const char *onOffText(bool value)
{
    return value ? "ON" : "OFF";
}

static const char *releasedOrLowText(bool driveLow)
{
    return driveLow ? "drive LOW" : "released";
}

static uint32_t adcRawToAdcInputMv(uint16_t raw)
{
    return (uint32_t)(((uint64_t)raw * ADC_VREF_MV + (ADC_RAW_MAX / 2u)) / ADC_RAW_MAX);
}

static uint32_t adcRawToRelayVoltageMv(uint16_t raw)
{
    return adcRawToAdcInputMv(raw) * RELAY_DIVIDER_NUMERATOR;
}

static int32_t temperatureRawToCentiC(uint16_t raw)
{
    int32_t sensorUv = (int32_t)(((uint64_t)raw * (uint64_t)ADC_VREF_MV * 1000u +
                                  (ADC_RAW_MAX / 2u)) /
                                 ADC_RAW_MAX);
    int32_t deltaUv = (int32_t)TEMP_SENSOR_25C_UV - sensorUv;

    return (deltaUv * 100) / (int32_t)TEMP_SENSOR_SLOPE_UV_C;
}

static void formatMilliVolts(char *dst, size_t dstSize, uint32_t mv)
{
    (void)snprintf(dst, dstSize, "%lu.%03lu V",
                   (unsigned long)(mv / 1000u),
                   (unsigned long)(mv % 1000u));
}

static void formatCentiC(char *dst, size_t dstSize, int32_t centiC)
{
    char sign = '+';
    if (centiC < 0)
    {
        sign = '-';
        centiC = -centiC;
    }

    (void)snprintf(dst, dstSize, "%c%ld.%02ld C",
                   sign,
                   (long)(centiC / 100),
                   (long)(centiC % 100));
}

static bool parseVoltageInputMv(const char *text, uint32_t *mvOut)
{
    if ((text == NULL) || (mvOut == NULL) || (text[0] == '\0'))
    {
        return false;
    }

    uint32_t whole = 0u;
    uint32_t frac = 0u;
    uint32_t fracDigits = 0u;
    bool seenDigit = false;
    bool seenDot = false;

    for (const char *p = text; *p != '\0'; p++)
    {
        if ((*p >= '0') && (*p <= '9'))
        {
            seenDigit = true;
            uint32_t digit = (uint32_t)(*p - '0');
            if (!seenDot)
            {
                if (whole > 100000u)
                {
                    return false;
                }
                whole = (whole * 10u) + digit;
            }
            else if (fracDigits < 3u)
            {
                frac = (frac * 10u) + digit;
                fracDigits++;
            }
        }
        else if ((*p == '.') || (*p == ','))
        {
            if (seenDot)
            {
                return false;
            }
            seenDot = true;
        }
        else
        {
            return false;
        }
    }

    if (!seenDigit)
    {
        return false;
    }

    while (fracDigits < 3u)
    {
        frac *= 10u;
        fracDigits++;
    }

    *mvOut = (whole * 1000u) + frac;
    return true;
}

static void clearScreen(void)
{
    printf("\033[2J");
    printf("\033[H");
}

static void makePair(char *dst,
                     size_t dstSize,
                     const char *name,
                     const char *value)
{
    (void)snprintf(dst, dstSize, "%-15s %s", name, value);
}

static void makeAlignedRow(char *dst,
                           size_t dstSize,
                           const char *leftName,
                           const char *leftValue,
                           const char *rightName,
                           const char *rightValue)
{
    char left[32];
    char right[32];

    /*
     * Keep the left column width fixed for both section headers and signal rows.
     * The previous name field was shorter than "MCU -> GreenPAK", so the header
     * pushed the center divider a few columns to the right.
     */
    if ((leftName != NULL) && (leftName[0] != '\0'))
    {
        (void)snprintf(left, sizeof(left), "%-15s %-10s", leftName, leftValue);
    }
    else
    {
        (void)snprintf(left, sizeof(left), "%-26s", "");
    }

    if ((rightName != NULL) && (rightName[0] != '\0'))
    {
        (void)snprintf(right, sizeof(right), "%-15s %s", rightName, rightValue);
    }
    else
    {
        (void)snprintf(right, sizeof(right), "%s", "");
    }

    (void)snprintf(dst, dstSize, "%s |  %s", left, right);
}

static void printHorizontalRule(const char *left, const char *fill, const char *right)
{
    printf("%s", left);

    /*
     * printFullLine() uses: left border + space + PANEL_WIDTH chars +
     * space + right border. The horizontal rules therefore need
     * PANEL_WIDTH + 2 fill characters to line up with the text rows.
     */
    for (uint32_t i = 0u; i < (PANEL_WIDTH + 2u); i++)
    {
        printf("%s", fill);
    }

    printf("%s\r\n", right);
}

static void printBoxTop(void)
{
    printHorizontalRule("┌", "─", "┐");
}

static void printBoxBottom(void)
{
    printHorizontalRule("└", "─", "┘");
}

static void printBoxRule(void)
{
    printHorizontalRule("├", "─", "┤");
}

static void printFullLine(const char *text)
{
    printf("│ " PANEL_INNER_FMT " │\r\n", (text != NULL) ? text : "");
}

static void printMainMenu(const char *message)
{
    char line[96];

    clearScreen();

    printBoxTop();
    (void)snprintf(line,
                   sizeof(line),
                   "SLG47011 Relay Test Console  >>> SELECTED I2C: 0x%02X <<<",
                   (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
    printFullLine(line);
    printBoxRule();
    printFullLine("Main menu");
    printFullLine("");
    printFullLine("m = manual interface");
    printFullLine("t = test interface");
    printFullLine("n = switch selected I2C device 0x08 / 0x18");
    printFullLine("p = refresh main menu");
    printFullLine("");
    printFullLine("Test interface contains automated behavior tests.");
    printBoxRule();
    printFullLine((message != NULL) ? message : "Ready.");
    printBoxBottom();
    printf("> ");
}

static void printTestMenu(const char *message)
{
    TestRunner_PrintMenu(message);
}

static void printManualPanel(const char *message)
{
    ManualControl_Status_t manual;
    GreenPakHost_OutputStatus_t host;
    char line[96];
    char pair[64];

    ManualControl_GetStatus(&manual);
    HAL_StatusTypeDef hostReadStatus = GreenPakHost_ReadOutputs(&host);

    clearScreen();

    printBoxTop();
    (void)snprintf(line,
                   sizeof(line),
                   "SLG47011 Relay Test Console  >>> SELECTED I2C: 0x%02X <<<",
                   (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
    printFullLine(line);
    printBoxRule();
    makeAlignedRow(line, sizeof(line), "MCU -> GreenPAK", "", "GreenPAK -> MCU", "");
    printFullLine(line);

    makeAlignedRow(line, sizeof(line), "SHUTDOWN_N", releasedOrLowText(manual.shutdownMcuDrivesLow),
                   "SHUTDOWN_N", levelText(manual.shutdownNPin));
    printFullLine(line);

    makeAlignedRow(line, sizeof(line), "REL_EN_H", levelText(manual.relEnH),
                   "REL_H", levelText(manual.relayH));
    printFullLine(line);

    makeAlignedRow(line, sizeof(line), "REL_EN_L", levelText(manual.relEnL),
                   "REL_L", levelText(manual.relayL));
    printFullLine(line);

    makeAlignedRow(line, sizeof(line), "RESET_N", levelText(manual.resetN),
                   "", "");
    printFullLine(line);

    makeAlignedRow(line, sizeof(line), "CURR_DIR", levelText(manual.currentDirection),
                   "", "");
    printFullLine(line);

    makeAlignedRow(line, sizeof(line), "", "",
                   "PRE_CHARGE", levelText(manual.preChargeEn));
    printFullLine(line);

    makeAlignedRow(line, sizeof(line), "", "",
                   "BOOST_PWR", levelText(manual.boostPower));
    printFullLine(line);

    makeAlignedRow(line, sizeof(line), "", "",
                   "REL_PWR_EN", levelText(manual.relayPwrEn));
    printFullLine(line);

    printBoxRule();
    (void)snprintf(line,
                   sizeof(line),
                   "I2C selected device 0x%02X command state:",
                   (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
    printFullLine(line);
    makePair(pair,
             sizeof(pair),
             "OUT0 WDT level",
             host.wdtOutputLevel ? "HIGH" : "LOW");
    printFullLine(pair);
    makePair(pair, sizeof(pair), "OUT1 RelayPWR", onOffText(host.relayPwrOutputLevel));
    printFullLine(pair);
    makePair(pair,
             sizeof(pair),
             "OUT2 ADC",
             host.adcOutputLevel ? "ENABLED" : "DISABLED");
    printFullLine(pair);
    printFullLine("");
    printFullLine("WDT background service:");
    makePair(pair,
             sizeof(pair),
             "WDT 0x08",
             GreenPakHost_GetWdtAutoToggleEnabledForDevice(GREENPAK_DEVICE_08) ?
                 "ENABLED" : "DISABLED");
    printFullLine(pair);
    makePair(pair,
             sizeof(pair),
             "WDT 0x18",
             GreenPakHost_GetWdtAutoToggleEnabledForDevice(GREENPAK_DEVICE_18) ?
                 "ENABLED" : "DISABLED");
    printFullLine(pair);

    if (hostReadStatus == HAL_OK)
    {
        printFullLine("Status          OK");
    }
    else
    {
        (void)snprintf(line,
                       sizeof(line),
                       "Status          FAIL, HAL error 0x%08lX",
                       (unsigned long)GreenPakHost_GetLastError());
        printFullLine(line);
    }

    printBoxRule();
    printFullLine((message != NULL) ? message : "Ready.");
    printBoxRule();
    printFullLine("n=switch I2C  i=scan  r=reset  s=shutdown");
    printFullLine("h=en_h  l=en_l  c=curr dir  e=relay pwr");
    printFullLine("o=adc  a=read adc");
    printFullLine("x=readback  v=set relay V  g=load RAM  G=load+verify RAM");
    printFullLine("w=wdt  d=wdt diag  p=refresh  b=main  t=test");
    printBoxBottom();
    printf("> ");
}

static void printHelp(void)
{
    clearScreen();
    printf("SLG47011 Relay Test Console\r\n");
    printf("\r\n");

    switch (s_currentMenu)
    {
        case APP_MENU_MAIN:
            printf("Main menu commands:\r\n");
            printf("  m : open manual interface\r\n");
            printf("  t : open test interface\r\n");
            printf("  n : switch selected GreenPAK between I2C 0x08 and 0x18\r\n");
            printf("  p : refresh main menu\r\n");
            printf("  ? : print this help\r\n");
            break;

        case APP_MENU_MANUAL:
            printf("Manual interface commands:\r\n");
            printf("  n : switch selected GreenPAK between I2C 0x08 and 0x18\r\n");
            printf("  i : force all address pins/DACs low and scan all I2C addresses\r\n");
            printf("  r : toggle Reset_N\r\n");
            printf("  s : toggle Shutdown_N drive-low/released state\r\n");
            printf("  h : toggle REL_EN_H\r\n");
            printf("  l : toggle REL_EN_L\r\n");
            printf("  c : toggle CURRENT_DIRECTION\r\n");
            printf("  e : toggle Relay PWR EN using I2C OUT1\r\n");
            printf("  o : enable/disable ADC using I2C OUT2\r\n");
            printf("  a : read ADC/Data Buffer values\r\n");
            printf("  x : read and decode I2C signal readback register 0x0062\r\n");
            printf("  v : set Relay pwr voltage DAC, enter real relay voltage in volts\r\n");
            printf("  g : load generated GreenPAK RAM image from slg47011_config_data.c\r\n");
            printf("  G : load generated GreenPAK RAM image and verify readback\r\n");
            printf("  w : enable/disable WDT for selected device (200 ms edges / 400 ms cycle)\r\n");
            printf("  d : print WDT diagnostics/timing statistics\r\n");
            printf("  p : refresh live panel, no state changes\r\n");
            printf("  b : back to main menu\r\n");
            printf("  t : open test interface\r\n");
            printf("  ? : print this help\r\n");
            printf("\r\nNotes:\r\n");
            printf("  - WDT is I2C OUT0 and remains enabled for a device after selecting the other device.\r\n");
            printf("  - If both WDTs are enabled, 0x08 and 0x18 are serviced back-to-back on one schedule.\r\n");
            printf("  - Relay PWR EN command is I2C OUT1.\r\n");
            printf("  - ADC enable command is I2C OUT2.\r\n");
            printf("  - Relay PWR EN feedback is PA10 / TP16 in the live panel.\r\n");
            break;

        case APP_MENU_TEST:
        default:
            printf("Test interface commands:\r\n");
            printf("  a : run all tests\r\n");
            printf("  <number> + Enter : run selected test, for example 1 or 10\r\n");
            printf("  l : list tests\r\n");
            printf("  d : apply default state\r\n");
            printf("  n : switch selected GreenPAK between I2C 0x08 and 0x18\r\n");
            printf("  p : refresh test interface\r\n");
            printf("  m : open manual interface\r\n");
            printf("  b : back to main menu\r\n");
            printf("  ? : print this help\r\n");
            break;
    }

    printf("\r\nPress p to return to the current screen.\r\n> ");
}

static void runI2cScanCommand(void)
{
    GreenPakFixture_Status_t fixtureStatus = GreenPakFixture_PrepareI2cScanAllAddressPinsLow();

    clearScreen();
    printf("------------------------------------------------------------\r\n");
    printf(" I2C scan\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Preparing fixture: %s\r\n", GreenPakFixture_StatusText(fixtureStatus));

    if (fixtureStatus != GREENPAK_FIXTURE_OK)
    {
        printf("Result: FAIL, fixture preparation failed\r\n");
        GreenPakFixture_ReleaseGreenPakOutputsToInputs();
        printf("------------------------------------------------------------\r\n> ");
        return;
    }

    GreenPakFixture_PrintScanFixtureStatus();

    I2cScan_Result_t result;
    (void)I2cScan_Run(&result);

    GreenPakFixture_ReleaseGreenPakOutputsToInputs();
    printf("\r\nGreenPAK output pins released back to Nucleo input mode.\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Press p to refresh live panel.\r\n> ");
}

static void toggleRelayPowerCommand(void)
{
    bool newLevel = false;
    HAL_StatusTypeDef st = GreenPakHost_ToggleRelayPowerEnable(&newLevel);

    if (st == HAL_OK)
    {
        printManualPanel(newLevel ? "I2C OUT1 Relay PWR EN -> ON" :
                                    "I2C OUT1 Relay PWR EN -> OFF");
    }
    else
    {
        printManualPanel("Relay PWR EN toggle failed on I2C OUT1");
    }
}

static void toggleAdcEnableCommand(void)
{
    bool newLevel = false;
    HAL_StatusTypeDef st = GreenPakHost_ToggleAdcEnable(&newLevel);

    if (st == HAL_OK)
    {
        printManualPanel(newLevel ? "I2C OUT2 ADC -> ENABLED" :
                                    "I2C OUT2 ADC -> DISABLED");
    }
    else
    {
        printManualPanel("ADC enable toggle failed on I2C OUT2");
    }
}

static void printRawAnalogValue(const char *name, uint16_t raw)
{
    printf("  %-31s : 0x%04X / %5u\r\n", name, raw, raw);
}

static void printRelayVoltageValue(const char *name, uint16_t raw)
{
    char adcText[20];
    char relayText[20];
    formatMilliVolts(adcText, sizeof(adcText), adcRawToAdcInputMv(raw));
    formatMilliVolts(relayText, sizeof(relayText), adcRawToRelayVoltageMv(raw));

    printf("  %-31s : 0x%04X / %5u  ADC pin %s  RelayV %s\r\n",
           name, raw, raw, adcText, relayText);
}

static void printTemperatureValue(const char *name, uint16_t raw)
{
    char adcText[20];
    char tempText[20];
    formatMilliVolts(adcText, sizeof(adcText), adcRawToAdcInputMv(raw));
    formatCentiC(tempText, sizeof(tempText), temperatureRawToCentiC(raw));

    printf("  %-31s : 0x%04X / %5u  Sensor %s  Approx %s\r\n",
           name, raw, raw, adcText, tempText);
}

static void readAnalogValuesCommand(void)
{
    GreenPakHost_OutputStatus_t host;
    GreenPakHost_AnalogValues_t values;

    (void)GreenPakHost_ReadOutputs(&host);
    HAL_StatusTypeDef st = GreenPakHost_ReadAnalogValues(&values);

    clearScreen();
    printf("------------------------------------------------------------\r\n");
    printf(" ADC / Data Buffer readout\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Selected I2C device              : 0x%02X\r\n",
           (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
    printf("ADC command state, OUT2          : %s\r\n",
           host.adcOutputLevel ? "ENABLED" : "DISABLED");

    if (!host.adcOutputLevel)
    {
        printf("Note: ADC is DISABLED. Values below may be stale.\r\n");
    }

    if (st == HAL_OK)
    {
        printf("\r\nAveraged values:\r\n");
        printRelayVoltageValue("DB0 Relay pwr continuous", values.relayPowerContinuousRaw);
        printRelayVoltageValue("DB1 Relay pwr during BOOST", values.relayPowerBoostRaw);
        printTemperatureValue("DB2 Internal temperature", values.temperatureRaw);
        printf("\r\nAssumptions:\r\n");
        printf("  ADC Vref = 1.620 V, 14-bit raw, RelayV divider = 100k/10k => factor 11.\r\n");
        printf("  Temperature uses uncalibrated approximation from internal sensor voltage.\r\n");
        printf("\r\nResult: OK\r\n");
    }
    else
    {
        printf("\r\nResult: FAIL, HAL error 0x%08lX\r\n",
               (unsigned long)GreenPakHost_GetLastError());
    }

    printf("------------------------------------------------------------\r\n");
    printf("Press p to refresh live panel.\r\n> ");
}


static const char *onOffUpperText(bool value)
{
    return value ? "ON" : "OFF";
}

static bool readbackBit(uint8_t value, uint8_t bitIndex)
{
    return ((value & (uint8_t)(1u << bitIndex)) != 0u);
}

static void printSignalReadbackLine(const char *name, uint8_t value, uint8_t bitIndex)
{
    printf("  IN%u %-16s : %s\r\n",
           (unsigned int)bitIndex,
           name,
           onOffUpperText(readbackBit(value, bitIndex)));
}

static void readSignalReadbackCommand(void)
{
    uint8_t value = 0u;
    HAL_StatusTypeDef st = GreenPakHost_ReadSignalReadback(&value);

    clearScreen();
    printf("------------------------------------------------------------\r\n");
    printf(" I2C signal readback\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Device   : 0x%02X\r\n",
           (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
    printf("Register : 0x0062\r\n");

    if (st == HAL_OK)
    {
        printf("Raw      : 0x%02X\r\n", value);
        printf("\r\nDecoded signals:\r\n");
        printSignalReadbackLine("Relay L", value, 0u);
        printSignalReadbackLine("Relay H", value, 1u);
        printSignalReadbackLine("Sdn latched", value, 2u);
        printSignalReadbackLine("Reset allowed", value, 3u);
        printSignalReadbackLine("Current dir", value, 4u);
        printSignalReadbackLine("Enable L", value, 5u);
        printSignalReadbackLine("Enable H", value, 6u);
        printSignalReadbackLine("WDT latched", value, 7u);
        printf("\r\nResult   : OK\r\n");
    }
    else
    {
        printf("\r\nResult   : FAIL, HAL error 0x%08lX\r\n",
               (unsigned long)GreenPakHost_GetLastError());
    }

    printf("------------------------------------------------------------\r\n");
    printf("Press p to refresh live panel.\r\n> ");
}

static void beginRelayVoltageInputCommand(void)
{
    s_manualVoltageInputActive = true;
    s_voltageInputLength = 0u;
    s_voltageInputBuffer[0] = '\0';

    clearScreen();
    printf("------------------------------------------------------------\r\n");
    printf(" Set relay power voltage stimulus\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Enter real relay voltage in volts, then press Enter.\r\n");
    printf("Example: 12.5 means RelayV = 12.5 V.\r\n");
    printf("The DAC output is RelayV / 11 to simulate the 100k/10k divider.\r\n");
    printf("Maximum before ADC Vref clipping is about 17.820 V.\r\n");
    printf("Press ESC to cancel.\r\n");
    printf("RelayV> ");
}

static void finishRelayVoltageInputCommand(void)
{
    uint32_t relayMv = 0u;
    uint32_t dacMv = 0u;
    uint16_t dacCode = 0u;
    bool clamped = false;
    char relayText[20];
    char dacText[20];

    s_manualVoltageInputActive = false;

    if (!parseVoltageInputMv(s_voltageInputBuffer, &relayMv))
    {
        printManualPanel("Invalid voltage input");
        return;
    }

    GreenPakFixture_Status_t st = GreenPakFixture_SetRelayPwrVoltageSimulatedMv(relayMv,
                                                                                 &dacMv,
                                                                                 &dacCode,
                                                                                 &clamped);
    if (st != GREENPAK_FIXTURE_OK)
    {
        printManualPanel("Failed to set Relay pwr voltage DAC");
        return;
    }

    formatMilliVolts(relayText, sizeof(relayText), relayMv);
    formatMilliVolts(dacText, sizeof(dacText), dacMv);

    clearScreen();
    printf("------------------------------------------------------------\r\n");
    printf(" Relay power voltage stimulus set\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Requested RelayV : %s\r\n", relayText);
    printf("DAC output       : %s  (RelayV / 11)\r\n", dacText);
    printf("DAC code         : %u / 4095\r\n", dacCode);
    if (clamped)
    {
        printf("Note             : Clamped to ADC Vref equivalent.\r\n");
    }
    printf("------------------------------------------------------------\r\n");
    printf("Press p to refresh live panel.\r\n> ");
}

static void handleRelayVoltageInputChar(uint8_t rxData)
{
    if ((rxData == 0x1Bu))
    {
        s_manualVoltageInputActive = false;
        printManualPanel("Relay voltage input cancelled");
        return;
    }

    if ((rxData == (uint8_t)'\r') || (rxData == (uint8_t)'\n'))
    {
        finishRelayVoltageInputCommand();
        return;
    }

    if ((rxData == 0x08u) || (rxData == 0x7Fu))
    {
        if (s_voltageInputLength > 0u)
        {
            s_voltageInputLength--;
            s_voltageInputBuffer[s_voltageInputLength] = '\0';
            printf("\b \b");
        }
        return;
    }

    bool validChar = (((rxData >= (uint8_t)'0') && (rxData <= (uint8_t)'9')) ||
                      (rxData == (uint8_t)'.') ||
                      (rxData == (uint8_t)','));
    if (!validChar)
    {
        return;
    }

    if (s_voltageInputLength < (VOLTAGE_INPUT_BUFFER_SIZE - 1u))
    {
        s_voltageInputBuffer[s_voltageInputLength++] = (char)rxData;
        s_voltageInputBuffer[s_voltageInputLength] = '\0';
        printf("%c", rxData);
    }
}


static void loadGeneratedRamConfigCommand(bool verifyAfterWrite)
{
    GreenPakRam_LoadResult_t result;

    clearScreen();
    printf("------------------------------------------------------------\r\n");
    printf(" GreenPAK volatile RAM load\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Target I2C  : 0x%02X\r\n",
           (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
    printf("Source data : App/Inc/slg47011_config_data.h\r\n");
    printf("              App/Src/slg47011_config_data.c\r\n");
    printf("Operation   : write generated RAM image%s\r\n",
           verifyAfterWrite ? " and verify" : "");
    printf("\r\nLoading...\r\n");
    DebugConsole_FlushMs(TERMINAL_FLUSH_MS);

    HAL_StatusTypeDef st = GreenPakRam_LoadGeneratedConfig(verifyAfterWrite, &result);

    printf("\r\nBytes written : %lu\r\n", (unsigned long)result.bytesWritten);

    if (verifyAfterWrite)
    {
        printf("Bytes checked : %lu\r\n", (unsigned long)result.verify.bytesChecked);
    }

    if (st == HAL_OK)
    {
        printf("Result        : PASS\r\n");
    }
    else
    {
        printf("Result        : FAIL\r\n");
        printf("HAL error     : 0x%08lX\r\n", (unsigned long)result.halError);

        if (verifyAfterWrite && !result.verify.ok && (result.verify.bytesChecked > 0u))
        {
            printf("Verify fail   : addr 0x%04X expected 0x%02X read 0x%02X\r\n",
                   result.verify.failAddr,
                   result.verify.expected,
                   result.verify.actual);
        }
    }

    printf("------------------------------------------------------------\r\n");
    printf("Press p to refresh live panel.\r\n> ");
}

static void printWdtDiagnosticsCommand(void)
{
    GreenPakHost_WdtDiagnostics_t diag;
    GreenPakHost_GetWdtDiagnostics(&diag);

    clearScreen();
    printf("------------------------------------------------------------\r\n");
    printf(" WDT diagnostics\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Selected I2C       : 0x%02X\r\n",
           (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
    printf("WDT 0x08           : %s\r\n",
           GreenPakHost_GetWdtAutoToggleEnabledForDevice(GREENPAK_DEVICE_08) ?
               "ENABLED" : "DISABLED");
    printf("WDT 0x18           : %s\r\n",
           GreenPakHost_GetWdtAutoToggleEnabledForDevice(GREENPAK_DEVICE_18) ?
               "ENABLED" : "DISABLED");
    printf("Selected auto-WDT  : %s\r\n", diag.autoToggleEnabled ? "ENABLED" : "DISABLED");
    printf("Selected OUT0 level: %s\r\n", diag.outputLevel ? "HIGH" : "LOW");
    printf("Target edge period : %lu ms\r\n", (unsigned long)diag.targetToggleIntervalMs);
    printf("Last edge interval : %lu ms\r\n", (unsigned long)diag.lastToggleIntervalMs);
    printf("Max edge interval  : %lu ms\r\n", (unsigned long)diag.maxToggleIntervalMs);
    printf("Service calls      : %lu\r\n", (unsigned long)diag.serviceCallCount);
    printf("Last service gap   : %lu ms\r\n", (unsigned long)diag.lastServiceIntervalMs);
    printf("Max service gap    : %lu ms\r\n", (unsigned long)diag.maxServiceIntervalMs);
    printf("Toggle writes OK   : %lu\r\n", (unsigned long)diag.toggleWriteCount);
    printf("Toggle writes FAIL : %lu\r\n", (unsigned long)diag.toggleWriteFailCount);
    printf("Last HAL error     : 0x%08lX\r\n", (unsigned long)diag.lastHalError);
    printf("------------------------------------------------------------\r\n");
    printf("Interpretation:\r\n");
    printf("  Max edge interval should stay comfortably below the GreenPAK WDT timeout.\r\n");
    printf("  Large service gaps mean the firmware is not servicing WDT often enough.\r\n");
    printf("  Any failed toggle write means an I2C write to OUT0 failed.\r\n");
    printf("------------------------------------------------------------\r\n");
    printf("Press p to refresh live panel.\r\n> ");
}

static void toggleWdtAutoToggleCommand(void)
{
    uint8_t address = GreenPakHost_GetSelectedAddress7Bit();
    bool enable = !GreenPakHost_GetWdtAutoToggleEnabled();
    HAL_StatusTypeDef st = GreenPakHost_SetWdtAutoToggle(enable);
    char message[96];

    if (st == HAL_OK)
    {
        (void)snprintf(message,
                       sizeof(message),
                       "WDT 0x%02X -> %s",
                       (unsigned int)address,
                       enable ? "ENABLED (200 ms edges)" : "DISABLED");
    }
    else
    {
        (void)snprintf(message,
                       sizeof(message),
                       "WDT 0x%02X update FAILED, HAL error 0x%08lX",
                       (unsigned int)address,
                       (unsigned long)GreenPakHost_GetLastError());
    }

    printManualPanel(message);
}

void App_Init(void)
{
    DebugConsole_Init();
    GreenPakFixture_Init(&hdac1);
    I2cScan_Init(&hi2c1);
    GreenPakHost_Init(&hi2c1);
    GreenPakRam_Init(&hi2c1);
    ManualControl_Init();

    s_currentMenu = APP_MENU_MAIN;
    printMainMenu("Ready. Press ? for help.");
    DebugConsole_FlushMs(TERMINAL_FLUSH_MS);
}

static void handleMainMenuCommand(uint8_t rxData)
{
    switch (rxData)
    {
        case 'm':
        case 'M':
            s_currentMenu = APP_MENU_MANUAL;
            printManualPanel("Manual interface");
            break;

        case 't':
        case 'T':
            s_currentMenu = APP_MENU_TEST;
            printTestMenu("Test interface");
            break;

        case 'n':
        case 'N':
        {
            char message[96];
            GreenPakHost_SelectNextDevice();
            (void)snprintf(message,
                           sizeof(message),
                           "Selected I2C -> 0x%02X (WDT states unchanged)",
                           (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
            printMainMenu(message);
            break;
        }

        case 'p':
        case 'P':
            printMainMenu("Refreshed main menu");
            break;

        case '?':
            printHelp();
            break;

        default:
            printMainMenu("Unknown command. Press ? for help.");
            break;
    }
}

static void handleManualMenuCommand(uint8_t rxData)
{
    switch (rxData)
    {
        case 'n':
        case 'N':
        {
            char message[96];
            GreenPakHost_SelectNextDevice();
            (void)snprintf(message,
                           sizeof(message),
                           "Selected I2C -> 0x%02X (WDT states unchanged)",
                           (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
            printManualPanel(message);
            break;
        }

        case 'i':
        case 'I':
            runI2cScanCommand();
            break;

        case 'r':
        case 'R':
            ManualControl_ToggleReset();
            printManualPanel("RESET_N toggled");
            break;

        case 's':
        case 'S':
            ManualControl_ToggleShutdown();
            printManualPanel("SHUTDOWN_N toggled");
            break;

        case 'h':
        case 'H':
            ManualControl_ToggleRelayEnableH();
            printManualPanel("REL_EN_H toggled");
            break;

        case 'l':
        case 'L':
            ManualControl_ToggleRelayEnableL();
            printManualPanel("REL_EN_L toggled");
            break;

        case 'c':
        case 'C':
            ManualControl_ToggleCurrentDirection();
            printManualPanel("CURRENT_DIRECTION toggled");
            break;

        case 'e':
        case 'E':
            toggleRelayPowerCommand();
            break;

        case 'o':
        case 'O':
            toggleAdcEnableCommand();
            break;

        case 'a':
        case 'A':
            readAnalogValuesCommand();
            break;

        case 'x':
        case 'X':
            readSignalReadbackCommand();
            break;

        case 'v':
        case 'V':
            beginRelayVoltageInputCommand();
            break;

        case 'g':
            loadGeneratedRamConfigCommand(false);
            break;

        case 'G':
            loadGeneratedRamConfigCommand(true);
            break;

        case 'w':
        case 'W':
            toggleWdtAutoToggleCommand();
            break;

        case 'd':
        case 'D':
            printWdtDiagnosticsCommand();
            break;

        case 'p':
        case 'P':
            printManualPanel("Refreshed current pin state");
            break;

        case 'b':
        case 'B':
            s_currentMenu = APP_MENU_MAIN;
            printMainMenu("Returned to main menu");
            break;

        case 't':
        case 'T':
            s_currentMenu = APP_MENU_TEST;
            printTestMenu("Test interface");
            break;

        case '?':
            printHelp();
            break;

        default:
            printManualPanel("Unknown command. Press ? for help.");
            break;
    }
}

static void clearTestNumberBuffer(void)
{
    s_testNumberLength = 0u;
    s_testNumberBuffer[0] = '\0';
}

static void appendTestNumberDigit(uint8_t rxData)
{
    if (s_testNumberLength < (TEST_NUMBER_BUFFER_SIZE - 1u))
    {
        s_testNumberBuffer[s_testNumberLength++] = (char)rxData;
        s_testNumberBuffer[s_testNumberLength] = '\0';
        printf("%c", rxData);
    }
    else
    {
        clearTestNumberBuffer();
        printTestMenu("Test number too long. Enter 1..9999.");
    }
}

static void runBufferedTestNumber(void)
{
    if (s_testNumberLength == 0u)
    {
        printTestMenu("Enter a test number, then press Enter.");
        return;
    }

    uint32_t id = (uint32_t)strtoul(s_testNumberBuffer, NULL, 10);
    clearTestNumberBuffer();

    if ((id == 0u) || (id > 65535u))
    {
        printTestMenu("Invalid test number.");
        return;
    }

    TestRunner_RunById((uint16_t)id);
}

static bool isDigitChar(uint8_t rxData)
{
    return ((rxData >= (uint8_t)'0') && (rxData <= (uint8_t)'9'));
}

static void handleTestMenuCommand(uint8_t rxData)
{
    if (isDigitChar(rxData))
    {
        appendTestNumberDigit(rxData);
        return;
    }

    if ((rxData == (uint8_t)'\r') || (rxData == (uint8_t)'\n'))
    {
        runBufferedTestNumber();
        return;
    }

    if ((rxData == 0x08u) || (rxData == 0x7Fu))
    {
        if (s_testNumberLength > 0u)
        {
            s_testNumberLength--;
            s_testNumberBuffer[s_testNumberLength] = '\0';
            printf("\b \b");
        }
        return;
    }

    clearTestNumberBuffer();

    switch (rxData)
    {
        case 'a':
        case 'A':
            TestRunner_RunAll();
            break;

        case 'l':
        case 'L':
            TestRunner_PrintList();
            break;

        case 'd':
        case 'D':
            TestRunner_ApplyDefaultCommand();
            break;

        case 'n':
        case 'N':
        {
            char message[96];
            GreenPakHost_SelectNextDevice();
            (void)snprintf(message,
                           sizeof(message),
                           "Selected I2C -> 0x%02X (WDT states unchanged)",
                           (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
            printTestMenu(message);
            break;
        }

        case 'm':
        case 'M':
            s_currentMenu = APP_MENU_MANUAL;
            printManualPanel("Manual interface");
            break;

        case 'b':
        case 'B':
            s_currentMenu = APP_MENU_MAIN;
            printMainMenu("Returned to main menu");
            break;

        case 'p':
        case 'P':
            printTestMenu("Refreshed test interface");
            break;

        case '?':
            printHelp();
            break;

        default:
            printTestMenu("Unknown command. Press ? for help.");
            break;
    }
}

void App_Run(void)
{
    uint8_t rxData = 0;

    while (1)
    {
        if (DebugConsole_ReadChar(&rxData))
        {
            switch (s_currentMenu)
            {
                case APP_MENU_MAIN:
                    handleMainMenuCommand(rxData);
                    break;

                case APP_MENU_MANUAL:
                    if (s_manualVoltageInputActive)
                    {
                        handleRelayVoltageInputChar(rxData);
                    }
                    else
                    {
                        handleManualMenuCommand(rxData);
                    }
                    break;

                case APP_MENU_TEST:
                default:
                    handleTestMenuCommand(rxData);
                    break;
            }

            DebugConsole_FlushMs(TERMINAL_FLUSH_MS);
        }

        (void)GreenPakHost_ServiceWdtAutoToggle(HAL_GetTick());
        DebugConsole_Update();
    }
}
