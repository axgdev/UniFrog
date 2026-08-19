#ifndef UNIFROG_ZLIB_PORT_H
#define UNIFROG_ZLIB_PORT_H

#ifdef __HCRTOS__
#include <kernel/lib/zlib.h>
#else
#include <zlib.h>
#endif

#endif
