/*
 * Bafang LCD SW102 Bluetooth firmware
 *
 * Released under the GPL License, Version 3
 */
#include <eeprom_hw.h>
#include "ble_uart.h"
#include "app_timer.h"
#include "main.h"
#include "button.h"
#include "buttons.h"
#include "lcd.h"
#include "ugui.h"
#include "fonts.h"
#include "uart.h"
#include "utils.h"
#include "screen.h"
#include "eeprom.h"
#include "mainscreen.h"
#include "nrf_delay.h"

/* Variable definition */

/* �GUI */
UG_GUI gui;

/* Buttons */
Button buttonM, buttonDWN, buttonUP, buttonPWR;

/* App Timer */
APP_TIMER_DEF(button_poll_timer_id); /* Button timer. */
#define BUTTON_POLL_INTERVAL APP_TIMER_TICKS(10/*ms*/, APP_TIMER_PRESCALER)

APP_TIMER_DEF(seconds_timer_id); /* Second counting timer. */
#define SECONDS_INTERVAL APP_TIMER_TICKS(1000/*ms*/, APP_TIMER_PRESCALER)
volatile uint32_t seconds_since_startup, seconds_since_reset;

APP_TIMER_DEF(gui_timer_id); /* GUI updates counting timer. */
#define GUI_INTERVAL APP_TIMER_TICKS(20/*ms*/, APP_TIMER_PRESCALER)
volatile uint32_t gui_ticks;

#define FONT12_Y 14 // we want a little bit of extra space

Field faultHeading = { .variant = FieldDrawText, .drawText = { .font = &MY_FONT_8X12, .msg = "FAULT" }};
Field faultCode = { .variant = FieldDrawText, .drawText = { .font = &FONT_5X12 } };
Field addrHeading = { .variant = FieldDrawText, .drawText = { .font = &MY_FONT_8X12, .msg = "PC" } };
Field addrCode = { .variant = FieldDrawText, .drawText = { .font = &FONT_5X12 } };
Field infoHeading = { .variant = FieldDrawText, .drawText = { .font = &MY_FONT_8X12, .msg = "Info" }};
Field infoCode = { .variant = FieldDrawText, .drawText = { .font = &FONT_5X12 } };

Screen faultScreen = {
    {
        .x = 0, .y = 0,
        .width = -1, .height = -1,
        .color = ColorInvert,
        .field = &faultHeading
    },
    {
        .x = 0, .y = FONT12_Y,
        .width = -1, .height = -1,
        .color = ColorNormal,
        .field = &faultCode
    },
    {
        .x = 0, .y = 2 * FONT12_Y,
        .width = -1, .height = -1,
        .color = ColorNormal,
        .field = &addrHeading
    },
    {
        .x = 0, .y = 3 * FONT12_Y,
        .width = -1, .height = -1,
        .color = ColorNormal,
        .field = &addrCode
    },
    {
        .x = 0, .y = 4 * FONT12_Y,
        .width = -1, .height = -1,
        .color = ColorNormal,
        .field = &infoHeading
    },
    {
        .x = 0, .y = 5 * FONT12_Y,
        .width = -1, .height = -1,
        .color = ColorNormal,
        .field = &infoCode
    },
    {
        .field = NULL
    }
};

/* Function prototype */
static void gpio_init(void);
static void init_app_timers(void);
/* UART RX/TX */



/**
 * @brief Application main entry.
 */
int main(void)
{
  gpio_init();
  lcd_init();
  uart_init();

  // kevinh FIXME - turn off ble for now because somtimes it calls app_error_fault_handler(1...) from nrf51822_sw102_ble_advdata
  // ble_init();

  /* eeprom_init AFTER ble_init! */
  eeprom_init();
  // FIXME
  // eeprom_read_configuration(get_configuration_variables());
  init_app_timers();
  system_power(true);

  UG_ConsoleSetArea(0, 0, 63, 127);
  UG_ConsoleSetForecolor(C_WHITE);

  /*
  UG_FontSelect(&MY_FONT_BATTERY);
  UG_ConsolePutString("5\n");
  UG_ConsolePutString("4\n");
  UG_ConsolePutString("3\n");
  UG_ConsolePutString("2\n");
  UG_ConsolePutString("1\n");
  UG_ConsolePutString("0\n");
  */

  /*
  UG_FontSelect(&MY_FONT_8X12);
  static const char degC[] = { 31, 'C', 0 };
  UG_ConsolePutString(degC);
  */

  UG_FontSelect(&MY_FONT_8X12);
  UG_ConsolePutString("boot\n");
  lcd_refresh();


  screen_init();
  //screenUpdate(); // FIXME - move into main loop

  // APP_ERROR_HANDLER(5);

  // Enter main loop.

  uint32_t lasttick = 0;
  while (1)
  {
    uint32_t tick = gui_ticks;
    if(tick != lasttick) {
      lasttick = tick;
      buttons_clock();
      screen_clock();
    }

    nrf_delay_ms(1); // let OS threads have time to run
  }

}

/**
 * @brief Hold system power (true) or not (false)
 */
void system_power(bool state)
{
  if (state)
    nrf_gpio_pin_set(SYSTEM_POWER_HOLD__PIN);
  else
    nrf_gpio_pin_clear(SYSTEM_POWER_HOLD__PIN);
}



/* Hardware Initialization */

static void gpio_init(void)
{
  /* POWER_HOLD */
  nrf_gpio_cfg_output(SYSTEM_POWER_HOLD__PIN);

  /* LCD (none SPI) */
  nrf_gpio_cfg_output(LCD_COMMAND_DATA__PIN);
  nrf_gpio_pin_set(LCD_COMMAND_DATA__PIN);
  nrf_gpio_cfg_output(LCD_RES__PIN);
  nrf_gpio_pin_clear(LCD_RES__PIN); // Hold LCD in reset until initialization

  /* Buttons */
  InitButton(&buttonPWR, BUTTON_PWR__PIN, NRF_GPIO_PIN_NOPULL, BUTTON_ACTIVE_HIGH);
  InitButton(&buttonM, BUTTON_M__PIN, NRF_GPIO_PIN_PULLUP, BUTTON_ACTIVE_LOW);
  InitButton(&buttonUP, BUTTON_UP__PIN, NRF_GPIO_PIN_PULLUP, BUTTON_ACTIVE_LOW);
  InitButton(&buttonDWN, BUTTON_DOWN__PIN, NRF_GPIO_PIN_PULLUP, BUTTON_ACTIVE_LOW);
}

#if 0
static void button_poll_timer_timeout(void *p_context)
{
    UNUSED_PARAMETER(p_context);

    PollButton(&buttonPWR);
    PollButton(&buttonM);
    PollButton(&buttonUP);
    PollButton(&buttonDWN);
}
#endif

static void seconds_timer_timeout(void *p_context)
{
    UNUSED_PARAMETER(p_context);

    seconds_since_startup++;
    seconds_since_reset++;
}

static void gui_timer_timeout(void *p_context)
{
    UNUSED_PARAMETER(p_context);

    gui_ticks++;
}


static void init_app_timers(void)
{
  // FIXME - not sure why I needed to do this manually: https://devzone.nordicsemi.com/f/nordic-q-a/31982/can-t-make-app_timer-work
  NRF_CLOCK->TASKS_LFCLKSTART = 1;
  while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0);

  // Start APP_TIMER to generate timeouts.
  APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, NULL);

#if 0
  // Create&Start button_poll_timer
  APP_ERROR_CHECK(app_timer_create(&button_poll_timer_id, APP_TIMER_MODE_REPEATED, button_poll_timer_timeout));
  APP_ERROR_CHECK(app_timer_start(button_poll_timer_id, BUTTON_POLL_INTERVAL, NULL));
#endif

  // Create&Start timers.
  APP_ERROR_CHECK(app_timer_create(&seconds_timer_id, APP_TIMER_MODE_REPEATED, seconds_timer_timeout));
  APP_ERROR_CHECK(app_timer_start(seconds_timer_id, SECONDS_INTERVAL, NULL));
  APP_ERROR_CHECK(app_timer_create(&gui_timer_id, APP_TIMER_MODE_REPEATED, gui_timer_timeout));
  APP_ERROR_CHECK(app_timer_start(gui_timer_id, GUI_INTERVAL, NULL));
}



/* Event handler */


/**@brief       Callback function for errors, asserts, and faults.
 *
 * @details     This function is called every time an error is raised in app_error, nrf_assert, or
 *              in the SoftDevice.
 *
 *              DEBUG_NRF flag must be set to trigger asserts!
 *
 */
void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
  /*
  UG_FontSelect(&MY_FONT_8X12);
  char buf[32];
  sprintf(buf, "ERR 0x%lx\n", id);
  UG_ConsolePutString(buf);

  UG_ConsolePutString("PC\n");
  UG_FontSelect(&FONT_5X12);
  sprintf(buf, "0x%lx\n", pc);
  UG_ConsolePutString(buf);
  UG_FontSelect(&MY_FONT_8X12);
  UG_ConsolePutString("INFO\n");
  UG_FontSelect(&FONT_5X12);
  sprintf(buf, "0x%lx\n", info);
  UG_ConsolePutString(buf);
  lcd_refresh();
  */

  fieldPrintf(&faultCode, "0x%lx", id);
  fieldPrintf(&addrCode, "0x%06lx", pc);

  if(id == NRF_FAULT_ID_SDK_ERROR) {
    // app errors include filename and line
    error_info_t *einfo = (error_info_t *) info;
    fieldPrintf(&infoCode, "%s/%d/%d", einfo->p_file_name ? (const char *) einfo->p_file_name : "",
        einfo->line_num,
        einfo->err_code);
  }
  else
    fieldPrintf(&infoCode, "%08lx", info);

  screenShow(&faultScreen);

  // FIXME - instead we should wait a few seconds and then reboot
  while (1)
    nrf_delay_ms(1000);
}


