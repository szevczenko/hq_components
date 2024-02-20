#include "power_on.h"

#include <stdio.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "menu_backend.h"
#include "menu_drv.h"
#include "parameters.h"

#define MODULE_NAME "[Power] "
#define DEBUG_LVL   PRINT_WARNING

#if CONFIG_DEBUG_CMD_CLIENT
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define POWER_HOLD_PIN 13

#define POWER_OFF_TIME_MIN parameters_getValue( PARAM_POWER_ON_MIN )

enum state_t
{
  STATE_IDLE,
  STATE_WAIT_TO_DISABLE,
  STATE_DISABLE_SYSTEM,
  STATE_OFF,
  STATE_TOP,
};

static const char* _state_name[] =
  {
    [STATE_IDLE] = "STATE_IDLE",
    [STATE_WAIT_TO_DISABLE] = "STATE_WAIT_TO_DISABLE",
    [STATE_DISABLE_SYSTEM] = "STATE_DISABLE_SYSTEM",
    [STATE_OFF] = "STATE_OFF" };

struct power_off_context
{
  enum state_t state;
  TickType_t power_off_timer;
};

static struct power_off_context ctx;

static void _change_state( enum state_t new_state )
{
  if ( new_state < STATE_TOP )
  {
    ctx.state = new_state;
    LOG( PRINT_INFO, "State -> %s", _state_name[new_state] );
  }
  else
  {
    LOG( PRINT_ERROR, "Error change state: %d", new_state );
  }
}

static void _state_idle( void )
{
  if ( false == backendIsConnected() )
  {
    ctx.power_off_timer = xTaskGetTickCount() + MS2ST( POWER_OFF_TIME_MIN * 60 * 1000 );
    _change_state( STATE_WAIT_TO_DISABLE );
    return;
  }
}

static void _state_wait_to_disable( void )
{
  if ( backendIsConnected() )
  {
    _change_state( STATE_IDLE );
    return;
  }

  LOG( PRINT_DEBUG, "Time to off %d ms", ST2MS( ctx.power_off_timer - xTaskGetTickCount() ) );

  if ( ctx.power_off_timer < xTaskGetTickCount() )
  {
    _change_state( STATE_DISABLE_SYSTEM );
  }
}

static void _state_disable_system( void )
{
  power_on_disable_system();
  _change_state( STATE_OFF );
}

static void _state_off( void )
{
  menuDrvDisableSystemProcess();
}

static void _power_on_task( void* arg )
{
  while ( 1 )
  {
    switch ( ctx.state )
    {
      case STATE_IDLE:
        _state_idle();
        break;

      case STATE_WAIT_TO_DISABLE:
        _state_wait_to_disable();
        break;

      case STATE_DISABLE_SYSTEM:
        _state_disable_system();
        break;

      case STATE_OFF:
        _state_off();
        break;

      default:
        ctx.state = STATE_IDLE;
    }

    osDelay( 1000 );
  }
}

void power_on_init( void )
{
  gpio_config_t io_conf = { 0 };

  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = ( 1 << POWER_HOLD_PIN );
  gpio_config( &io_conf );
}

void power_on_enable_system( void )
{
  gpio_set_level( POWER_HOLD_PIN, 1 );
}

void power_on_disable_system( void )
{
  MOTOR_LED_SET_RED( 0 );
  SERVO_VIBRO_LED_SET_RED( 0 );
  MOTOR_LED_SET_GREEN( 0 );
  SERVO_VIBRO_LED_SET_GREEN( 0 );
  gpio_set_level( POWER_HOLD_PIN, 0 );
}

void power_on_reset_timer( void )
{
  if ( ctx.state == STATE_WAIT_TO_DISABLE )
  {
    _change_state( STATE_IDLE );
  }
}

void power_on_start_task( void )
{
  xTaskCreate( _power_on_task, "_power_on_task", 2056, NULL, NORMALPRIO, NULL );
}
