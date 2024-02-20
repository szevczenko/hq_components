#include "app_config.h"
#include "but.h"
#include "freertos/timers.h"

// #include "ssd1306.h"
#include "battery.h"
#include "buzzer.h"
#include "cmd_client.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/semphr.h"
#include "keepalive.h"
#include "oled.h"
#include "ssdFigure.h"
#include "wifidrv.h"

#define MODULE_NAME "[SLEEP] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_SLEEP
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define WAKE_UP_PIN 12

typedef enum
{
  SLEEP_STATE_NO_INIT,
  SLEEP_STATE_NO_SLEEP,
  SLEEP_STATE_GO_TO_SLEEP,
  SLEEP_STATE_START_SLEEPING,
  SLEEP_STATE_UNSLEEP_PROCESS,
  SLEEP_STATE_WAKE_UP,
} sleep_state_t;

static sleep_state_t sleep_state;
static uint8_t sleep_signal;

bool is_sleeping( void )
{
  return ( sleep_state != SLEEP_STATE_NO_SLEEP ) || ( sleep_signal == 1 );
}

void go_to_sleep( void )
{
  sleep_signal = 1;
}

void go_to_wake_up( void )
{
  sleep_signal = 0;
}

static void sleep_task( void* arg )
{
  while ( 1 )
  {
    switch ( sleep_state )
    {
      case SLEEP_STATE_NO_SLEEP:
        if ( sleep_signal == 1 )
        {
          sleep_state = SLEEP_STATE_GO_TO_SLEEP;
        }

        break;

      case SLEEP_STATE_GO_TO_SLEEP:
        vTaskDelay( MS2ST( 3000 ) );
        if ( sleep_signal == 1 )
        {
          sleep_state = SLEEP_STATE_START_SLEEPING;
        }
        else
        {
          sleep_state = SLEEP_STATE_WAKE_UP;
        }

        break;

      case SLEEP_STATE_START_SLEEPING:
        LOG( PRINT_INFO, "SLEEP_STATE_START_SLEEPING" );
        esp_sleep_enable_timer_wakeup( 10 * 1000000 );
        esp_sleep_enable_gpio_wakeup();
        gpio_wakeup_enable( WAKE_UP_PIN, GPIO_INTR_LOW_LEVEL );
        esp_wifi_stop();
        oled_clearScreen();
        oled_update();
        BUZZER_OFF();
        esp_light_sleep_start();
        vTaskDelay( MS2ST( 3000 ) );
        if ( sleep_signal == 0 )
        {
          sleep_state = SLEEP_STATE_WAKE_UP;
        }
        else
        {
          sleep_state = SLEEP_STATE_UNSLEEP_PROCESS;
        }

        break;

      case SLEEP_STATE_UNSLEEP_PROCESS:
        LOG( PRINT_INFO, "SLEEP_STATE_UNSLEEP_PROCESS" );
        if ( wifiDrvConnect() == ESP_OK )
        {
          LOG( PRINT_INFO, "wifiDrvConnected" );
          if ( !cmdClientIsConnected() )
          {
            if ( cmdClientTryConnect( 3000 ) == 1 )
            {
              LOG( PRINT_INFO, "cmdClientTryConnected" );
              sendKeepAliveFrame();
              vTaskDelay( MS2ST( 1000 ) );
            }
          }

          LOG( PRINT_INFO, "cmdClientConnect or wifiDrvConnected error" );
        }

        BUZZER_ON();
        vTaskDelay( MS2ST( 100 ) );
        BUZZER_OFF();

        if ( sleep_signal == 0 )
        {
          sleep_state = SLEEP_STATE_WAKE_UP;
          LOG( PRINT_INFO, "SLEEP_STATE_WAKE_UP" );
        }
        else
        {
          sleep_state = SLEEP_STATE_START_SLEEPING;
        }

        break;

      case SLEEP_STATE_WAKE_UP:
        if ( sleep_signal == 1 )
        {
          sleep_state = SLEEP_STATE_GO_TO_SLEEP;
        }
        else
        {
          sleep_state = SLEEP_STATE_NO_SLEEP;
        }

        break;

      default:
        break;
    }

    vTaskDelay( MS2ST( 250 ) );
  }
}

void init_sleep( void )
{
  xTaskCreate( sleep_task, "sleep_task", 1024, NULL, 5, NULL );
  sleep_state = SLEEP_STATE_NO_SLEEP;
}
