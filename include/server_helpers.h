#ifndef SERVER_HELPERS_H
#define SERVER_HELPERS_H

#include "messages.h"

#include <stddef.h>
#include <sys/types.h> 

ssize_t read_exact(int fd, void *buf, size_t count);

#endif 