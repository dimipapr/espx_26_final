#include <libwebsockets.h>

#include <string.h>
#include <stdio.h>

#define JETSTREAM_HOST "jetstream1.us-east.bsky.network"
#define JETSTREAM_PORT 443
#define JETSTREAM_PATH "/subscribe?wantedCollections=app.bsky.feed.post"

static int jetstream_callback(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len)
{
    (void)wsi;
    (void)user;
    (void)in;
    (void)len;

    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            fprintf(stderr, "Connected to Jetstream.\n");
            break;
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

    while (1){
        lws_service(context, 0);
    }


    lws_context_destroy(context);
    return 0;
}