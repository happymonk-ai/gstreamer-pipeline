// gcc nats_rtsp_server.c -o nats_rtsp_server -lgstnet-1.0 `pkg-config --cflags --libs gstreamer-1.0 libnats gstreamer-rtsp-server-1.0 json-c`

#include <gst/gst.h>
#include <gst/net/gstnettimeprovider.h>
#include <gst/rtsp-server/rtsp-server.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <pthread.h>
#include <zconf.h>
#include <json-c/json.h>

#define PORT "8090"

GMainLoop *loop;
static GstRTSPServer *server;
static GstRTSPMountPoints *mounts;
static GstRTSPMediaFactory *factory;
static GstClock *global_clock;

static natsConnection *conn = NULL;
static natsSubscription *sub = NULL;
static natsStatus s;
static natsStatistics *stats = NULL;
static volatile bool done = false;
static const char *device_subject = "device.*.stream";
static const char *interpipe_subject = "interpipe.*.stream";
static const char *queueGroup = "device::stream::crud";

#define TEST_TYPE_RTSP_MEDIA_FACTORY (test_rtsp_media_factory_get_type())
#define TEST_TYPE_RTSP_MEDIA (test_rtsp_media_get_type())

GType test_rtsp_media_get_type(void);

typedef struct TestRTSPMediaClass TestRTSPMediaClass;
typedef struct TestRTSPMedia TestRTSPMedia;

struct TestRTSPMediaClass
{
    GstRTSPMediaClass parent;
};

struct TestRTSPMedia
{
    GstRTSPMedia parent;
};

static gboolean custom_setup_rtpbin(GstRTSPMedia *media, GstElement *rtpbin);

G_DEFINE_TYPE(TestRTSPMedia, test_rtsp_media, GST_TYPE_RTSP_MEDIA);

static void
test_rtsp_media_class_init(TestRTSPMediaClass *test_klass)
{
    GstRTSPMediaClass *klass = (GstRTSPMediaClass *)(test_klass);
    klass->setup_rtpbin = custom_setup_rtpbin;
}

static void
test_rtsp_media_init(TestRTSPMedia *media)
{
}

static gboolean
custom_setup_rtpbin(GstRTSPMedia *media, GstElement *rtpbin)
{
    g_object_set(rtpbin, "ntp-time-source", 3, NULL);
    return TRUE;
}

static gboolean server_function(char *endpt, char *device_url, char *device_id, struct json_object *json_obj)
{

    g_print("Starting RTSP Server Streaming for device id: %s\n", device_id);

    global_clock = gst_system_clock_obtain();
    gst_net_time_provider_new(global_clock, NULL, 8090);

    /* get the mount points for this server, every server has a default object
     * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points(server);

    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */
    gchar *gst_string, *server_str;
    const char* json_str;

    gst_string = g_strdup_printf("(rtspsrc location=%s name=src-%s drop-on-latency=true is-live=true latency=0 ! rtpjitterbuffer drop-on-latency=true ! queue ! rtph265depay name=dep-%s ! h265parse name=par-%s ! queue ! rtph265pay name=pay0 pt=96 )", device_url, device_id, device_id, device_id);
    factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, gst_string);

    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_media_gtype(factory, TEST_TYPE_RTSP_MEDIA);
    gst_rtsp_media_factory_set_clock(factory, global_clock);

    /* attach the test factory to the /test url */
    gst_rtsp_mount_points_add_factory(mounts, endpt, factory);

    /* don't need the ref to the mapper anymore */
    g_object_unref(mounts);

    /* attach the server to the default maincontext */
    gst_rtsp_server_attach(server, NULL);

    /* start serving */
    g_print("stream ready at rtsp://127.0.0.1:%s/%s\n", PORT, endpt);

    server_str = g_strdup_printf("rtsp://127.0.0.1:%s/%s", PORT, endpt);

    json_object_object_add(json_obj, "stream_url", json_object_new_string(server_str));

    json_str = json_object_to_json_string_ext(json_obj, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);

    for (count = 0; (s == NATS_OK) && (count < 1); count++)
    {
        s = natsConnection_Publish(conn, interpipe_subject, (const void *)(json_obj), );
    }

    if (s == NATS_OK)
        s = natsConnection_FlushTimeout(conn, 1000);

    if (s == NATS_OK)
    {
        printStats(STATS_OUT, conn, NULL, stats);
        printPerf("Sent");
    }
    else
    {
        printf("Error: %d - %s\n", s, natsStatus_GetText(s));
        nats_PrintLastErrorStack(stderr);
    }

    natsStatistics_Destroy(stats);

    g_free(gst_string);
    g_free(server_str);

    return TRUE;
}

static char *removeChar(char *str, char charToRemmove)
{
    int i, j;
    int len = strlen(str);
    for (i = 0; i < len; i++)
    {
        if (str[i] == charToRemmove)
        {
            for (j = i; j < len; j++)
            {
                str[j] = str[j + 1];
            }
            len--;
            i--;
        }
    }

    return str;
}

/*
 * In real life the operations done here are way more complicated, but it's
 * only an example.
 */
struct json_object *find_something(struct json_object *jobj, const char *key)
{
    struct json_object *tmp;

    json_object_object_get_ex(jobj, key, &tmp);

    return tmp;
}

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("Received msg: %s - %.*s\n",
           natsMsg_GetSubject(msg),
           natsMsg_GetDataLength(msg),
           natsMsg_GetData(msg));

    struct json_object *jobj, *rtsp_url, *id, *endpt;
    const char *stream_url, *deviceid, *stream_endpt;
    char *new_url, *new_id, *new_endpt;

    jobj = json_tokener_parse(natsMsg_GetData(msg));

    rtsp_url = find_something(jobj, "stream_url");
    id = find_something(jobj, "id");
    endpt = find_something(jobj, "stream_endpt");

    stream_url = json_object_to_json_string_ext(rtsp_url, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
    deviceid = json_object_to_json_string_ext(id, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
    stream_endpt = json_object_to_json_string_ext(endpt, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);

    new_url = malloc(strlen(stream_url + 1));
    new_id = malloc(strlen(deviceid + 1));
    new_endpt = malloc(strlen(stream_endpt + 1));

    strcpy(new_url, stream_url);
    strcpy(new_id, deviceid);
    strcpy(new_endpt, stream_endpt);

    new_url = removeChar(new_url, '\\');
    new_url = removeChar(new_url, '"');
    new_id = removeChar(new_id, '"');
    new_endpt = removeChar(new_endpt, '\\');
    new_endpt = removeChar(new_endpt, '"');

    printf("SERVER URL: %s\n", new_url);
    printf("DEVICE ID: %s\n", new_id);
    printf("END PT: %s\n", new_endpt);

    char *natsSubject = strchr(natsMsg_GetSubject(msg), '.');

    char natsSubj[50];

    strcpy(natsSubj, natsSubject);

    switch (natsSubj[1])
    {
    case 'a':
        printf("Adding the device stream to the RTSP server\n");

        if (!server_function(new_endpt, new_url, new_id, jobj))
        {
            g_printerr("Cannot add the stream\n");
        }

        break;
    case 'r':
        printf("Removing the device stream\n");
        break;
    case 'u':
        printf("Updating the device stream\n");
        break;
    default:
        break;
    }

    free(new_url);
    free(new_id);
    free(new_endpt);

    // Need to destroy the message!
    natsMsg_Destroy(msg);
}

int main(int argc, gchar *argv[])
{
    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    /* create a server instance */
    server = gst_rtsp_server_new();
    g_object_set(server, "service", PORT, NULL);

    printf("Listening on queue group %s\n", queueGroup);

    // Creates a connection to the NATS URL
    s = natsConnection_Connect(&conn, opts);
    // s = natsConnection_ConnectTo(&conn, nats_url);
    if (s == NATS_OK)
    {
        s = natsConnection_QueueSubscribe(&sub, conn, device_subject, queueGroup, onMsg, (void *)&done);
    }
    g_main_loop_run(loop);

    if (s == NATS_OK)
    {
        for (; !done;)
        {
            nats_Sleep(100);
        }
    }

    // Anything that is created need to be destroyed
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    return 0;
}