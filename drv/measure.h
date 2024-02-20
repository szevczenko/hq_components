#ifndef _MEASURE_H
#define _MEASURE_H

#include "app_config.h"

#define ACCUMULATOR_SIZE_TAB         20
#define ACCUMULATOR_ADC_CH           0    //0.028 [V/adc]
#define ACCUMULATOR_HIGH_VOLTAGE     600
#define ACCUMULATOR_LOW_VOLTAGE      395
#define ACCUMULATOR_VERY_LOW_VOLTAGE 350

#define FILTER_TABLE_SIZE   8
#define FILTER_TABLE_S_SIZE 10

#define MOTOR_ADC_CH 2
#define SERVO_ADC_CH 1    //1

typedef enum
{
  MEAS_MOTOR,
  MEAS_SERVO,
  MEAS_TEMPERATURE,
  MEAS_ACCUM
} _type_measure;

typedef enum
{
  MEAS_CH_IN,
  // MEAS_CH_MOTOR,
  MEAS_CH_12V,
  // MEAS_CH_TEMP,
  MEAS_CH_LAST
} enum_meas_ch;

void init_measure( void );
void measure_start( void );
void measure_meas_calibration_value( void );
uint32_t measure_get_filtered_value( enum_meas_ch type );
float accum_get_voltage( void );
float measure_get_temperature( void );

#endif