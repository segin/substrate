#pragma once
/* Mock console.h */
typedef struct console_backend console_backend_t;
void console_register(console_backend_t *backend);
