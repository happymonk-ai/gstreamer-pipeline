// gcc js_mp4.c -o js_mp4 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libnats json-c`

#define BUFFY_IMPL
#include <gst/gst.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <gst/app/gstappsink.h>
#include <zconf.h>
#include <json-c/json.h>
#include <string.h>
#include <stdio.h>
#include <string.h>

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

const char *stream1 = "device_stream";
const char *subject1 = "stream.*.frame";

static char *nats_url = "nats://216.48.181.154:4222";

static gboolean add_mp4(gchar *location, gchar *id);

/* Jetstream Publisher Error */
static void
_jsPubErr(jsCtx *js, jsPubAckErr *pae, void *closure)
{
    int *errors = (int *)closure;

    printf("Error: %u - Code: %u - Text: %s\n", pae->Err, pae->ErrCode, pae->ErrText);
    printf("Original message: %.*s\n", natsMsg_GetDataLength(pae->Msg), natsMsg_GetData(pae->Msg));

    *errors = (*errors + 1);
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

    /* Fetch the device id from pointer data */
    gchar *device_id = user_data;

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
            json_object_object_add(jobj2, "latitude", json_object_new_string("11.342423"));
            json_object_object_add(jobj2, "longitude", json_object_new_string("77.728165"));
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

/* Callback function when EOF of MP4 file is reached */
static GstFlowReturn eos_sample(GstAppSink *sink, gpointer data)
{
    gchar *device_id, *file_loc;

    device_id = data;

    file_loc = g_strdup_printf("/home/nivetheni/1080p_videos/%s.mp4", device_id);

    g_printerr("********************The device-%s has reached EOS********************\n", device_id);

    /* Restarting the pipeline after EOF */
    if (!add_mp4(file_loc, device_id))
    {
        g_printerr("Cannot start streaming\n");
    }

    return GST_FLOW_OK;
}

/* Starting the MP4 stream */
static gboolean add_mp4(gchar *location, gchar *id)
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("Starting the mp4 streaming pipeline for %s\n", id);

    gchar *gst_str, *sink_name;

    sink_name = g_strdup_printf("sink-%s", id);

    gst_str = g_strdup_printf("filesrc location=%s ! decodebin name=decode-%s ! videoconvert name=convert-%s ! videoscale name=scale-%s ! video/x-raw, format=GRAY8, width = 720, height = 720 ! appsink name=%s", location, id, id, id, sink_name);

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
    g_signal_connect(app_sink, "new-sample", G_CALLBACK(new_sample), id);
    g_signal_connect(app_sink, "eos", G_CALLBACK(eos_sample), id);

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

/* Initialize the pipeline with fakesrc and sink */
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
        break;
    case GST_MESSAGE_EOS:
        g_print("we reached EOS\n");
        g_main_loop_quit(loop);
        break;
    default:
        break;
    }
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    gchar *location, *id;

    loop = g_main_loop_new(NULL, FALSE);

    /* Initial pipeline */
    if (!start_pipeline())
    {
        g_printerr("ERROR: Failed to start pipeline");
        gst_object_unref(pipe1);
        return -1;
    }

    /* Adding bus to pipeline */
    bus = gst_element_get_bus(pipe1);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", (GCallback)cb_message,
                     pipe1);

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

    /* Sending device info to pipeline */
    /* increase the interation accordingly to add more videos */
    for (int i = 1; i <= 1; i++)
    {
        location = g_strdup_printf("/home/nivetheni/1080p_videos/army/army%d.mp4", i);
        id = g_strdup_printf("%d", i);

        if (!add_mp4(location, id))
        {
            g_printerr("Cannot start streaming\n");
        }
        sleep(3);
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