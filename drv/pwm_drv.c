/**
 *******************************************************************************
 * @file    pwm_drv.c
 * @author  Dmytro Shevchenko
 * @brief   PWM driver source file
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "pwm_drv.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "esp_log.h"

/* Private macros ------------------------------------------------------------*/
#define MODULE_NAME "[Dev] "
#define DEBUG_LVL   PRINT_DEBUG
#define TAG         MODULE_NAME

#if CONFIG_DEBUG_DEVICE_MANAGER
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define ARRAY_SIZE( _array )   sizeof( _array ) / sizeof( _array[0] )
#define TIMEBASE_RESOLUTION_HZ 1000000    // 1MHz, 1us per tick
#define TIMEBASE_PERIOD        20000    // 20000 ticks, 20ms

/* Private functions ---------------------------------------------------------*/

static void _init_pwm( pwm_drv_t* dev )
{
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = ( 1ULL << dev->pin );
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 1;
  gpio_config( &io_conf );
  /* Timer init */
  mcpwm_timer_handle_t timer = NULL;
  dev->ticks_period = TIMEBASE_RESOLUTION_HZ / dev->frequency;
  mcpwm_timer_config_t timer_config = {
    .group_id = dev->timer_group,
    .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
    .resolution_hz = TIMEBASE_RESOLUTION_HZ,
    .period_ticks = dev->ticks_period,
    .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
  };
  ESP_ERROR_CHECK( mcpwm_new_timer( &timer_config, &timer ) );
  dev->timer = (void*) timer;

  /* Oper init */
  mcpwm_oper_handle_t oper = NULL;
  mcpwm_operator_config_t operator_config = {
    .group_id = dev->timer_group,    // operator must be in the same group to the timer
  };
  ESP_ERROR_CHECK( mcpwm_new_operator( &operator_config, &oper ) );
  dev->oper = (void*) oper;

  ESP_LOGI( TAG, "Connect timer and operator" );
  ESP_ERROR_CHECK( mcpwm_operator_connect_timer( oper, timer ) );

  ESP_LOGI( TAG, "Create comparator and generator from the operator" );
  mcpwm_cmpr_handle_t comparator = NULL;
  mcpwm_comparator_config_t comparator_config = {
    .flags.update_cmp_on_tez = true,
  };
  ESP_ERROR_CHECK( mcpwm_new_comparator( oper, &comparator_config, &comparator ) );
  dev->comparator = (void*) comparator;

  mcpwm_gen_handle_t generator = NULL;
  mcpwm_generator_config_t generator_config = {
    .gen_gpio_num = dev->pin,
  };
  ESP_ERROR_CHECK( mcpwm_new_generator( oper, &generator_config, &generator ) );
  dev->generator = (void*) generator;

  // set the initial compare value, so that the servo will spin to the center position
  ESP_ERROR_CHECK( mcpwm_comparator_set_compare_value( comparator, 0 ) );

  ESP_LOGI( TAG, "Set generator action on timer and compare event" );
  if ( dev->mode == PWM_DRV_DUTY_MODE_HIGH )
  {
    // go high on counter empty
    ESP_ERROR_CHECK( mcpwm_generator_set_action_on_timer_event( generator,
                                                                MCPWM_GEN_TIMER_EVENT_ACTION( MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH ) ) );
    // go low on compare threshold
    ESP_ERROR_CHECK( mcpwm_generator_set_action_on_compare_event( generator,
                                                                  MCPWM_GEN_COMPARE_EVENT_ACTION( MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW ) ) );
  }
  else
  {
    // go high on counter empty
    ESP_ERROR_CHECK( mcpwm_generator_set_action_on_timer_event( generator,
                                                                MCPWM_GEN_TIMER_EVENT_ACTION( MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW ) ) );
    // go low on compare threshold
    ESP_ERROR_CHECK( mcpwm_generator_set_action_on_compare_event( generator,
                                                                  MCPWM_GEN_COMPARE_EVENT_ACTION( MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_HIGH ) ) );
  }
  ESP_ERROR_CHECK( mcpwm_timer_enable( timer ) );
}

/* Public functions ---------------------------------------------------------*/

void PWMDrv_Init( pwm_drv_t* dev, const char* name, pwm_drv_mode_t mode, uint32_t frequency, uint8_t timer_group, uint8_t pin )
{
  dev->name = name;
  dev->mode = mode;
  dev->frequency = frequency;
  dev->pin = pin;
  dev->timer_group = timer_group;
  _init_pwm( dev );
}

error_code_t PWMDrv_SetDuty( pwm_drv_t* dev, float duty )
{
  mcpwm_cmpr_handle_t comparator = (mcpwm_cmpr_handle_t) dev->comparator;
  mcpwm_timer_handle_t timer = (mcpwm_timer_handle_t) dev->timer;
  mcpwm_timer_start_stop( timer, MCPWM_TIMER_START_NO_STOP );
  ESP_ERROR_CHECK( mcpwm_comparator_set_compare_value( comparator, dev->ticks_period * duty / 100 ) );
  return ERROR_CODE_OK;
}

error_code_t PWMDrv_SetFrequency( pwm_drv_t* dev, uint32_t frequency )
{
  return ERROR_CODE_OK;
}

error_code_t PWMDrv_Stop( pwm_drv_t* dev, bool is_high )
{
  mcpwm_timer_handle_t timer = (mcpwm_timer_handle_t) dev->timer;
  mcpwm_timer_start_stop( timer, MCPWM_TIMER_STOP_EMPTY );
  gpio_set_level( dev->pin, is_high );
  return ERROR_CODE_OK;
}
