#include "measure.h"

#include <stdint.h>

#include "app_config.h"
#include "cmd_server.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/timers.h"
#include "measure.h"
#include "parameters.h"
#include "parse_cmd.h"
#include "ultrasonar.h"

#define MODULE_NAME "[Meas] "
#define DEBUG_LVL   PRINT_WARNING

#if CONFIG_DEBUG_MEASURE
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

static const adc_bitwidth_t width = ADC_BITWIDTH_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11;

#define ADC_IN_CH    ADC_CHANNEL_6
#define ADC_MOTOR_CH ADC_CHANNEL_7
#define ADC_SERVO_CH ADC_CHANNEL_4
#define ADC_12V_CH   ADC_CHANNEL_5
#define ADC_CE_CH    ADC_CHANNEL_0

#ifndef ADC_REFRES
#define ADC_REFRES 4096
#endif

#define DEFAULT_VREF  1100    // Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES 64    // Multisampling

#define SILOS_START_MEASURE 100

typedef struct
{
  char* ch_name;
  adc_channel_t channel;
  adc_unit_t unit;
  uint32_t adc;
  uint32_t filtered_adc;
  uint32_t filter_table[FILTER_TABLE_SIZE];
  float meas_voltage;
} meas_data_t;

static meas_data_t meas_data[MEAS_CH_LAST] =
  {
    [MEAS_CH_IN] = {.unit = ADC_UNIT_1,  .channel = ADC_IN_CH,  .ch_name = "MEAS_CH_IN" },
 // [MEAS_CH_MOTOR] = { .unit = ADC_UNIT_1, .channel = ADC_MOTOR_CH, .ch_name = "MEAS_CH_MOTOR"},
    [MEAS_CH_12V] = { .unit = ADC_UNIT_1, .channel = ADC_12V_CH, .ch_name = "MEAS_CH_12V"},
};

static uint32_t table_size;
static uint32_t table_iter;

static uint32_t filtered_value( uint32_t* tab, uint8_t size )
{
  uint16_t ret_val = *tab;

  for ( uint8_t i = 1; i < size; i++ )
  {
    ret_val = ( ret_val + tab[i] ) / 2;
  }

  return ret_val;
}

void init_measure( void )
{
}

static void _read_adc_values( adc_oneshot_unit_handle_t adc1_handle, adc_oneshot_unit_handle_t adc2_handle )
{
  for ( uint8_t ch = 0; ch < MEAS_CH_LAST; ch++ )
  {
    meas_data[ch].adc = 0;
    // Multisampling
    for ( int i = 0; i < NO_OF_SAMPLES; i++ )
    {
      int adc_reading = 0;
      ESP_ERROR_CHECK( adc_oneshot_read( meas_data[ch].unit == ADC_UNIT_1 ? adc1_handle : adc2_handle, meas_data[ch].channel, &adc_reading ) );
      LOG( PRINT_DEBUG, "ADC%d Channel[%d] Raw Data: %d", meas_data[ch].unit + 1, meas_data[ch].channel, adc_reading );
      meas_data[ch].adc += adc_reading;
    }

    meas_data[ch].adc /= NO_OF_SAMPLES;
    meas_data[ch].filter_table[table_iter % FILTER_TABLE_SIZE] = meas_data[ch].adc;
    meas_data[ch].filtered_adc = filtered_value( &meas_data[ch].adc, table_size );
  }

  table_iter++;
  if ( table_size < FILTER_TABLE_SIZE - 1 )
  {
    table_size++;
  }
}

static void measure_process( void* arg )
{
  (void) arg;
  //-------------ADC1 Init---------------//
  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
  };
  ESP_ERROR_CHECK( adc_oneshot_new_unit( &init_config1, &adc1_handle ) );

  //-------------ADC1 Config---------------//
  adc_oneshot_chan_cfg_t config = {
    .bitwidth = width,
    .atten = atten,
  };

  for ( int i = 0; i < MEAS_CH_LAST; i++ )
  {
    if ( meas_data[i].unit == ADC_UNIT_1 )
    {
      ESP_ERROR_CHECK( adc_oneshot_config_channel( adc1_handle, meas_data[i].channel, &config ) );
    }
  }

  //-------------ADC2 Init---------------//
  adc_oneshot_unit_handle_t adc2_handle;
  adc_oneshot_unit_init_cfg_t init_config2 = {
    .unit_id = ADC_UNIT_2,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK( adc_oneshot_new_unit( &init_config2, &adc2_handle ) );

  //-------------ADC2 Config---------------//
  for ( int i = 0; i < MEAS_CH_LAST; i++ )
  {
    if ( meas_data[i].unit == ADC_UNIT_2 )
    {
      ESP_ERROR_CHECK( adc_oneshot_config_channel( adc2_handle, meas_data[i].channel, &config ) );
    }
  }

  while ( 1 )
  {
    vTaskDelay( MS2ST( 100 ) );

    _read_adc_values( adc1_handle, adc2_handle );

    if ( ultrasonar_is_connected() )
    {
      uint32_t silos_height = parameters_getValue( PARAM_SILOS_HEIGHT ) * 10;
      uint32_t silos_distance = ultrasonar_get_distance() > SILOS_START_MEASURE ? ultrasonar_get_distance() - SILOS_START_MEASURE : 0;
      if ( silos_distance > silos_height )
      {
        silos_distance = silos_height;
      }

      int silos_percent = ( silos_height - silos_distance ) * 100 / silos_height;
      if ( ( silos_percent < 0 ) || ( silos_percent > 100 ) )
      {
        silos_percent = 0;
      }
      uint32_t silos_is_low = silos_percent < 10;
      LOG( PRINT_INFO, "Silos %d %d", silos_percent, silos_is_low );
      parameters_setValue( PARAM_LOW_LEVEL_SILOS, silos_is_low );
      parameters_setValue( PARAM_SILOS_LEVEL, (uint32_t) silos_percent );
      parameters_setValue( PARAM_SILOS_SENSOR_IS_CONNECTED, 1 );
    }
    else
    {
      parameters_setValue( PARAM_SILOS_SENSOR_IS_CONNECTED, 0 );
      parameters_setValue( PARAM_LOW_LEVEL_SILOS, 0 );
      parameters_setValue( PARAM_SILOS_LEVEL, 0 );
    }

    parameters_setValue( PARAM_VOLTAGE_ACCUM, (uint32_t) ( accum_get_voltage() * 10000.0 ) );
  }
}

void measure_start( void )
{
  xTaskCreate( measure_process, "measure_process", 4096, NULL, 10, NULL );
  init_measure();
}

void measure_meas_calibration_value( void )
{
  LOG( PRINT_INFO, "%s", __func__ );
}

uint32_t measure_get_filtered_value( enum_meas_ch type )
{
  if ( type < MEAS_CH_LAST )
  {
    return meas_data[type].filtered_adc;
  }

  return 0;
}

float measure_get_temperature( void )
{
  //   int temp = -( (int) measure_get_filtered_value( MEAS_CH_TEMP ) ) / 31 + 100;
  return 0;
}

float accum_get_voltage( void )
{
  float voltage = 0;
  voltage = (float) measure_get_filtered_value( MEAS_CH_12V ) / 4096.0 / 2.6;
  return voltage;
}
