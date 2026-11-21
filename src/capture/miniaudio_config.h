/* Shared miniaudio configuration: suave only needs low-level device I/O. */
#ifndef SUAVE_MINIAUDIO_CONFIG_H
#define SUAVE_MINIAUDIO_CONFIG_H

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE

#include "miniaudio.h"

#endif
