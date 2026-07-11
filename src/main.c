#include <errno.h>
#include <libwebsockets.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "event_classifier.h"
#include "message.h"
#include "queue.h"


#define JETSTREAM_HOST "jetstream1.us-east.bsky.network"
#define JETSTREAM_PORT 443
#define JETSTREAM_PATH \
    "/subscribe?wantedCollections=app.bsky.feed.post"


/* -------------------------------------------------------------------------- */
/* Shared state                                                               */
/* -------------------------------------------------------------------------- */

static volatile sig_atomic_t stop_requested = 0;
static volatile sig_atomic_t consumer_finished = 0;

static queue_t message_queue;

static size_t dropped_messages = 0U;
static pthread_mutex_t dropped_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    size_t commit_count;
    size_t identity_count;
    size_t account_count;
    size_t info_count;
    size_t unknown_count;
} event_counters_t;

typedef struct {
    unsigned long long idle;
    unsigned long long total;
} cpu_sample_t;

static event_counters_t event_counters;
static pthread_mutex_t event_counters_mutex = PTHREAD_MUTEX_INITIALIZER;


/* -------------------------------------------------------------------------- */
/* Forward declarations                                                       */
/* -------------------------------------------------------------------------- */

static void handle_signal(int signal_number);

static void *producer_thread(void *arg);
static void *consumer_thread(void *arg);
static void *monitor_thread(void *arg);

static int jetstream_callback(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len
);

static int read_cpu_sample(cpu_sample_t *sample);


/* -------------------------------------------------------------------------- */
/* WebSocket configuration                                                    */
/* -------------------------------------------------------------------------- */

static const struct lws_protocols protocols[] = {
    {
        .name = "jetstream-client",
        .callback = jetstream_callback,
        .per_session_data_size = 0,
        .rx_buffer_size = 0
    },
    {0}
};


/* -------------------------------------------------------------------------- */
/* Signal handling                                                            */
/* -------------------------------------------------------------------------- */

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}


/* -------------------------------------------------------------------------- */
/* Producer thread                                                            */
/* -------------------------------------------------------------------------- */

static void *producer_thread(void *arg)
{
    queue_t *queue = arg;

    struct lws_context_creation_info context_info;
    struct lws_client_connect_info connection_info;
    struct lws_context *context;

    memset(&context_info, 0, sizeof(context_info));
    memset(&connection_info, 0, sizeof(connection_info));

    context_info.port = CONTEXT_PORT_NO_LISTEN;
    context_info.protocols = protocols;
    context_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    context = lws_create_context(&context_info);
    if (context == NULL) {
        stop_requested = 1;
        return NULL;
    }

    connection_info.context = context;
    connection_info.address = JETSTREAM_HOST;
    connection_info.port = JETSTREAM_PORT;
    connection_info.path = JETSTREAM_PATH;
    connection_info.host = JETSTREAM_HOST;
    connection_info.origin = JETSTREAM_HOST;
    connection_info.ssl_connection = LCCSCF_USE_SSL;
    connection_info.protocol = NULL;
    connection_info.local_protocol_name = protocols[0].name;
    connection_info.opaque_user_data = queue;

    if (lws_client_connect_via_info(&connection_info) == NULL) {
        lws_context_destroy(context);
        stop_requested = 1;
        return NULL;
    }

    while (!stop_requested) {
        if (lws_service(context, 100) < 0) {
            stop_requested = 1;
            break;
        }
    }

    lws_context_destroy(context);

    return NULL;
}


/* -------------------------------------------------------------------------- */
/* Consumer thread                                                            */
/* -------------------------------------------------------------------------- */

static void *consumer_thread(void *arg)
{
    queue_t *queue = arg;
    message_t message;
    event_kind_t kind;


    while (1) {
        if (queue_pop(queue, &message) != 0) {
            consumer_finished = 1;
            return NULL;
        }

        /* An empty message is the shutdown sentinel. */
        if (message.stored_len == 0U &&
            message.actual_len == 0U) {
            break;
        }

        kind = classify_event(&message);

        pthread_mutex_lock(&event_counters_mutex);

        switch (kind) {
            case EVENT_KIND_COMMIT:
                event_counters.commit_count++;
                break;

            case EVENT_KIND_IDENTITY:
                event_counters.identity_count++;
                break;

            case EVENT_KIND_ACCOUNT:
                event_counters.account_count++;
                break;

            case EVENT_KIND_INFO:
                event_counters.info_count++;
                break;

            case EVENT_KIND_UNKNOWN:
                event_counters.unknown_count ++;
                break;
            default:
                event_counters.unknown_count ++;
                break;
        }

        pthread_mutex_unlock(&event_counters_mutex);
    }

    consumer_finished = 1;

    return NULL;
}


/* -------------------------------------------------------------------------- */
/* Monitor thread                                                             */
/* -------------------------------------------------------------------------- */

static void timespec_add_one_second(struct timespec *time){
    time->tv_sec += 1;
}

static void *monitor_thread(void *arg)
{
    queue_t *queue = arg;

    char csv_filename[64];
    time_t creation_time;
    struct tm creation_tm;
    FILE *csv_file;

    event_counters_t interval_counts;

    cpu_sample_t previous_cpu;
    cpu_sample_t current_cpu;

    size_t total_dropped = 0U;
    double cpu_percent = 0.0;

    struct timespec next_wakeup;
    struct timespec timestamp;

    creation_time = time(NULL);

    if (localtime_r(&creation_time, &creation_tm) == NULL) {
        stop_requested = 1;
        return NULL;
    }

    if (strftime(
            csv_filename,
            sizeof(csv_filename),
            "metrics_%Y%m%d_%H%M%S.csv",
            &creation_tm
        ) == 0U) {
        stop_requested = 1;
        return NULL;
    }

    csv_file = fopen(csv_filename, "w");
    if (csv_file == NULL) {
        stop_requested = 1;
        return NULL;
    }

    fprintf(
        csv_file,
        "Seconds,Nanoseconds,Commit_Count,Identity_Count,"
        "Account_Count,Info_Count,Buffer_Occupancy_Pct,CPU_Pct\n"
    );

    fflush(csv_file);

    if (clock_gettime(CLOCK_MONOTONIC, &next_wakeup) != 0) {
        fclose(csv_file);
        stop_requested = 1;
        return NULL;
    }

    if (read_cpu_sample(&previous_cpu) != 0) {
        fclose(csv_file);
        stop_requested = 1;
        return NULL;
    }

    while (!consumer_finished) {
        size_t interval_dropped;
        size_t interval_max_queue;
        size_t current_queue_count;

        unsigned long long total_delta;
        unsigned long long idle_delta;
        unsigned long long busy_delta;

        double buffer_occupancy_percent;

        int sleep_result;

        timespec_add_one_second(&next_wakeup);

        do {
            sleep_result = clock_nanosleep(
                CLOCK_MONOTONIC,
                TIMER_ABSTIME,
                &next_wakeup,
                NULL
            );
        } while (sleep_result == EINTR);

        if (sleep_result != 0) {
            stop_requested = 1;
            break;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
            stop_requested = 1;
            break;
        }

        if (read_cpu_sample(&current_cpu) != 0) {
            stop_requested = 1;
            break;
        }

        total_delta =
            current_cpu.total - previous_cpu.total;

        idle_delta =
            current_cpu.idle - previous_cpu.idle;

        busy_delta =
            total_delta - idle_delta;

        if (total_delta > 0U) {
            cpu_percent =
                ((double)busy_delta /
                 (double)total_delta) * 100.0;
        } else {
            cpu_percent = 0.0;
        }

        previous_cpu = current_cpu;

        pthread_mutex_lock(&event_counters_mutex);

        interval_counts = event_counters;
        memset(&event_counters, 0, sizeof(event_counters));

        pthread_mutex_unlock(&event_counters_mutex);

        pthread_mutex_lock(&dropped_mutex);

        interval_dropped = dropped_messages;
        dropped_messages = 0U;

        pthread_mutex_unlock(&dropped_mutex);

        interval_max_queue = queue_take_max_count(queue);
        current_queue_count = queue_count(queue);

        buffer_occupancy_percent =
            ((double)current_queue_count /
             (double)queue_capacity()) * 100.0;

        total_dropped += interval_dropped;

        fprintf(
            csv_file,
            "%ld,%ld,%zu,%zu,%zu,%zu,%.2f,%.2f\n",
            (long)timestamp.tv_sec,
            timestamp.tv_nsec,
            interval_counts.commit_count,
            interval_counts.identity_count,
            interval_counts.account_count,
            interval_counts.info_count,
            buffer_occupancy_percent,
            cpu_percent
        );

        fflush(csv_file);
    }

    fclose(csv_file);

    return NULL;
}


/* -------------------------------------------------------------------------- */
/* WebSocket callback                                                         */
/* -------------------------------------------------------------------------- */

static int jetstream_callback(
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len
)
{
    static message_t current_message;

    queue_t *queue = lws_get_opaque_user_data(wsi);

    (void)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            fprintf(stderr, "Connected to Jetstream.\n");
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            size_t available;
            size_t bytes_to_copy;

            if (queue == NULL) {
                fprintf(stderr, "Callback has no message queue.\n");
                return -1;
            }

            current_message.actual_len += len;

            available =
                MESSAGE_CAPACITY - current_message.stored_len;

            bytes_to_copy = len < available
                ? len
                : available;

            if (bytes_to_copy > 0U) {
                memcpy(
                    current_message.data +
                        current_message.stored_len,
                    in,
                    bytes_to_copy
                );

                current_message.stored_len += bytes_to_copy;
            }

            if (bytes_to_copy < len) {
                current_message.truncated = 1;
            }

            if (lws_is_final_fragment(wsi) &&
                lws_remaining_packet_payload(wsi) == 0) {
                int push_result;

                push_result =
                    queue_try_push(queue, &current_message);

                if (push_result == 1) {
                    pthread_mutex_lock(&dropped_mutex);
                    dropped_messages++;
                    pthread_mutex_unlock(&dropped_mutex);
                } else if (push_result != 0) {
                    stop_requested = 1;
                    return -1;
                }

                memset(
                    &current_message,
                    0,
                    sizeof(current_message)
                );
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
            fprintf(stderr, "Jetstream connection closed.\n");
            stop_requested = 1;
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_err(
                "Connection error: %s\n",
                in != NULL
                    ? (const char *)in
                    : "unknown error"
            );

            stop_requested = 1;
            break;

        default:
            break;
    }

    return 0;
}

static int read_cpu_sample(cpu_sample_t *sample)
{
    FILE *file;

    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;

    if (sample == NULL) {
        return -1;
    }

    file = fopen("/proc/stat", "r");
    if (file == NULL) {
        return -1;
    }

    if (fscanf(
            file,
            "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
            &user,
            &nice,
            &system,
            &idle,
            &iowait,
            &irq,
            &softirq,
            &steal
        ) != 8) {
        fclose(file);
        return -1;
    }

    fclose(file);

    sample->idle = idle + iowait;

    sample->total =
        user +
        nice +
        system +
        idle +
        iowait +
        irq +
        softirq +
        steal;

    return 0;
}


/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
    pthread_t producer;
    pthread_t consumer;
    pthread_t monitor;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (queue_init(&message_queue) != 0) {
        return 1;
    }

    if (pthread_create(
            &consumer,
            NULL,
            consumer_thread,
            &message_queue
        ) != 0) {
        queue_destroy(&message_queue);
        return 1;
    }

    if (pthread_create(
            &monitor,
            NULL,
            monitor_thread,
            &message_queue
        ) != 0) {
        message_t sentinel;

        fprintf(stderr, "Failed to create monitor thread.\n");

        stop_requested = 1;

        memset(&sentinel, 0, sizeof(sentinel));
        queue_push(&message_queue, &sentinel);

        pthread_join(consumer, NULL);

        queue_destroy(&message_queue);
        pthread_mutex_destroy(&dropped_mutex);
        pthread_mutex_destroy(&event_counters_mutex);

        return 1;
    }

    if (pthread_create(
            &producer,
            NULL,
            producer_thread,
            &message_queue
        ) != 0) {
        message_t sentinel;

        fprintf(stderr, "Failed to create producer thread.\n");

        stop_requested = 1;

        memset(&sentinel, 0, sizeof(sentinel));
        queue_push(&message_queue, &sentinel);

        pthread_join(monitor, NULL);
        pthread_join(consumer, NULL);

        queue_destroy(&message_queue);
        pthread_mutex_destroy(&dropped_mutex);
        pthread_mutex_destroy(&event_counters_mutex);

        return 1;
    }

    while (!stop_requested) {
        sleep(1);
    }

    pthread_join(producer, NULL);

    {
        message_t sentinel;

        memset(&sentinel, 0, sizeof(sentinel));

        if (queue_push(&message_queue, &sentinel) != 0) {
            fprintf(
                stderr,
                "Failed to send consumer shutdown message.\n"
            );
        }
    }

    pthread_join(consumer, NULL);
    pthread_join(monitor, NULL);

    queue_destroy(&message_queue);
    pthread_mutex_destroy(&dropped_mutex);
    pthread_mutex_destroy(&event_counters_mutex);

    return 0;
}