/**
 * @file e108-gn03.c
 * @brief Driver for E108-GN03 GNSS module
 * 
 * The E108-GN03 series is a high-performance, multi-mode satellite positioning
 * and navigation module based on the AT6558R solution. Supports BDS/GPS/GLONASS.
 */

#include "e108-gn03.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

static const char* TAG = "e108-gn03";

// Helper function to map ESP-IDF errors to our custom error codes
static e108_err_t e108_map_esp_err(esp_err_t err) {
    switch(err) {
        case ESP_OK: return E108_OK;
        case ESP_ERR_INVALID_ARG: return E108_ERR_INVALID_ARG;
        case ESP_ERR_NO_MEM: return E108_ERR_NO_MEMORY;
        case ESP_ERR_TIMEOUT: return E108_ERR_TIMEOUT;
        default: return E108_ERR_CMD_FAILED;
    }
}

/**
 * @brief E108-GN03 driver object structure
 */
struct e108_gn03_t
{
  int uart_port; /* UART port number */
  uint32_t uart_baud_rate; /* Current UART baud rate */
  bool is_initialized; /* Flag indicating if driver is initialized */
};

/**
 * @brief Calculate NMEA checksum
 * 
 * Calculates the XOR of all characters in the sentence
 * 
 * @param sentence NMEA sentence without the '$' prefix
 * @return uint8_t Calculated checksum
 */
static uint8_t e108_calculate_checksum( const char* sentence )
{
  uint8_t checksum = 0;

  // XOR all characters in the sentence
  while ( *sentence && *sentence != '*' )
  {
    checksum ^= *sentence++;
  }

  return checksum;
}

/**
 * @brief Send command to the E108-GN03 module
 * 
 * @param driver Driver handle
 * @param cmd Command string to send
 * @return int Number of bytes sent, or negative value on error
 */
static int e108_send_command_internal( e108_gn03_handle_t driver, const char* cmd )
{
  if ( driver == NULL || !driver->is_initialized )
  {
    return -1;
  }

  // Use ESP-IDF UART API to send data
  return uart_write_bytes( driver->uart_port, cmd, strlen( cmd ) );
}

/**
 * @brief Initialize E108-GN03 driver
 * 
 * @param config Pointer to driver configuration
 * @return e108_gn03_handle_t Driver handle, or NULL on error
 */
e108_gn03_handle_t e108_init( const e108_gn03_config_t* config )
{
  if ( config == NULL )
  {
    ESP_LOGE( TAG, "Invalid configuration" );
    return NULL;
  }

  // Allocate memory for driver handle
  e108_gn03_handle_t driver = calloc( 1, sizeof( struct e108_gn03_t ) );
  if ( driver == NULL )
  {
    ESP_LOGE( TAG, "Failed to allocate driver memory" );
    return NULL;
  }

  // Configure UART
  uart_config_t uart_config = {
    .baud_rate = config->uart_baud_rate,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
  };

  // Initialize UART
  esp_err_t ret = uart_param_config( config->uart_port, &uart_config );
  if ( ret != ESP_OK )
  {
    ESP_LOGE( TAG, "UART parameter configuration failed" );
    free( driver );
    return NULL;
  }

  // Set UART pins
  ret = uart_set_pin( config->uart_port, config->uart_tx_pin, config->uart_rx_pin,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE );
  if ( ret != ESP_OK )
  {
    ESP_LOGE( TAG, "UART set pin failed" );
    free( driver );
    return NULL;
  }

  // Install UART driver
  const int uart_buffer_size = 2048;
  ret = uart_driver_install( config->uart_port, uart_buffer_size, uart_buffer_size,
                             0, NULL, 0 );
  if ( ret != ESP_OK )
  {
    ESP_LOGE( TAG, "UART driver installation failed" );
    free( driver );
    return NULL;
  }

  // Initialize driver object
  driver->uart_port = config->uart_port;
  driver->uart_baud_rate = config->uart_baud_rate;
  driver->is_initialized = true;

  ESP_LOGI( TAG, "E108-GN03 driver initialized successfully" );
  return driver;
}

/**
 * @brief Deinitialize E108-GN03 driver
 * 
 * @param driver Driver handle
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_deinit( e108_gn03_handle_t driver )
{
  if ( driver == NULL || !driver->is_initialized )
  {
    return E108_ERR_INVALID_ARG;
  }

  // Uninstall UART driver
  esp_err_t ret = uart_driver_delete( driver->uart_port );
  if ( ret != ESP_OK )
  {
    ESP_LOGE( TAG, "UART driver deletion failed" );
    return e108_map_esp_err(ret);
  }

  // Free driver memory
  driver->is_initialized = false;
  free( driver );

  ESP_LOGI( TAG, "E108-GN03 driver deinitialized" );
  return E108_OK;
}

/**
 * @brief Set the serial communication baud rate using CAS01 command
 * 
 * @param driver Driver handle
 * @param baud_rate Baud rate code
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_baud_rate( e108_gn03_handle_t driver, int baud_rate )
{
  if ( driver == NULL || !driver->is_initialized )
  {
    return E108_ERR_INVALID_ARG;
  }

  if ( baud_rate < 0 || baud_rate > 5 )
  {
    ESP_LOGE( TAG, "Invalid baud rate code: %d", baud_rate );
    return E108_ERR_INVALID_ARG;
  }

  char cmd[32];
  char cmd_without_prefix[20];
  uint8_t checksum;

  // Format the command without '$' for checksum calculation
  sprintf( cmd_without_prefix, "PCAS01,%d", baud_rate );

  // Calculate checksum
  checksum = e108_calculate_checksum( cmd_without_prefix );

  // Format the full command with checksum
  sprintf( cmd, "$%s*%02X\r\n", cmd_without_prefix, checksum );

  // Send the command
  if ( e108_send_command_internal( driver, cmd ) < 0 )
  {
    ESP_LOGE( TAG, "Failed to send baud rate command" );
    return E108_ERR_UART_WRITE;
  }

  // Update driver's baud rate
  uint32_t actual_baud = 0;
  switch ( baud_rate )
  {
    case 0:
      actual_baud = 4800;
      break;
    case 1:
      actual_baud = 9600;
      break;
    case 2:
      actual_baud = 19200;
      break;
    case 3:
      actual_baud = 38400;
      break;
    case 4:
      actual_baud = 57600;
      break;
    case 5:
      actual_baud = 115200;
      break;
  }

  if ( actual_baud > 0 )
  {
    // Update UART configuration with new baud rate
    uart_set_baudrate( driver->uart_port, actual_baud );
    driver->uart_baud_rate = actual_baud;
  }

  return E108_OK;
}

/**
 * @brief Set the positioning update rate using CAS02 command
 * 
 * Format: $PCAS02,fixInt*CS<CR><LF>
 * Example: $PCAS02,1000*2E (sets update rate to 1Hz)
 * 
 * @param driver Driver handle
 * @param update_interval Update interval in ms:
 *                       1000 = 1Hz (1 positioning point per second)
 *                       500 = 2Hz (2 positioning points per second)  
 *                       250 = 4Hz (4 positioning points per second)
 *                       200 = 5Hz (5 positioning points per second)
 *                       100 = 10Hz (10 positioning points per second)
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_update_rate( e108_gn03_handle_t driver, int update_interval )
{
  if ( driver == NULL || !driver->is_initialized )
  {
    return E108_ERR_INVALID_ARG;
  }

  // Check for valid update intervals
  if ( update_interval != 1000 && update_interval != 500 && update_interval != 250 && update_interval != 200 && update_interval != 100 )
  {
    ESP_LOGE( TAG, "Invalid update interval: %d", update_interval );
    return E108_ERR_INVALID_ARG;
  }

  char cmd[32];  // Increase buffer size from 24 to 32
  char cmd_without_prefix[24];
  uint8_t checksum;

  // Format the command without '$' for checksum calculation
  sprintf( cmd_without_prefix, "PCAS02,%d", update_interval );

  // Calculate checksum
  checksum = e108_calculate_checksum( cmd_without_prefix );

  // Format the full command with checksum
  sprintf( cmd, "$%s*%02X\r\n", cmd_without_prefix, checksum );

  // Send the command
  if ( e108_send_command_internal( driver, cmd ) < 0 )
  {
    ESP_LOGE( TAG, "Failed to send update rate command" );
    return E108_ERR_UART_WRITE;
  }

  return E108_OK;
}

/**
 * @brief Append parameter to command string based on configuration value
 * 
 * @param buffer Buffer to append to
 * @param value Parameter value (-1 means leave blank)
 */
static void append_nmea_param( char* buffer, int value )
{
  if ( value == -1 )
  {
    strcat( buffer, "," );
  }
  else
  {
    char temp[8];
    sprintf( temp, ",%d", value );
    strcat( buffer, temp );
  }
}

/**
 * @brief Set the NMEA sentence output configuration using CAS03 command
 * 
 * Format: $PCAS03,nGGA,nGLL,nGSA,nGSV,nRMC,nVTG,nZDA,nANT,nDHV,nLPS,res1,res2,nUTC,nGST,res3,res4,res5,nTIM*CS<CR><LF>
 * Example: $PCAS03,1,1,1,1,1,1,1,1,0,0,,,1,1,,,,1*33
 * 
 * @param driver Driver handle
 * @param config Pointer to NMEA output configuration structure
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_nmea_output( e108_gn03_handle_t driver, const e108_nmea_config_t* config )
{
  if ( driver == NULL || !driver->is_initialized || config == NULL )
  {
    return E108_ERR_INVALID_ARG;
  }

  // Command string will be large due to many parameters
  char cmd_without_prefix[100] = "PCAS03";
  char cmd[120];
  uint8_t checksum;

  // Append all parameters to the command
  append_nmea_param( cmd_without_prefix, config->nGGA );
  append_nmea_param( cmd_without_prefix, config->nGLL );
  append_nmea_param( cmd_without_prefix, config->nGSA );
  append_nmea_param( cmd_without_prefix, config->nGSV );
  append_nmea_param( cmd_without_prefix, config->nRMC );
  append_nmea_param( cmd_without_prefix, config->nVTG );
  append_nmea_param( cmd_without_prefix, config->nZDA );
  append_nmea_param( cmd_without_prefix, config->nANT );
  append_nmea_param( cmd_without_prefix, config->nDHV );
  append_nmea_param( cmd_without_prefix, config->nLPS );
  append_nmea_param( cmd_without_prefix, config->res1 );
  append_nmea_param( cmd_without_prefix, config->res2 );
  append_nmea_param( cmd_without_prefix, config->nUTC );
  append_nmea_param( cmd_without_prefix, config->nGST );
  append_nmea_param( cmd_without_prefix, config->res3 );
  append_nmea_param( cmd_without_prefix, config->res4 );
  append_nmea_param( cmd_without_prefix, config->res5 );
  append_nmea_param( cmd_without_prefix, config->nTIM );

  // Calculate checksum
  checksum = e108_calculate_checksum( cmd_without_prefix );

  // Format the full command with checksum
  sprintf( cmd, "$%s*%02X\r\n", cmd_without_prefix, checksum );

  // Send the command
  if ( e108_send_command_internal( driver, cmd ) < 0 )
  {
    ESP_LOGE( TAG, "Failed to send NMEA output command" );
    return E108_ERR_UART_WRITE;
  }

  return E108_OK;
}

/**
 * @brief Configure GNSS working system using CAS04 command
 * 
 * Format: $PCAS04,mode*CS<CR><LF>
 * Examples:
 *   $PCAS04,3*1A  (GPS + BeiDou dual mode)
 *   $PCAS04,1*18  (Single GPS working mode)
 *   $PCAS04,2*1B  (Single BeiDou working mode)
 * 
 * @param driver Driver handle
 * @param mode Working system mode (see e108_system_mode_t enum)
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_system_mode( e108_gn03_handle_t driver, e108_system_mode_t mode )
{
  if ( driver == NULL || !driver->is_initialized )
  {
    return E108_ERR_INVALID_ARG;
  }

  // Validate mode parameter
  if ( mode < E108_MODE_GPS || mode > E108_MODE_ALL )
  {
    ESP_LOGE( TAG, "Invalid system mode: %d", mode );
    return E108_ERR_INVALID_ARG;
  }

  char cmd[32];  // Increase buffer size from 16 to 32
  char cmd_without_prefix[16];
  uint8_t checksum;

  // Format the command without '$' for checksum calculation
  sprintf( cmd_without_prefix, "PCAS04,%d", mode );

  // Calculate checksum
  checksum = e108_calculate_checksum( cmd_without_prefix );

  // Format the full command with checksum
  sprintf( cmd, "$%s*%02X\r\n", cmd_without_prefix, checksum );

  // Send the command
  if ( e108_send_command_internal( driver, cmd ) < 0 )
  {
    ESP_LOGE( TAG, "Failed to send system mode command" );
    return E108_ERR_UART_WRITE;
  }

  return E108_OK;
}

/**
 * @brief Set the NMEA protocol type using CAS05 command
 * 
 * Format: $PCAS05,ver*CS<CR><LF>
 * Example: $PCAS05,1*19
 * 
 * @param driver Driver handle
 * @param protocol_type NMEA protocol type (see e108_protocol_type_t enum)
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_set_protocol_type( e108_gn03_handle_t driver, e108_protocol_type_t protocol_type )
{
  if ( driver == NULL || !driver->is_initialized )
  {
    return E108_ERR_INVALID_ARG;
  }

  // Validate protocol type
  if ( protocol_type != E108_PROTOCOL_NMEA_4_1 && protocol_type != E108_PROTOCOL_BDS_GPS_DUAL && protocol_type != E108_PROTOCOL_GPS_NMEA_0183 )
  {
    ESP_LOGE( TAG, "Invalid protocol type: %d", protocol_type );
    return E108_ERR_INVALID_ARG;
  }

  char cmd[32];  // Increase buffer size from 16 to 32
  char cmd_without_prefix[16];
  uint8_t checksum;

  // Format the command without '$' for checksum calculation
  sprintf( cmd_without_prefix, "PCAS05,%d", protocol_type );

  // Calculate checksum
  checksum = e108_calculate_checksum( cmd_without_prefix );

  // Format the full command with checksum
  sprintf( cmd, "$%s*%02X\r\n", cmd_without_prefix, checksum );

  // Send the command
  if ( e108_send_command_internal( driver, cmd ) < 0 )
  {
    ESP_LOGE( TAG, "Failed to send protocol type command" );
    return E108_ERR_UART_WRITE;
  }

  return E108_OK;
}

/**
 * @brief Query product information using CAS06 command
 * 
 * Format: $PCAS06,info*CS<CR><LF>
 * Example: $PCAS06,0*1B (Query firmware version)
 * 
 * @param driver Driver handle
 * @param info_type Type of information to query (see e108_info_type_t enum)
 * @return e108_err_t E108_OK on success, or error code on failure
 */
e108_err_t e108_query_product_info( e108_gn03_handle_t driver, e108_info_type_t info_type )
{
  if ( driver == NULL || !driver->is_initialized )
  {
    return E108_ERR_INVALID_ARG;
  }

  // Validate info_type parameter
  if ( info_type != E108_INFO_FIRMWARE_VERSION && info_type != E108_INFO_HARDWARE_MODEL && info_type != E108_INFO_WORKING_MODE && info_type != E108_INFO_CUSTOMER_NUMBER && info_type != E108_INFO_UPGRADE_CODE )
  {
    ESP_LOGE( TAG, "Invalid info type: %d", info_type );
    return E108_ERR_INVALID_ARG;
  }

  char cmd[32];  // Increase buffer size from 16 to 32
  char cmd_without_prefix[16];
  uint8_t checksum;

  // Format the command without '$' for checksum calculation
  sprintf( cmd_without_prefix, "PCAS06,%d", info_type );

  // Calculate checksum
  checksum = e108_calculate_checksum( cmd_without_prefix );

  // Format the full command with checksum
  sprintf( cmd, "$%s*%02X\r\n", cmd_without_prefix, checksum );

  // Send the command
  if ( e108_send_command_internal( driver, cmd ) < 0 )
  {
    ESP_LOGE( TAG, "Failed to send product info query command" );
    return E108_ERR_UART_WRITE;
  }

  // Note: Response handling should be implemented separately
  // as it requires reading from UART and parsing the response

  return E108_OK;
}

/**
 * @brief Convert NMEA format latitude/longitude to decimal degrees
 * 
 * @param nmea_coord NMEA coordinate in ddmm.mmmm format
 * @param direction N/S or E/W indicator
 * @return double Coordinate in decimal degrees (negative for S/W)
 */
double e108_nmea_to_decimal_degrees( const char* nmea_coord, char direction )
{
  if ( nmea_coord == NULL || *nmea_coord == '\0' )
  {
    return 0.0;
  }

  double value = atof( nmea_coord );

  // Extract degrees (integer part of value)
  int degrees = (int) ( value / 100 );

  // Calculate minutes (fractional part)
  double minutes = value - ( degrees * 100 );

  // Convert to decimal degrees
  double decimal_degrees = degrees + ( minutes / 60.0 );

  // Adjust sign based on direction
  if ( direction == 'S' || direction == 'W' )
  {
    decimal_degrees = -decimal_degrees;
  }

  return decimal_degrees;
}

/**
 * @brief Extract a field from comma-delimited NMEA sentence
 * 
 * @param start Pointer to the start of the field
 * @param buffer Buffer to store the extracted field
 * @param buffer_size Size of the buffer
 * @return const char* Pointer to the character after the field
 */
static const char* extract_field( const char* start, char* buffer, size_t buffer_size )
{
  size_t i = 0;

  // Skip if we're already at a comma or end of string
  if ( *start == ',' || *start == '\0' || *start == '*' )
  {
    buffer[0] = '\0';
    return ( *start == '\0' || *start == '*' ) ? start : start + 1;
  }

  // Copy until comma, end of string, or checksum marker
  while ( *start != ',' && *start != '\0' && *start != '*' && i < buffer_size - 1 )
  {
    buffer[i++] = *start++;
  }

  buffer[i] = '\0';

  // Skip over the comma
  return ( *start == '\0' || *start == '*' ) ? start : start + 1;
}

/**
 * @brief Parse NMEA GGA sentence
 * 
 * Format: $--GGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,xx.x,x.x,M,x.x,M,x.x,xxxx*hh
 * Example: $GPGGA,065545.789,2109.9551,N,12023.4047,E,1,9,0.85,18.1,M,8.0,M,,*5E
 * 
 * @param sentence The NMEA sentence string to parse
 * @param gga_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_gga( const char* sentence, e108_gga_data_t* gga_data )
{
  if ( sentence == NULL || gga_data == NULL )
  {
    return -1;
  }

  // Initialize the structure
  memset( gga_data, 0, sizeof( e108_gga_data_t ) );
  gga_data->valid = false;

  // Verify that this is a GGA sentence
  if ( strstr( sentence, "GGA" ) == NULL )
  {
    return -2;    // Not a GGA sentence
  }

  // Extract message ID (e.g., $GPGGA)
  const char* cursor = sentence;

  // Skip the $ character if present
  if ( *cursor == '$' )
  {
    cursor++;
  }

  char field[20];

  // Extract Message ID (e.g., GPGGA)
  cursor = extract_field( cursor, gga_data->message_id, sizeof( gga_data->message_id ) );

  // Extract UTC time
  cursor = extract_field( cursor, gga_data->utc_time, sizeof( gga_data->utc_time ) );

  // Extract latitude
  cursor = extract_field( cursor, field, sizeof( field ) );

  // Extract N/S indicator
  char ns_indicator[2];
  cursor = extract_field( cursor, ns_indicator, sizeof( ns_indicator ) );
  if ( ns_indicator[0] != '\0' )
  {
    gga_data->north_south = ns_indicator[0];
  }

  // Convert latitude to decimal degrees
  gga_data->latitude = e108_nmea_to_decimal_degrees( field, gga_data->north_south );

  // Extract longitude
  cursor = extract_field( cursor, field, sizeof( field ) );

  // Extract E/W indicator
  char ew_indicator[2];
  cursor = extract_field( cursor, ew_indicator, sizeof( ew_indicator ) );
  if ( ew_indicator[0] != '\0' )
  {
    gga_data->east_west = ew_indicator[0];
  }

  // Convert longitude to decimal degrees
  gga_data->longitude = e108_nmea_to_decimal_degrees( field, gga_data->east_west );

  // Extract fix quality
  cursor = extract_field( cursor, field, sizeof( field ) );
  gga_data->fix_quality = ( field[0] != '\0' ) ? atoi( field ) : 0;

  // Extract satellites used
  cursor = extract_field( cursor, field, sizeof( field ) );
  gga_data->satellites_used = ( field[0] != '\0' ) ? atoi( field ) : 0;

  // Extract HDOP
  cursor = extract_field( cursor, field, sizeof( field ) );
  gga_data->hdop = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract altitude
  cursor = extract_field( cursor, field, sizeof( field ) );
  gga_data->altitude = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract altitude units
  cursor = extract_field( cursor, field, sizeof( field ) );
  if ( field[0] != '\0' )
  {
    gga_data->altitude_units = field[0];
  }

  // Extract geoid separation
  cursor = extract_field( cursor, field, sizeof( field ) );
  gga_data->geoid_separation = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract geoid units
  cursor = extract_field( cursor, field, sizeof( field ) );
  if ( field[0] != '\0' )
  {
    gga_data->geoid_units = field[0];
  }

  // Extract differential time
  cursor = extract_field( cursor, field, sizeof( field ) );
  gga_data->diff_time = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract differential station ID
  cursor = extract_field( cursor, field, sizeof( field ) );
  gga_data->diff_station_id = ( field[0] != '\0' ) ? atoi( field ) : 0;

  // Mark as valid if we have the minimum required fields
  gga_data->valid = ( gga_data->fix_quality > 0 );

  return 0;
}

/**
 * @brief Parse NMEA GSA sentence
 * 
 * Format: $--GSA,a,a,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,xx,xx,xx*hh
 * Example: $GPGSA,A,3,10,24,12,32,25,21,15,20,31,,,,1.25,0.85,0.91*04
 * 
 * @param sentence The NMEA sentence string to parse
 * @param gsa_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_gsa( const char* sentence, e108_gsa_data_t* gsa_data )
{
  if ( sentence == NULL || gsa_data == NULL )
  {
    return -1;
  }

  // Initialize the structure
  memset( gsa_data, 0, sizeof( e108_gsa_data_t ) );
  gsa_data->valid = false;

  // Verify that this is a GSA sentence
  if ( strstr( sentence, "GSA" ) == NULL )
  {
    return -2;    // Not a GSA sentence
  }

  // Extract message ID (e.g., $GPGSA)
  const char* cursor = sentence;

  // Skip the $ character if present
  if ( *cursor == '$' )
  {
    cursor++;
  }

  char field[20];

  // Extract Message ID (e.g., GPGSA)
  cursor = extract_field( cursor, gsa_data->message_id, sizeof( gsa_data->message_id ) );

  // Extract Mode1 (A=Automatic or M=Manual)
  char mode1[2];
  cursor = extract_field( cursor, mode1, sizeof( mode1 ) );
  if ( mode1[0] != '\0' )
  {
    gsa_data->mode1 = mode1[0];
  }

  // Extract Mode2 (1=No fix, 2=2D fix, 3=3D fix)
  cursor = extract_field( cursor, field, sizeof( field ) );
  gsa_data->mode2 = ( field[0] != '\0' ) ? atoi( field ) : 0;

  // Extract satellite IDs (up to 12)
  for ( int i = 0; i < 12; i++ )
  {
    cursor = extract_field( cursor, field, sizeof( field ) );
    gsa_data->satellite_ids[i] = ( field[0] != '\0' ) ? atoi( field ) : 0;
  }

  // Extract PDOP (Position Dilution of Precision)
  cursor = extract_field( cursor, field, sizeof( field ) );
  gsa_data->pdop = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract HDOP (Horizontal Dilution of Precision)
  cursor = extract_field( cursor, field, sizeof( field ) );
  gsa_data->hdop = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract VDOP (Vertical Dilution of Precision)
  cursor = extract_field( cursor, field, sizeof( field ) );
  gsa_data->vdop = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Mark as valid if we have a valid fix
  gsa_data->valid = ( gsa_data->mode2 > 0 );

  return 0;
}

/**
 * @brief Parse NMEA GSV sentence
 * 
 * Format: $--GSV,x,x,x,x,x,x,x,...*hh
 * Example:
 * $GPGSV,3,1,12,14,75,001,31,32,67,111,38,31,57,331,33,26,47,221,20*73
 * $GPGSV,3,2,12,25,38,041,29,29,30,097,32,193,26,176,35,22,23,301,30*47
 * $GPGSV,3,3,12,10,20,185,28,44,20,250,,16,17,217,21,03,14,315,*7D
 * 
 * @param sentence The NMEA sentence string to parse
 * @param gsv_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_gsv( const char* sentence, e108_gsv_data_t* gsv_data )
{
  if ( sentence == NULL || gsv_data == NULL )
  {
    return -1;
  }

  // Verify that this is a GSV sentence
  if ( strstr( sentence, "GSV" ) == NULL )
  {
    return -2;    // Not a GSV sentence
  }

  // Extract message ID (e.g., $GPGSV)
  const char* cursor = sentence;

  // Skip the $ character if present
  if ( *cursor == '$' )
  {
    cursor++;
  }

  char field[20];

  // Extract Message ID (e.g., GPGSV)
  cursor = extract_field( cursor, gsv_data->message_id, sizeof( gsv_data->message_id ) );

  // Extract total number of messages
  cursor = extract_field( cursor, field, sizeof( field ) );
  gsv_data->total_messages = ( field[0] != '\0' ) ? atoi( field ) : 0;

  // Extract message number
  cursor = extract_field( cursor, field, sizeof( field ) );
  gsv_data->message_number = ( field[0] != '\0' ) ? atoi( field ) : 0;

  // Extract number of satellites in view
  cursor = extract_field( cursor, field, sizeof( field ) );
  gsv_data->satellites_in_view = ( field[0] != '\0' ) ? atoi( field ) : 0;

  // Check message number validity
  if ( gsv_data->message_number < 1 || gsv_data->message_number > gsv_data->total_messages )
  {
    return -3;    // Invalid message number
  }

  // Parse satellite information (up to 4 satellites per message)
  int sat_offset = ( gsv_data->message_number - 1 ) * 4;
  int max_sats_this_message = ( gsv_data->satellites_in_view - sat_offset > 4 ) ?
                                4 :
                                ( gsv_data->satellites_in_view - sat_offset );

  // Only process this message if it's new (message_number == 1) or is a continuation
  if ( gsv_data->message_number == 1 )
  {
    // Initialize the satellite data for a new sequence
    memset( gsv_data->satellites, 0, sizeof( gsv_data->satellites ) );
  }

  // Process each satellite in this message
  for ( int i = 0; i < max_sats_this_message && ( sat_offset + i ) < E108_MAX_SATELLITES; i++ )
  {
    e108_satellite_info_t* sat = &gsv_data->satellites[sat_offset + i];

    // Extract satellite ID
    cursor = extract_field( cursor, field, sizeof( field ) );
    sat->satellite_id = ( field[0] != '\0' ) ? atoi( field ) : 0;

    // Extract elevation angle
    cursor = extract_field( cursor, field, sizeof( field ) );
    sat->elevation = ( field[0] != '\0' ) ? atoi( field ) : 0;

    // Extract azimuth angle
    cursor = extract_field( cursor, field, sizeof( field ) );
    sat->azimuth = ( field[0] != '\0' ) ? atoi( field ) : 0;

    // Extract signal-to-noise ratio
    cursor = extract_field( cursor, field, sizeof( field ) );
    sat->snr = ( field[0] != '\0' ) ? atoi( field ) : 0;

    // Mark this satellite info as valid if we have a satellite ID
    sat->valid = ( sat->satellite_id > 0 );
  }

  // Mark as valid if we have at least some satellite data
  gsv_data->valid = ( gsv_data->satellites_in_view > 0 );

  return 0;
}

/**
 * @brief Parse NMEA RMC sentence
 * 
 * Format: $--RMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,xx,xx,xxxx,xx,a*hh
 * Example: $GPRMC,100646.000,A,3109.9704,N,12123.4219,E,0.257,335.62,291216,,,A*59
 * 
 * @param sentence The NMEA sentence string to parse
 * @param rmc_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_rmc( const char* sentence, e108_rmc_data_t* rmc_data )
{
  if ( sentence == NULL || rmc_data == NULL )
  {
    return -1;
  }

  // Initialize the structure
  memset( rmc_data, 0, sizeof( e108_rmc_data_t ) );
  rmc_data->valid = false;

  // Verify that this is an RMC sentence
  if ( strstr( sentence, "RMC" ) == NULL )
  {
    return -2;    // Not an RMC sentence
  }

  // Extract message ID (e.g., $GPRMC)
  const char* cursor = sentence;

  // Skip the $ character if present
  if ( *cursor == '$' )
  {
    cursor++;
  }

  char field[20];

  // Extract Message ID (e.g., GPRMC)
  cursor = extract_field( cursor, rmc_data->message_id, sizeof( rmc_data->message_id ) );

  // Extract UTC time
  cursor = extract_field( cursor, rmc_data->utc_time, sizeof( rmc_data->utc_time ) );

  // Extract status (A=Valid, V=Invalid)
  char status[2];
  cursor = extract_field( cursor, status, sizeof( status ) );
  if ( status[0] != '\0' )
  {
    rmc_data->status = status[0];
  }

  // Extract latitude
  cursor = extract_field( cursor, field, sizeof( field ) );

  // Extract N/S indicator
  char ns_indicator[2];
  cursor = extract_field( cursor, ns_indicator, sizeof( ns_indicator ) );
  if ( ns_indicator[0] != '\0' )
  {
    rmc_data->north_south = ns_indicator[0];
  }

  // Convert latitude to decimal degrees
  rmc_data->latitude = e108_nmea_to_decimal_degrees( field, rmc_data->north_south );

  // Extract longitude
  cursor = extract_field( cursor, field, sizeof( field ) );

  // Extract E/W indicator
  char ew_indicator[2];
  cursor = extract_field( cursor, ew_indicator, sizeof( ew_indicator ) );
  if ( ew_indicator[0] != '\0' )
  {
    rmc_data->east_west = ew_indicator[0];
  }

  // Convert longitude to decimal degrees
  rmc_data->longitude = e108_nmea_to_decimal_degrees( field, rmc_data->east_west );

  // Extract speed over ground (knots)
  cursor = extract_field( cursor, field, sizeof( field ) );
  rmc_data->speed = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract track angle (degrees True)
  cursor = extract_field( cursor, field, sizeof( field ) );
  rmc_data->track_angle = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract date (ddmmyy)
  cursor = extract_field( cursor, rmc_data->date, sizeof( rmc_data->date ) );

  // Extract magnetic variation
  cursor = extract_field( cursor, field, sizeof( field ) );
  rmc_data->magnetic_variation = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Extract magnetic variation direction (E/W)
  char mag_dir[2];
  cursor = extract_field( cursor, mag_dir, sizeof( mag_dir ) );
  if ( mag_dir[0] != '\0' )
  {
    rmc_data->mag_var_direction = mag_dir[0];
  }

  // Extract mode indicator (A=Autonomous, D=Differential, E=Estimated)
  char mode[2];
  cursor = extract_field( cursor, mode, sizeof( mode ) );
  if ( mode[0] != '\0' )
  {
    rmc_data->mode = mode[0];
  }

  // Mark as valid if the status is 'A'
  rmc_data->valid = ( rmc_data->status == 'A' );

  return 0;
}

/**
 * @brief Parse NMEA VTG sentence
 * 
 * Format: $--VTG,xx,T,xx,M,xx,N,xx,K*hh
 * Example: $GPVTG,335.62,T,,M,0.257,N,0.477,K,A*38
 * 
 * @param sentence The NMEA sentence string to parse
 * @param vtg_data Pointer to structure to store parsed data
 * @return int 0 on success, negative value on error
 */
int e108_parse_vtg( const char* sentence, e108_vtg_data_t* vtg_data )
{
  if ( sentence == NULL || vtg_data == NULL )
  {
    return -1;
  }

  // Initialize the structure
  memset( vtg_data, 0, sizeof( e108_vtg_data_t ) );
  vtg_data->valid = false;

  // Verify that this is a VTG sentence
  if ( strstr( sentence, "VTG" ) == NULL )
  {
    return -2;    // Not a VTG sentence
  }

  // Extract message ID (e.g., $GPVTG)
  const char* cursor = sentence;

  // Skip the $ character if present
  if ( *cursor == '$' )
  {
    cursor++;
  }

  char field[20];

  // Extract Message ID (e.g., GPVTG)
  cursor = extract_field( cursor, vtg_data->message_id, sizeof( vtg_data->message_id ) );

  // Extract track angle (true)
  cursor = extract_field( cursor, field, sizeof( field ) );
  vtg_data->track_true = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Skip the 'T' indicator
  cursor = extract_field( cursor, field, sizeof( field ) );    // Should be 'T'

  // Extract track angle (magnetic)
  cursor = extract_field( cursor, field, sizeof( field ) );
  vtg_data->track_magnetic = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Skip the 'M' indicator
  cursor = extract_field( cursor, field, sizeof( field ) );    // Should be 'M'

  // Extract speed (knots)
  cursor = extract_field( cursor, field, sizeof( field ) );
  vtg_data->speed_knots = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Skip the 'N' indicator
  cursor = extract_field( cursor, field, sizeof( field ) );    // Should be 'N'

  // Extract speed (km/h)
  cursor = extract_field( cursor, field, sizeof( field ) );
  vtg_data->speed_kmh = ( field[0] != '\0' ) ? atof( field ) : 0.0f;

  // Skip the 'K' indicator
  cursor = extract_field( cursor, field, sizeof( field ) );    // Should be 'K'

  // Extract mode indicator
  cursor = extract_field( cursor, field, sizeof( field ) );
  if ( field[0] != '\0' )
  {
    vtg_data->mode = field[0];
  }

  // Mark as valid if we have valid mode ('A' for Autonomous, 'D' for Differential)
  vtg_data->valid = ( vtg_data->mode == 'A' || vtg_data->mode == 'D' );

  return 0;
}
