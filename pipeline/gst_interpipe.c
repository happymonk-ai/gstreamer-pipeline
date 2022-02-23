// gcc gst_interpipe.c -o gst_interpipe `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libnats gstreamer-rtsp-server-1.0`

#include <gst/gst.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <gst/app/gstappsink.h>
#include <string.h>
#include <gst/net/gstnet.h>
#include <json-c/json.h>

#define PLAYBACK_DELAY_MS 40

struct arg_struct
{
  char *topic;
  char *deviceid;
  char *url;
  char *port;
  char *endpoint;
};

static GMainLoop *loop;
static GstBus *bus;
static GstMessage *msg;
static GstElement *pipe1, *pipe2, *webrtc1, *app_sink;

static GstClock *net_clock;
static gchar *address = "127.0.0.1";
static gint clock_port = 8090;


static char *nats_url = "nats://164.52.213.244:4222";
// static natsConnection      *conn = NULL;
// static natsSubscription    *sub  = NULL;
// static natsStatus          s;
// static volatile bool       done  = false;
// static const char *subject = "device.*.stream";
// static const char *queueGroup = "device::stream::crud";

// static natsStatistics *stats = NULL;

// static char *port1 = "8554";
// static char *port2 = "8564";
// static char *port3 = "8574";

// static gchar *endpt = "/test";

// const char *topic1 = "device.add.stream";
// const char *topic2 = "device.remove.stream";
// const char *topic3 = "device.update.stream";

// int i=0;

static char *device_url = "rtsp://nivetheni:Chandrika5@192.168.29.64/Streaming/Channels/101?transportmode=unicast&profile=Profile_1";

static GstFlowReturn new_sample(GstAppSink *sink, gpointer user_data)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstBuffer *buffer_bytes;
    GstMapInfo info;
    GstMapInfo map;
    GstClockTime timestamp;
    struct json_object *jobj;

    gchar *deviceId = user_data;

    natsConnection *conn1 = NULL;
    natsStatistics *stats1 = NULL;
    natsOptions *opts1 = NULL;
    natsStatus s1;
    int dataLen1 = 0;

    gchar *subj1 = g_strdup_printf("device::stream::%s", deviceId);

    printf("Sending messages to subject '%s'\n", subj1);
    // s = natsConnection_Connect(&conn, opts1);
    s1 = natsConnection_ConnectTo(&conn1, nats_url);

    if (s1 == NATS_OK)
        s1 = natsStatistics_Create(&stats1);

    if (s1 == NATS_OK)
        start = nats_Now();

    /* Retrieve the sample */
    sample = gst_app_sink_pull_sample(sink);

    if (sample)
    {

        buffer = gst_sample_get_buffer(sample);
        timestamp = GST_BUFFER_PTS(buffer);
        if (gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ)))
        {
            // g_print("FRAME BYTES: %s\n", (info.data));
            // g_print("TIMESTAMP: %lu\n", timestamp);
            jobj = json_object_new_object();
            json_object_object_add(jobj, "id", json_object_new_string(deviceId));
            json_object_object_add(jobj, "frame_bytes", json_object_new_string(info.data));
            json_object_object_add(jobj, "timestamp", json_object_new_int(timestamp));

            printf("%s\n", json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY));
        }

        for (count = 0; (s1 == NATS_OK) && (count < 1); count++)
        {
            s1 = natsConnection_Publish(conn1, subj1, (const void *)(info.data), info.size);
        }

        if (s1 == NATS_OK)
            s1 = natsConnection_FlushTimeout(conn1, 1000);

        if (s1 == NATS_OK)
        {
            printStats(STATS_OUT, conn1, NULL, stats1);
            printPerf("Sent");
        }
        else
        {
            printf("Error: %d - %s\n", s1, natsStatus_GetText(s1));
            nats_PrintLastErrorStack(stderr);
        }

        // Destroy all our objects to avoid report of memory leak
        natsStatistics_Destroy(stats1);
        natsConnection_Destroy(conn1);
        natsOptions_Destroy(opts1);

        // To silence reports of memory still in used with valgrind
        // nats_Close();

        gst_buffer_unmap(buffer, &info);
        gst_sample_unref(sample);
        // g_free(subj1);
        // g_free(deviceId);

        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

static gboolean natsStream_interpipesrc(gchar *device_id, gchar *device_url)
{
    g_print("Starting Nats Streaming for device id: %s\n", device_id);
    GstStateChangeReturn ret;
    GError *error = NULL;

    gchar *gst_str, *decode_name, *convert_name, *scale_name, *sink_name;

    decode_name = g_strdup_printf ("decode-%s", device_id);
    convert_name = g_strdup_printf ("convert-%s", device_id);
    scale_name = g_strdup_printf ("scale-%s", device_id);
    sink_name = g_strdup_printf ("sink-%s", device_id);

    gst_str = g_strdup_printf ("interpipesrc listen-to=%s ! decodebin name=%s ! videoconvert name=%s ! videoscale name=%s ! video/x-raw, format=GRAY8, width = 1024, height = 1024 ! appsink name=%s", device_id, decode_name, convert_name, scale_name, sink_name);

    pipe2 = gst_parse_launch (gst_str, &error);

    if (error) {
        g_printerr ("Failed to parse launch: %s\n", error->message);
        g_error_free (error);
        goto err;
    }

    gst_pipeline_use_clock (GST_PIPELINE (pipe2), net_clock);

    /* Set this high enough so that it's higher than the minimum latency
    * on all receivers */
    gst_pipeline_set_latency (GST_PIPELINE (pipe2), 500 * GST_MSECOND);

    app_sink = gst_bin_get_by_name (GST_BIN (pipe2), sink_name);
    g_assert_nonnull (app_sink);

    g_object_set(app_sink, "emit-signals", TRUE, NULL);
    g_signal_connect(app_sink, "new-sample", G_CALLBACK(new_sample), device_id);

    ret = gst_element_set_state (GST_ELEMENT (pipe2), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    // g_free(decode_name);
    // g_free(convert_name);
    // g_free(scale_name);
    // g_free(sink_name);
    // g_free(gst_str);
    return TRUE;

    err:
      g_print ("State change failure\n");
      if (pipe2)
      {
          gst_object_unref (pipe2);
      }
      return FALSE;
}


static gboolean device_interpipesink(gchar *device_id, gchar *device_url)
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    gchar *source_name, *depay_name, *parse_name;

    source_name = g_strdup_printf ("source-%s", device_id);
    depay_name = g_strdup_printf ("depay-%s", device_id);
    parse_name = g_strdup_printf ("parse-%s", device_id);

    g_print("Starting the interpipesink pipeline for %s\n", device_id);

    gchar *gst_str;

    gst_str = g_strdup_printf ("rtspsrc location=%s latency=40 is-live=true name=%s ntp-time-source=3 buffer-mode=4 ntp-sync=TRUE ! rtph265depay name=%s ! h265parse name=%s ! interpipesink name=%s ", device_url, source_name, depay_name, parse_name, device_id);

    pipe1 = gst_parse_launch (gst_str, &error);

    if (error) {
        g_printerr ("Failed to parse launch: %s\n", error->message);
        g_error_free (error);
        goto err;
    }

    gst_pipeline_use_clock (GST_PIPELINE (pipe1), net_clock);
    
    /* Set this high enough so that it's higher than the minimum latency
    * on all receivers */
    gst_pipeline_set_latency (GST_PIPELINE (pipe1), 500 * GST_MSECOND);

    ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    g_free(gst_str);
    g_free(source_name);
    g_free(depay_name);
    g_free(parse_name);
    return TRUE;

    err:
      g_print ("State change failure\n");
      if (pipe1)
      {
          gst_object_unref (pipe1);
      }
      return FALSE;
}

static gboolean initial_pipeline()
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    pipe1 = gst_parse_launch ("fakesrc ! queue ! fakesink", &error);

    if (error) {
        g_printerr ("Failed to parse launch: %s\n", error->message);
        g_error_free (error);
        goto err;
    }

    g_print ("Starting pipeline, not transmitting yet\n");

    ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    return TRUE;

    err:
      g_print ("State change failure\n");
      if (pipe1)
      {
          gst_object_unref (pipe1);
      }
      return FALSE;
}

/* called when a new message is posted on the bus */
static void
cb_message (GstBus     *bus,
            GstMessage *message,
            gpointer    user_data)
{
  GstElement *pipeline = GST_ELEMENT (user_data);
  GError *err;
  gchar *debug_info;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("we received an error!\n");
      gst_message_parse_error(message, &err, &debug_info);
      g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(message->src), err->message);
      g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
      g_clear_error(&err);
      g_free(debug_info);
      break;
    case GST_MESSAGE_EOS:
      g_print ("EOS Reached\n");
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

//     struct arg_struct *args = malloc(sizeof(struct arg_struct));
//     args->deviceid = malloc(20);
//     args->topic = malloc(10);
//     strcpy(args->topic, natsMsg_GetSubject(msg));
//     strcpy(args->deviceid, natsMsg_GetData(msg));

//     char natsSubj[50];

//     strcpy(natsSubj, strchr(args->topic, '.'));

//     switch (natsSubj[1])
//     {
//     case 'a':
//         printf("Adding the device stream\n");
//         if (!device_interpipesink (args->deviceid, args->url)) 
//         {
//             g_printerr("ERROR: Failed to start interpipesink");
//         }

//         if (!natsStream_interpipesrc (args->deviceid, args->url))
//         {
//             g_printerr("ERROR: Failed to start interpipesrc for Nats Streaming");
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
    
//     // Need to destroy the message!
//     free(args);
//     natsMsg_Destroy(msg);
// }

// void *call_Nats()
// {
//     // Creates a connection to the NATS URL
//     s = natsConnection_Connect(&conn, opts);
//     //   s = natsConnection_ConnectTo(&conn, nats_url);
//     if (s == NATS_OK)
//     {
//         s = natsConnection_QueueSubscribe(&sub, conn, subject, queueGroup, onMsg, (void*) &done);
//     }
    
//     if (s == NATS_OK)
//     {
//         for (;!done;) {
//             nats_Sleep(100);
//         }
//     }

// }

// int
// main (int argc, char *argv[])
// {

//   GOptionContext *context;
//   GError *error = NULL;

//   context = g_option_context_new ("- gstreamer interpipe");
//   g_option_context_add_group (context, gst_init_get_option_group ());
//   if (!g_option_context_parse (context, &argc, &argv, &error)) {
//     g_printerr ("Error initializing: %s\n", error->message);
//     return -1;
//   }

//   net_clock = gst_net_client_clock_new ("net_clock", server, clock_port, 0);
//   if (net_clock == NULL) {
//     g_print ("Failed to create net clock client for %s:%d\n",
//         server, clock_port);
//     return 1;
//   }

//   /* Wait for the clock to stabilise */
//   gst_clock_wait_for_sync (net_clock, GST_CLOCK_TIME_NONE);

//   loop = g_main_loop_new (NULL, FALSE);

//   if (!initial_pipeline ()) 
//   {
//     g_printerr("ERROR: Failed to start pipeline");
//     return -1;
//   }

//   printf("Listening on queue group %s\n", queueGroup);

// //   if (!device_interpipesink ("asjhjWaslk", "rtsp://127.0.0.1:8090//stream1"))
// //   {
// //       g_printerr("ERROR: Failed to start interpipesink");
// //   }
// //   if (!natsStream_interpipesrc ("asjhjWaslk", "rtsp://127.0.0.1:8090//stream1"))
// //   {
// //       g_printerr("ERROR: Failed to start interpipesrc for Nats Streaming");
// //   }

//   // Creates a connection to the NATS URL
//   s = natsConnection_Connect(&conn, opts);
//   //   s = natsConnection_ConnectTo(&conn, nats_url);
//   if (s == NATS_OK)
//   {
//       s = natsConnection_QueueSubscribe(&sub, conn, subject, queueGroup, onMsg, (void*) &done);
//   }
    
//   if (s == NATS_OK)
//   {
//       for (;!done;) {
//           nats_Sleep(100);
//       }
//   }

//   bus = gst_element_get_bus(pipe1);
//   gst_bus_add_signal_watch (bus);
//   g_signal_connect (bus, "message", (GCallback) cb_message,
//       pipe1);


//   printf("Loop starts\n");
//   g_main_loop_run (loop);


//   if (pipe1) {
//       gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_NULL);
//       g_print ("Pipeline stopped\n");
//       gst_object_unref (pipe1);
//   }

//   g_main_loop_unref (loop);

//     // Anything that is created need to be destroyed
//   natsSubscription_Destroy(sub);
//   natsConnection_Destroy(conn);


//   // If there was an error, print a stack trace and exit
//   if (s != NATS_OK)
//   {
//       nats_PrintLastErrorStack(stderr);
//       exit(2);
//   }

//   return 0;
// }