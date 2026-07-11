#ifndef EVENT_CLASSIFIER_H
#define EVENT_CLASSIFIER_H

#include "message.h"

typedef enum {
    EVENT_KIND_UNKNOWN = 0,
    EVENT_KIND_COMMIT,
    EVENT_KIND_IDENTITY,
    EVENT_KIND_ACCOUNT,
    EVENT_KIND_INFO
} event_kind_t;

event_kind_t classify_event(const message_t *message);

#endif