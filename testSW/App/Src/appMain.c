#include "appMain.h"
#include "debug_console.h"
#include "greenpak_fixture.h"
#include "greenpak_host.h"
#include "i2c_scan.h"
#include "manual_control.h"
#include "main.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;
extern DAC_HandleTypeDef hdac1;
extern DAC_HandleTypeDef hdac2;

#define TERMINAL_FLUSH_MS 200u
#define PANEL_WIDTH       66u
#define PANEL_INNER_FMT   "%-66.66s"

typedef enum
{
    APP_MENU_MAIN = 0,
    APP_MENU_MANUAL,
    APP_MENU_TEST
} AppMenu_t;

static AppMenu_t s_currentMenu = APP_MENU_MAIN;

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
    clearScreen();

    printBoxTop();
    printFullLine("SLG47011 Relay Test Console");
    printBoxRule();
    printFullLine("Main menu");
    printFullLine("");
    printFullLine("m = manual interface");
    printFullLine("t = test interface");
    printFullLine("p = refresh main menu");
    printFullLine("");
    printFullLine("The test interface is only a placeholder for now.");
    printBoxRule();
    printFullLine((message != NULL) ? message : "Ready.");
    printBoxBottom();
    printf("> ");
}

static void printTestMenu(const char *message)
{
    clearScreen();

    printBoxTop();
    printFullLine("SLG47011 Relay Test Console - Test interface");
    printBoxRule();
    printFullLine("Test interface placeholder.");
    printFullLine("We will add automated tests here next.");
    printFullLine("");
    printFullLine("m = manual interface");
    printFullLine("b = back to main menu");
    printFullLine("p = refresh");
    printBoxRule();
    printFullLine((message != NULL) ? message : "Ready.");
    printBoxBottom();
    printf("> ");
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
    printFullLine("SLG47011 Relay Test Console                              I2C: 0x08");
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
    printFullLine("I2C:");
    makePair(pair,
             sizeof(pair),
             "OUT0 WDT",
             host.wdtAutoToggleEnabled ? "ENABLED" : "DISABLED");
    printFullLine(pair);
    makePair(pair, sizeof(pair), "OUT1 RelayPWR", onOffText(host.relayPwrOutputLevel));
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
    printFullLine("i=scan  r=reset  s=shutdown  h=en_h  l=en_l");
    printFullLine("e=relay pwr  w=wdt OUT0  p=refresh  b=main  t=test");
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
            printf("  p : refresh main menu\r\n");
            printf("  ? : print this help\r\n");
            break;

        case APP_MENU_MANUAL:
            printf("Manual interface commands:\r\n");
            printf("  i : force all address pins/DACs low and scan all I2C addresses\r\n");
            printf("  r : toggle Reset_N\r\n");
            printf("  s : toggle Shutdown_N drive-low/released state\r\n");
            printf("  h : toggle REL_EN_H\r\n");
            printf("  l : toggle REL_EN_L\r\n");
            printf("  e : toggle Relay PWR EN using I2C OUT1\r\n");
            printf("  w : enable/disable automatic I2C OUT0 WDT toggle every 500 ms\r\n");
            printf("  p : refresh live panel, no state changes\r\n");
            printf("  b : back to main menu\r\n");
            printf("  t : open test interface\r\n");
            printf("  ? : print this help\r\n");
            printf("\r\nNotes:\r\n");
            printf("  - WDT is I2C OUT0.\r\n");
            printf("  - Relay PWR EN command is I2C OUT1.\r\n");
            printf("  - Relay PWR EN feedback is PA10 / TP16 in the live panel.\r\n");
            break;

        case APP_MENU_TEST:
        default:
            printf("Test interface commands:\r\n");
            printf("  m : open manual interface\r\n");
            printf("  b : back to main menu\r\n");
            printf("  p : refresh test interface\r\n");
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

static void toggleWdtAutoToggleCommand(void)
{
    bool enable = !GreenPakHost_GetWdtAutoToggleEnabled();
    (void)GreenPakHost_SetWdtAutoToggle(enable);

    printManualPanel(enable ? "I2C OUT0 WDT auto-toggle -> ENABLED, 500 ms" :
                              "I2C OUT0 WDT auto-toggle -> DISABLED");
}

void App_Init(void)
{
    DebugConsole_Init();
    GreenPakFixture_Init(&hdac1, &hdac2);
    I2cScan_Init(&hi2c1);
    GreenPakHost_Init(&hi2c1);
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

        case 'e':
        case 'E':
            toggleRelayPowerCommand();
            break;

        case 'w':
        case 'W':
            toggleWdtAutoToggleCommand();
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

static void handleTestMenuCommand(uint8_t rxData)
{
    switch (rxData)
    {
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
                    handleManualMenuCommand(rxData);
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
