#include "menu_drv.h"

#include <stdarg.h>
#include <stdint.h>

#include "app_config.h"
#include "battery.h"
#include "but.h"
#include "esp_task_wdt.h"
#include "freertos/semphr.h"

#include "menu_backend.h"
#include "oled.h"
#include "parameters.h"
#include "power_on.h"
#include "ssd1306.h"
#include "ssdFigure.h"
#include "wifidrv.h"

#define MODULE_NAME "[MENU Drv] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_MENU_BACKEND
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define CONFIG_MENU_TEST_TASK 0
#define PROCESS_TASK_TIMEOUT  50
#define MENU_TAB_SIZE         8
#define POWER_OFF_TIMEOUT_MS  3500
#define POWER_OFF_BLOCK_MS    5000

typedef enum
{
  MENU_STATE_INIT,
  MENU_STATE_IDLE,
  MENU_STATE_ENTER,
  MENU_STATE_EXIT,
  MENU_STATE_PROCESS,
  MENU_STATE_INFO,
  MENU_STATE_POWER_OFF_COUNT,
  MENU_STATE_POWER_OFF,
  MENU_STATE_EMERGENCY_DISABLE,
  MENU_STATE_ERROR_CHECK,
  MENU_STATE_TOP,
} menu_state_t;

typedef struct
{
  menu_state_t state;
  menu_state_t last_state;
  menu_token_t* entered_menu_tab[MENU_TAB_SIZE];
  TickType_t save_timeout;
  bool save_flag;
  bool exit_req;
  bool enter_req;
  bool error_flag;
  bool power_off_req;
  bool emergency_led_status;
  bool emergency_disable_is_active;
  uint32_t led_cnt;
  int error_code;
  char* error_msg;
  menu_token_t* new_menu;
  SemaphoreHandle_t update_screen_req;
  TickType_t power_off_timer;
  TickType_t block_power_off_timer;
} menu_drv_t;

static menu_drv_t ctx;

extern void mainMenuInit( menu_drv_init_t init_type );
extern void menuInitBootUpMenu( void );

static char* state_name[] =
  {
    [MENU_STATE_INIT] = "MENU_STATE_INIT",
    [MENU_STATE_IDLE] = "MENU_STATE_IDLE",
    [MENU_STATE_ENTER] = "MENU_STATE_ENTER",
    [MENU_STATE_EXIT] = "MENU_STATE_EXIT",
    [MENU_STATE_PROCESS] = "MENU_STATE_PROCESS",
    [MENU_STATE_INFO] = "MENU_STATE_INFO",
    [MENU_STATE_ERROR_CHECK] = "MENU_STATE_ERROR_CHECK",
    [MENU_STATE_EMERGENCY_DISABLE] = "MENU_STATE_EMERGENCY_DISABLE",
    [MENU_STATE_POWER_OFF_COUNT] = "MENU_STATE_POWER_OFF_COUNT",
    [MENU_STATE_POWER_OFF] = "MENU_STATE_POWER_OFF",
};

static but_t button1_menu, button2_menu, button3_menu, button4_menu, button5_menu, button6_menu, button7_menu,
  button8_menu, button9_menu, button10_menu;

static void update_screen( void )
{
  xSemaphoreGive( ctx.update_screen_req );
}

void menuDrvSaveParameters( void )
{
  ctx.save_timeout = xTaskGetTickCount() + MS2ST( 5000 );
  ctx.save_flag = 1;
}

static void save_process( void )
{
  if ( ( ctx.save_flag == 1 ) && ( ctx.save_timeout < xTaskGetTickCount() ) )
  {
    parameters_save();
    ctx.save_flag = 0;
  }
}

int menuDrvElementsCnt( menu_token_t* menu )
{
  if ( menu->menu_list == NULL )
  {
    LOG( PRINT_INFO, "menu->menu_list == NULL (%s)\n", dictionary_get_string( menu->name_dict ) );
    return 0;
  }

  int len = 0;
  menu_token_t** actual_token = menu->menu_list;

  do
  {
    if ( actual_token[len] == NULL )
    {
      return len;
    }

    len++;
  } while ( len < 255 );

  return 0;
}

static menu_token_t* last_tab_element( void )
{
  for ( uint8_t i = 0; i < 8; i++ )
  {
    if ( ( ctx.entered_menu_tab[i] == NULL ) && ( i != 0 ) )
    {
      return ctx.entered_menu_tab[i - 1];
    }
  }

  return NULL;
}

static int tab_len( void )
{
  for ( uint8_t i = 0; i < 8; i++ )
  {
    if ( ctx.entered_menu_tab[i] == NULL )
    {
      return i;
    }
  }

  return 0;
}

static void add_menu_tab( menu_token_t* menu )
{
  int pos = tab_len();

  ctx.entered_menu_tab[pos] = menu;
}

static void remove_last_menu_tab( void )
{
  int pos = tab_len();

  if ( pos > 1 )
  {
    ctx.entered_menu_tab[pos - 1] = NULL;
  }
}

void go_to_main_menu( void )
{
  for ( uint8_t i = 1; i < 8; i++ )
  {
    ctx.entered_menu_tab[i] = NULL;
  }

  update_screen();
}

static void menu_fall_turn_on_off_but_cb( void* arg )
{
  but_t* button = (but_t*) arg;

  if ( button->fall_callback != NULL )
  {
    button->fall_callback( button->arg );
    update_screen();
  }

  power_on_reset_timer();
  backendToggleEmergencyDisable();
}

static void menu_timer_power_off_but_cb( void* arg )
{
  but_t* button = (but_t*) arg;

  if ( button->fall_callback != NULL )
  {
    button->fall_callback( button->arg );
    update_screen();
  }

  if ( ctx.block_power_off_timer > xTaskGetTickCount() )
  {
    return;
  }

  ctx.power_off_req = true;
  ctx.state = MENU_STATE_POWER_OFF_COUNT;
  ctx.power_off_timer = xTaskGetTickCount() + MS2ST( POWER_OFF_TIMEOUT_MS );
}

static void menu_rise_power_off_but_cb( void* arg )
{
  power_on_reset_timer();
  ctx.power_off_req = false;
}

static void menu_timer_long_power_off_but_cb( void* arg )
{
  if ( ( ctx.state == MENU_STATE_POWER_OFF_COUNT ) || ( ctx.state == MENU_STATE_POWER_OFF ) )
  {
    ctx.state = MENU_STATE_POWER_OFF;
  }
}

static void menu_fall_callback_but_cb( void* arg )
{
  but_t* button = (but_t*) arg;

  if ( button->fall_callback != NULL )
  {
    button->fall_callback( button->arg );
    update_screen();
  }

  power_on_reset_timer();
}

static void menu_rise_callback_but_cb( void* arg )
{
  but_t* button = (but_t*) arg;

  if ( button->rise_callback != NULL )
  {
    button->rise_callback( button->arg );
    update_screen();
  }

  power_on_reset_timer();
}

static void menu_timer_callback_but_cb( void* arg )
{
  but_t* button = (but_t*) arg;

  if ( button->timer_callback != NULL )
  {
    button->timer_callback( button->arg );
    update_screen();
  }
}

static void menu_init_buttons( void )
{
  button1.arg = &button1_menu;
  button1.fall_callback = menu_fall_callback_but_cb;
  button1.rise_callback = menu_rise_callback_but_cb;
  button1.timer_callback = menu_timer_callback_but_cb;

  button2.arg = &button2_menu;
  button2.fall_callback = menu_fall_callback_but_cb;
  button2.rise_callback = menu_rise_callback_but_cb;
  button2.timer_callback = menu_timer_callback_but_cb;

  button3.arg = &button3_menu;
  button3.fall_callback = menu_fall_callback_but_cb;
  button3.rise_callback = menu_rise_callback_but_cb;
  button3.timer_callback = menu_timer_callback_but_cb;

  button4.arg = &button4_menu;
  button4.fall_callback = menu_fall_callback_but_cb;
  button4.rise_callback = menu_rise_callback_but_cb;
  button4.timer_callback = menu_timer_callback_but_cb;

  button5.arg = &button5_menu;
  button5.fall_callback = menu_fall_callback_but_cb;
  button5.rise_callback = menu_rise_callback_but_cb;
  button5.timer_callback = menu_timer_callback_but_cb;

  button6.arg = &button6_menu;
  button6.fall_callback = menu_fall_callback_but_cb;
  button6.rise_callback = menu_rise_callback_but_cb;
  button6.timer_callback = menu_timer_callback_but_cb;

  button7.arg = &button7_menu;
  button7.fall_callback = menu_fall_callback_but_cb;
  button7.rise_callback = menu_rise_callback_but_cb;
  button7.timer_callback = menu_timer_callback_but_cb;

  button8.arg = &button8_menu;
  button8.fall_callback = menu_fall_callback_but_cb;
  button8.rise_callback = menu_rise_callback_but_cb;
  button8.timer_callback = menu_timer_callback_but_cb;

  button9.arg = &button9_menu;
  button9.fall_callback = menu_fall_callback_but_cb;
  button9.rise_callback = menu_rise_callback_but_cb;
  button9.timer_callback = menu_timer_callback_but_cb;

  button10.arg = &button10_menu;
  button10.fall_callback = menu_fall_turn_on_off_but_cb;
  button10.rise_callback = menu_rise_power_off_but_cb;
  button10.timer_callback = menu_timer_power_off_but_cb;
  button10.timer_long_callback = menu_timer_long_power_off_but_cb;
}

static void menu_activate_but( menu_token_t* menu )
{
  button2_menu.arg = (void*) menu;
  button2_menu.fall_callback = menu->button.up.fall_callback;
  button2_menu.rise_callback = menu->button.up.rise_callback;
  button2_menu.timer_callback = menu->button.up.timer_callback;

  button3_menu.arg = (void*) menu;
  button3_menu.fall_callback = menu->button.down.fall_callback;
  button3_menu.rise_callback = menu->button.down.rise_callback;
  button3_menu.timer_callback = menu->button.down.timer_callback;

  button1_menu.arg = (void*) menu;
  button1_menu.fall_callback = menu->button.enter.fall_callback;
  button1_menu.rise_callback = menu->button.enter.rise_callback;
  button1_menu.timer_callback = menu->button.enter.timer_callback;

  button4_menu.arg = (void*) menu;
  button4_menu.fall_callback = menu->button.up_minus.fall_callback;
  button4_menu.rise_callback = menu->button.up_minus.rise_callback;
  button4_menu.timer_callback = menu->button.up_minus.timer_callback;

  button6_menu.arg = (void*) menu;
  button6_menu.fall_callback = menu->button.up_plus.fall_callback;
  button6_menu.rise_callback = menu->button.up_plus.rise_callback;
  button6_menu.timer_callback = menu->button.up_plus.timer_callback;

  button5_menu.arg = (void*) menu;
  button5_menu.fall_callback = menu->button.down_minus.fall_callback;
  button5_menu.rise_callback = menu->button.down_minus.rise_callback;
  button5_menu.timer_callback = menu->button.down_minus.timer_callback;

  button7_menu.arg = (void*) menu;
  button7_menu.fall_callback = menu->button.down_plus.fall_callback;
  button7_menu.rise_callback = menu->button.down_plus.rise_callback;
  button7_menu.timer_callback = menu->button.down_plus.timer_callback;

  button8_menu.arg = (void*) menu;
  button8_menu.fall_callback = menu->button.exit.fall_callback;
  button8_menu.rise_callback = menu->button.exit.rise_callback;
  button8_menu.timer_callback = menu->button.exit.timer_callback;

  button9_menu.arg = (void*) menu;
  button9_menu.fall_callback = menu->button.motor_on.fall_callback;
  button9_menu.rise_callback = menu->button.motor_on.rise_callback;
  button9_menu.timer_callback = menu->button.motor_on.timer_callback;

  button10_menu.arg = (void*) menu;
  button10_menu.fall_callback = menu->button.on_off.fall_callback;
  button10_menu.rise_callback = menu->button.on_off.rise_callback;
  button10_menu.timer_callback = menu->button.on_off.timer_callback;
}

void menu_deactivate_but( void )
{
  button1_menu.fall_callback = NULL;
  button1_menu.rise_callback = NULL;
  button1_menu.timer_callback = NULL;

  button2_menu.fall_callback = NULL;
  button2_menu.rise_callback = NULL;
  button2_menu.timer_callback = NULL;

  button3_menu.fall_callback = NULL;
  button3_menu.rise_callback = NULL;
  button3_menu.timer_callback = NULL;

  button4_menu.fall_callback = NULL;
  button4_menu.rise_callback = NULL;
  button4_menu.timer_callback = NULL;

  button5_menu.fall_callback = NULL;
  button5_menu.rise_callback = NULL;
  button5_menu.timer_callback = NULL;

  button6_menu.fall_callback = NULL;
  button6_menu.rise_callback = NULL;
  button6_menu.timer_callback = NULL;

  button7_menu.fall_callback = NULL;
  button7_menu.rise_callback = NULL;
  button7_menu.timer_callback = NULL;

  button8_menu.fall_callback = NULL;
  button8_menu.rise_callback = NULL;
  button8_menu.timer_callback = NULL;

  button9_menu.fall_callback = NULL;
  button9_menu.rise_callback = NULL;
  button9_menu.timer_callback = NULL;

  button10_menu.fall_callback = NULL;
  button10_menu.rise_callback = NULL;
  button10_menu.timer_callback = NULL;
}

static void menu_state_init( void )
{
  oled_printFixed( 2, MENU_HEIGHT + 2 * LINE_HEIGHT, dictionary_get_string( DICT_WAIT_TO_INIT ), OLED_FONT_SIZE_11 );
  oled_update();
  menu_init_buttons();
  ctx.state = MENU_STATE_IDLE;
}

static void menu_state_idle( menu_token_t* menu )
{
  if ( ctx.enter_req )
  {
    if ( ctx.new_menu == NULL )
    {
      ctx.error_flag = true;
      ctx.error_msg = "Menu is NULL";
      ctx.state = MENU_STATE_ERROR_CHECK;
      ctx.enter_req = false;
      return;
    }

    add_menu_tab( ctx.new_menu );
    ctx.state = MENU_STATE_ENTER;
    ctx.enter_req = false;
    return;
  }

  if ( menu == NULL )
  {
    oled_printFixed( 2, MENU_HEIGHT + 2 * LINE_HEIGHT, dictionary_get_string( DICT_MENU_IDLE_STATE ), OLED_FONT_SIZE_11 );
    oled_update();
    osDelay( 100 );
  }
  else
  {
    ctx.state = MENU_STATE_ENTER;
    ctx.enter_req = false;
  }
}

static void menu_state_enter( menu_token_t* menu )
{
  if ( menu->menu_cb.button_init_cb != NULL )
  {
    menu->menu_cb.button_init_cb( menu );
  }

  menu_deactivate_but();
  menu_activate_but( menu );

  if ( menu->menu_cb.enter != NULL )
  {
    menu->menu_cb.enter( menu );
  }

  ctx.state = MENU_STATE_PROCESS;
}

static void menu_state_process( menu_token_t* menu )
{
  xSemaphoreTake( ctx.update_screen_req, (TickType_t) MS2ST( 300 ) );
  oled_clearScreen();

  if ( menu->menu_cb.process != NULL )
  {
    menu->menu_cb.process( (void*) menu );
  }
  else
  {
    ctx.error_flag = true;
    ctx.error_msg = "Process cb NULL";
    ctx.state = MENU_STATE_EXIT;
  }

  drawBattery( 115, 1, battery_get_voltage(), battery_get_charging_status() );

  uint8_t signal = 0;
  if ( wifiDrvIsConnected() )
  {
    int rssi = wifiDrvGetRssi();
    if ( rssi >= -50 && rssi < 0 )
    {
      signal = 5;
    }
    else if ( rssi >= -60 && rssi < -50 )
    {
      signal = 4;
    }
    else if ( rssi >= -70 && rssi < -60 )
    {
      signal = 3;
    }
    else if ( rssi >= -80 && rssi < -70 )
    {
      signal = 2;
    }
    else
    {
      signal = 1;
    }
  }

  drawSignal( 100, 1, signal );

  oled_update();
  osDelay( 5 );

  if ( ctx.enter_req || ctx.exit_req )
  {
    ctx.state = MENU_STATE_EXIT;
  }
}

static void menu_state_emergency_disable( void )
{
  oled_clearScreen();
  oled_printFixed( 2, MENU_HEIGHT, dictionary_get_string( DICT_MENU_STOP ), OLED_FONT_SIZE_26 );    //Font_16x26
  oled_update();
  if ( ctx.led_cnt % 10 == 0 )
  {
    MOTOR_LED_SET_GREEN( 0 );
    SERVO_VIBRO_LED_SET_GREEN( 0 );
    MOTOR_LED_SET_RED( ctx.emergency_led_status );
    SERVO_VIBRO_LED_SET_RED( ctx.emergency_led_status );
    ctx.emergency_led_status = ctx.emergency_led_status ? false : true;
  }

  ctx.led_cnt++;
  osDelay( 100 );
}

static void menu_state_exit( menu_token_t* menu )
{
  menu_deactivate_but();

  if ( menu->menu_cb.exit != NULL )
  {
    menu->menu_cb.exit( menu );
  }

  if ( ctx.exit_req )
  {
    remove_last_menu_tab();
    ctx.exit_req = false;
  }

  ctx.state = MENU_STATE_ERROR_CHECK;
}

static void menu_state_error_check( menu_token_t* menu )
{
  static char buff[128];

  if ( ctx.error_flag )
  {
    oled_clearScreen();
    if ( menu != NULL )
    {
      oled_printFixed( 2, 0, dictionary_get_string( menu->name_dict ), OLED_FONT_SIZE_16 );
    }

    oled_printFixed( 2, MENU_HEIGHT, "Error menu_drv", OLED_FONT_SIZE_11 );
    sprintf( buff, "Error %d...", ctx.error_code );
    oled_printFixed( 2, MENU_HEIGHT + LINE_HEIGHT, buff, OLED_FONT_SIZE_11 );

    if ( ctx.error_msg != NULL )
    {
      sprintf( buff, "Msg: %s", ctx.error_msg );
      oled_printFixed( 2, MENU_HEIGHT + 2 * LINE_HEIGHT, buff, OLED_FONT_SIZE_11 );
    }
    else
    {
      oled_printFixed( 2, MENU_HEIGHT + 2 * LINE_HEIGHT, "Undef", OLED_FONT_SIZE_11 );
    }

    oled_update();
    ctx.error_flag = false;
    ctx.error_code = 0;
    ctx.error_msg = NULL;
    ctx.exit_req = true;
    osDelay( 1250 );
  }

  ctx.state = MENU_STATE_IDLE;
}

static void menu_state_power_off_count( menu_token_t* menu )
{
  char buff[64] = { 0 };

  if ( !ctx.power_off_req )
  {
    MOTOR_LED_SET_RED( 0 );
    SERVO_VIBRO_LED_SET_RED( 0 );
    MOTOR_LED_SET_GREEN( 0 );
    SERVO_VIBRO_LED_SET_GREEN( 0 );
    ctx.state = ctx.emergency_disable_is_active ? MENU_STATE_EMERGENCY_DISABLE : MENU_STATE_PROCESS;
    return;
  }

  int time = 0;
  TickType_t now = xTaskGetTickCount();

  if ( ctx.power_off_timer > now )
  {
    time = ST2MS( ctx.power_off_timer - now ) / 1000;
  }
  else
  {
    ctx.state = MENU_STATE_POWER_OFF;
    return;
  }

  oled_clearScreen();
  oled_printFixed( 2, 5, dictionary_get_string( DICT_POWER_OFF ), OLED_FONT_SIZE_26 );

  sprintf( buff, "%d", time );
  oled_printFixed( 52, 2 * MENU_HEIGHT, buff, OLED_FONT_SIZE_26 );

  oled_update();
  osDelay( 100 );
}

static void menu_state_power_off( menu_token_t* menu )
{
  power_on_disable_system();

  oled_clearScreen();
  oled_update();
  osDelay( 100 );
}

static void menu_task( void* arg )
{
  menu_token_t* menu = NULL;
  int prev_state = -1;

  ctx.state = MENU_STATE_INIT;
  while ( 1 )
  {
    // esp_task_wdt_reset();
    menu = last_tab_element();
    if ( prev_state != ctx.state )
    {
      if ( menu != NULL )
      {
        LOG( PRINT_INFO, "state: %s, menu %s", state_name[ctx.state], dictionary_get_string( menu->name_dict ) );
      }
      else
      {
        LOG( PRINT_INFO, "state %s, menu is NULL", state_name[ctx.state] );
      }

      prev_state = ctx.state;
    }

    switch ( ctx.state )
    {
      case MENU_STATE_INIT:
        menu_state_init();
        break;

      case MENU_STATE_IDLE:
        menu_state_idle( menu );
        break;

      case MENU_STATE_ENTER:
        menu_state_enter( menu );
        break;

      case MENU_STATE_EXIT:
        menu_state_exit( menu );
        break;

      case MENU_STATE_PROCESS:
        menu_state_process( menu );
        save_process();
        break;

      case MENU_STATE_INFO:

        break;

      case MENU_STATE_POWER_OFF_COUNT:
        menu_state_power_off_count( menu );
        break;

      case MENU_STATE_POWER_OFF:
        menu_state_power_off( menu );
        break;

      case MENU_STATE_EMERGENCY_DISABLE:
        menu_state_emergency_disable();
        break;

      case MENU_STATE_ERROR_CHECK:
        menu_state_error_check( menu );
        break;

      default:
        ctx.state = MENU_STATE_IDLE;
        break;
    }
  }
}

void menuEnter( menu_token_t* menu )
{
  ctx.new_menu = menu;
  ctx.enter_req = true;
  update_screen();
}

void menuSetMain( menu_token_t* menu )
{
  for ( uint8_t i = 0; i < MENU_TAB_SIZE; i++ )
  {
    ctx.entered_menu_tab[i] = NULL;
  }

  ctx.entered_menu_tab[0] = menu;
  if ( ctx.state != MENU_STATE_INIT )
  {
    ctx.state = MENU_STATE_IDLE;
  }
}

void menuDrv_Exit( menu_token_t* menu )
{
  ctx.exit_req = true;
  update_screen();
}

void menuPrintfInfo( const char* format, ... )
{
  static char infoBuff[256];
  va_list ap;

  va_start( ap, format );
  vsnprintf( infoBuff, sizeof( infoBuff ), format, ap );
  va_end( ap );

  oled_printFixed( 2, MENU_HEIGHT, infoBuff, OLED_FONT_SIZE_11 );
}

void menuDrvEnterEmergencyDisable( void )
{
  ctx.last_state = ctx.state;
  ctx.state = MENU_STATE_EMERGENCY_DISABLE;
  ctx.emergency_led_status = true;
  ctx.emergency_disable_is_active = true;
  MOTOR_LED_SET_GREEN( 0 );
  SERVO_VIBRO_LED_SET_GREEN( 0 );
  ctx.led_cnt = 0;
  menu_deactivate_but();
}

void menuDrvExitEmergencyDisable( void )
{
  ctx.state = ctx.last_state;
  menu_token_t* menu = last_tab_element();

  ctx.emergency_disable_is_active = false;

  MOTOR_LED_SET_RED( 0 );
  SERVO_VIBRO_LED_SET_RED( 0 );

  if ( menu != NULL )
  {
    menu_activate_but( menu );
  }
}

void menuDrvDisableSystemProcess( void )
{
  while ( 1 )
  {
    oled_clearScreen();
    oled_update();
  }
}

void menuDrvInit( menu_drv_init_t init_type )
{
  ctx.update_screen_req = xSemaphoreCreateBinary();
  ctx.block_power_off_timer = MS2ST( POWER_OFF_BLOCK_MS ) + xTaskGetTickCount();

  if ( init_type == MENU_DRV_LOW_BATTERY_INIT )
  {
    mainMenuInit( MENU_DRV_LOW_BATTERY_INIT );
  }
  else
  {
    if ( parameters_getValue( PARAM_BOOT_UP_SYSTEM ) )
    {
      menuInitBootUpMenu();
    }
    else
    {
      mainMenuInit( MENU_DRV_NORMAL_INIT );
    }
  }

#if CONFIG_MENU_TEST_TASK
  xTaskCreate( menu_test, "menu_test", 8192, NULL, 12, NULL );
#else
  xTaskCreate( menu_task, "menu_task", 8192, NULL, 12, NULL );
#endif
  menuBackendInit();
  update_screen();
}
