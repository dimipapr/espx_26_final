#include "jsmn.h"

#include "event_classifier.h"

#include <string.h>

#define MAX_JSON_TOKENS 512U

static int token_equals(
    const char *json,
    const jsmntok_t *token,
    const char *text
){
    size_t token_length;
    size_t text_length;

    if (token->type != JSMN_STRING){
        return 0;
    }

    token_length = (size_t)(token->end - token->start);
    text_length = strlen(text);

    if (token_length != text_length){
        return 0;
    }

    return strncmp(
        json + token->start,
        text,
        token_length
    ) == 0;
}

event_kind_t classify_event( const message_t *message){
    jsmn_parser parser;
    jsmntok_t tokens[MAX_JSON_TOKENS];
    int token_count;
    int index;

    if (message == NULL || message->stored_len == 0U || message->truncated){
        return EVENT_KIND_UNKNOWN;
    }

    jsmn_init(&parser);

    token_count = jsmn_parse(
        &parser,
        message->data,
        message->stored_len,
        tokens,
        MAX_JSON_TOKENS
    );

    if (token_count <1 ){
        return EVENT_KIND_UNKNOWN;
    }

    if (tokens[0].type != JSMN_OBJECT){
        return EVENT_KIND_UNKNOWN;
    }

    for (index = 1; index + 1 < token_count; index++) {
        if (!token_equals(message->data, &tokens[index], "kind")) {
            continue;
        }

        if (token_equals(
                message->data,
                &tokens[index + 1],
                "commit"
            )) {
            return EVENT_KIND_COMMIT;
        }

        if (token_equals(
                message->data,
                &tokens[index + 1],
                "identity"
            )) {
            return EVENT_KIND_IDENTITY;
        }

        if (token_equals(
                message->data,
                &tokens[index + 1],
                "account"
            )) {
            return EVENT_KIND_ACCOUNT;
        }

        if (token_equals(
                message->data,
                &tokens[index + 1],
                "info"
            )) {
            return EVENT_KIND_INFO;
        }

        return EVENT_KIND_UNKNOWN;
    }
    return EVENT_KIND_UNKNOWN;
}