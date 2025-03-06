#include "xkc-kl200-uart.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TXD_PIN  ( GPIO_NUM_17 )
#define RXD_PIN  ( GPIO_NUM_16 )
#define BUF_SIZE ( 128 )

static int uart_number;

static void init_uart( int uart_num, uint32_t baud_rate, uint64_t tx_pin, uint64_t rx_pin )
{
  uart_number = uart_num;
  const uart_config_t uart_config = {
    .baud_rate = baud_rate,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };

  uart_param_config( uart_number, &uart_config );
  uart_set_pin( uart_number, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE );
  uart_driver_install( uart_number, BUF_SIZE * 2, 0, 0, NULL, 0 );
}

static void send_command( const uint8_t* command, size_t length )
{
  printf( "Send %d: ", length );
  for ( int i = 0; i < length; i++ )
  {
    printf( "%02X ", command[i] );
  }
  printf( "\n" );
  uart_write_bytes( uart_number, (const char*) command, length );
}

static xkc_error_code_t read_response( uint8_t* response, size_t length, uint8_t expected_cmd )
{
  int len = uart_read_bytes( uart_number, response, length, 100 / portTICK_PERIOD_MS );
  if ( len > 0 )
  {
    if ( response[0] == 0x62 && response[1] == expected_cmd && response[len - 2] == 0x66 )
    {
      return XKC_CODE_OK;
    }
    else
    {
      printf( "Invalid response %d %x: ", len, response[len - 2] );
      for ( int i = 0; i < len; i++ )
      {
        printf( "%02X ", response[i] );
      }
      printf( "\n" );
      return XKC_CODE_FAIL;
    }
  }
  return XKC_CODE_TIMEOUT;
}

xkc_error_code_t xkc_restore_factory_settings( void )
{
  uint8_t command[] = { 0x62, 0x39, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x00 };    // XOR8 to be calculated
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  send_command( command, sizeof( command ) );
  uint8_t response[BUF_SIZE];
  return read_response( response, BUF_SIZE, 0x39 );
}

xkc_error_code_t xkc_configure_baud_rate( xkc_baud_rate_t baud_rate )
{
  uint8_t command[] = { 0x62, 0x30, 0x09, 0xFF, 0xFF, 0x00, baud_rate, 0x00, 0x00 };    // XOR8 to be calculated
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  send_command( command, sizeof( command ) );
  uint8_t response[BUF_SIZE];
  return read_response( response, BUF_SIZE, 0x30 );
}

xkc_error_code_t xkc_read_current_configuration( xkc_configuration_t* config )
{
  uint8_t command[] = { 0x62, 0x31, 0x09, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 };    // XOR8 to be calculated
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  send_command( command, sizeof( command ) );
  uint8_t response[BUF_SIZE];
  xkc_error_code_t result = read_response( response, BUF_SIZE, 0x31 );
  if ( result == XKC_CODE_OK )
  {
    config->baud_rate = response[5];
    config->device_address = ( response[6] << 8 ) | response[7];
    config->calibration_distance = ( response[8] << 8 ) | response[9];
    config->upload_mode = response[10];
    config->upload_interval = response[11];
    config->led_mode = response[12];
    config->relay_mode = response[13];
    config->line_mode = response[14];
  }
  return result;
}

xkc_error_code_t xkc_configure_upload_mode( xkc_upload_mode_t mode )
{
  uint8_t command[] = { 0x62, 0x34, 0x09, 0xFF, 0xFF, 0x00, mode, 0x00, 0x00 };    // XOR8 to be calculated
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  send_command( command, sizeof( command ) );
  uint8_t response[BUF_SIZE];
  return read_response( response, BUF_SIZE, 0x34 );
}

xkc_error_code_t xkc_configure_upload_interval( uint8_t interval )
{
  uint8_t command[] = { 0x62, 0x35, 0x09, 0xFF, 0xFF, 0x00, interval, 0x00, 0x00 };    // XOR8 to be calculated
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  send_command( command, sizeof( command ) );
  uint8_t response[BUF_SIZE];
  return read_response( response, BUF_SIZE, 0x35 );
}

xkc_error_code_t xkc_configure_led_mode( xkc_led_mode_t mode )
{
  uint8_t command[] = { 0x62, 0x37, 0x09, 0xFF, 0xFF, 0x00, mode, 0x00, 0x00 };    // XOR8 to be calculated
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  send_command( command, sizeof( command ) );
  uint8_t response[BUF_SIZE];
  return read_response( response, BUF_SIZE, 0x37 );
}

xkc_error_code_t xkc_configure_relay_mode( xkc_relay_mode_t mode )
{
  uint8_t command[] = { 0x62, 0x38, 0x09, 0xFF, 0xFF, 0x00, mode, 0x00, 0x00 };    // XOR8 to be calculated
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  send_command( command, sizeof( command ) );
  uint8_t response[BUF_SIZE];
  return read_response( response, BUF_SIZE, 0x38 );
}

xkc_error_code_t xkc_configure_line_mode( xkc_line_mode_t mode )
{
  uint8_t command[] = { 0x61, 0x30, 0x09, 0xFF, 0xFF, 0x00, mode, 0x00, 0x00 };    // XOR8 to be calculated
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  send_command( command, sizeof( command ) );
  uint8_t response[BUF_SIZE];
  return read_response( response, BUF_SIZE, 0x30 );
}

xkc_error_code_t xkc_read_distance( uint16_t* distance )
{
  uint8_t command[] = { 0x62, 0x33, 0x09, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 };    // Command to read distance
  command[8] = command[0] ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];    // XOR8 checksum
  send_command( command, sizeof( command ) );

  uint8_t response[BUF_SIZE];
  xkc_error_code_t result = read_response( response, BUF_SIZE, 0x33 );
  if ( result == XKC_CODE_OK )
  {
    *distance = ( response[5] << 8 ) | response[6];    // Extract distance from response
  }
  return result;
}

static void measurement_task( void* pvParameters )
{
  xkc_error_code_t res = xkc_configure_upload_mode( UPLOAD_MODE_MANUAL );
  printf( "Upload mode: %d\n", res );
  res = xkc_configure_line_mode( LINE_MODE_UART );
  printf( "Configure line mode: %d\n", res );
  while ( 1 )
  {
    uint16_t distance;
    xkc_error_code_t result = xkc_read_distance( &distance );
    if ( result == XKC_CODE_OK )
    {
      ESP_LOGI( "Measurement", "Distance: %d mm", distance );
    }
    else
    {
      ESP_LOGE( "Measurement", "Failed to read distance" );
    }
    vTaskDelay( pdMS_TO_TICKS( 1000 ) );    // Delay for 1 second
  }
}

void xkc_init( int uart_num, uint32_t baud_rate, uint64_t tx_pin, uint64_t rx_pin )
{
  init_uart( uart_num, baud_rate, tx_pin, rx_pin );
  xTaskCreate( measurement_task, "measurement_task", 2048, NULL, 5, NULL );
}
