#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stddef.h>

#include "message.h"

#define QUEUE_CAPACITY 1024U


typedef struct{
    message_t buffer[QUEUE_CAPACITY];

    size_t head;
    size_t tail;
    size_t count;
    size_t max_count;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} queue_t;


int queue_init(queue_t *queue);
void queue_destroy(queue_t *queue);

int queue_push(queue_t *queue, const message_t *message);
int queue_try_push(queue_t *queue, const message_t *message);
int queue_pop(queue_t *queue, message_t *out_message);

size_t queue_count(queue_t *queue);
size_t queue_capacity(void);
size_t queue_max_count(queue_t *queue);

#endif