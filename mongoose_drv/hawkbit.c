// #include "hawkbit.h"

// #include <mongoose.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>

// typedef struct
// {
//   char _putConfigDataHref[512];
//   char _getDeploymentBaseHref[512];
//   char _getCancelActionHref[512];
//   char _getSoftwareModuleHref[1024];
//   char _configData[512];
//   unsigned long _nextPoll;
//   unsigned long _pollInterval;
//   unsigned long _jobSchedule;
//   bool _jobFeedbackChanged;
//   int _currentActionId;
//   unsigned long _updateSize;
//   struct mg_connection* conn;
//   uint16_t _serverPort;
//   char* _serverName;
//   char* _tenantId;
//   char* _controllerId;
//   char* _securityToken;
//   hawkbit_security_type _securityType;
//   hawkbit_execution_status _currentExecutionStatus;
//   hawkbit_execution_result _currentExecutionResult;
//   hawkbit_deployment_mode _currentDeploymentMode;
// } HawkbitDdi;

// // Helper function to create headers
// static char* createHeaders( HawkbitDdi* self, const char* acceptType )
// {
//   static char headers[1024];
//   size_t strsize = 0;
//   snprintf( headers, sizeof( headers ), "Host: %s\r\n", self->_serverName );
//   strsize = strlen( headers );
//   switch ( self->_securityType )
//   {
//     case HAWKBIT_SEC_GATEWAYTOKEN:
//     case HAWKBIT_SEC_TARGETTOKEN:
//       snprintf( headers + strsize, sizeof( headers ) - strsize, "Authorization: %s %s\r\n",
//                 ( self->_securityType == HAWKBIT_SEC_GATEWAYTOKEN ) ? "GatewayToken" : "TargetToken",
//                 self->_securityToken );
//       strsize = strlen( headers );
//       break;
//     default:
//       break;
//   }
//   if ( acceptType != NULL && strlen( acceptType ) > 0 )
//   {
//     snprintf( headers + strsize, sizeof( headers ) - strsize, "Accept: %s\r\n", acceptType );
//     strsize = strlen( headers );
//   }
//   snprintf( headers + strsize, sizeof( headers ) - strsize, "Connection: close\r\n" );
//   return headers;
// }

// void HawkbitDdi_init( HawkbitDdi* self, char* serverName, uint16_t serverPort, char* tenantId, char* controllerId, char* securityToken, hawkbit_security_type securityType )
// {
//   self->_serverName = serverName;
//   self->_serverPort = serverPort;
//   self->_tenantId = tenantId;
//   self->_controllerId = controllerId;
//   self->_securityToken = securityToken;
//   self->_securityType = securityType;
//   self->_nextPoll = 0;
//   self->_pollInterval = 300000UL;
//   self->_jobFeedbackChanged = false;
//   self->_currentActionId = -1;
//   self->_currentExecutionStatus = HAWKBIT_EX_CLOSED;
//   self->_currentExecutionResult = HAWKBIT_RES_NONE;
// }

// void HawkbitDdi_begin( HawkbitDdi* self, struct mg_connection* conn )
// {
//   self->conn = conn;
//   self->_currentExecutionStatus = HAWKBIT_EX_CLOSED;
//   self->_currentExecutionResult = HAWKBIT_RES_NONE;
//   pollController( self );
//   putConfigData( self, HAWKBIT_CONFIGDATA_REPLACE );
//   HawkbitDdi_work( self );
// }

// int HawkbitDdi_work( HawkbitDdi* self )
// {
//   int retStatus = -1;
//   if ( mg_time() > self->_nextPoll )
//   {
//     pollController( self );
//     if ( strnlen( self->_putConfigDataHref, sizeof( self->_putConfigDataHref ) ) > 0 )
//     {
//       printf( "Need to put config data\n" );
//       putConfigData( self, HAWKBIT_CONFIGDATA_MERGE );
//     }
//     if ( strnlen( self->_getDeploymentBaseHref, sizeof( self->_getDeploymentBaseHref ) ) > 0 && self->_currentActionId <= 0 )
//     {
//       printf( "Need to get Deployment Base\n" );
//       getDeploymentBase( self );
//     }
//     if ( strnlen( self->_getCancelActionHref, sizeof( self->_getCancelActionHref ) ) > 0 )
//     {
//       printf( "Need to get Cancel Action Information\n" );
//       getCancelAction( self );
//     }
//   }
//   if ( self->_currentActionId > 0 )
//   {
//     switch ( self->_currentExecutionStatus )
//     {
//       case HAWKBIT_EX_PROCEEDING:
//         getAndInstallUpdateImage( self );
//         break;
//       case HAWKBIT_EX_SCHEDULED:
//         if ( mg_time() > self->_jobSchedule )
//         {
//           self->_currentExecutionStatus = HAWKBIT_EX_PROCEEDING;
//           self->_currentExecutionResult = HAWKBIT_RES_NONE;
//           self->_jobFeedbackChanged = true;
//         }
//         break;
//       case HAWKBIT_EX_CANCELED:
//       case HAWKBIT_EX_CLOSED:
//         break;
//       default:
//         self->_currentExecutionStatus = HAWKBIT_EX_PROCEEDING;
//         self->_currentExecutionResult = HAWKBIT_RES_NONE;
//         self->_jobFeedbackChanged = true;
//         break;
//     }
//     if ( self->_jobFeedbackChanged )
//     {
//       if ( self->_currentExecutionStatus == HAWKBIT_EX_CANCELED )
//       {
//         postCancelFeedback( self );
//       }
//       else
//       {
//         postDeploymentBaseFeedback( self );
//       }
//       self->_jobFeedbackChanged = false;
//     }
//     if ( self->_currentExecutionStatus == HAWKBIT_EX_CLOSED )
//     {
//       self->_currentActionId = 0;
//       ESP_restart();
//     }
//   }
//   return retStatus;
// }

// void pollController( HawkbitDdi* self )
// {
//   char url[256];
//   snprintf( url, sizeof( url ), "https://%s:%d/%s/controller/v1/%s", self->_serverName, self->_serverPort, self->_tenantId, self->_controllerId );
//   struct mg_mgr mgr;
//   mg_mgr_init( &mgr );
//   self->conn = mg_http_connect( &mgr, url, NULL, NULL );
//   if ( self->conn == NULL )
//   {
//     printf( "Connection failed!\n" );
//     return;
//   }
//   mg_printf( self->conn, "GET %s HTTP/1.1\r\n%s\r\n", url, createHeaders( self, "application/hal+json" ) );
//   mg_mgr_poll( &mgr, 5000 );
//   mg_mgr_free( &mgr );
// }

// void putConfigData( HawkbitDdi* self, HAWKBIT_CONFIGDATA_MODE cf_mode )
// {
//   char url[256];
//   snprintf( url, sizeof( url ), "https://%s:%d/%s/controller/v1/%s/configData", self->_serverName, self->_serverPort, self->_tenantId, self->_controllerId );
//   struct mg_mgr mgr;
//   mg_mgr_init( &mgr );
//   self->conn = mg_http_connect( &mgr, url, NULL, NULL );
//   if ( self->conn == NULL )
//   {
//     printf( "Connection failed!\n" );
//     return;
//   }
//   struct mg_str json_data = mg_str( self->_configData );
//   mg_printf( self->conn, "PUT %s HTTP/1.1\r\n%s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%.*s",
//              url, createHeaders( self, "application/json" ), (int) json_data.len, (int) json_data.len, json_data.p );
//   mg_mgr_poll( &mgr, 5000 );
//   mg_mgr_free( &mgr );
// }

// void getDeploymentBase( HawkbitDdi* self )
// {
//   char url[256];
//   snprintf( url, sizeof( url ), "https://%s:%d/%s", self->_serverName, self->_serverPort, self->_getDeploymentBaseHref );
//   struct mg_mgr mgr;
//   mg_mgr_init( &mgr );
//   self->conn = mg_http_connect( &mgr, url, NULL, NULL );
//   if ( self->conn == NULL )
//   {
//     printf( "Connection failed!\n" );
//     return;
//   }
//   mg_printf( self->conn, "GET %s HTTP/1.1\r\n%s\r\n", url, createHeaders( self, "application/hal+json" ) );
//   mg_mgr_poll( &mgr, 5000 );
//   mg_mgr_free( &mgr );
// }

// void postDeploymentBaseFeedback( HawkbitDdi* self )
// {
//   char url[256];
//   snprintf( url, sizeof( url ), "https://%s:%d/%s/controller/v1/%s/deploymentBase/%d/feedback", self->_serverName, self->_serverPort, self->_tenantId, self->_controllerId, self->_currentActionId );
//   struct mg_mgr mgr;
//   mg_mgr_init( &mgr );
//   self->conn = mg_http_connect( &mgr, url, NULL, NULL );
//   if ( self->conn == NULL )
//   {
//     printf( "Connection failed!\n" );
//     return;
//   }
//   struct mg_str json_data = mg_str( self->_configData );
//   mg_printf( self->conn, "POST %s HTTP/1.1\r\n%s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%.*s",
//              url, createHeaders( self, "application/json" ), (int) json_data.len, (int) json_data.len, json_data.p );
//   mg_mgr_poll( &mgr, 5000 );
//   mg_mgr_free( &mgr );
// }

// void getCancelAction( HawkbitDdi* self )
// {
//   char url[256];
//   snprintf( url, sizeof( url ), "https://%s:%d/%s", self->_serverName, self->_serverPort, self->_getCancelActionHref );
//   struct mg_mgr mgr;
//   mg_mgr_init( &mgr );
//   self->conn = mg_http_connect( &mgr, url, NULL, NULL );
//   if ( self->conn == NULL )
//   {
//     printf( "Connection failed!\n" );
//     return;
//   }
//   mg_printf( self->conn, "GET %s HTTP/1.1\r\n%s\r\n", url, createHeaders( self, "application/hal+json" ) );
//   mg_mgr_poll( &mgr, 5000 );
//   mg_mgr_free( &mgr );
// }

// void postCancelFeedback( HawkbitDdi* self )
// {
//   char url[256];
//   snprintf( url, sizeof( url ), "https://%s:%d/%s/controller/v1/%s/deploymentBase/%d/feedback", self->_serverName, self->_serverPort, self->_tenantId, self->_controllerId, self->_currentActionId );
//   struct mg_mgr mgr;
//   mg_mgr_init( &mgr );
//   self->conn = mg_http_connect( &mgr, url, NULL, NULL );
//   if ( self->conn == NULL )
//   {
//     printf( "Connection failed!\n" );
//     return;
//   }
//   struct mg_str json_data = mg_str( self->_configData );
//   mg_printf( self->conn, "POST %s HTTP/1.1\r\n%s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%.*s",
//              url, createHeaders( self, "application/json" ), (int) json_data.len, (int) json_data.len, json_data.p );
//   mg_mgr_poll( &mgr, 5000 );
//   mg_mgr_free( &mgr );
// }

// void getAndInstallUpdateImage( HawkbitDdi* self )
// {
//   // Implementation here
// }

// unsigned long convertTime( char* timeString )
// {
//   uint8_t partNo = 0;
//   unsigned long milliseconds = 0;
//   char* number = strtok( timeString, ":" );
//   while ( number != NULL )
//   {
//     int timeNumber = atoi( number );
//     switch ( partNo )
//     {
//       case 0:
//         milliseconds += (unsigned long) timeNumber * 60UL * 60UL * 1000UL;
//         break;
//       case 1:
//         milliseconds += (unsigned long) timeNumber * 60UL * 1000UL;
//         break;
//       case 2:
//         milliseconds += (unsigned long) timeNumber * 1000UL;
//         break;
//       default:
//         return 0UL;
//     }
//     number = strtok( NULL, ":" );
//     partNo++;
//   }
//   return milliseconds;
// }

// void splitHref( char* href_string )
// {
//   printf( "Split Href\n" );
//   uint8_t partNo = 0;
//   char* endPtr;
//   size_t curLen;
//   t_href href_param;
//   memset( &href_param, 0, sizeof( href_param ) );
//   char* component = strtok( href_string, "/" );
//   while ( component != NULL )
//   {
//     switch ( partNo )
//     {
//       case 0:
//         component = strtok( NULL, ":/" );
//         break;
//       case 1:
//         strncpy( href_param.href_server, component, sizeof( href_param.href_server ) );
//         component = strtok( NULL, "/" );
//         break;
//       case 2:
//         href_param.href_port = strtol( component, &endPtr, 10 );
//         if ( endPtr != NULL && *endPtr != '\0' )
//         {
//           href_param.href_port = 443;
//           href_param.href_url[0] = '/';
//           strncpy( &href_param.href_url[1], component, sizeof( href_param.href_url ) - 1 );
//         }
//         component = strtok( NULL, "" );
//         break;
//       case 3:
//         curLen = strnlen( href_param.href_url, sizeof( href_param.href_url ) );
//         href_param.href_url[curLen] = '/';
//         strncpy( &href_param.href_url[curLen + 1], component, sizeof( href_param.href_url ) - curLen - 1 );
//         component = strtok( NULL, "" );
//         break;
//     }
//     partNo++;
//   }
// }
