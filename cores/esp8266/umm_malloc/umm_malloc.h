/* ----------------------------------------------------------------------------
 * umm_malloc.h - a memory allocator for embedded systems (microcontrollers)
 *
 * See copyright notice in LICENSE.TXT
 * ----------------------------------------------------------------------------
 */

#ifndef UMM_MALLOC_H
#define UMM_MALLOC_H

#include <stdint.h>

//C This include is not in upstream
#include "umm_malloc_cfg.h"   /* user-dependent */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */

extern void  umm_init( void );
extern void  umm_init_vm( void *vmaddr, unsigned int vmsize );
extern void *umm_malloc( size_t size );
extern void *umm_calloc( size_t num, size_t size );
extern void *umm_realloc( void *ptr, size_t size );
extern void  umm_free( void *ptr );

/* ------------------------------------------------------------------------ */

void  umm_push_heap( int heap_number );
void  umm_pop_heap( void );

#define UMM_HEAP_INTERNAL 0
#define UMM_HEAP_EXTERNAL 1

#ifdef __cplusplus
}
#endif

#endif /* UMM_MALLOC_H */
