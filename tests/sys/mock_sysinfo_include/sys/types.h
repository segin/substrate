#ifndef _MOCK_SYS_TYPES_H
#define _MOCK_SYS_TYPES_H
// Include host types via include_next to bypass this mock
#include_next <sys/types.h>
#include <stdint.h> // Ensure stdint types are available
#endif
