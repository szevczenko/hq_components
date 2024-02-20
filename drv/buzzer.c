#include "buzzer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "parameters.h"

#define BUZZER_INIT()

static uint32_t buzzer_timer;

void buzzer_click( void )
{
  buzzer_timer = xTaskGetTickCount() + MS2ST( 100 );
  if ( parameters_getValue( PARAM_BUZZER ) )
  {
    BUZZER_ON();
  }
}

static void buzzer_task( void* arg )
{
  while ( 1 )
  {
    if ( ( buzzer_timer != 0 ) && ( buzzer_timer < xTaskGetTickCount() ) )
    {
      BUZZER_OFF();
      buzzer_timer = 0;
    }

    vTaskDelay( MS2ST( 20 ) );
  }
}

void buzzer_init( void )
{
  BUZZER_INIT();
  gpio_config_t io_conf;

  //disable interrupt
  io_conf.intr_type = GPIO_INTR_DISABLE;
  //disable pull-down mode
  io_conf.pull_down_en = 0;
  //interrupt of rising edge
  io_conf.pin_bit_mask = ( 1ULL << BUZZER_PIN );
  //set as input mode
  io_conf.mode = GPIO_MODE_OUTPUT;
  //enable pull-up mode
  io_conf.pull_up_en = 0;
  gpio_config( &io_conf );
  BUZZER_OFF();
  xTaskCreate( buzzer_task, "buzzer_task", 1024, NULL, 13, NULL );
}
