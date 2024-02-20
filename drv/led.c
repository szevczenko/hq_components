/*
 * but.c
 *
 *  Author: Demetriusz
 */
#include "led.h"

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "parameters.h"
#include "pwm_drv.h"

#define MODULE_NAME "[LED] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_BUTTON
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define PWM_UNIT MCPWM_UNIT_0
#define PWM_DUTY _get_brightness()

static pwm_drv_t pwm_drv[LED_AMOUNT];

static float _get_brightness( void )
{
  uint32_t value = parameters_getValue( PARAM_BRIGHTNESS );
  if ( value < 1 || value >= 10 )
  {
    return 100.0;
  }
  else
  {
    return (float) value * 10.0;
  }
}

static void _set_led( LEDs_t led, bool on_off )
{
  if ( on_off )
  {
    PWMDrv_SetDuty( &pwm_drv[led], PWM_DUTY );
  }
  else
  {
    PWMDrv_Stop( &pwm_drv[led], false );
  }
}

void set_motor_green_led( bool on_off )
{
  _set_led( LED_UPPER_GREEN, on_off );
}

void set_servo_green_led( bool on_off )
{
  _set_led( LED_BOTTOM_GREEN, on_off );
}

void set_motor_red_led( bool on_off )
{
  _set_led( LED_UPPER_RED, on_off );
}

void set_servo_red_led( bool on_off )
{
  _set_led( LED_BOTTOM_RED, on_off );
}

void init_leds( void )
{
  PWMDrv_Init( &pwm_drv[LED_UPPER_RED], "up_red", PWM_DRV_DUTY_MODE_HIGH, 10000, 0, MOTOR_LED_RED );
  PWMDrv_Init( &pwm_drv[LED_BOTTOM_RED], "down_red", PWM_DRV_DUTY_MODE_HIGH, 10000, 0, SERVO_VIBRO_LED_RED );
  PWMDrv_Init( &pwm_drv[LED_UPPER_GREEN], "up_green", PWM_DRV_DUTY_MODE_HIGH, 10000, 0, MOTOR_LED_GREEN );
  PWMDrv_Init( &pwm_drv[LED_BOTTOM_GREEN], "down_green", PWM_DRV_DUTY_MODE_HIGH, 10000, 1, SERVO_VIBRO_LED_GREEN );
}
