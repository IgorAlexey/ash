#ifndef ASH_BASE_POISON_H
#define ASH_BASE_POISON_H

#if defined(__GNUC__)
#pragma GCC poison strcpy strcat sprintf vsprintf strncpy strncat gets
#pragma GCC poison malloc calloc realloc reallocarray free
#pragma GCC poison aligned_alloc posix_memalign memalign valloc pvalloc
#pragma GCC poison strdup strndup asprintf vasprintf getline getdelim
#endif

#endif
