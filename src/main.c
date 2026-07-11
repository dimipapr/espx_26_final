#include <libwebsockets.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "message.h"
#include "queue.h"

#define JETSTREAM_HOST "jetstream1.us-east.bsky.network"
#define JETSTREAM_PORT 443
#define JETSTREAM_PATH "/subscribe?wantedCollections=app.bsky.feed.post"

static queue_t message_queue;

static void *consumer_thread(void *arg)
{
    queue_t *queue = arg;
    message_t message;

    while (1) {
        if (queue_pop(queue, &message) != 0) {
            return NULL;
        }

        fprintf(
            stderr,
            "consumer: stored=%zu actual=%zu truncated=%d "
            "queue=%zu max_queue=%zu\n",
            message.stored_len,
            message.actual_len,
            message.truncated,
            queue_count(queue),
            queue_max_count(queue)
        );
    }

    return NULL;
}

static int jetstream_callback(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len)
{

    // static size_t current_message_size = 0;
    // static size_t max_message_size = 0;
    // static size_t message_count = 0;
    // static double average_message_size = 0.0;

    static message_t current_message;

    // (void)wsi;
    (void)user;
    // (void)in;
    // (void)len;

    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            fprintf(stderr, "Connected to Jetstream.\n");
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
        {   
            size_t available;
            size_t bytes_to_copy;

            current_message.actual_len += len;
            available = MESSAGE_CAPACITY - current_message.stored_len;
            bytes_to_copy = len<available? len: available;

            if (bytes_to_copy > 0){
                memcpy(
                    current_message.data + current_message.stored_len,
                    in,
                    bytes_to_copy
                );

                current_message.stored_len += bytes_to_copy;
            }

            if (bytes_to_copy < len){
                current_message.truncated = 1;
            }

            // fwrite(in, 1, len, stdout);

            if (lws_is_final_fragment(wsi) &&
                    lws_remaining_packet_payload(wsi) == 0) {

                if (queue_push(&message_queue, &current_message) != 0) {
                    fprintf(stderr, "Failed to queue message.\n");
                }
                memset(&current_message, 0, sizeof(current_message));
            }

            break;
        }
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_err(
                "Connection error: %s\n",
                in != NULL ? (const char *)in : "unknown error"
            );
            break;
        default:
            break;
        }
    return 0;
}

static const struct lws_protocols protocols[] = {
    {
        .name = "jetstream-client",
        .callback = jetstream_callback,
        .per_session_data_size = 0,
        .rx_buffer_size = 0
    },
    {0}
};


int main(void){

    struct lws_context_creation_info info;
    struct lws_client_connect_info connection_info;
    struct lws_context *context;

    memset(&info, 0, sizeof(info));
    memset(&connection_info, 0, sizeof(connection_info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    pthread_t consumer;
    if (queue_init(&message_queue) != 0){
        return 1;
    }

    if (pthread_create(
        &consumer,
        NULL,
        consumer_thread,
        &message_queue) != 0){
            queue_destroy(&message_queue);
            return 1;
    }

    context = lws_create_context(&info);
    if (context == NULL)return 1;

    connection_info.context = context;
    connection_info.address = JETSTREAM_HOST;
    connection_info.port = JETSTREAM_PORT;
    connection_info.path = JETSTREAM_PATH;
    connection_info.host = JETSTREAM_HOST;
    connection_info.origin = JETSTREAM_HOST;
    connection_info.ssl_connection = LCCSCF_USE_SSL;

    connection_info.protocol = NULL;
    connection_info.local_protocol_name = protocols[0].name;

    if (lws_client_connect_via_info(&connection_info) == NULL){
        lws_context_destroy(context);
        return 1;
    }

    while (1) {
        if (lws_service(context, 0) < 0) {
            break;
        }
    }


    lws_context_destroy(context);
    return 0;
}