#include <stdio.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ECHO_UART_PORT_NUM UART_NUM_1
#define RX_PIN             16
#define TX_PIN             17

#define FILTERED_TABLE_SIZE 32

static uint8_t read_buff[1024];
static uint16_t read_data;
static uint32_t distance;
static uint16_t filtered_tab[FILTERED_TABLE_SIZE];
static uint32_t tab_iterator;
static bool silos_is_connected;
static uint8_t bad_read_data_count;

static void uart_init( void )
{
  uart_config_t uart_config =
    {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_APB,
    };
  int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
  intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

  ESP_ERROR_CHECK( uart_driver_install( ECHO_UART_PORT_NUM, 1024 * 2, 0, 0, NULL, intr_alloc_flags ) );
  ESP_ERROR_CHECK( uart_param_config( ECHO_UART_PORT_NUM, &uart_config ) );
  ESP_ERROR_CHECK( uart_set_pin( ECHO_UART_PORT_NUM, TX_PIN, RX_PIN, -1, -1 ) );
}

static void sonar_task( void* arg )
{
  while ( 1 )
  {
    bool is_correct_readed = false;
    // Read data from the UART
    int len = uart_read_bytes( ECHO_UART_PORT_NUM, read_buff, sizeof( read_buff ), MS2ST( 20 ) );

    for ( int i = 0; i < len; i++ )
    {
      if ( ( read_buff[i] == 0xFF ) && ( i + 2 < len ) )
      {
        read_data = ( read_buff[i + 1] << 8 ) | ( read_buff[i + 2] );

        if ( read_data > 3100 )
        {
          continue;
        }

        i += 2;
        filtered_tab[tab_iterator++ % FILTERED_TABLE_SIZE] = read_data;
        for ( uint32_t j = 0; j < FILTERED_TABLE_SIZE; j++ )
        {
          distance += filtered_tab[j];
        }

        distance = distance / FILTERED_TABLE_SIZE;
        printf( "[SONAR] Read data %d\n\r", read_data );
        is_correct_readed = true;
      }
    }
    if ( is_correct_readed )
    {
      silos_is_connected = is_correct_readed;
      bad_read_data_count = 0;
    }
    else
    {
      if ( bad_read_data_count++ > 5 )
      {
        silos_is_connected = is_correct_readed;
      }
    }

    osDelay( 100 );
  }
}

bool ultrasonar_is_connected( void )
{
  return silos_is_connected;
}

uint32_t ultrasonar_get_distance( void )
{
  return distance;
}

void ultrasonar_start( void )
{
  uart_init();
  xTaskCreate( sonar_task, "sonar_task", 4096, NULL, 10, NULL );
}
