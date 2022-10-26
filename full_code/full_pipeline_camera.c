
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
#include <dotenv.h>
#define __USE_XOPEN_EXTENDED
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <ftw.h>
#include <unistd.h>

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

static natsConnection *conn = NULL;
static natsStatistics *stats = NULL;
static jsCtx *js = NULL;
static jsOptions jsOpts;
static jsErrCode jerr = 0;
static natsStatus s;
volatile int errors = 0;

struct FTW *ftwbuf;

const char *stream1 = "device_stream";
const char *subject1 = "stream.*.frame";

static char *nats_url = "nats://216.48.181.154:5222";

char latitude[10][50]={"0", "12.972442", "11.3410364", "11.004556", "13.067439", "11.65376", "10.850516"};
char longitude[10][50]={"0", "77.580643", "77.7171642", "76.961632", "80.237617", "78.15538", "76.271080"};

static GError *error = NULL;
static gchar *service;
static gchar *uri = NULL;
static gint64 num_loops = -1;

static gboolean add_device(gchar *location, gchar *id);
static gboolean camera_server(char *device_id, char *device_url);

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

int nftw(const char *path, int (*fn)(const char *,
       const struct stat *, int, struct FTW *), int fd_limit, int flags);

int unlink_cb(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int rv = remove(fpath);
    g_print("Deleted the folder\n");

    if (rv)
        perror(fpath);

    return rv;
}

int rmrf(char *path)
{
    return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}


static gboolean hls_server_device(char *file_name, char *device_url, char *file_path)
{

    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("\nStarting the hls streaming for CAMERA-%s\n", file_name);

    gchar *gst_str1, *gst_str2, *gst_str3;

    int ID_1;

    ID_1 = atoi(file_name);

    if ((ID_1 == 4) || (ID_1 == 6))
    {
        gst_str1 = g_strdup_printf("rtspsrc location=%s user-id=test user-pw=test123456789 ! rtph265depay ! avdec_h265 ! clockoverlay ! videoconvert ! videoscale ! video/x-raw,width=640, height=360 ! x265enc bitrate=512 ! hlssink2 playlist-root=https://hls.ckdr.co.in/streams/stream%s playlist-location=%s/%s.m3u8 location=%s", device_url, file_name, file_path, file_name, file_path);
    }
    else
    {
        gst_str1 = g_strdup_printf("rtspsrc location=%s user-id=test user-pw=test123456789 ! rtph264depay ! avdec_h264 ! clockoverlay ! videoconvert ! videoscale ! video/x-raw,width=640, height=360 ! x264enc bitrate=512 ! hlssink2 playlist-root=https://hls.ckdr.co.in/streams/stream%s playlist-location=%s/%s.m3u8 location=%s", device_url, file_name, file_path, file_name, file_path);
    }

    gst_str2 = "/segment.%05d.ts target-duration=10 playlist-length=3 max-files=6";

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

/* Callback when a sample is fetched inside the pipeline */
static GstFlowReturn new_sample(GstAppSink *sink, gpointer user_data)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstBuffer *buffer_bytes;
    GstCaps *caps;
    GstStructure *str;
    GstMapInfo info;
    GstMapInfo map;
    gboolean res;
    gint width, height;
    GstClockTime timestamp;
    struct json_object *jobj1, *jobj2;
    const gchar *jstr1, *jstr2;
    gchar *frame_data;
    int int_id;

    /* Fetch the device id from pointer data */
    gchar *device_id = user_data;
    int_id = atoi(device_id);

    printf("The %s has started\n", device_id);

    gchar *subj1 = g_strdup_printf("stream.%s.frame", device_id);

    printf("Sending messages to subject '%s'\n", subj1);

    /* Retrieve the sample */
    sample = gst_app_sink_pull_sample(sink);

    if (sample)
    {
        /* Get the sample buffer */
        buffer = gst_sample_get_buffer(sample);
        timestamp = GST_BUFFER_PTS(buffer);

        if (gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ)))
        {
            /* Defining json */
            jobj1 = json_object_new_object();
            jobj2 = json_object_new_object();
            json_object_object_add(jobj1, "device_id", json_object_new_string(device_id));
            json_object_object_add(jobj1, "frame_bytes", json_object_new_string_len(info.data, info.size));
            json_object_object_add(jobj1, "timestamp", json_object_new_int((unsigned long)time(NULL)));
            json_object_object_add(jobj2, "latitude", json_object_new_string(latitude[int_id]));
            json_object_object_add(jobj2, "longitude", json_object_new_string(longitude[int_id]));
            jstr2 = json_object_to_json_string_ext(jobj2, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
            json_object_object_add(jobj1, "geo-location", json_object_new_string(jstr2));

            /* Converting Json to String */
            jstr1 = json_object_to_json_string(jobj1);

            int len = strlen(jstr1);

            g_print("LENGTH - %d\n", len);

            /* Publishing the Json string through Jetstream*/
            for (count = 0; (s == NATS_OK) && (count < 1); count++)
            {
                s = js_PublishAsync(js, subj1, (const void *)(jstr1), len, NULL);
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
                printf("published async\n");
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

        g_free(jobj2);
        g_free(jobj1);

        gst_buffer_unmap(buffer, &info);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

/* called when a new message is posted on the bus */
static void
cb_message(GstBus *bus,
           GstMessage *message,
           gpointer user_data)
{
    GstElement *pipe1 = GST_ELEMENT(user_data);
    GError *err;
    gchar *debug_info, *id, *location, *file_path;
    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
        g_print("we received an error!\n");
        gst_message_parse_error(message, &err, &debug_info);
        g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(message->src), err->message);
        g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
        g_print("*********************THE CAMERA WENT OFFLINE************************\n\n");

        id = gst_object_get_name(message->src);
        // g_print("THE DEVICE ID IS: %s\n", id);

        location = getenv(g_strdup_printf("RTSP_URL_%s", id));
        // g_print("THE LOCATION IS: %s\n", location);

        g_print("*********************RECONNECTING THE PIPELINE************************\n");

        // if (!camera_server(id, location))
        // {
        //     g_printerr("Cannot add the device-%s stream to RTSP Server\n", id);
        // }

        if (!add_device(location, id))
        {
            g_printerr("Cannot start streaming\n");
        }
        file_path = g_strdup_printf("/app/streams/stream%s", id);
        mkdir(file_path, 0777);

        if (!hls_server_device(id, location, file_path))
        {
            g_printerr("Cannot add stream to HLS Server!\n");
        }

        g_clear_error(&err);
        g_free(debug_info);

        break;
    case GST_MESSAGE_EOS:
        g_print("EOS Reached\n");
        g_print("*********************THE CAMERA WENT OFFLINE************************\n");
        break;
    case GST_MESSAGE_STATE_CHANGED:
        /* We are only interested in state-changed messages from the pipeline */
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipe1))
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

/* Starting the MP4 stream */
static gboolean add_device(gchar *location, gchar *id)
{
    GstStateChangeReturn ret;
    GError *error = NULL;
    int ID;

    g_print("Starting the device streaming pipeline for CAMERA-%s\n", id);

    gchar *gst_str, *sink_name;

    sink_name = g_strdup_printf("sink-%s", id);

    ID = atoi(id);

    if ((ID == 4) || (ID == 6))
    {
        gst_str = g_strdup_printf("rtspsrc location=%s name=%s user-id=test user-pw=test123456789 ! queue max-size-buffers=2 ! rtph265depay name=depay-%s ! h265parse name=parse-%s ! decodebin name=decode-%s ! videoconvert name=convert-%s ! videoscale name=scale-%s ! video/x-raw, format=GRAY8, width = 512, height = 512 ! appsink name=sink-%s", location, id, id, id, id, id, id, id);
    }
    else
    {
        gst_str = g_strdup_printf("rtspsrc location=%s name=%s user-id=test user-pw=test123456789 ! queue max-size-buffers=2 ! rtph264depay name=depay-%s ! h264parse name=parse-%s ! decodebin name=decode-%s ! videoconvert name=convert-%s ! videoscale name=scale-%s ! video/x-raw, format=GRAY8, width = 512, height = 512 ! appsink name=sink-%s", location, id, id, id, id, id, id, id);
    }

    g_print("%s\n", gst_str);

    pipe1 = gst_parse_launch(gst_str, &error);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    app_sink = gst_bin_get_by_name(GST_BIN(pipe1), sink_name);
    g_assert_nonnull(app_sink);

    g_object_set(app_sink, "emit-signals", TRUE, NULL);
    // g_signal_connect(app_sink, "new-sample", G_CALLBACK(new_sample), id);

    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    g_free(sink_name);
    g_free(gst_str);
    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
    }
    return FALSE;
}

static gboolean camera_server(char *device_id, char *device_url)
{
    gchar *server_str, *file_path, *hls_str, *gst_string;
    int ID;

    ID = atoi(device_id);

    char *endpt = g_strdup_printf("/stream%s", device_id);

    g_print("Starting RTSP Server Streaming for device id: %s\n", device_id);

    // g_print("%s, %s, %s\n", endpt, device_url, device_enc);

    global_clock = gst_system_clock_obtain();
    gst_net_time_provider_new(global_clock, NULL, 8554);

    /* get the mount points for this server, every server has a default object
     * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points(server);

    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */

    if ((ID == 4) || (ID == 6))
    {
        gst_string = g_strdup_printf("(rtspsrc location=%s user-id=test user-pw=test123456789 name=src-%s drop-on-latency=true is-live=true latency=0 ! rtpjitterbuffer drop-on-latency=true ! queue ! rtph265depay name=dep-%s ! h265parse name=parse-%s ! queue ! rtph265pay name=pay0 pt=96 )", device_url, device_id, device_id, device_id);
    }
    else
    {
        gst_string = g_strdup_printf("(rtspsrc location=%s user-id=test user-pw=test123456789 name=src-%s drop-on-latency=true is-live=true latency=0 ! rtpjitterbuffer drop-on-latency=true ! queue ! rtph264depay name=dep-%s ! h264parse name=parse-%s ! queue ! rtph264pay name=pay0 pt=96 )", device_url, device_id, device_id, device_id);
    }

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
    g_print("stream ready at rtsp://115.99.215.132:%s/%s\n", PORT, endpt);

    g_free(gst_string);

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

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    // env_load("./.env", false);

    gchar *location, *id, *file_path;

    loop = g_main_loop_new(NULL, FALSE);

    /* Initial pipeline */
    if (!start_pipeline())
    {
        g_printerr("ERROR: Failed to start pipeline");
        gst_object_unref(pipe1);
        return -1;
    }

    // /* Adding bus to pipeline */
    // bus = gst_element_get_bus(pipe1);
    // gst_bus_add_signal_watch(bus);
    // g_signal_connect(bus, "message", (GCallback)cb_message,
    //                  pipe1);


    /* create a RTSP server instance */
    server = gst_rtsp_server_new();

    /* assigning port */
    g_object_set(server, "service", PORT, NULL);

    /* Nats Connection */
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

    // location = "rtsp://happymonk:admin123@192.168.1.2:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif";

    id = g_strdup_printf("1");
    location = getenv(g_strdup_printf("RTSP_URL_%s", id));

    // if (!camera_server(id, location))
    // {
    //     g_printerr("Cannot add the device-%s stream to RTSP Server\n", id);
    // }

    // if (!add_device(location, id))
    // {
    //     g_printerr("Cannot start streaming\n");
    // }
    file_path = g_strdup_printf("/app/streams/stream%s", id);
    rmrf(file_path);
    mkdir(file_path, 0777);

    if (!hls_server_device(id, location, file_path))
    {
        g_printerr("Cannot add stream to HLS Server!\n");
    }
    sleep(1);

    /* Sending device info to pipeline */
    /* increase the interation accordingly to add more videos */
    for (int i = 2; i <= 6; i++)
    {
        // location = g_strdup_printf("rtsp://192.168.1.10%d:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif", i);
        location = getenv(g_strdup_printf("RTSP_URL_%d", i));
        
        id = g_strdup_printf("%d", i);

        // if (!camera_server(id, location))
        // {
        //     g_printerr("Cannot add the device-%s stream to RTSP Server\n", id);
        // }

        // if (!add_device(location, id))
        // {
        //     g_printerr("Cannot start streaming\n");
        // }
        file_path = g_strdup_printf("/app/streams/stream%s", id);
        rmrf(file_path);
        mkdir(file_path, 0777);

        if (!hls_server_device(id, location, file_path))
        {
            g_printerr("Cannot add stream to HLS Server!\n");
        }
        sleep(1);
    }

    g_main_loop_run(loop);

    gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_NULL);
    g_print("Pipeline has been stopped\n");

    // Destroy all our objects to avoid report of memory leak
    jsCtx_Destroy(js);
    natsStatistics_Destroy(stats);
    natsConnection_Destroy(conn);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    g_main_loop_unref(loop);
    gst_object_unref(pipe1);
    gst_message_unref(msg);
    gst_object_unref(bus);

    return 0;
}