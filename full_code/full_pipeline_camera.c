
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include "test-replay-server.h"
#include <gst/net/gstnettimeprovider.h>
#include <stdio.h>
#include <stdlib.h>
#include "/app/nats.c/examples/examples.h"
#include <gst/net/gstnet.h>
#include <json.h>
#include <string.h>
#include <gst/app/gstappsink.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory.h>

#define PORT "8554"

static GMainLoop *loop;
static GstRTSPServer *server;
static GstRTSPMountPoints *mounts;
static GstRTSPMediaFactory *factory;
static GstClock *global_clock;
static GstClock *net_clock;
static gchar *clock_server = "0.0.0.0";
static gint clock_port = 8554;

static GstBus *bus;
static GstMessage *msg;
static GMainLoop *loop;
static GstElement *pipe1, *app_sink;

static char *nats_url = "nats://216.48.181.154:4222";
static natsConnection *conn = NULL;
static natsSubscription *sub = NULL;
static natsStatus s;
static volatile bool done = false;
static const char *subject = "device.*.stream";
static const char *queueGroup = "device::stream::crud";
static jsCtx *js = NULL;
static jsOptions jsOpts;
static jsErrCode jerr = 0;
volatile int errors = 0;

static natsStatistics *stats = NULL;

const char *stream1 = "device_stream";
const char *subject1 = "stream.*.frame";

static GError *error = NULL;
static gchar *service;
static gchar *uri = NULL;
static gint64 num_loops = -1;

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

static void
_jsPubErr(jsCtx *js, jsPubAckErr *pae, void *closure)
{
    int *errors = (int *)closure;

    printf("Error: %u - Code: %u - Text: %s\n", pae->Err, pae->ErrCode, pae->ErrText);
    printf("Original message: %.*s\n", natsMsg_GetDataLength(pae->Msg), natsMsg_GetData(pae->Msg));

    *errors = (*errors + 1);
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

static gboolean hls_server_device(char *file_name, char *file_url, char *file_path)
{

    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("\nStarting the hls streaming\n");

    gchar *gst_str1, *gst_str2, *gst_str3;

    gst_str1 = g_strdup_printf("rtspsrc location=%s ! rtph264depay ! avdec_h264 ! clockoverlay ! videoconvert ! videoscale ! video/x-raw,width=640, height=360 ! x264enc bitrate=512 ! hlssink2 playlist-root=http://127.0.0.1:8080/stream%s playlist-location=%s/%s.m3u8 location=%s", file_url, file_name, file_path, file_name, file_path);

    gst_str2 = "/segment.%05d.ts target-duration=30  max-files=30 playlist-length=30";

    gst_str3 = (char *)malloc(1 + strlen(gst_str1) + strlen(gst_str2));

    strcpy(gst_str3, gst_str1);

    strcat(gst_str3, gst_str2);

    /* Build the pipeline */
    pipe1 = gst_parse_launch(gst_str3, &error);

    /* Start playing */
    gst_element_set_state(pipe1, GST_STATE_PLAYING);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    g_free(gst_str3);
    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
    }
    return FALSE;
}

static GstFlowReturn new_sample(GstAppSink *sink, gpointer user_data)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo info;
    GstClockTime timestamp;
    struct json_object *jobj, *video_id;
    const gchar *id, *jstr;
    gchar *nats_str, *json_str;

    json_str = user_data;

    printf("Received Json '%s'\n", json_str);

    jobj = json_tokener_parse(json_str);
    video_id = find_something(jobj, "device_id");

    id = json_object_to_json_string(video_id);

    gchar *subj1 = g_strdup_printf("stream.%s.frame", id);

    printf("Sending messages to subject '%s'\n", subj1);

    /* Retrieve the sample */
    sample = gst_app_sink_pull_sample(sink);

    if (sample)
    {
        buffer = gst_sample_get_buffer(sample);
        timestamp = GST_BUFFER_PTS(buffer);

        if (gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ)))
        {

            json_object_object_add(jobj, "frame_bytes", json_object_new_string_len(info.data, info.size));
            json_object_object_add(jobj, "timestamp", json_object_new_int((unsigned long)time(NULL)));

            jstr = json_object_to_json_string(jobj);

            int len = strlen(jstr);

            g_print("LENGTH - %d\n", len);

            // nats_str = malloc(strlen(jstr) + 1);

            // strcpy(nats_str, jstr);

            for (count = 0; (s == NATS_OK) && (count < 1); count++)
            {
                s = js_PublishAsync(js, subj1, (const void *)(jstr), len, NULL);
            }

            if (s == NATS_OK)
            {
                jsPubOptions jsPubOpts;

                jsPubOptions_Init(&jsPubOpts);
                // Let's set it to 30 seconds, if getting "Timeout" errors,
                // this may need to be increased based on the number of messages
                // being sent.
                jsPubOpts.MaxWait = 1000000000;
                s = js_PublishAsyncComplete(js, &jsPubOpts);
            }

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
        }

        gst_buffer_unmap(buffer, &info);
        gst_sample_unref(sample);

        return GST_FLOW_OK;
    }
    g_free(video_id);
    g_free(jobj);
    return GST_FLOW_ERROR;
}

static gboolean add_device(char *id, char *url, char *json_str)
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("Starting the camera streaming pipeline for %s\n", id);

    gchar *gst_str, *sink_name;

    sink_name = g_strdup_printf("sink-%s", id);

    gst_str = g_strdup_printf("rtspsrc location=%s latency=40 is-live=true name=%s ntp-time-source=3 buffer-mode=4 ntp-sync=TRUE ! rtph264depay name=depay-%s ! h264parse name=parse-%s ! decodebin name=decode-%s ! videoconvert name=convert-%s ! videoscale name=scale-%s ! video/x-raw, format=GRAY8, width = 720, height = 720 ! appsink name=%s", url, id, id, id, id, id, id, sink_name);

    pipe1 = gst_parse_launch(gst_str, &error);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }
    else
    {
        g_print("The pipeline launched successfully\n");
    }

    /*checking whether the appsink is null*/
    app_sink = gst_bin_get_by_name(GST_BIN(pipe1), sink_name);
    g_assert_nonnull(app_sink);

    /*callback function to fetch the frames from sample*/
    g_object_set(app_sink, "emit-signals", TRUE, NULL);
    g_signal_connect(app_sink, "new-sample", G_CALLBACK(new_sample), json_str);

    /*playing the pipeline*/
    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }

    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
    }
    return FALSE;
}

static gboolean camera_server(char *device_url, char *device_id, struct json_object *json_obj)
{
    gchar *server_str_1, *file_path_1, *hls_str_1;
    const char *json_str_1;
    char *new_json_str_1;

    // char *endpt = g_strdup_printf("/stream%s", device_id);

    // g_print("Starting RTSP Server Streaming for device id: %s\n", device_id);

    //global_clock = gst_system_clock_obtain();
    //gst_net_time_provider_new(global_clock, NULL, 8554);

    ///* get the mount points for this server, every server has a default object
    // * that be used to map uri mount points to media factories */
    //mounts = gst_rtsp_server_get_mount_points(server);

    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */

    //gchar *gst_string;

    //gst_string = g_strdup_printf("(rtspsrc location=%s name=src-%s drop-on-latency=true is-live=true latency=0 ! rtpjitterbuffer drop-on-latency=true ! queue ! rtph264depay name=dep-%s ! h264parse name=parse-%s ! queue ! rtph264pay name=pay0 pt=96 )", device_url, device_id, device_id, device_id);
    //factory = gst_rtsp_media_factory_new();
    //gst_rtsp_media_factory_set_launch(factory, gst_string);

    //gst_rtsp_media_factory_set_shared(factory, TRUE);
    //gst_rtsp_media_factory_set_media_gtype(factory, TEST_TYPE_RTSP_MEDIA);
    //gst_rtsp_media_factory_set_clock(factory, global_clock);

    /* attach the test factory to the /test url */
    //gst_rtsp_mount_points_add_factory(mounts, endpt, factory);

    /* don't need the ref to the mapper anymore */
    //g_object_unref(mounts);

    /* attach the server to the default maincontext */
    //gst_rtsp_server_attach(server, NULL);

    /* start serving */
    //g_print("stream ready at rtsp://127.0.0.1:%s/%s\n", PORT, endpt);

    //server_str_1 = g_strdup_printf("rtsp://127.0.0.1:%s/%s", PORT, endpt);

    /* converting object to string*/
    json_str_1 = json_object_to_json_string(json_obj);

    new_json_str_1 = malloc(strlen(json_str_1) + 1);

    strcpy(new_json_str_1, json_str_1);

    if (!add_device(device_id, device_url, new_json_str_1))
    {
       g_printerr("Cannot add stream to Jetstream!\n");
    }

    file_path_1 = g_strdup_printf("/app/streams/stream%s", device_id);
    mkdir(file_path_1, 0777);

    if (!hls_server_device(device_id, device_url, file_path_1))
    {
        g_printerr("Cannot add stream to HLS Server!\n");
    }

    //g_free(gst_string);

    return TRUE;
}

static gboolean start_pipeline()
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    pipe1 = gst_parse_launch("fakesrc ! queue ! fakesink", &error);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    g_print("Starting pipeline, not transmitting yet\n");

    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
    }
    return FALSE;
}

/* called when a new message is posted on the bus */
static void
cb_message(GstBus *bus,
           GstMessage *message,
           gpointer user_data)
{
    GstElement *pipeline = GST_ELEMENT(user_data);
    GError *err;
    gchar *debug_info;

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
        g_print("we received an error!\n");
        gst_message_parse_error(message, &err, &debug_info);
        g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(message->src), err->message);
        g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
        g_clear_error(&err);
        g_free(debug_info);
        break;
    case GST_MESSAGE_EOS:
        g_print("EOS Reached\n");
        break;
    case GST_MESSAGE_STATE_CHANGED:
        /* We are only interested in state-changed messages from the pipeline */
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline))
        {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
            g_print("Pipeline state changed from %s to %s:\n",
                    gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
        }
        break;
    default:
        break;
    }
}

// static void
// onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
// {
//     printf("Received msg: %s - %.*s\n",
//            natsMsg_GetSubject(msg),
//            natsMsg_GetDataLength(msg),
//            natsMsg_GetData(msg));

//     struct json_object *jobj, *video_id, *video_path, *stream_endpt, *video_type;
//     const char *id, *path, *endpt, *type;
//     char *new_id, *new_path, *new_endpt, *new_type;

//     /* Tokenizing the json string */
//     jobj = json_tokener_parse(natsMsg_GetData(msg));
//     /* Fetching the respective field's value*/
//     video_id = find_something(jobj, "device_id");
//     video_path = find_something(jobj, "device_url");
//     stream_endpt = find_something(jobj, "stream_endpt");
//     video_type = find_something(jobj, "type");

//     /*converting the value to string */
//     id = json_object_to_json_string_ext(video_id, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
//     path = json_object_to_json_string_ext(video_path, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
//     endpt = json_object_to_json_string_ext(stream_endpt, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
//     type = json_object_to_json_string_ext(video_type, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);

//     /*convert const char* to char* */
//     new_id = malloc(strlen(id) + 1);
//     new_path = malloc(strlen(path) + 1);
//     new_endpt = malloc(strlen(endpt) + 1);
//     new_type = malloc(strlen(type) + 1);

//     strcpy(new_id, id);
//     strcpy(new_path, path);
//     strcpy(new_endpt, endpt);
//     strcpy(new_type, type);

//     new_id = removeChar(new_id, '"');
//     new_path = removeChar(new_path, '\\');
//     new_path = removeChar(new_path, '"');
//     new_endpt = removeChar(new_endpt, '\\');
//     new_endpt = removeChar(new_endpt, '"');
//     new_type = removeChar(new_type, '"');

//     /* fetching the type of operation need to be performed*/
//     char *natsSubject = strchr(natsMsg_GetSubject(msg), '.');

//     char natsSubj[50];

//     strcpy(natsSubj, natsSubject);

//     switch (natsSubj[1])
//     {
//     case 'a':
//         printf("Adding the device stream\n");
//         if (strcmp(new_type, "video") == 0)
//         {
//             /*adding the stream to RTSP mp4 server*/
//             if (!video_server(new_endpt, new_path, new_id, jobj))
//             {
//                 g_printerr("Cannot add the mp4 stream to RTSP Server\n");
//             }
//         }
//         if (strcmp(new_type, "stream") == 0)
//         {
//             /*adding the stream to RTSP camera server*/
//             if (!camera_server(new_endpt, new_path, new_id, jobj))
//             {
//                 g_printerr("Cannot add the mp4 stream to RTSP Server\n");
//             }
//         }
//         break;
//     case 'r':
//         printf("Removing the device stream\n");
//         break;
//     case 'u':
//         printf("Updating the device stream\n");
//         break;
//     default:
//         break;
//     }

//     free(jobj);
//     free(video_id);
//     // free(video_path);
//     // free(stream_endpt);
//     free(video_type);

//     // // Need to destroy the message!
//     natsMsg_Destroy(msg);
// }

int main(int argc, gchar *argv[])
{
    /* Initializing the gstreamer */
    gst_init(&argc, &argv);

    struct json_object *init_json1, *init_json2;

    init_json1 = json_object_new_object();
    init_json2 = json_object_new_object();

    /* Initializing the main loop */
    loop = g_main_loop_new(NULL, FALSE);

    /* Starting the pipeline with fakesrc and sink */
    if (!start_pipeline())
    {
        g_printerr("ERROR: Failed to start pipeline");
        gst_object_unref(pipe1);
        return -1;
    }

    /* Adding bus to the pipeline */
    bus = gst_element_get_bus(pipe1);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", (GCallback)cb_message, pipe1);

    /* create a RTSP server instance */
    //server = gst_rtsp_server_new();

    /* assigning port */
    //g_object_set(server, "service", PORT, NULL);

    // Creates a connection to the NATS URL
    s = natsConnection_ConnectTo(&conn, nats_url);
    printf("Connected to Nats\n");

    if (s == NATS_OK)
        s = jsOptions_Init(&jsOpts);

    if (s == NATS_OK)
    {
        jsOpts.PublishAsync.ErrHandler = _jsPubErr;
        jsOpts.PublishAsync.ErrHandlerClosure = (void *)&errors;
        s = natsConnection_JetStream(&js, conn, &jsOpts);
        printf("Connected to Jetstream\n");
    }

    if (s == NATS_OK)
    {
        jsStreamInfo *si = NULL;

        // First check if the stream already exists.
        s = js_GetStreamInfo(&si, js, stream1, NULL, &jerr);
        if (s == NATS_NOT_FOUND)
        {
            jsStreamConfig cfg;

            // Initialize the configuration structure.
            jsStreamConfig_Init(&cfg);
            cfg.Name = stream1;
            // Set the subject
            cfg.Subjects = (const char *[1]){subject1};
            cfg.SubjectsLen = 1;
            // Make it a memory stream.
            cfg.Storage = js_MemoryStorage;
            // Add the stream,
            s = js_AddStream(&si, js, &cfg, NULL, &jerr);
            printf("The stream is added\n");
        }

        if (s == NATS_OK)
        {
            printf("Stream %s has %" PRIu64 " messages (%" PRIu64 " bytes)\n",
                   si->Config->Name, si->State.Msgs, si->State.Bytes);

            // Need to destroy the returned stream object.
            jsStreamInfo_Destroy(si);
        }
    }

    if (s == NATS_OK)
    {
        s = natsStatistics_Create(&stats);
        printf("Stats are created\n");
    }

    if (s == NATS_OK)
    {
        start = nats_Now();
    }

    // printf("Listening on queue group %s\n", queueGroup);

    // if (s == NATS_OK)
    // {
    //     s = natsConnection_QueueSubscribe(&sub, conn, subject, queueGroup, onMsg, (void *)&done);
    // }

    for (int i = 1; i <= 7; i++)
    {
        char *new_path = g_strdup_printf("rtsp://happymonk:admin123@192.168.1.10%d:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif", i);
        char *new_id = g_strdup_printf("%d", i);

        json_object_object_add(init_json1, "device_id", json_object_new_string(new_id));
        json_object_object_add(init_json1, "device_url", json_object_new_string(new_path));
        json_object_object_add(init_json1, "type", json_object_new_string("stream"));
        json_object_object_add(init_json2, "latitude", json_object_new_string("11.342423"));
        json_object_object_add(init_json2, "longitude", json_object_new_string("77.728165"));
        const char *jstr2 = json_object_to_json_string_ext(init_json2, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
        json_object_object_add(init_json1, "geo-location", json_object_new_string(jstr2));

        if (!camera_server(new_path, new_id, init_json1))
        {
            g_printerr("Cannot add the mp4 stream to RTSP Server\n");
        }
        sleep(5);
    }

    /* starting the loop */
    g_main_loop_run(loop);

    /* on any error */
    gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_NULL);
    g_print("Pipeline has been stopped\n");

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // Destroy all our objects to avoid report of memory leak
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    natsSubscription_Destroy(sub);
    g_main_loop_unref(loop);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    gst_object_unref(pipe1);
    gst_message_unref(msg);
    gst_object_unref(bus);

    return 0;
}
