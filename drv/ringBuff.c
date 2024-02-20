#include "ringBuff.h"

#if CONFIG_USE_RING_BUFFER
int ring_buffer_init( ring_buffer_t* rbd, rb_attr_t* attr )
{
  int err = -1;

  if ( ( rbd != NULL ) && ( attr != NULL ) )
  {
    if ( ( attr->buffer != NULL ) && ( attr->s_elem > 0 ) )
    {
      /* Check that the size of the ring buffer is a power of 2 */
      if ( ( ( attr->n_elem - 1 ) & attr->n_elem ) == 0 )
      {
        /* Initialize the ring buffer internal variables */
        rbd->head = 0;
        rbd->tail = 0;
        rbd->buf = attr->buffer;
        rbd->s_elem = attr->s_elem;
        rbd->n_elem = attr->n_elem;
        err = 0;
      }
    }
  }

  return err;
}

static int _ring_buffer_full( ring_buffer_t* rb )
{
  return ( ( rb->head - rb->tail ) == rb->n_elem ) ? 1 : 0;
}

static int _ring_buffer_empty( ring_buffer_t* rb )
{
  return ( ( rb->head - rb->tail ) == 0U ) ? 1 : 0;
}

int ring_buffer_put( ring_buffer_t* rbd, const void* data )
{
  int err = 0;

  if ( _ring_buffer_full( rbd ) == 0 )
  {
    const size_t offset = ( rbd->head & ( rbd->n_elem - 1 ) ) * rbd->s_elem;
    memcpy( &( rbd->buf[offset] ), data, rbd->s_elem );
    rbd->head++;
  }
  else
  {
    err = -1;
  }

  return err;
}

int ring_buffer_get( ring_buffer_t* rbd, void* data )
{
  int err = 0;

  if ( _ring_buffer_empty( rbd ) == 0 )
  {
    const size_t offset = ( rbd->tail & ( rbd->n_elem - 1 ) ) * rbd->s_elem;
    memcpy( data, &( rbd->buf[offset] ), rbd->s_elem );
    rbd->tail++;
  }
  else
  {
    err = -1;
  }

  return err;
}

#endif    //#if CONFIG_USE_RING_BUFFER
