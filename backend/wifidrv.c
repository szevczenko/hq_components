#include "wifidrv.h"

#include "app_config.h"
#include "cmd_client.h"
#include "cmd_server.h"
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
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sleep_e.h"

#define MODULE_NAME "[WiFi] "
#define DEBUG_LVL   PRINT_INFO

#if CONFIG_DEBUG_WIFI
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

#define DEFAULT_SCAN_LIST_SIZE         32
#define CONFIG_TCPIP_EVENT_THD_WA_SIZE 4096
#define MAX_VAL( a, b )                a > b ? a : b

typedef enum
{
  WIFI_APP_INIT = 0,
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
    [WIFI_APP_DEINIT] = "WIFI_APP_DEINIT" };

typedef struct
{
  wifi_app_status_t state;
  int retry;
  bool is_started;
  bool is_power_save;
  bool connected;
  bool disconect_req;
  bool connect_req;
  bool read_wifi_data;
  uint32_t connect_attemps;
  uint32_t reason_disconnect;
  wifi_ap_record_t scan_list[DEFAULT_SCAN_LIST_SIZE];
  wifi_config_t wifi_config;
  wifiConData_t wifi_con_data;
  int rssi;
} wifidrv_ctx_t;

static wifidrv_ctx_t ctx;
static uint8_t wifi_type;

wifi_config_t wifi_config_ap =
  {
    .ap =
      {
           .password = WIFI_AP_PASSWORD,
           .max_connection = 2,
           .authmode = WIFI_AUTH_WPA_WPA2_PSK},
};

//esp_pm_config_esp8266_t pm_config;

static void wifi_read_info_cb( void* arg, wifi_vendor_ie_type_t type, const uint8_t sa[6],
                               const vendor_ie_data_t* vnd_ie, int rssi );

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

static void on_wifi_disconnect( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;

  ctx.connected = false;
  ctx.reason_disconnect = event->reason;
  // tcpip_adapter_down(TCPIP_ADAPTER_IF_STA);
}

static void scan_done_handler( void )
{
  LOG( PRINT_INFO, "SCAN DONE!!!!!!" );
}

static void wifi_scan_done_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  switch ( event_id )
  {
    case WIFI_EVENT_SCAN_DONE:
      scan_done_handler();
      break;
    default:
      break;
  }
}

static void got_ip_event_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  switch ( event_id )
  {
    case IP_EVENT_STA_GOT_IP:
      LOG( PRINT_INFO, "%s Have IP", __func__, event_base, event_id );
      if ( ( memcmp( ctx.wifi_con_data.ssid, ctx.wifi_config.sta.ssid,
                     MAX_VAL( strlen( (char*) ctx.wifi_config.sta.ssid ), strlen( (char*) ctx.wifi_con_data.ssid ) ) )
             != 0 )
           || ( memcmp( ctx.wifi_con_data.password, ctx.wifi_config.sta.password,
                        MAX_VAL( strlen( (char*) ctx.wifi_config.sta.password ), strlen( (char*) ctx.wifi_con_data.password ) ) )
                != 0 ) )
      {
        strncpy( (char*) ctx.wifi_con_data.ssid, (char*) ctx.wifi_config.sta.ssid, sizeof( ctx.wifi_con_data.ssid ) );
        strncpy( (char*) ctx.wifi_con_data.password, (char*) ctx.wifi_config.sta.password,
                 sizeof( ctx.wifi_con_data.password ) );
        wifiDataSave( &ctx.wifi_con_data );
      }

      ctx.connected = true;
      break;
  }
}

static void debug_handler( void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data )
{
  LOG( PRINT_DEBUG, "%s EVENT_WIFI %s %d", __func__, event_base, event_id );
  if ( event_id == WIFI_EVENT_STA_DISCONNECTED )
  {
    __attribute__( ( unused ) ) wifi_event_sta_disconnected_t* data = event_data;
    LOG( PRINT_DEBUG, "Ssid %s bssid %x.%x.%x.%x.%x.%x len %d reason %d/n/r", data->ssid, data->bssid[0],
         data->bssid[1], data->bssid[2], data->bssid[3], data->bssid[4], data->bssid[5], data->ssid_len,
         data->reason );
  }
}

int wifiDataSave( wifiConData_t* data )
{
  nvs_handle my_handle;
  esp_err_t err;

  // Open
  err = nvs_open( WIFI_AP_NAME, NVS_READWRITE, &my_handle );
  if ( err != ESP_OK )
  {
    nvs_close( my_handle );
    return err;
  }

  err = nvs_set_blob( my_handle, "wifi", data, sizeof( wifiConData_t ) );

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

esp_err_t wifiDataRead( wifiConData_t* data )
{
  nvs_handle my_handle;
  esp_err_t err;

  // Open
  err = nvs_open( WIFI_AP_NAME, NVS_READWRITE, &my_handle );
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
    err = nvs_get_blob( my_handle, "wifi", data, &required_size );
    nvs_close( my_handle );
    return err;
  }

  nvs_close( my_handle );
  return ESP_ERR_NVS_NOT_FOUND;
}

static void wifi_init( void )
{
  /* Nadawanie nazwy WiFi Access point oraz przypisanie do niego mac adresu */
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    uint8_t mac[6];
    esp_efuse_mac_get_default( mac );
    strcpy( (char*) wifi_config_ap.ap.ssid, WIFI_AP_NAME );
    for ( int i = 0; i < sizeof( mac ); i++ )
    {
      sprintf( (char*) &wifi_config_ap.ap.ssid[strlen( (char*) wifi_config_ap.ap.ssid )], ":%x", mac[i] );
    }

    wifi_config_ap.ap.ssid_len = strlen( (char*) wifi_config_ap.ap.ssid );
  }

  ctx.wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  ctx.wifi_config.sta.pmf_cfg.capable = true;

  /* Inicjalizacja WiFi */
  ESP_ERROR_CHECK( esp_netif_init() );
  ESP_ERROR_CHECK( esp_event_loop_create_default() );
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    esp_netif_create_default_wifi_ap();
  }
  else
  {
    esp_netif_create_default_wifi_sta();
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

  ESP_ERROR_CHECK( esp_wifi_init( &cfg ) );

  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    wifiStartAccessPoint();
  }
  else
  {
    if ( wifiDataRead( &ctx.wifi_con_data ) == ESP_OK )
    {
      strncpy( (char*) ctx.wifi_config.sta.ssid, (char*) ctx.wifi_con_data.ssid, sizeof( ctx.wifi_config.sta.ssid ) );
      strncpy( (char*) ctx.wifi_config.sta.password, (char*) ctx.wifi_con_data.password,
               sizeof( ctx.wifi_config.sta.password ) );
      ctx.read_wifi_data = true;
    }
    else
    {
      esp_wifi_set_config( WIFI_IF_STA, &ctx.wifi_config );
    }

    wifiStartDevice();
  }

  ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL ) );
  ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &wifi_scan_done_handler, NULL ) );
  ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_event_handler, NULL ) );
  ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, WIFI_EVENT_MASK_ALL, &debug_handler, NULL ) );
  ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, WIFI_EVENT_MASK_ALL, &debug_handler, NULL ) );

  _change_state( WIFI_APP_IDLE );
  LOG( PRINT_INFO, "Wifi init ok" );
}

static void wifi_idle( void )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    _change_state( WIFI_APP_START );
    ctx.connected = true;
  }
  else
  {
    vTaskDelay( MS2ST( 250 ) );
    if ( ctx.connect_req )
    {
      _change_state( WIFI_APP_CONNECT );
    }
  }
}

static void wifi_connect( void )
{
  int ret = 0;

  if ( !ctx.is_started )
  {
    LOG( PRINT_INFO, "WiFiDrv: WiFi start Device" );
    wifiStartDevice();
  }

  esp_wifi_set_config( ESP_IF_WIFI_STA, &ctx.wifi_config );
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
    }

    ctx.connect_attemps++;
  }
}

static void wifi_wait_connect( void )
{
  if ( ctx.connected )
  {
    ctx.disconect_req = false;
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
      _change_state( WIFI_APP_IDLE );
      ctx.is_started = false;
      esp_wifi_disconnect();
      esp_wifi_stop();
    }

    vTaskDelay( MS2ST( 250 ) );
    ctx.connect_attemps++;
  }
}

static void wifi_app_start( void )
{
  osDelay( 100 );
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    cmdServerStart();
  }
  else
  {
    cmdClientStart();
  }

  _change_state( WIFI_APP_READY );
}

static void wifi_app_stop( void )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    cmdServerStop();
  }
  else
  {
    cmdClientStop();
  }

  if ( ctx.disconect_req )
  {
    ctx.disconect_req = false;
    if ( ctx.reason_disconnect != 0 )
    {
      LOG( PRINT_INFO, "WiFi reason %d", ctx.reason_disconnect );
      ctx.reason_disconnect = 0;
    }
  }

  esp_wifi_disconnect();
  ctx.connected = 0;
  _change_state( WIFI_APP_IDLE );
}

static void wifi_app_ready( void )
{
  if ( ctx.disconect_req || !ctx.connected || ctx.connect_req )
  {
    LOG( PRINT_INFO, "WiFi STOP reason disconnect_req %d connect %d conect_req %d", ctx.disconect_req,
         !ctx.connected, ctx.connect_req );
    _change_state( WIFI_APP_STOP );
  }

  wifi_ap_record_t ap_info = { 0 };

  esp_wifi_sta_get_ap_info( &ap_info );
  ctx.rssi = ap_info.rssi;
  vTaskDelay( MS2ST( 200 ) );
}

int wifiDrvStartScan( void )
{
  wifi_scan_config_t scan_config = { 0 };

  if ( ( ctx.state == WIFI_APP_IDLE ) || ( ctx.state == WIFI_APP_READY ) )
  {
    if ( !ctx.is_started )
    {
      wifiStartDevice();
      osDelay( 100 );
    }

    return esp_wifi_scan_start( &scan_config, true );
  }

  return -1;
}

static void print_auth_mode( int authmode )
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

static void print_cipher_type( int pairwise_cipher, int group_cipher )
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

void wifiDrvGetScanResult( uint16_t* ap_count )
{
  uint16_t number = DEFAULT_SCAN_LIST_SIZE;

  ESP_ERROR_CHECK( esp_wifi_scan_get_ap_records( &number, ctx.scan_list ) );
  ESP_ERROR_CHECK( esp_wifi_scan_get_ap_num( ap_count ) );
  for ( uint32_t i = 0; i < *ap_count; i++ )
  {
    LOG( PRINT_DEBUG, "AP: %s CH %d CH2 %d RSSI %d", ctx.scan_list[i].ssid, ctx.scan_list[i].primary,
         ctx.scan_list[i].second, ctx.scan_list[i].rssi );
    print_auth_mode( ctx.scan_list[i].authmode );
    if ( ctx.scan_list[i].authmode != WIFI_AUTH_WEP )
    {
      print_cipher_type( ctx.scan_list[i].pairwise_cipher, ctx.scan_list[i].group_cipher );
    }
  }
}

int wifiDrvGetNameFromScannedList( uint8_t number, char* name )
{
  strcpy( name, (char*) ctx.scan_list[number].ssid );
  return true;
}

int wifiDrvSetFromAPList( uint8_t num )
{
  if ( num > DEFAULT_SCAN_LIST_SIZE )
  {
    return 0;
  }

  strncpy( (char*) ctx.wifi_config.sta.ssid, (char*) ctx.scan_list[num].ssid, sizeof( ctx.wifi_config.sta.ssid ) );
  return 1;
}

int wifiDrvSetAPName( char* name, size_t len )
{
  memset( ctx.wifi_config.sta.ssid, 0, sizeof( ctx.wifi_config.sta.ssid ) );
  if ( len > sizeof( ctx.wifi_config.sta.ssid ) )
  {
    LOG( PRINT_ERROR, "%s name is to long", __func__ );
    return 0;
  }

  memcpy( ctx.wifi_config.sta.ssid, name, len );
  LOG( PRINT_INFO, "Set AP Name %s", ctx.wifi_config.sta.ssid );
  return 1;
}

int wifiDrvGetAPName( char* name )
{
  strcpy( name, (char*) ctx.wifi_config.sta.ssid );
  return true;
}

int wifiDrvSetPassword( char* passwd, size_t len )
{
  strncpy( (char*) ctx.wifi_config.sta.password, passwd, sizeof( ctx.wifi_config.sta.password ) );
  return 1;
}

int wifiDrvConnect( void )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    return -1;
  }

  ctx.connect_req = true;
  return 1;
}

int wifiDrvDisconnect( void )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    return -1;
  }

  ctx.disconect_req = true;
  return 1;
}

int wifiStartDevice( void )
{
  if ( ctx.is_started )
  {
    return false;
  }

  ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_STA ) );
  ESP_ERROR_CHECK( esp_wifi_start() );
  ctx.is_started = true;
  esp_wifi_set_vendor_ie_cb( wifi_read_info_cb, NULL );
  wifiDrvPowerSave( false );
  return 1;
}

int wifiStartAccessPoint( void )
{
  ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_AP ) );
  ESP_ERROR_CHECK( esp_wifi_set_config( ESP_IF_WIFI_AP, &wifi_config_ap ) );
  ESP_ERROR_CHECK( esp_wifi_start() );
  ctx.is_started = true;
  return 1;
}

int wifiDrvIsConnected( void )
{
  return ctx.connected;
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

static void wifi_event_task( void* pv )
{
  while ( 1 )
  {
    // if (ctx.retry > 10) {
    //   stastus_reg_eth = 0;
    //   esp_wifi_stop();
    //   osDelay(1000);
    //   wifiDrvConnect();
    //   ctx.retry = 0;
    // }

    switch ( ctx.state )
    {
      case WIFI_APP_INIT:
        wifi_init();
        break;

      case WIFI_APP_IDLE:
        wifi_idle();
        break;

      case WIFI_APP_CONNECT:
        wifi_connect();
        break;

      case WIFI_APP_WAIT_CONNECT:
        wifi_wait_connect();
        break;

      case WIFI_APP_START:
        wifi_app_start();
        break;

      case WIFI_APP_STOP:
        wifi_app_stop();
        break;

      case WIFI_APP_READY:
        wifi_app_ready();
        break;

      case WIFI_APP_DEINIT:
        wifi_app_stop();
        break;

      default:
        _change_state( WIFI_APP_IDLE );
    }
  }
}

static void wifi_read_info_cb( void* arg, wifi_vendor_ie_type_t type, const uint8_t sa[6],
                               const vendor_ie_data_t* vnd_ie, int rssi )
{
}

bool wifiDrvIsReadedData( void )
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

void wifiDrvInit( void )
{
  if ( wifi_type == T_WIFI_TYPE_SERVER )
  {
    cmdServerStartTask();
  }
  else
  {
    cmdClientStartTask();
  }

  xTaskCreate( wifi_event_task, "wifi_event_task", CONFIG_TCPIP_EVENT_THD_WA_SIZE, NULL, NORMALPRIO, NULL );
}

void wifiDrvSetWifiType( wifi_type_t type )
{
  wifi_type = type;
}
