#include "mongoose_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mongoose.h"

struct mg_mgr mgr;
static TaskHandle_t mongooseTaskHandle = NULL;

static void _task( void* pvParameters )
{
  while ( 1 )
  {
    mg_mgr_poll( &mgr, 1000 );    // Poll mongoose manager every 1000 ms
  }
}

void MongooseTask_Init( void )
{
  mg_mgr_init( &mgr );
  xTaskCreate( _task, "mg_poll", 4096, NULL, 5, &mongooseTaskHandle );
}

void MongooseTask_Deinit( void )
{
  if ( mongooseTaskHandle != NULL )
  {
    vTaskDelete( mongooseTaskHandle );
    mongooseTaskHandle = NULL;
  }
  mg_mgr_free( &mgr );
}
