// gcc js_mp4.c -o js_mp4 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libnats json-c`

#include <gst/gst.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <gst/app/gstappsink.h>
#include <zconf.h>
#include <json-c/json.h>
#include <string.h>
#include <gst/net/gstnet.h>

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

const char *stream1 = "mp4stream";
const char *subject1 = "device.*.frame";

static char *nats_url = "nats://216.48.189.5:4222";

static void
_jsPubErr(jsCtx *js, jsPubAckErr *pae, void *closure)
{
    int *errors = (int *)closure;

    printf("Error: %u - Code: %u - Text: %s\n", pae->Err, pae->ErrCode, pae->ErrText);
    printf("Original message: %.*s\n", natsMsg_GetDataLength(pae->Msg), natsMsg_GetData(pae->Msg));

    *errors = (*errors + 1);

    // If we wanted to resend the original message, we would do something like that:
    //
    // js_PublishMsgAsync(js, &(pae->Msg), NULL);
    //
    // Note that we use `&(pae->Msg)` so that the library set it to NULL if it takes
    // ownership, and the library will not destroy the message when this callback returns.

    // No need to destroy anything, everything is handled by the library.
}

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

    gchar *device_id = user_data;

    printf("The %s has started\n", device_id);

    gchar *subj1 = g_strdup_printf("device.%s.frame", device_id);

    printf("Sending messages to subject '%s'\n", subj1);

    /* Retrieve the sample */
    sample = gst_app_sink_pull_sample(sink);

    if (sample)
    {
        buffer = gst_sample_get_buffer(sample);
        timestamp = GST_BUFFER_PTS(buffer);
        if (gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ)))
        {
            // g_print("%s", info.data);
        }

        for (count = 0; (s == NATS_OK) && (count < 1); count++)
        {
            s = js_PublishAsync(js, subj1, (const void *)(info.data), info.size, NULL);
        }

        if (s == NATS_OK)
        {
            jsPubOptions jsPubOpts;

            jsPubOptions_Init(&jsPubOpts);
            // Let's set it to 30 seconds, if getting "Timeout" errors,
            // this may need to be increased based on the number of messages
            // being sent.
            jsPubOpts.MaxWait = 100000;
            s = js_PublishAsyncComplete(js, &jsPubOpts);
            printf("published async\n");
            // if (s == NATS_TIMEOUT)
            // {
            //     // Let's get the list of pending messages. We could resend,
            //     // etc, but for now, just destroy them.
            //     natsMsgList list;

            //     js_PublishAsyncGetPendingList(&list, js);
            //     natsMsgList_Destroy(&list);
            // }
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

        // g_free(jobj);
        gst_buffer_unmap(buffer, &info);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

static gboolean add_mp4(gchar *location, gchar *id)
{

    // gst_parse_launch("filesrc location=./gstreamer_tutorials/sample2.mp4 ! decodebin ! videoconvert ! videoscale ! video/x-raw, format=GRAY8, width = 512, height = 512 ! appsink name=sink",NULL);

    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("Starting the mp4 streaming pipeline for %s\n", id);

    gchar *gst_str, *sink_name;

    sink_name = g_strdup_printf("sink-%s", id);

    gst_str = g_strdup_printf("filesrc location=%s ! decodebin name=decode-%s ! videoconvert name=convert-%s ! videoscale name=scale-%s ! video/x-raw, format=GRAY8, width = 1024, height = 1024 ! appsink name=%s", location, id, id, id, sink_name);

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

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
        g_print("we received an error!\n");

        //   g_main_loop_quit (loop);
        break;
    case GST_MESSAGE_EOS:
        g_print("we reached EOS\n");
        g_main_loop_quit(loop);
        break;
    default:
        break;
    }
}

static char *mkrndstr(size_t length)
{

    static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-#'?!";
    char *randomString;

    if (length)
    {
        randomString = malloc(length + 1);

        if (randomString)
        {
            int l = (int)(sizeof(charset) - 1);
            int key;
            for (int n = 0; n < length; n++)
            {
                key = rand() % l;
                randomString[n] = charset[key];
            }

            randomString[length] = '\0';
        }
    }

    return randomString;
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);

    gchar *location, *id;

    loop = g_main_loop_new(NULL, FALSE);

    if (!start_pipeline())
    {
        g_printerr("ERROR: Failed to start pipeline");
        gst_object_unref(pipe1);
        return -1;
    }

    bus = gst_element_get_bus(pipe1);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", (GCallback)cb_message,
                     pipe1);

    // s = natsConnection_Connect(&conn, opts);
    s = natsConnection_ConnectTo(&conn, "nats://216.48.189.5:4222");
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

    // increase the interation accordingly to add more videos
    for (int i = 1; i <= 1; i++)
    {
        location = g_strdup_printf("/home/nivetheni/1080p_videos/%d.mp4", i);
        id = g_strdup_printf("stream%d", i);

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
    // natsOptions_Destroy(opts1);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    gst_object_unref(pipe1);
    gst_message_unref(msg);
    gst_object_unref(bus);

    return 0;
}