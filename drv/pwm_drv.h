/**
 *******************************************************************************
 * @file    pwm_drv.h
 * @author  Dmytro Shevchenko
 * @brief   PWM driver
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _PWM_DRV_H_
#define _PWM_DRV_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "error_code.h"

/* Public types --------------------------------------------------------------*/

typedef enum
{
  PWM_DRV_DUTY_MODE_HIGH = 0, /*!<Active high duty, i.e. duty cycle proportional to high time for asymmetric MCPWM*/
  PWM_DRV_DUTY_MODE_LOW, /*!<Active low duty,  i.e. duty cycle proportional to low  time for asymmetric MCPWM, out of phase(inverted) MCPWM*/
} pwm_drv_mode_t;

typedef struct
{
  const char* name;
  float duty;
  pwm_drv_mode_t mode;
  uint32_t frequency;
  uint32_t ticks_period;
  uint8_t pin;
  uint8_t timer_group;

  /* Private variables */
  void* timer;
  void* comparator;
  void* generator;
  void* oper;
} pwm_drv_t;

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init pwm drv.
 * @param   [in] dev - device pointer driver
 * @param   [in] name - sensor name
 * @param   [in] mode - mode active low or high
 * @param   [in] frequency - frequency pwm in [Hz]
 * @param   [in] timer_group - ESP32 timer group
 * @param   [in] pin - GPIO number
 */
void PWMDrv_Init( pwm_drv_t* dev, const char* name, pwm_drv_mode_t mode, uint32_t frequency, uint8_t timer_group, uint8_t pin );

/**
 * @brief   Set duty.
 * @param   [in] dev - device pointer driver
 * @param   [in] duty - duty in percent [0-100]
 * @return  error code
 */
error_code_t PWMDrv_SetDuty( pwm_drv_t* dev, float duty );

/**
 * This function not ready
 * @brief   Set frequency.
 * @param   [in] dev - set frequency
 * @param   [in] alert_value - alert value
 * @return  error code
 */
error_code_t PWMDrv_SetFrequency( pwm_drv_t* dev, uint32_t frequency );

/**
 * @brief   Stop PWM
 * @param   [in] dev - device pointer driver
 * @param   [in] is_high - is high output pin state
 * @return  error code
 */
error_code_t PWMDrv_Stop( pwm_drv_t* dev, bool is_high );

#endif