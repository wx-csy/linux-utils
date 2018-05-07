#ifndef __MALLOC_H__
#define __MALLOC_H__

#include <stddef.h>

void* do_malloc(size_t size);
void do_free(void *ptr);

#endif /* ifndef __MALLOC_H__ */
