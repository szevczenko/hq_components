/**
 *******************************************************************************
 * @file    water_flow_sensor.c
 * @author  Dmytro Shevchenko
 * @brief   Water flow sensor source file
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/

#include "water_flow_sensor.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "freertos/timers.h"

/* Private macros ------------------------------------------------------------*/
#define MODULE_NAME "[Dev] "
#define DEBUG_LVL   PRINT_DEBUG

#if CONFIG_DEBUG_DEVICE_MANAGER
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define ARRAY_SIZE( _array ) sizeof( _array ) / sizeof( _array[0] )
#define SHORT_PERIOD_MS      10000
#define LONG_PERIOD_MS       60000

/* Private functions declaration ---------------------------------------------*/

static void _set_alert( void* user_data, bool value );
static void _set_reset( void* user_data, bool value );

/* Private variables ---------------------------------------------------------*/

// static json_parse_token_t output_tokens[] = {
//   {.bool_cb = _set_alert,
//    .name = "alert"},
//   { .bool_cb = _set_reset,
//    .name = "reset"},
// };

/* Private functions ---------------------------------------------------------*/

static void IRAM_ATTR gpio_isr_handler( void* arg )
{
  water_flow_sensor_t* dev = (water_flow_sensor_t*) arg;
  dev->counter++;
  dev->value_cl = WATER_FLOW_CONVERT_L_TO_CL( dev->counter ) / dev->pulses_per_liter;
  if ( dev->state != WATER_FLOW_SENSOR_STATE_IDLE )
  {
    portBASE_TYPE HPTaskAwoken = pdFALSE;
    if ( dev->state == WATER_FLOW_SENSOR_STATE_NO_WATER_LONG_PERIOD || dev->state == WATER_FLOW_SENSOR_STATE_NO_WATER_SHORT_PERIOD )
    {
      dev->state = WATER_FLOW_SENSOR_STATE_START_MEASURE;
      dev->event_cb( WATER_FLOW_SENSOR_EVENT_WATER_FLOW_BACK, 0 );
      xTimerChangePeriodFromISR( dev->timer, MS2ST( SHORT_PERIOD_MS ), &HPTaskAwoken );
      xTimerStartFromISR( dev->timer, &HPTaskAwoken );
    }
    else
    {
      xTimerResetFromISR( dev->timer, &HPTaskAwoken );
    }
  }
  if ( dev->value_cl >= WATER_FLOW_CONVERT_L_TO_CL( dev->alert_value_l ) )
  {
    dev->event_cb( WATER_FLOW_SENSOR_EVENT_ALERT_VALUE, dev->value_cl );
  }
}

static void _set_alert( void* user_data, bool value )
{
  water_flow_sensor_t* dev = (water_flow_sensor_t*) user_data;
  LOG( PRINT_INFO, "%s %s %d", __func__, dev->name, value );
  WaterFlowSensor_SetAlertValue( dev, value );
}

static void _set_reset( void* user_data, bool value )
{
  water_flow_sensor_t* dev = (water_flow_sensor_t*) user_data;
  LOG( PRINT_INFO, "%s %s %d", __func__, dev->name, value );
  WaterFlowSensor_ResetValue( dev );
}

static void _init_sensor( water_flow_sensor_t* dev )
{
  static bool is_interrupt_installed = false;
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_POSEDGE;
  io_conf.pin_bit_mask = ( 1ULL << dev->pin );
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_down_en = 0;
  io_conf.pull_up_en = 1;
  gpio_config( &io_conf );
  gpio_set_intr_type( dev->pin, GPIO_INTR_POSEDGE );

  if ( is_interrupt_installed == false )
  {
    gpio_install_isr_service( 0 );
    is_interrupt_installed = true;
  }

  gpio_isr_handler_add( dev->pin, gpio_isr_handler, (void*) dev );
}

static void vTimerCallback( TimerHandle_t xTimer )
{
  water_flow_sensor_t* dev = (water_flow_sensor_t*) pvTimerGetTimerID( xTimer );
  printf( "vTimerCallback\n" );

  switch ( dev->state )
  {
    case WATER_FLOW_SENSOR_STATE_START_MEASURE:
      dev->state = WATER_FLOW_SENSOR_STATE_NO_WATER_SHORT_PERIOD;
      dev->event_cb( WATER_FLOW_SENSOR_EVENT_NO_WATER_SHORT_PERIOD, 0 );
      assert( xTimerChangePeriod( xTimer, MS2ST( LONG_PERIOD_MS ), 0 ) == pdPASS );
      assert( xTimerStart( xTimer, 0 ) == pdPASS );
      break;

    case WATER_FLOW_SENSOR_STATE_NO_WATER_SHORT_PERIOD:
      dev->state = WATER_FLOW_SENSOR_STATE_NO_WATER_LONG_PERIOD;
      dev->event_cb( WATER_FLOW_SENSOR_EVENT_NO_WATER_LONG_PERIOD, 0 );
      assert( xTimerChangePeriod( xTimer, MS2ST( LONG_PERIOD_MS ), 0 ) == pdPASS );
      assert( xTimerStart( xTimer, 0 ) == pdPASS );
      break;

    default:
      break;
  }
}

/* Public functions ---------------------------------------------------------*/

void WaterFlowSensor_Init( water_flow_sensor_t* dev, const char* name, uint32_t pulses_per_liter, water_flow_sensor_event_cb event_cb, uint8_t pin )
{
  assert( dev );
  assert( pulses_per_liter > 0 );
  dev->name = name;
  dev->event_cb = event_cb;
  dev->pin = pin;
  dev->pulses_per_liter = pulses_per_liter;
  dev->alert_value_l = UINT32_MAX;
  dev->state = WATER_FLOW_SENSOR_STATE_IDLE;
  _init_sensor( dev );
  dev->timer = (void*) xTimerCreate( "Timer",
                                     MS2ST( SHORT_PERIOD_MS ),
                                     pdFALSE,
                                     (void*) dev,
                                     vTimerCallback );
}

error_code_t WaterFlowSensor_SetAlertValue( water_flow_sensor_t* dev, uint32_t alert_value )
{
  assert( dev );
  dev->alert_value_l = alert_value;
  return ERROR_CODE_OK;
}

error_code_t WaterFlowSensor_ResetValue( water_flow_sensor_t* dev )
{
  assert( dev );
  dev->value_cl = 0;
  dev->counter = 0; 
  dev->state = WATER_FLOW_SENSOR_STATE_IDLE;
  assert( xTimerStop( dev->timer, 0 ) == pdPASS );
  return ERROR_CODE_OK;
}

error_code_t WaterFlowSensor_StartMeasure( water_flow_sensor_t* dev )
{
  assert( dev );
  dev->value_cl = 0;
  dev->state = WATER_FLOW_SENSOR_STATE_START_MEASURE;
  assert( xTimerChangePeriod( dev->timer, MS2ST( SHORT_PERIOD_MS ), 0 ) == pdPASS );
  assert( xTimerStart( dev->timer, 0 ) == pdPASS );
  return ERROR_CODE_OK;
}

error_code_t WaterFlowSensor_StopMeasure( water_flow_sensor_t* dev )
{
  assert( dev );
  dev->state = WATER_FLOW_SENSOR_STATE_IDLE;
  xTimerStop( dev->timer, 0 );
  return ERROR_CODE_OK;
}

uint32_t WaterFlowSensor_GetValue( water_flow_sensor_t* dev )
{
  assert( dev );
  return dev->value_cl;
}

void WaterFlowSensor_SetPulsesPerLiter( water_flow_sensor_t* dev, uint32_t pulses_per_liter )
{
  assert( pulses_per_liter > 0 );
  dev->pulses_per_liter = pulses_per_liter;
}

size_t WaterFlowSensor_GetStr( water_flow_sensor_t* dev, char* buffer, size_t buffer_size, bool is_add_comma )
{
  assert( dev );
  assert( buffer );
  assert( buffer_size > 0 );
  if ( is_add_comma )
  {
    return snprintf( buffer, buffer_size, ",\"%s\":%ld", dev->name, dev->value_cl );
  }
  else
  {
    return snprintf( buffer, buffer_size, "\"%s\":%ld", dev->name, dev->value_cl );
  }
}
