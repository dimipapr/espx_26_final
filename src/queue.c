#include "queue.h"

#include <string.h>

int queue_init(queue_t *queue)
{
    int result;

    if (queue == NULL) {
        return -1;
    }

    memset(queue, 0, sizeof(*queue));

    result = pthread_mutex_init(&queue->mutex, NULL);
    if (result != 0) {
        return -1;
    }

    result = pthread_cond_init(&queue->not_empty, NULL);
    if (result != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }

    result = pthread_cond_init(&queue->not_full, NULL);
    if (result != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        return -1;
    }

    return 0;
}

void queue_destroy( queue_t *queue){
    if (queue == NULL)return;

    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
}
/**
 * return values: 
 *      0   - message queued
 *      1   - queue full, message dropped
 *      -1  - internal error
 */
int queue_try_push(queue_t *queue, const message_t *message){
    if (queue == NULL || message == NULL){
        return -1;
    }

    if (pthread_mutex_lock(&queue->mutex) != 0){
        return -1;
    }

    if (queue->count == QUEUE_CAPACITY){
        pthread_mutex_unlock(&queue->mutex);
        return 1;
    }

    queue->buffer[queue->tail] = *message;
    queue->tail = (queue->tail +1U) % QUEUE_CAPACITY;
    queue->count ++;
    if (queue->count > queue->max_count) {
        queue->max_count = queue->count;
    }

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

int queue_push(queue_t *queue, const message_t *message)
{
    if (queue == NULL || message == NULL)return -1;
    if (pthread_mutex_lock(&queue->mutex) != 0)return -1;

    while (queue->count == QUEUE_CAPACITY) {
        if (pthread_cond_wait(
                &queue->not_full,
                &queue->mutex) != 0) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
    }

    queue->buffer[queue->tail] = *message;
    queue->tail = (queue->tail + 1U) % QUEUE_CAPACITY;
    queue->count++;
    if (queue->count > queue->max_count){
        queue->max_count = queue->count;
    }

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

int queue_pop(queue_t *queue, message_t *out_message){
    if (queue == NULL || out_message == NULL){
        return -1;
    }

    if (pthread_mutex_lock(&queue->mutex) != 0)return -1;

    while (queue->count == 0U){
        if (pthread_cond_wait(
            &queue->not_empty,
            &queue->mutex) != 0)
        {
                pthread_mutex_unlock(&queue->mutex);
                return -1;
        }
    }

    *out_message = queue->buffer[queue->head];
    queue->head = (queue->head + 1U) % QUEUE_CAPACITY;
    queue->count --;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
        
}

size_t queue_count(queue_t *queue)
{
    size_t count;

    if (queue == NULL) {
        return 0U;
    }

    if (pthread_mutex_lock(&queue->mutex) != 0) {
        return 0U;
    }

    count = queue->count;

    pthread_mutex_unlock(&queue->mutex);

    return count;
}

size_t queue_max_count(queue_t *queue)
{
    size_t max_count;

    if (queue == NULL) {
        return 0U;
    }

    if (pthread_mutex_lock(&queue->mutex) != 0) {
        return 0U;
    }

    max_count = queue->max_count;

    pthread_mutex_unlock(&queue->mutex);

    return max_count;
}

size_t queue_take_max_count(queue_t *queue)
{
    size_t max_count;

    if (queue == NULL) {
        return 0U;
    }

    pthread_mutex_lock(&queue->mutex);

    max_count = queue->max_count;
    queue->max_count = queue->count;

    pthread_mutex_unlock(&queue->mutex);

    return max_count;
}

size_t queue_capacity(void)
{
    return QUEUE_CAPACITY;
}