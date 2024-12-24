#ifndef MONGOOSE_TASK_H
#define MONGOOSE_TASK_H

#include "mongoose.h"

extern struct mg_mgr mgr;

/**
 * @brief Initializes the mongoose task and manager.
 */
void MongooseTask_Init( void );

/**
 * @brief Deinitializes the mongoose task and manager.
 */
void MongooseTask_Deinit( void );

#endif    // MONGOOSE_TASK_H
