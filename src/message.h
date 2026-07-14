#ifndef MESSAGE_H
#define MESSAGE_H

#include <stddef.h>

#define MESSAGE_CAPACITY (64U * 1024U)

typedef struct {
    char data[MESSAGE_CAPACITY];
    size_t stored_len;
    size_t actual_len;
    int truncated;
}message_t;

#endif