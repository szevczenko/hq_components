/**
 *******************************************************************************
 * @file    hawkbit_parser.c
 * @author  Dmytro Shevchenko
 * @brief   HAWKBIT parser http request
 *******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "hawkbit_parser.h"

#include "app_config.h"
#include "mongoose.h"

/* Private macros ------------------------------------------------------------*/
#define MODULE_NAME "[HAWKBIT_P] "
#define DEBUG_LVL   PRINT_DEBUG

#if CONFIG_DEBUG_HTTP_HAWKBIT
#define LOG( _lvl, ... ) \
  debug_printf( DEBUG_LVL, _lvl, MODULE_NAME __VA_ARGS__ )
#else
#define LOG( PRINT_INFO, ... )
#endif

/* Private variables ---------------------------------------------------------*/
const char* hawkbit_deployment_action_array[HAWKBIT_DEPLOYMENT_ACTION_LAST] =
  {
    [HAWKBIT_DEPLOYMENT_ACTION_FORCED] = "forced",
    [HAWKBIT_DEPLOYMENT_ACTION_SOFT] = "soft",
    [HAWKBIT_DEPLOYMENT_ACTION_DOWNLOAD_ONLY] = "downloadonly",
    [HAWKBIT_DEPLOYMENT_ACTION_TIME_FORCED] = "timeforced",
};

const char* hawkbit_execution_status_array[HAWKBIT_EXECUTION_STATUS_MAX] =
  {
    [HAWKBIT_EXECUTION_STATUS_CANCELED] = "canceled",
    [HAWKBIT_EXECUTION_STATUS_REJECTED] = "rejected",
    [HAWKBIT_EXECUTION_STATUS_CLOSED] = "closed",
    [HAWKBIT_EXECUTION_STATUS_PROCEEDING] = "proceeding",
    [HAWKBIT_EXECUTION_STATUS_SCHEDULED] = "scheduled",
    [HAWKBIT_EXECUTION_STATUS_RESUMED] = "resumed",
};

/* Private functions ---------------------------------------------------------*/

static hawkbit_deployment_action_t _get_action_type( const char* action )
{
  for ( int i = HAWKBIT_DEPLOYMENT_ACTION_UNKNOWN + 1; i < HAWKBIT_DEPLOYMENT_ACTION_LAST; i++ )
  {
    if ( memcmp( hawkbit_deployment_action_array[i], action, strlen( hawkbit_deployment_action_array[i] ) ) == 0 )
    {
      return i;
    }
  }
  return HAWKBIT_DEPLOYMENT_ACTION_UNKNOWN;
}

/* Public functions ----------------------------------------------------------*/

bool HAWKBITParser_ParseUrl( const char* jsonString, char* urlConfigData, size_t urlConfigDataSize, char* urlDeploymentBase, size_t urlDeploymentBaseSize, char* urlCancelAction, size_t urlCancelActionSize )
{
  assert( jsonString );
  assert( urlConfigData );
  assert( urlDeploymentBase );
  assert( urlCancelAction );
  assert( urlConfigDataSize );
  assert( urlDeploymentBaseSize );
  assert( urlCancelActionSize );

  if ( strlen( jsonString ) == 0 )
  {
    return false;
  }

  memset( urlConfigData, 0, urlConfigDataSize );
  memset( urlDeploymentBase, 0, urlDeploymentBaseSize );
  memset( urlCancelAction, 0, urlCancelActionSize );

  struct mg_str json = mg_str( jsonString );

  char* configData = mg_json_get_str( json, "$._links.configData.href" );
  if ( configData != NULL )
  {
    snprintf( urlConfigData, urlConfigDataSize, "%s", configData );
    free( configData );
  }

  char* deploymentBase = mg_json_get_str( json, "$._links.deploymentBase.href" );
  if ( deploymentBase != NULL )
  {
    snprintf( urlDeploymentBase, urlDeploymentBaseSize, "%s", deploymentBase );
    free( deploymentBase );
  }

  char* cancelAction = mg_json_get_str( json, "$._links.cancelAction.href" );
  if ( cancelAction != NULL )
  {
    snprintf( urlCancelAction, urlCancelActionSize, "%s", cancelAction );
    free( cancelAction );
  }

  return true;
}

bool HAWKBITParse_ParseDeployment( const char* jsonString, hawkbit_deployment_t* deployment )
{
  assert( jsonString );
  assert( deployment );

  struct mg_str json = mg_str( jsonString );
  int len;
  int offset = mg_json_get( json, "$.deployment", &len );

  if ( offset < 0 )
  {
    LOG( PRINT_ERROR, "Invalid JSON: missing deployment" );
    return false;
  }

  struct mg_str deploymentObj = mg_str_n( json.buf + offset, len );

  char* updateStr = mg_json_get_str( deploymentObj, "$.update" );
  if ( updateStr != NULL )
  {
    deployment->update = _get_action_type( updateStr );
    free( updateStr );
  }

  char* downloadStr = mg_json_get_str( deploymentObj, "$.download" );
  if ( downloadStr != NULL )
  {
    deployment->download = _get_action_type( downloadStr );
    free( downloadStr );
  }

  deployment->chunkSize = 0;
  struct mg_str chunksArray = mg_json_get_tok( deploymentObj, "$.chunks" );
  if ( chunksArray.len > 0 )
  {
    struct mg_str chunk;
    size_t chunkOffset = 0;
    while ( ( chunkOffset = mg_json_next( chunksArray, chunkOffset, NULL, &chunk ) ) > 0 && deployment->chunkSize < HAWKBIT_MAX_CHUNKS )
    {
      hawkbit_chunk_t* chunkPtr = &deployment->chunk[deployment->chunkSize++];

      char* partStr = mg_json_get_str( chunk, "$.part" );
      if ( partStr != NULL )
      {
        snprintf( chunkPtr->part, sizeof( chunkPtr->part ), "%s", partStr );
        free( partStr );
      }

      char* versionStr = mg_json_get_str( chunk, "$.version" );
      if ( versionStr != NULL )
      {
        snprintf( chunkPtr->version, sizeof( chunkPtr->version ), "%s", versionStr );
        free( versionStr );
      }

      char* nameStr = mg_json_get_str( chunk, "$.name" );
      if ( nameStr != NULL )
      {
        snprintf( chunkPtr->name, sizeof( chunkPtr->name ), "%s", nameStr );
        free( nameStr );
      }

      chunkPtr->artifactsSize = 0;
      struct mg_str artifactsArray = mg_json_get_tok( chunk, "$.artifacts" );
      if ( artifactsArray.len > 0 )
      {
        struct mg_str artifact;
        size_t artifactOffset = 0;
        while ( ( artifactOffset = mg_json_next( artifactsArray, artifactOffset, NULL, &artifact ) ) > 0 && chunkPtr->artifactsSize < HAWKBIT_MAX_ARTIFACTS )
        {
          hawkbit_artifacts_t* artifactPtr = &chunkPtr->artifacts[chunkPtr->artifactsSize++];

          char* filenameStr = mg_json_get_str( artifact, "$.filename" );
          if ( filenameStr != NULL )
          {
            snprintf( artifactPtr->filename, sizeof( artifactPtr->filename ), "%s", filenameStr );
            free( filenameStr );
          }

          artifactPtr->size = mg_json_get_long( artifact, "$.size", 0 );

          char* downloadHttpStr = mg_json_get_str( artifact, "$._links.download-http.href" );
          if ( downloadHttpStr != NULL )
          {
            snprintf( artifactPtr->download_http, sizeof( artifactPtr->download_http ), "%s", downloadHttpStr );
            free( downloadHttpStr );
          }
        }
      }
    }
  }

  // Parse actionHistory
  struct mg_str actionHistoryArray = mg_json_get_tok( json, "$.actionHistory.messages" );
  if ( actionHistoryArray.len > 0 )
  {
    struct mg_str message;
    size_t messageOffset = 0;
    while ( ( messageOffset = mg_json_next( actionHistoryArray, messageOffset, NULL, &message ) ) > 0 )
    {
      LOG( PRINT_INFO, "Action History Message: %.*s", (int) message.len, message.buf );
    }
  }

  return true;
}

bool HAWKBITParse_FindInActionHistory( const char* jsonString, const char* findString )
{
  assert( jsonString );
  assert( findString );

  struct mg_str json = mg_str( jsonString );
  struct mg_str actionHistoryArray = mg_json_get_tok( json, "$.actionHistory.messages" );

  if ( actionHistoryArray.len > 0 )
  {
    struct mg_str message;
    size_t messageOffset = 0;
    while ( ( messageOffset = mg_json_next( actionHistoryArray, messageOffset, NULL, &message ) ) > 0 )
    {
      if ( strncmp( message.buf, findString, message.len ) == 0 )
      {
        return true;
      }
    }
  }

  return false;
}

bool HAWKBITParser_ParseCancelAction( const char* jsonString, int* actionId )
{
  struct mg_str json = mg_str( jsonString );
  char* actionIdStr = mg_json_get_str( json, "$.cancelAction.stopId" );

  if ( actionIdStr != NULL )
  {
    *actionId = atoi( actionIdStr );
    free( actionIdStr );
    return true;
  }

  return false;
}
