// gcc full_pipeline.c -o full_pipeline -lgstnet-1.0 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0 json-c libnats`
// python -m http.server 8080 --bind 127.0.0.1

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dotenv.h>
#include "./jsJson.c"
#include "./gstreamer.c"
#include "/home/nivetheni/nats.c/examples/examples.h"

#define PORT getenv("RTSP_PORT")

static GMainLoop *loop;
static GstBus *bus;
static GstMessage *msg;

static natsConnection *conn = NULL;
static natsSubscription *sub = NULL;
static natsStatus s;
static jsCtx *js = NULL;
static jsOptions jsOpts;
static jsErrCode jerr = 0;
static natsStatistics *stats = NULL;
static volatile bool done = false;
volatile int errors = 0;

int main(int argc, gchar *argv[])
{
    /* Initializing the gstreamer */
    gst_init(&argc, &argv);

    env_load("./.env", false);

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

    // Creates a connection to the NATS URL
    s = natsConnection_ConnectTo(&conn, getenv("NATS_URL"));
    printf("Connected to Nats\n");

    if (s == NATS_OK)
        s = jsOptions_Init(&jsOpts);

    // Creates a Jetstream connection
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
        s = js_GetStreamInfo(&si, js, getenv("NATS_FRAME_STREAM"), NULL, &jerr);
        if (s == NATS_NOT_FOUND)
        {
            jsStreamConfig cfg;

            // Initialize the configuration structure.
            jsStreamConfig_Init(&cfg);
            cfg.Name = getenv("NATS_FRAME_STREAM");
            // Set the subject
            cfg.Subjects = (const char *[1]){getenv("NATS_FRAME_SUBJECT")};
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

    printf("Listening on queue group %s\n", getenv("NATS_QUEUE_GROUP"));

    if (s == NATS_OK)
    {
        s = natsConnection_QueueSubscribe(&sub, conn, getenv("NATS_QUEUE_SUBJECT"), getenv("NATS_QUEUE_GROUP"), onMsg, (void *)&done);
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