#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_on.h"

#define MODULE_NAME "[Battery] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_BATTERY
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define DEFAULT_VREF       1100    // Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES      64    // Multisampling
#define CHARGER_STATUS_PIN 35

static const adc_channel_t channel = ADC_CHANNEL_6;    // GPIO34 if ADC1, GPIO14 if ADC2
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;
static const adc_bitwidth_t width = ADC_BITWIDTH_12;

// max ADC: 375 - 4.2 V

#define MIN_ADC             290
#define MAX_ADC_FOR_MAX_VOL 2000
#define MIN_VOL             3200
#define CRITICAL_VOLTAGE    3000
#define MAX_VOL             4200

#define ADC_BUFFOR_SIZE 32
static uint32_t voltage;
static uint32_t voltage_sum;
static uint32_t voltage_average;
static uint32_t voltage_table[ADC_BUFFOR_SIZE];
static uint32_t voltage_measure_cnt;
static uint32_t voltage_table_size;
static bool voltage_is_measured;

static bool _adc_calibration_init( adc_unit_t u, adc_channel_t ch, adc_atten_t at, adc_cali_handle_t* out_handle )
{
  adc_cali_handle_t handle = NULL;
  esp_err_t ret = ESP_FAIL;
  bool calibrated = false;

  if ( !calibrated )
  {
    LOG( PRINT_INFO, "calibration scheme version is %s", "Line Fitting" );
    adc_cali_line_fitting_config_t cali_config = {
      .unit_id = u,
      .atten = at,
      .bitwidth = width,
    };
    ret = adc_cali_create_scheme_line_fitting( &cali_config, &handle );
    if ( ret == ESP_OK )
    {
      calibrated = true;
    }
  }

  *out_handle = handle;
  if ( ret == ESP_OK )
  {
    LOG( PRINT_INFO, "Calibration Success" );
  }
  else if ( ret == ESP_ERR_NOT_SUPPORTED || !calibrated )
  {
    LOG( PRINT_ERROR, "eFuse not burnt, skip software calibration" );
  }
  else
  {
    LOG( PRINT_ERROR, "Invalid arg or no memory" );
  }

  return calibrated;
}

static void adc_task()
{
  //-------------ADC1 Init---------------//
  adc_oneshot_unit_handle_t adc1_handle;
  adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = unit,
  };
  ESP_ERROR_CHECK( adc_oneshot_new_unit( &init_config1, &adc1_handle ) );

  //-------------ADC1 Config---------------//
  adc_oneshot_chan_cfg_t config = {
    .bitwidth = width,
    .atten = atten,
  };
  ESP_ERROR_CHECK( adc_oneshot_config_channel( adc1_handle, channel, &config ) );

  //-------------ADC1 Calibration Init---------------//
  adc_cali_handle_t adc1_cali_chan0_handle = NULL;
  bool do_calibration1_chan0 = _adc_calibration_init( unit, channel, atten, &adc1_cali_chan0_handle );

  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.pull_down_en = 0;
  io_conf.pin_bit_mask = BIT64( CHARGER_STATUS_PIN );
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = 0;
  gpio_config( &io_conf );

  while ( 1 )
  {
    int adc_reading = 0;
    int voltage_meas = 0;
    // Multisampling
    for ( int i = 0; i < NO_OF_SAMPLES; i++ )
    {
      ESP_ERROR_CHECK( adc_oneshot_read( adc1_handle, channel, &adc_reading ) );
      LOG( PRINT_DEBUG, "ADC%d Channel[%d] Raw Data: %d", unit + 1, channel, adc_reading );
      if ( do_calibration1_chan0 )
      {
        ESP_ERROR_CHECK( adc_cali_raw_to_voltage( adc1_cali_chan0_handle, adc_reading, &voltage_meas ) );
        LOG( PRINT_DEBUG, "ADC%d Channel[%d] Cali Voltage: %d mV", unit + 1, channel, voltage_meas );
      }
    }

    adc_reading /= NO_OF_SAMPLES;
    // Convert adc_reading to voltage in mV

    LOG( PRINT_DEBUG, "Raw: %d\tVoltage: %dmV\n", adc_reading, voltage );

    voltage = MAX_VOL * voltage_meas / MAX_ADC_FOR_MAX_VOL;

    voltage_table[voltage_measure_cnt % ADC_BUFFOR_SIZE] = voltage;
    voltage_measure_cnt++;
    if ( voltage_table_size < ADC_BUFFOR_SIZE )
    {
      voltage_table_size++;
    }

    voltage_sum = 0;
    for ( uint8_t i = 0; i < voltage_table_size; i++ )
    {
      voltage_sum += voltage_table[i];
    }

    voltage_average = voltage_sum / voltage_table_size;

    voltage_is_measured = true;

    if ( voltage < CRITICAL_VOLTAGE )
    {
      LOG( PRINT_INFO, "Found critical battery voltage. Power off" );
      power_on_disable_system();
    }

    LOG( PRINT_DEBUG, "Average: %d measured %d", voltage_average, voltage );
    vTaskDelay( MS2ST( 1000 ) );
  }
}

bool battery_is_measured( void )
{
  return voltage_is_measured;
}

float battery_get_voltage( void )
{
  return (float) voltage_average / 1000;
}

bool battery_get_charging_status( void )
{
  return gpio_get_level( CHARGER_STATUS_PIN ) == 0;
}

void battery_init( void )
{
  xTaskCreate( adc_task, "adc_task", 4096, NULL, 5, NULL );
}
