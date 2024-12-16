#include "wifidrv.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "json.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/netdb.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define MODULE_NAME "[WiFi] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_WIFI
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define USE_DEBUG_HANDLER 0

#define CALLBACKS_LIST_SIZE            8
#define DEFAULT_SCAN_LIST_SIZE         32
#define CONFIG_TCPIP_EVENT_THD_WA_SIZE 4096
#define MAX_VAL( a, b )                a > b ? a : b

/**
 * @brief Defines the maximum length in bytes of a JSON representation of the IP information
 * assuming all ips are 4*3 digits, and all characters in the ssid require to be escaped.
 * example: {"ssid":"abcdefghijklmnopqrstuvwxyz012345","ip":"192.168.1.119","netmask":"255.255.255.0","gw":"192.168.1.1","urc":99}
 * Run this JS (browser console is easiest) to come to the conclusion that 159 is the worst case.
 * ```
 * var a = {"ssid":"abcdefghijklmnopqrstuvwxyz012345","ip":"255.255.255.255","netmask":"255.255.255.255","gw":"255.255.255.255","urc":99};
 * // Replace all ssid characters with a double quote which will have to be escaped
 * a.ssid = a.ssid.split('').map(() => '"').join('');
 * console.log(JSON.stringify(a).length); // => 158 +1 for null
 * console.log(JSON.stringify(a)); // print it
 * ```
 */
#define JSON_IP_INFO_SIZE 159

/**
 * @brief Defines the maximum length in bytes of a JSON representation of an access point.
 *
 *  maximum ap string length with full 32 char ssid: 75 + \\n + \0 = 77\n
 *  example: {"ssid":"abcdefghijklmnopqrstuvwxyz012345","chan":12,"rssi":-100,"auth":4},\n
 *  BUT: we need to escape JSON. Imagine a ssid full of \" ? so it's 32 more bytes hence 77 + 32 = 99.\n
 *  this is an edge case but I don't think we should crash in a catastrophic manner just because
 *  someone decided to have a funny wifi name.
 */
#define JSON_ONE_APP_SIZE 99

typedef enum
{
  WIFI_APP_DISABLE = 0,
  WIFI_APP_INIT,
  WIFI_APP_IDLE,
  WIFI_APP_CONNECT,
  WIFI_APP_WAIT_CONNECT,
  WIFI_APP_SCAN,
  WIFI_APP_START,
  WIFI_APP_STOP,
  WIFI_APP_READY,
  WIFI_APP_DEINIT,
  WIFI_APP_TOP,
} wifi_app_status_t;

char* wifi_state_name[] =
  {
    [WIFI_APP_INIT] = "WIFI_APP_INIT",
    [WIFI_APP_IDLE] = "WIFI_APP_IDLE",
    [WIFI_APP_CONNECT] = "WIFI_APP_CONNECT",
    [WIFI_APP_WAIT_CONNECT] = "WIFI_APP_WAIT_CONNECT",
    [WIFI_APP_SCAN] = "WIFI_APP_SCAN",
    [WIFI_APP_START] = "WIFI_APP_START",
    [WIFI_APP_STOP] = "WIFI_APP_STOP",
    [WIFI_APP_READY] = "WIFI_APP_READY",
    [WIFI_APP_DEINIT] = "WIFI_APP_DEINIT",
    [WIFI_APP_DISABLE] = "WIFI_APP_DISABLE",
};

typedef enum update_reason_code_t
{
  UPDATE_CONNECTION_OK = 0,
  UPDATE_FAILED_ATTEMPT = 1,
  UPDATE_USER_DISCONNECT = 2,
  UPDATE_LOST_CONNECTION = 3
} update_reason_code_t;

typedef struct
{
  wifi_drv_callback callbacks[CALLBACKS_LIST_SIZE];
  size_t size;
} callback_list_t;

typedef struct
{
  wifi_app_status_t state;
  int retry;
  bool is_started;
  bool is_power_save;
  bool connected;
  bool disconnect_req;
  bool connect_req;
  bool read_wifi_data;
  bool is_scanned;
  uint32_t connect_attemps;
  uint32_t reason_disconnect;
  uint32_t client_cnt;

  wifi_ap_record_t scan_list[DEFAULT_SCAN_LIST_SIZE];
  uint16_t scanned_ap_num;
  wifi_config_t wifi_config_sta;
  wifi_config_t wifi_config_ap;
  wifiConData_t wifi_saved_data;
  int rssi;
  char ip_addr[IP4ADDR_STRLEN_MAX];
  char ip_info_json[JSON_IP_INFO_SIZE];
  char access_points_json[DEFAULT_SCAN_LIST_SIZE * JSON_ONE_APP_SIZE + 4];
  SemaphoreHandle_t ip_mutex;
  SemaphoreHandle_t json_mutex;
  esp_netif_t* esp_netif_sta;
  esp_netif_t* esp_netif_ap;

  callback_list_t on_connect_cb;
  callback_list_t on_disconnect_cb;
} wifidrv_ctx_t;

static wifidrv_ctx_t ctx =
  {
    .wifi_config_ap = {
                       .ap = {
        .password = WIFI_AP_PASSWORD,
        .max_connection = 2,
        .authmode = WIFI_AUTH_WPA_WPA2_PSK },
                       },
};
static uint8_t wifi_type;

static void _safe_update_sta_ip_string( uint32_t ip );
static bool _generate_access_points_json( void );

static void _init_list( callback_list_t* list )
{
  memset( list, 0, sizeof( callback_list_t ) );
}

static void _add_to_list( callback_list_t* list, wifi_drv_callback cb )
{
  assert( list->size < CALLBACKS_LIST_SIZE );
  list->callbacks[list->size] = cb;
  list->size++;
}

static void _run_from_cb_list( callback_list_t* list )
{
  for ( int i = 0; i < list->size; i++ )
  {
    list->callbacks[i]();
  }
}

static void _wifi_read_info_cb( void* arg, wifi_vendor_ie_type_t type, const uint8_t sa[6],
                                const vendor_ie_data_t* vnd_ie, int rssi )
{
}

static int _wifi_data_save( void )
{
  LOG( PRINT_INFO, "%s", __func__ );
  nvs_handle my_handle;
  esp_err_t err;

  strncpy( (char*) ctx.wifi_saved_data.ssid, (char*) ctx.wifi_config_sta.sta.ssid, sizeof( ctx.wifi_saved_data.ssid ) );
  strncpy( (char*) ctx.wifi_saved_data.password, (char*) ctx.wifi_config_sta.sta.password,
           sizeof( ctx.wifi_saved_data.password ) );

  // Open
  err = nvs_open( "wifi_config_sta", NVS_READWRITE, &my_handle );
  if ( err != ESP_OK )
  {
    nvs_close( my_handle );
    return err;
  }

  err = nvs_set_blob( my_handle, "wifi", &ctx.wifi_saved_data, sizeof( wifiConData_t ) );

  if ( err != ESP_OK )
  {
    nvs_close( my_handle );
    return err;
  }

  // Commit
  err = nvs_commit( my_handle );
  if ( err != ESP_OK )
  {
    nvs_close( my_handle );
    return err;
  }

  // Close
  nvs_close( my_handle );
  return ESP_OK;
}

static esp_err_t _wifi_data_read( void )
{
  nvs_handle my_handle;
  esp_err_t err;

  // Open
  err = nvs_open( "wifi_config_sta", NVS_READWRITE, &my_handle );
  if ( err != ESP_OK )
  {
    return err;
  }

  // Read the size of memory space required for blob
  size_t required_size = 0;    // value will default to 0, if not set yet in NVS

  err = nvs_get_blob( my_handle, "wifi", NULL, &required_size );
  if ( ( err != ESP_OK ) && ( err != ESP_ERR_NVS_NOT_FOUND ) )
  {
    nvs_close( my_handle );
    return err;
  }

  // Read previously saved blob if available
  if ( required_size == sizeof( wifiConData_t ) )
  {
    err = nvs_get_blob( my_handle, "wifi", &ctx.wifi_saved_data, &required_size );
    nvs_close( my_handle );
    if ( err == ESP_OK )
    {
      ctx.read_wifi_data = true;
      strncpy( (char*) ctx.wifi_config_sta.sta.ssid, (char*) ctx.wifi_saved_data.ssid, sizeof( ctx.wifi_config_sta.sta.ssid ) );
      strncpy( (char*) ctx.wifi_config_sta.sta.password, (char*) ctx.wifi_saved_data.password,
               sizeof( ctx.wifi_config_sta.sta.password ) );
    }
    return err;
  }

  nvs_close( my_handle );
  return ESP_ERR_NVS_NOT_FOUND;
}

static bool _start_sta_mode( void )
{
  if ( ctx.is_started )
  {
    return false;
  }

  ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_STA ) );
  ESP_ERROR_CHECK( esp_wifi_start() );
  ctx.is_started = true;
  esp_wifi_set_vendor_ie_cb( _wifi_read_info_cb, NULL );
  wifiDrvPowerSave( false );
  return true;
}

static bool _start_access_point( void )
{
  if ( ctx.is_started )
  {
    return false;
  }

  ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_AP ) );
  ESP_ERROR_CHECK( esp_wifi_set_config( ESP_IF_WIFI_AP, &ctx.wifi_config_ap ) );
  ESP_ERROR_CHECK( esp_wifi_start() );
  ctx.is_started = true;
  return true;
}

static bool _start_ap_sta_mode( void )
{
  if ( ctx.is_started )
  {
    return false;
  }

  ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_APSTA ) );
  ESP_ERROR_CHECK( esp_wifi_set_config( ESP_IF_WIFI_AP, &ctx.wifi_config_ap ) );
  ESP_ERROR_CHECK( esp_wifi_start() );
  esp_wifi_set_vendor_ie_cb( _wifi_read_info_cb, NULL );
  wifiDrvPowerSave( false );
  ctx.is_started = true;
  return true;
}

static void _change_state( wifi_app_status_t new_state )
{
  if ( new_state < WIFI_APP_TOP )
  {
    ctx.state = new_state;
    LOG( PRINT_INFO, "State: %s", wifi_state_name[new_state] );
  }
  else
  {
    LOG( PRINT_ERROR, "Error change state: %d", new_state );
  }
}

static void _on_wifi_disconnect( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;

  ctx.connected = false;
  ctx.reason_disconnect = event->reason;
  _safe_update_sta_ip_string( 0 );
  // tcpip_adapter_down(TCPIP_ADAPTER_IF_STA);
}

static void _scan_done_handler( void )
{
  ctx.scanned_ap_num = DEFAULT_SCAN_LIST_SIZE;
  ESP_ERROR_CHECK( esp_wifi_scan_get_ap_records( &ctx.scanned_ap_num, ctx.scan_list ) );
  LOG( PRINT_INFO, "SCAN DONE %d %s!!!!!!", ctx.scanned_ap_num, ctx.scan_list[0].ssid );
  _generate_access_points_json();
  ctx.is_scanned = false;
}

static void _wifi_scan_done_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  switch ( event_id )
  {
    case WIFI_EVENT_SCAN_DONE:
      _scan_done_handler();
      break;
    default:
      break;
  }
}

static bool _lock_sta_ip_string( TickType_t xTicksToWait )
{
  if ( ctx.ip_mutex )
  {
    if ( xSemaphoreTake( ctx.ip_mutex, xTicksToWait ) == pdTRUE )
    {
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    return false;
  }
}

static void _unlock_sta_ip_string( void )
{
  xSemaphoreGive( ctx.ip_mutex );
}

static void _safe_update_sta_ip_string( uint32_t ip )
{
  if ( _lock_sta_ip_string( portMAX_DELAY ) )
  {
    esp_ip4_addr_t ip4;
    ip4.addr = ip;

    char str_ip[IP4ADDR_STRLEN_MAX];
    esp_ip4addr_ntoa( &ip4, str_ip, IP4ADDR_STRLEN_MAX );

    strcpy( ctx.ip_addr, str_ip );

    LOG( PRINT_INFO, "Set STA IP String to: %s", ctx.ip_addr );

    _unlock_sta_ip_string();
  }
}

static void _clear_ip_info_json( void )
{
  LOG( PRINT_INFO, "Clear ip info" );
  strcpy( ctx.ip_info_json, "{}\n" );
}

static bool _generate_ip_info_json( update_reason_code_t update_reason_code )
{
  if ( wifiDrvLockJsonBuffer( portMAX_DELAY ) )
  {
    wifi_config_t* config = &ctx.wifi_config_sta;
    const char* ip_info_json_format = ",\"ip\":\"%s\",\"netmask\":\"%s\",\"gw\":\"%s\",\"urc\":%d}\n";

    memset( ctx.ip_info_json, 0x00, JSON_IP_INFO_SIZE );

    /* to avoid declaring a new buffer we copy the data directly into the buffer at its correct address */
    strcpy( ctx.ip_info_json, "{\"ssid\":" );
    json_print_string( config->sta.ssid, (unsigned char*) ( ctx.ip_info_json + strlen( ctx.ip_info_json ) ) );

    size_t ip_info_json_len = strlen( ctx.ip_info_json );
    size_t remaining = JSON_IP_INFO_SIZE - ip_info_json_len;
    if ( update_reason_code == UPDATE_CONNECTION_OK )
    {
      LOG( PRINT_INFO, "Set ip info" );
      /* rest of the information is copied after the ssid */
      esp_netif_ip_info_t ip_info;
      ESP_ERROR_CHECK( esp_netif_get_ip_info( ctx.esp_netif_sta, &ip_info ) );

      char ip[IP4ADDR_STRLEN_MAX]; /* note: IP4ADDR_STRLEN_MAX is defined in lwip */
      char gw[IP4ADDR_STRLEN_MAX];
      char netmask[IP4ADDR_STRLEN_MAX];

      esp_ip4addr_ntoa( &ip_info.ip, ip, IP4ADDR_STRLEN_MAX );
      esp_ip4addr_ntoa( &ip_info.gw, gw, IP4ADDR_STRLEN_MAX );
      esp_ip4addr_ntoa( &ip_info.netmask, netmask, IP4ADDR_STRLEN_MAX );

      snprintf( ( ctx.ip_info_json + ip_info_json_len ), remaining, ip_info_json_format,
                ip,
                netmask,
                gw,
                (int) update_reason_code );
    }
    else
    {
      _clear_ip_info_json();
    }
    wifiDrvUnlockJsonBuffer();
    return true;
  }
  return false;
}

static void _clear_access_points_json( void )
{
  strcpy( ctx.access_points_json, "[]\n" );
}

static bool _generate_access_points_json( void )
{
  if ( wifiDrvLockJsonBuffer( portMAX_DELAY ) )
  {
    strcpy( ctx.access_points_json, "[" );

    const char ap_str[] = ",\"chan\":%d,\"rssi\":%d,\"auth\":%d}%c\n";

    /* stack buffer to hold on to one AP until it's copied over to accessp_json */
    char one_ap[JSON_ONE_APP_SIZE];
    for ( int i = 0; i < ctx.scanned_ap_num; i++ )
    {
      wifi_ap_record_t ap = ctx.scan_list[i];

      /* ssid needs to be json escaped. To save on heap memory it's directly printed at the correct address */
      strcat( ctx.access_points_json, "{\"ssid\":" );
      json_print_string( (unsigned char*) ap.ssid, (unsigned char*) ( ctx.access_points_json + strlen( ctx.access_points_json ) ) );

      /* print the rest of the json for this access point: no more string to escape */
      snprintf( one_ap, (size_t) JSON_ONE_APP_SIZE, ap_str,
                ap.primary,
                ap.rssi,
                ap.authmode,
                i == ctx.scanned_ap_num - 1 ? ']' : ',' );

      /* add it to the list */
      strcat( ctx.access_points_json, one_ap );
    }
    wifiDrvUnlockJsonBuffer();
    return true;
  }
  return false;
}

static void _got_ip_event_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  switch ( event_id )
  {
    case IP_EVENT_STA_GOT_IP:
      LOG( PRINT_INFO, "%s Have IP", __func__, event_base, event_id );
      if ( ( memcmp( ctx.wifi_saved_data.ssid, ctx.wifi_config_sta.sta.ssid,
                     MAX_VAL( strlen( (char*) ctx.wifi_config_sta.sta.ssid ), strlen( (char*) ctx.wifi_saved_data.ssid ) ) )
             != 0 )
           || ( memcmp( ctx.wifi_saved_data.password, ctx.wifi_config_sta.sta.password,
                        MAX_VAL( strlen( (char*) ctx.wifi_config_sta.sta.password ), strlen( (char*) ctx.wifi_saved_data.password ) ) )
                != 0 ) )
      {
        _wifi_data_save();
      }
      ip_event_got_ip_t* ip_event_got_ip = (ip_event_got_ip_t*) event_data;
      _safe_update_sta_ip_string( ip_event_got_ip->ip_info.ip.addr );

      ctx.connected = true;
      break;
  }
}

#if USE_DEBUG_HANDLER
static void _debug_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  LOG( PRINT_DEBUG, "%s EVENT_WIFI %s %d", __func__, event_base, event_id );
  if ( event_id == WIFI_EVENT_STA_DISCONNECTED )
  {
    __attribute__( ( unused ) ) wifi_event_sta_disconnected_t* data = event_data;
    LOG( PRINT_DEBUG, "Ssid %s bssid %x.%x.%x.%x.%x.%x len %d reason %d", data->ssid, data->bssid[0],
         data->bssid[1], data->bssid[2], data->bssid[3], data->bssid[4], data->bssid[5], data->ssid_len,
         data->reason );
  }
}
#endif

static void _client_connection_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  switch ( event_id )
  {
    case WIFI_EVENT_AP_STACONNECTED:
      {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        LOG( PRINT_DEBUG, "station " MACSTR " join, AID=%d", MAC2STR( event->mac ), event->aid );
        ctx.client_cnt++;
        break;
      }
    case WIFI_EVENT_AP_STADISCONNECTED:
      {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        LOG( PRINT_DEBUG, "station " MACSTR " leave, AID=%d", MAC2STR( event->mac ), event->aid );
        ctx.client_cnt--;
        break;
      }
  }
}

static void _init_driver( void )
{
  /* Nadawanie nazwy WiFi Access point oraz przypisanie do niego mac adresu */
  uint8_t mac[6];
  esp_efuse_mac_get_default( mac );
  strcpy( (char*) ctx.wifi_config_ap.ap.ssid, WIFI_AP_NAME );
  for ( int i = 0; i < sizeof( mac ); i++ )
  {
    sprintf( (char*) &ctx.wifi_config_ap.ap.ssid[strlen( (char*) ctx.wifi_config_ap.ap.ssid )], ":%x", mac[i] );
  }

  ctx.wifi_config_ap.ap.ssid_len = strlen( (char*) ctx.wifi_config_ap.ap.ssid );

  ctx.wifi_config_sta.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  ctx.wifi_config_sta.sta.pmf_cfg.capable = true;

  /* Inicjalizacja WiFi */
  ESP_ERROR_CHECK( esp_netif_init() );
  ESP_ERROR_CHECK( esp_event_loop_create_default() );

  ctx.esp_netif_ap = esp_netif_create_default_wifi_ap();
  ctx.esp_netif_sta = esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

  ESP_ERROR_CHECK( esp_wifi_init( &cfg ) );

  /* DHCP AP configuration */
  esp_netif_dhcps_stop( ctx.esp_netif_ap ); /* DHCP client/server must be stopped before setting new IP information. */
  esp_netif_ip_info_t ap_ip_info;
  memset( &ap_ip_info, 0x00, sizeof( ap_ip_info ) );
  inet_pton( AF_INET, DEFAULT_AP_IP, &ap_ip_info.ip );
  inet_pton( AF_INET, DEFAULT_AP_GATEWAY, &ap_ip_info.gw );
  inet_pton( AF_INET, DEFAULT_AP_NETMASK, &ap_ip_info.netmask );
  ESP_ERROR_CHECK( esp_netif_set_ip_info( ctx.esp_netif_ap, &ap_ip_info ) );
  ESP_ERROR_CHECK( esp_netif_dhcps_start( ctx.esp_netif_ap ) );
  ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &_on_wifi_disconnect, NULL ) );
  ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &_wifi_scan_done_handler, NULL ) );
  ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, &_got_ip_event_handler, NULL ) );
  ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, WIFI_EVENT_MASK_ALL, &_client_connection_handler, NULL ) );
#if USE_DEBUG_HANDLER
  ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, WIFI_EVENT_MASK_ALL, &_debug_handler, NULL ) );
  ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, WIFI_EVENT_MASK_ALL, &_debug_handler, NULL ) );
#endif
}

static void _state_init( void )
{
  ctx.is_scanned = false;
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    _start_access_point();
  }
  else
  {
    if ( wifi_type == T_WIFI_TYPE_CLIENT )
    {
      _start_sta_mode();
    }
    else
    {
      _start_ap_sta_mode();
    }
  }

  _clear_access_points_json();
  _clear_ip_info_json();
  _change_state( WIFI_APP_IDLE );
  LOG( PRINT_INFO, "Wifi init ok" );
}

static void _state_idle( void )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    _change_state( WIFI_APP_START );
    ctx.connected = true;
  }
  else
  {
    if ( wifi_type == T_WIFI_TYPE_CLI_SER )
    {
      _start_ap_sta_mode();
    }
    vTaskDelay( MS2ST( 50 ) );
    if ( ctx.connect_req )
    {
      _change_state( WIFI_APP_CONNECT );
    }
  }
}

static void _state_connect( void )
{
  assert( wifi_type != T_WIFI_TYPE_SERVER );
  int ret = 0;
  if ( !ctx.is_started )
  {
    LOG( PRINT_INFO, "WiFiDrv: WiFi start Device" );
    if ( wifi_type == T_WIFI_TYPE_CLIENT )
    {
      _start_sta_mode();
    }
  }

  esp_wifi_set_config( ESP_IF_WIFI_STA, &ctx.wifi_config_sta );
  ret = esp_wifi_connect();
  if ( ret == ESP_OK )
  {
    _change_state( WIFI_APP_WAIT_CONNECT );
  }
  else
  {
    LOG( PRINT_INFO, "Internal error connect %d attemps %d", ret, ctx.connect_attemps );
    _change_state( WIFI_APP_IDLE );
    if ( ctx.connect_attemps > 3 )
    {
      ctx.connect_attemps = 0;
      ctx.connect_req = false;
      ctx.is_started = false;
      esp_wifi_stop();
      _change_state( WIFI_APP_STOP );
    }

    ctx.connect_attemps++;
  }
}

static void _state_wait_connecting( void )
{
  if ( ctx.connected )
  {
    ctx.disconnect_req = false;
    ctx.connect_req = false;
    ctx.connect_attemps = 0;
    _change_state( WIFI_APP_START );
  }
  else
  {
    if ( ctx.connect_attemps > 30 )
    {
      LOG( PRINT_INFO, "Timeout connect" );
      ctx.connect_req = false;
      ctx.connect_attemps = 0;
      _change_state( WIFI_APP_STOP );
      if ( T_WIFI_TYPE_CLIENT == wifi_type )
      {
        ctx.is_started = false;
        esp_wifi_stop();
      }
    }
    vTaskDelay( MS2ST( 250 ) );
    ctx.connect_attemps++;
  }
}

static void _state_start( void )
{
  assert( _generate_ip_info_json( UPDATE_CONNECTION_OK ) );
  _run_from_cb_list( &ctx.on_connect_cb );
  _change_state( WIFI_APP_READY );
}

static void _state_stop( void )
{
  _run_from_cb_list( &ctx.on_disconnect_cb );

  if ( ctx.disconnect_req )
  {
    ctx.disconnect_req = false;
    if ( ctx.reason_disconnect != 0 )
    {
      LOG( PRINT_INFO, "WiFi reason %d", ctx.reason_disconnect );
      ctx.reason_disconnect = 0;
      _generate_ip_info_json( UPDATE_FAILED_ATTEMPT );
    }
    else
    {
      _generate_ip_info_json( UPDATE_USER_DISCONNECT );
    }
  }
  esp_wifi_disconnect();
  ctx.connected = 0;
  _change_state( WIFI_APP_IDLE );
}

static void _state_deinit( void )
{
  ctx.is_started = false;
  ctx.retry = 0;
  ctx.is_started = false;
  ctx.is_power_save = false;
  ctx.connected = false;
  ctx.disconnect_req = false;
  ctx.connect_req = false;
  ctx.connect_attemps = 0;
  ctx.reason_disconnect = 0;
  ctx.client_cnt = 0;
  ctx.scanned_ap_num = 0;
  ctx.is_scanned = false;

  _clear_access_points_json();
  _clear_ip_info_json();

  esp_wifi_disconnect();
  esp_wifi_stop();
  _change_state( WIFI_APP_DISABLE );
}

static void _state_disable( void )
{
  osDelay( 100 );
}

static void _state_ready( void )
{
  if ( ctx.disconnect_req || !ctx.connected || ctx.connect_req )
  {
    LOG( PRINT_INFO, "WiFi STOP reason disconnect_req %d connect %d connect_req %d", ctx.disconnect_req,
         !ctx.connected, ctx.connect_req );
    _change_state( WIFI_APP_STOP );
  }

  wifi_ap_record_t ap_info = { 0 };

  esp_wifi_sta_get_ap_info( &ap_info );
  ctx.rssi = ap_info.rssi;
  vTaskDelay( MS2ST( 200 ) );
}

static void _print_auth_mode( int authmode )
{
  switch ( authmode )
  {
    case WIFI_AUTH_OPEN:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_OPEN" );
      break;
    case WIFI_AUTH_WEP:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_WEP" );
      break;
    case WIFI_AUTH_WPA_PSK:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_WPA_PSK" );
      break;
    case WIFI_AUTH_WPA2_PSK:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_WPA2_PSK" );
      break;
    case WIFI_AUTH_WPA_WPA2_PSK:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK" );
      break;
    case WIFI_AUTH_WPA2_ENTERPRISE:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_WPA2_ENTERPRISE" );
      break;
    case WIFI_AUTH_WPA3_PSK:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_WPA3_PSK" );
      break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK" );
      break;
    default:
      LOG( PRINT_DEBUG, "Authmode \tWIFI_AUTH_UNKNOWN" );
      break;
  }
}

static void _print_cipher_type( int pairwise_cipher, int group_cipher )
{
  switch ( pairwise_cipher )
  {
    case WIFI_CIPHER_TYPE_NONE:
      LOG( PRINT_DEBUG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_NONE" );
      break;
    case WIFI_CIPHER_TYPE_WEP40:
      LOG( PRINT_DEBUG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP40" );
      break;
    case WIFI_CIPHER_TYPE_WEP104:
      LOG( PRINT_DEBUG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_WEP104" );
      break;
    case WIFI_CIPHER_TYPE_TKIP:
      LOG( PRINT_DEBUG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP" );
      break;
    case WIFI_CIPHER_TYPE_CCMP:
      LOG( PRINT_DEBUG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_CCMP" );
      break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
      LOG( PRINT_DEBUG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP" );
      break;
    default:
      LOG( PRINT_DEBUG, "Pairwise Cipher \tWIFI_CIPHER_TYPE_UNKNOWN" );
      break;
  }

  switch ( group_cipher )
  {
    case WIFI_CIPHER_TYPE_NONE:
      LOG( PRINT_DEBUG, "Group Cipher \tWIFI_CIPHER_TYPE_NONE" );
      break;
    case WIFI_CIPHER_TYPE_WEP40:
      LOG( PRINT_DEBUG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP40" );
      break;
    case WIFI_CIPHER_TYPE_WEP104:
      LOG( PRINT_DEBUG, "Group Cipher \tWIFI_CIPHER_TYPE_WEP104" );
      break;
    case WIFI_CIPHER_TYPE_TKIP:
      LOG( PRINT_DEBUG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP" );
      break;
    case WIFI_CIPHER_TYPE_CCMP:
      LOG( PRINT_DEBUG, "Group Cipher \tWIFI_CIPHER_TYPE_CCMP" );
      break;
    case WIFI_CIPHER_TYPE_TKIP_CCMP:
      LOG( PRINT_DEBUG, "Group Cipher \tWIFI_CIPHER_TYPE_TKIP_CCMP" );
      break;
    default:
      LOG( PRINT_DEBUG, "Group Cipher \tWIFI_CIPHER_TYPE_UNKNOWN" );
      break;
  }
}

#if WIFI_TEST_TASK
static void _scan_and_connect( const char* test_ap_name, const char* password )
{
  uint16_t ap_count = DEFAULT_SCAN_LIST_SIZE;
  char ssid[MAX_SSID_SIZE];
  int cnt = 0;

  while ( !wifiDrvIsReadyToScan() && cnt < 20 )
  {
    osDelay( 100 );
    cnt++;
  }

  assert( true == wifiDrvStartScan() );
  wifiDrvGetScanResult( &ap_count );
  LOG( PRINT_INFO, "Find ap_count: %d", ap_count );
  assert( ap_count <= DEFAULT_SCAN_LIST_SIZE );
  bool is_found_wifi = false;
  uint8_t i = 0;
  for ( i = 0; i < ap_count; i++ )
  {
    memset( ssid, 0, sizeof( ssid ) );
    wifiDrvGetNameFromScannedList( i, ssid );
    LOG( PRINT_INFO, "Scan result %u: %s", i, ssid, test_ap_name );
    if ( memcmp( ssid, test_ap_name, strlen( test_ap_name ) ) == 0 )
    {
      LOG( PRINT_INFO, "Found: %s", ssid );
      is_found_wifi = true;
      break;
    }
  }
  assert( is_found_wifi );
  const char* json_str = wifiDrvGetAccessPointsListJson();
  LOG( PRINT_INFO, "json devices: %s", json_str );

  assert( wifiDrvSetAPName( test_ap_name, strlen( test_ap_name ) ) );
  assert( wifiDrvSetPassword( password, strlen( password ) ) );
  assert( wifiDrvConnect() );

  LOG( PRINT_INFO, "Try to connect: %s", ssid );

  cnt = 0;
  while ( !wifiDrvIsConnected() && cnt < 50 )
  {
    osDelay( 100 );
    cnt++;
  }
  assert( wifiDrvIsConnected() );
  json_str = wifiDrvGetIpInfoJson();
  LOG( PRINT_INFO, "IP json: %s", json_str );
  LOG( PRINT_INFO, "Scan and connect: PASS" );
}

static void _wifi_test_task( void* pv )
{
  /* AP Tests */
  int seconds_to_wait = 180;
  wifiDrvStop();
  wifiDrvSetWifiType( T_WIFI_TYPE_SERVER );
  wifiDrvStart();
  LOG( PRINT_INFO, "Start wifi device as server, wait %d seconds to any connection", seconds_to_wait );
  int cnt = 0;
  while ( wifiDrvGetClientCount() == 0 && cnt < seconds_to_wait )
  {
    osDelay( 1000 );
    cnt++;
  }

  LOG( PRINT_INFO, "wifiDrvGetClientCount() %" PRIu32 "", wifiDrvGetClientCount() );

  /* STA Tests */
  const char* test_ap1_name = "wifitest";
  const char* test_ap1_password = "12345678";
  const char* test_ap2_name = "TP-Link_2AC1";
  const char* test_ap2_password = "19681115";
  wifiDrvStop();
  wifiDrvSetWifiType( T_WIFI_TYPE_CLIENT );
  wifiDrvStart();
  LOG( PRINT_INFO, "Start device as client, please turn on %s and %s", test_ap1_name, test_ap2_name );
  osDelay( 10000 );
  _scan_and_connect( test_ap1_name, test_ap1_password );
  _scan_and_connect( test_ap2_name, test_ap2_password );

  /* Soft AP Tests */
  osDelay( 1000 );
  wifiDrvStop();
  wifiDrvSetWifiType( T_WIFI_TYPE_CLI_SER );
  wifiDrvStart();

  _scan_and_connect( test_ap2_name, test_ap2_password );

  while ( wifiDrvGetClientCount() == 0 )
  {
    osDelay( 100 );
  }

  wifiDrvStop();

  vTaskDelete( NULL );
}
#endif

static void _wifi_event_task( void* pv )
{
  _init_driver();
  while ( 1 )
  {
    switch ( ctx.state )
    {
      case WIFI_APP_INIT:
        _state_init();
        break;

      case WIFI_APP_IDLE:
        _state_idle();
        break;

      case WIFI_APP_CONNECT:
        _state_connect();
        break;

      case WIFI_APP_WAIT_CONNECT:
        _state_wait_connecting();
        break;

      case WIFI_APP_START:
        _state_start();
        break;

      case WIFI_APP_STOP:
        _state_stop();
        break;

      case WIFI_APP_READY:
        _state_ready();
        break;

      case WIFI_APP_DEINIT:
        _state_deinit();
        break;

      case WIFI_APP_DISABLE:
        _state_disable();
        break;

      default:
        _change_state( WIFI_APP_IDLE );
    }
  }
}

void wifiDrvInit( void )
{
  esp_log_level_set( "wifi", ESP_LOG_WARN );
  esp_log_level_set( "esp_netif_handlers", ESP_LOG_WARN );
  _wifi_data_read();
  _init_list( &ctx.on_connect_cb );
  _init_list( &ctx.on_disconnect_cb );
  ctx.ip_mutex = xSemaphoreCreateMutex();
  ctx.json_mutex = xSemaphoreCreateMutex();
  xTaskCreate( _wifi_event_task, "_wifi_event_task", CONFIG_TCPIP_EVENT_THD_WA_SIZE, NULL, NORMALPRIO, NULL );
#if WIFI_TEST_TASK
  xTaskCreate( _wifi_test_task, "_wifi_test_task", CONFIG_TCPIP_EVENT_THD_WA_SIZE, NULL, NORMALPRIO, NULL );
#endif
}

void wifiDrvSetWifiType( wifi_type_t type )
{
  assert( ctx.state == WIFI_APP_DISABLE );
  wifi_type = type;
}

void wifiDrvStop( void )
{
  int cnt = 0;
  if ( ctx.state != WIFI_APP_DISABLE )
  {
    _change_state( WIFI_APP_DEINIT );
  }
  /* Waiting to changing state to Disable */
  while ( ctx.state != WIFI_APP_DISABLE && cnt < 20 )
  {
    osDelay( 100 );
    cnt++;
  }
  assert( ctx.state == WIFI_APP_DISABLE );
}

void wifiDrvStart( void )
{
  if ( ctx.state == WIFI_APP_DISABLE )
  {
    int cnt = 0;
    _change_state( WIFI_APP_INIT );
    /* Waiting to changing state */
    while ( ctx.state <= WIFI_APP_INIT && cnt < 20 )
    {
      osDelay( 25 );
      cnt++;
    }
    assert( ctx.state > WIFI_APP_INIT );
  }
}

bool _scan( bool block )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    return false;
  }

  if ( ctx.is_scanned )
  {
    return false;
  }

  wifi_scan_config_t scan_config = { 0 };

  if ( ( ctx.state == WIFI_APP_IDLE ) || ( ctx.state == WIFI_APP_READY ) )
  {
    if ( wifi_type == T_WIFI_TYPE_CLIENT )
    {
      _start_sta_mode();
    }
    else
    {
      _start_ap_sta_mode();
    }
    ctx.is_scanned = true;
    LOG( PRINT_INFO, "start scan %d", block );
    return esp_wifi_scan_start( &scan_config, block ) == ESP_OK;
  }

  return false;
}

bool wifiDrvStartScan( void )
{
  return _scan( true );
}

bool wifiDrvStartScanNoBlock( void )
{
  return _scan( false );
}

void wifiDrvGetScanResult( uint16_t* ap_count )
{
  *ap_count = ctx.scanned_ap_num;
  for ( uint32_t i = 0; i < ctx.scanned_ap_num; i++ )
  {
    LOG( PRINT_DEBUG, "AP: %s CH %d CH2 %d RSSI %d", ctx.scan_list[i].ssid, ctx.scan_list[i].primary,
         ctx.scan_list[i].second, ctx.scan_list[i].rssi );
    _print_auth_mode( ctx.scan_list[i].authmode );
    if ( ctx.scan_list[i].authmode != WIFI_AUTH_WEP )
    {
      _print_cipher_type( ctx.scan_list[i].pairwise_cipher, ctx.scan_list[i].group_cipher );
    }
  }
}

bool wifiDrvGetNameFromScannedList( uint8_t number, char* name )
{
  strcpy( name, (char*) ctx.scan_list[number].ssid );
  return true;
}

bool wifiDrvSetFromAPList( uint8_t num )
{
  if ( num > DEFAULT_SCAN_LIST_SIZE )
  {
    return false;
  }

  strncpy( (char*) ctx.wifi_config_sta.sta.ssid, (char*) ctx.scan_list[num].ssid, sizeof( ctx.wifi_config_sta.sta.ssid ) );
  return true;
}

bool wifiDrvSetAPName( const char* name, size_t len )
{
  memset( ctx.wifi_config_sta.sta.ssid, 0, sizeof( ctx.wifi_config_sta.sta.ssid ) );
  if ( len > sizeof( ctx.wifi_config_sta.sta.ssid ) )
  {
    LOG( PRINT_ERROR, "%s name is to long", __func__ );
    return false;
  }

  memcpy( ctx.wifi_config_sta.sta.ssid, name, len );
  LOG( PRINT_INFO, "Set AP Name %s", ctx.wifi_config_sta.sta.ssid );
  return true;
}

bool wifiDrvGetAPName( char* name )
{
  strcpy( name, (char*) ctx.wifi_config_sta.sta.ssid );
  return true;
}

bool wifiDrvSetPassword( const char* passwd, size_t len )
{
  memset( ctx.wifi_config_sta.sta.password, 0, sizeof( ctx.wifi_config_sta.sta.password ) );
  if ( len > sizeof( ctx.wifi_config_sta.sta.password ) )
  {
    LOG( PRINT_ERROR, "%s name is to long", __func__ );
    return false;
  }

  memcpy( ctx.wifi_config_sta.sta.password, passwd, len );
  LOG( PRINT_INFO, "Set AP Password %s", ctx.wifi_config_sta.sta.password );
  return true;
}

bool wifiDrvConnect( void )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    return false;
  }

  ctx.connect_req = true;
  return true;
}

bool wifiDrvDisconnect( void )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    return false;
  }

  ctx.disconnect_req = true;
  return true;
}

bool wifiDrvIsConnected( void )
{
  return ctx.connected && ctx.connect_req == false;
}

bool wifiDrvIsReadyToScan( void )
{
  return ctx.state == WIFI_APP_IDLE || ctx.state == WIFI_APP_READY;
}

bool wifiDrvReadyToConnect( void )
{
  return ctx.state == WIFI_APP_IDLE;
}

bool wifiDrvTryingConnect( void )
{
  return ctx.state == WIFI_APP_WAIT_CONNECT || ctx.state == WIFI_APP_CONNECT;
}

bool wifiDrvIsReadData( void )
{
  return ctx.read_wifi_data;
}

int wifiDrvGetRssi( void )
{
  return ctx.rssi;
}

void wifiDrvPowerSave( bool state )
{
  //pm_config.light_sleep_enable = state;
  ctx.is_power_save = state;
  // if (esp_pm_configure(&pm_config) != ESP_OK)
  // {
  //   LOG(PRINT_INFO, "WiFi Error: error set power save");
  // }
}

void wifiDrvRegisterConnectCb( wifi_drv_callback cb )
{
  _add_to_list( &ctx.on_connect_cb, cb );
}

void wifiDrvRegisterDisconnectCb( wifi_drv_callback cb )
{
  _add_to_list( &ctx.on_disconnect_cb, cb );
}

uint32_t wifiDrvGetClientCount( void )
{
  return ctx.client_cnt;
}

bool wifiDrvGetIpAddr( char* ip, size_t len )
{
  if ( strlen( ip ) < len )
  {
    memset( ip, 0, len );
    _lock_sta_ip_string( portMAX_DELAY );
    strcpy( ip, ctx.ip_addr );
    _unlock_sta_ip_string();
    return true;
  }

  return false;
}

bool wifiDrvLockJsonBuffer( size_t ms )
{
  if ( ctx.json_mutex )
  {
    if ( xSemaphoreTake( ctx.json_mutex, MS2ST( ms ) ) == pdTRUE )
    {
      LOG( PRINT_INFO, "Lock ctx.json_mutex" );
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    return false;
  }
}

void wifiDrvUnlockJsonBuffer( void )
{
  xSemaphoreGive( ctx.json_mutex );
  LOG( PRINT_INFO, "Unlock ctx.json_mutex" );
}

const char* wifiDrvGetAccessPointsListJson( void )
{
  // _generate_access_points_json();
  return (const char*) ctx.access_points_json;
}

const char* wifiDrvGetIpInfoJson( void )
{
  return (const char*) ctx.ip_info_json;
}