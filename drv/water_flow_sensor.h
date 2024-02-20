/**
 *******************************************************************************
 * @file    water_flow_sensor.h
 * @author  Dmytro Shevchenko
 * @brief   Water flow water sensor header file
 *******************************************************************************
 */

/* Define to prevent recursive inclusion ------------------------------------*/

#ifndef _WATER_FLOW_SENSOR_H_
#define _WATER_FLOW_SENSOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "error_code.h"

/* Public macro --------------------------------------------------------------*/

#define WATER_FLOW_CONVERT_L_TO_CL( l )  ( l * 100 )
#define WATER_FLOW_CONVERT_CL_TO_L( cl ) ( cl / 100 )

/* Public types --------------------------------------------------------------*/

typedef enum
{
  WATER_FLOW_SENSOR_STATE_IDLE,
  WATER_FLOW_SENSOR_STATE_START_MEASURE,
  WATER_FLOW_SENSOR_STATE_NO_WATER_SHORT_PERIOD,
  WATER_FLOW_SENSOR_STATE_NO_WATER_LONG_PERIOD,
  WATER_FLOW_SENSOR_STATE_LAST
} water_flow_sensor_state_t;

typedef enum
{
  WATER_FLOW_SENSOR_EVENT_ALERT_VALUE,
  WATER_FLOW_SENSOR_EVENT_NO_WATER_SHORT_PERIOD,
  WATER_FLOW_SENSOR_EVENT_NO_WATER_LONG_PERIOD,
  WATER_FLOW_SENSOR_EVENT_WATER_FLOW_BACK,
  WATER_FLOW_SENSOR_EVENT_LAST
} water_flow_sensor_event_t;

typedef void ( *water_flow_sensor_event_cb )( water_flow_sensor_event_t event, uint32_t value );

typedef struct
{
  const char* name;
  uint32_t counter;
  uint32_t value_cl;    // centi litre: 1 l = 100 cl
  uint32_t pulses_per_liter;
  water_flow_sensor_event_cb event_cb;
  uint32_t alert_value_l;
  uint8_t pin;
  void* timer;
  water_flow_sensor_state_t state;
} water_flow_sensor_t;

/* Public functions ----------------------------------------------------------*/

/**
 * @brief   Init input device.
 * @param   [in] dev - device pointer driver
 * @param   [in] name - sensor name
 * @param   [in] pulses_per_liter - pulses per liter
 * @param   [in] event_cb - alert callback
 * @param   [in] pin - GPIO number
 */
void WaterFlowSensor_Init( water_flow_sensor_t* dev, const char* name, uint32_t pulses_per_liter, water_flow_sensor_event_cb event_cb, uint8_t pin );

/**
 * @brief   Water flow sensor reset value.
 * @param   [in] dev - device pointer driver
 */
error_code_t WaterFlowSensor_ResetValue( water_flow_sensor_t* dev );

/**
 * @brief   Water flow sensor start measure water flow.
 *          Start timer with event 'no water'
 * @param   [in] dev - device pointer driver
 */
error_code_t WaterFlowSensor_StartMeasure( water_flow_sensor_t* dev );

/**
 * @brief   Water flow sensor stop measure water flow.
 *          Stop timer with event 'no water'
 * @param   [in] dev - device pointer driver
 */
error_code_t WaterFlowSensor_StopMeasure( water_flow_sensor_t* dev );

/**
 * @brief   Water flow sensor set alert value.
 * @param   [in] dev - device pointer driver
 * @param   [in] alert_value_l - alert value in liters
 * @return  error code
 */
error_code_t WaterFlowSensor_SetAlertValue( water_flow_sensor_t* dev, uint32_t alert_value_l );

/**
 * @brief   Water flow sensor get value in centiliters.
 * @param   [in] dev - device pointer driver
 * @return  measured flow in centi liters
 */
uint32_t WaterFlowSensor_GetValue( water_flow_sensor_t* dev );

/**
 * @brief   Water flow sensor set pulses per liter.
 * @param   [in] dev - device pointer driver
 * @param   [in] pulses_per_liter - pulses per liter
 */
void WaterFlowSensor_SetPulsesPerLiter( water_flow_sensor_t* dev, uint32_t pulses_per_liter );

/**
 * @brief   Water flow sensor set alert value.
 * @param   [in] dev - device pointer driver
 * @param   [out] buffer - buffer where write data
 * @param   [in] buffer_size - buffer size
 * @param   [in] is_add_comma - if true add comma on start of msg
 * 
 * @return  error code
 */
size_t WaterFlowSensor_GetStr( water_flow_sensor_t* dev, char* buffer, size_t buffer_size, bool is_add_comma );

#endif