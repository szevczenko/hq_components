#ifndef _RING_BUFF_H
#define _RING_BUFF_H
#include "app_config.h"
#include "stdint.h"

#ifndef CONFIG_USE_RING_BUFFER
#define CONFIG_USE_RING_BUFFER TRUE
#endif

#if CONFIG_USE_RING_BUFFER

#define RING_BUFFER_MAX 4

typedef struct
{
  size_t s_elem;
  size_t n_elem;
  void* buffer;
} rb_attr_t;

typedef struct
{
  size_t s_elem;
  size_t n_elem;
  uint8_t* buf;
  volatile size_t head;
  volatile size_t tail;
} ring_buffer_t;

int ring_buffer_init( ring_buffer_t* rbd, rb_attr_t* attr );
int ring_buffer_get( ring_buffer_t* rbd, void* data );
int ring_buffer_put( ring_buffer_t* rbd, const void* data );

#endif    //CONFIG_USE_RING_BUFFER
#endif    //_RING_BUFF_H
