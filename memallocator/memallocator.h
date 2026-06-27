#ifndef MEMALLOCATOR_H
#define MEMALLOCATOR_H

#include <stddef.h>

void *mymalloc(size_t size);
void myfree(void *ptr);

#endif
