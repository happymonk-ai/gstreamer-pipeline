// gcc gstreamer_mp4.c -o gstreamer_mp4 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libnats`

#include <gst/gst.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <gst/app/gstappsink.h>
#include <zconf.h>


static GstBus *bus;
static GstMessage *msg; 
static GMainLoop *loop;
static gboolean terminate = FALSE;

struct arg_struct
{
  char *deviceid;
  char *url;
};

struct pair
{
	char *str1;
	char *str2;
};

struct pair pairs[5] = {
	{"/home/nivetheni/nats.c/device_stream/video23.mp4", "haKDBkjhadlk"},
	{"/home/nivetheni/nats.c/device_stream/test_video.mp4", "ioalkjNmahnKL"}};

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData
{
    GstElement *app_sink;

} CustomData;

GstElement *bin;
GstElement *pipeline;
GstElement *source;
GstElement *depay;
GstElement *parse;
GstElement *tee;
GstElement *app_queue;
GstElement *decode;
GstElement *convert;
GstElement *scale;
GstElement *filter;
GstElement *app_sink;
GstElement *file_queue;
GstElement *fakesink;
GstPad *scale_sink_pad;

static gboolean start_pipeline();
static gboolean add_mp4(gchar *device_id, gchar *device_url);
static void on_new_decoded_pad(GstElement* decode, GstPad* new_pad, gpointer user_data);
static GstFlowReturn new_sample(GstAppSink *sink, gpointer user_data);
static void cb_message (GstBus *bus, GstMessage *message, gpointer user_data);


int main (int argc, char *argv[])
{
    gst_init(&argc, &argv);

    struct arg_struct *args = malloc(sizeof(struct arg_struct));

    loop = g_main_loop_new (NULL, FALSE);

    if (!start_pipeline ()) 
    {
        g_printerr("ERROR: Failed to start pipeline");
        gst_object_unref(pipeline);
        return -1;
    }

    bus = gst_element_get_bus(pipeline);
    gst_bus_add_signal_watch (bus);
    g_signal_connect (bus, "message", (GCallback) cb_message,
      pipeline);

    for (int i = 0; i < 2; i++)
    {
      args->url = pairs[i].str1;
      args->deviceid = pairs[i].str2;
      
      if (!add_mp4(args->url, args->deviceid)){
          g_printerr("Cannot start streaming\n");
      }
      sleep(3);
    }

    g_main_loop_run (loop);

    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
    g_print ("Pipeline has been stopped\n");

    gst_object_unref (pipeline);
    gst_message_unref (msg);
    gst_object_unref(bus);
    g_free(args);

    return 0;
}

/* called when a new message is posted on the bus */
static void
cb_message (GstBus     *bus,
            GstMessage *message,
            gpointer    user_data)
{
  GstElement *pipeline = GST_ELEMENT (user_data);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("we received an error!\n");

    //   g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("we reached EOS\n");
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }
}

static gboolean start_pipeline()
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    pipeline = gst_parse_launch ("fakesrc ! queue ! fakesink", &error);

    if (error) {
        g_printerr ("Failed to parse launch: %s\n", error->message);
        g_error_free (error);
        goto err;
    }

    g_print ("Starting pipeline, not transmitting yet\n");

    ret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    return TRUE;

    err:
      g_print ("State change failure\n");
      if (pipeline)
      {
          gst_object_unref (pipeline);
      }
      return FALSE;
}

static gboolean add_mp4(gchar *location, gchar *device_id)
{

// gst_parse_launch("filesrc location=./gstreamer_tutorials/sample2.mp4 ! decodebin ! videoconvert ! videoscale ! video/x-raw, format=GRAY8, width = 512, height = 512 ! appsink name=sink",NULL);
    
    g_print("******************Starting the streaming for mp4 file: %s******************\n", device_id);

    int ret;
    gchar *bin_tmp, *depay_tmp, *parse_tmp, *decode_tmp, *convert_tmp, *scale_tmp, *filter_tmp, *sink_tmp;
    CustomData data;

    bin_tmp = g_strdup_printf ("bin-%s", device_id);
    decode_tmp = g_strdup_printf ("decode-%s", device_id);
    convert_tmp = g_strdup_printf ("convert-%s", device_id);
    scale_tmp = g_strdup_printf ("scale-%s", device_id);
    filter_tmp = g_strdup_printf ("filter-%s", device_id);
    sink_tmp = g_strdup_printf ("sink-%s", device_id);

    bin = gst_bin_new(bin_tmp);

    /* Create the elements */
    source = gst_element_factory_make("filesrc", device_id);
    decode = gst_element_factory_make("decodebin", decode_tmp);
    g_free (decode_tmp);
    convert = gst_element_factory_make("videoconvert", convert_tmp);
    g_free (convert_tmp);
    scale = gst_element_factory_make("videoscale", scale_tmp);
    g_free (scale_tmp);
    filter = gst_element_factory_make("capsfilter", filter_tmp);
    g_free (filter_tmp);
    data.app_sink = gst_element_factory_make("appsink", sink_tmp);
    g_free (sink_tmp);

    if (!bin || !source || !decode || !convert || !scale || !filter || !data.app_sink)
    {
        g_printerr("Not all elements could be created.\n");
        return FALSE;
    }

    /* must add elements to pipeline before linking them */
    gst_bin_add_many(GST_BIN(bin), source, decode, convert, scale, filter, data.app_sink, NULL);
    if (!gst_element_link_many(source, decode, NULL))
    {
        g_printerr("Elements could not be linked.\n");
        return FALSE;
    }
    else
    {
        g_print("Elements are linked successfully.\n");
    }

    if (!gst_element_link_many(convert, scale, filter, data.app_sink, NULL))
    {
        g_printerr("Elements could not be linked.\n");
        return FALSE;
    }
    else
    {
        g_print("Elements are linked successfully.\n");
    }


    gst_bin_add(GST_BIN(pipeline), bin);
    g_free (bin_tmp);

    GstCaps *filtercaps = gst_caps_new_simple("video/x-raw",
                                              "format", G_TYPE_STRING, "GRAY8",
                                              "width", G_TYPE_INT, 1024,
                                              "height", G_TYPE_INT, 1024,
                                              NULL);

    g_object_set(G_OBJECT(filter), "caps", filtercaps, NULL);
    gst_caps_unref(filtercaps);

    g_object_set(source, "location", location, NULL);

    /* Connect to the pad-added signal */
    g_signal_connect(decode, "pad-added", G_CALLBACK(on_new_decoded_pad), convert);

    g_object_set(data.app_sink, "emit-signals", TRUE, NULL);
    g_signal_connect(data.app_sink, "new-sample", G_CALLBACK(new_sample), device_id);

    /* Set to pipeline branch to PLAYING */
    ret = gst_element_sync_state_with_parent (source);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent (decode);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent (convert);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent (scale);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent (filter);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent (data.app_sink);
    g_assert_true (ret);
    ret = gst_element_sync_state_with_parent (bin);
    g_assert_true (ret);

}

static void on_new_decoded_pad(GstElement* decode, GstPad* new_pad, gpointer user_data)
{
    g_print("The Decodebin Pad Handler is called\n");

    GstPad *conv_sink_pad = gst_element_get_static_pad (user_data, "sink");
    g_assert_nonnull (conv_sink_pad);
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;
    
    g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (decode));
    
    /* If our converter is already linked, we have nothing to do here */
    if (gst_pad_is_linked (conv_sink_pad)) {
        g_print ("We are already linked. Ignoring.\n");
        goto exit;
    }
    
    /* Check the new pad's type */
    new_pad_caps = gst_pad_get_current_caps (new_pad);
    new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
    new_pad_type = gst_structure_get_name (new_pad_struct);
    if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
        g_print ("It has type '%s' which is raw audio. Ignoring.\n", new_pad_type);
        goto exit;
    }
    
    /* Attempt the link */
    ret = gst_pad_link (new_pad, conv_sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) {
        g_print ("Type is '%s' but link failed.\n", new_pad_type);
    } else {
        g_print ("Link succeeded (type '%s').\n", new_pad_type);
    }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref (conv_sink_pad);
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

    gchar *deviceID = user_data;

    printf("The stream has started\n");

    gchar *subject = g_strdup_printf ("device.frame.%s", deviceID);

    // const char *subj = "device.frame";

    natsConnection *conn = NULL;
    natsStatistics *stats = NULL;
    natsOptions *opts = NULL;
    natsStatus s;
    int dataLen = 0;

    printf("Sending messages to subject '%s'\n", subject);
    // s = natsConnection_Connect(&conn, opts);
    s = natsConnection_ConnectTo(&conn, "nats://164.52.213.244:4222");

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if (s == NATS_OK)
        start = nats_Now();

    /* Retrieve the sample */
    sample = gst_app_sink_pull_sample(sink);

    if (sample)
    {
        caps = gst_sample_get_caps (sample);
        if (!caps) {
            g_print ("could not get snapshot format\n");
            exit (-1);
        }

        str = gst_caps_get_structure (caps, 0);

        /* we need to get the final caps on the buffer to get the size */
        res = gst_structure_get_int (str, "width", &width);
        res |= gst_structure_get_int (str, "height", &height);
        if (!res) {
            g_print ("could not get snapshot dimension\n");
            exit (-1);
        }
        else {
            g_print("HEIGHT : %d\n", height);
            g_print("WIDTH : %d\n", width);
        }

        buffer = gst_sample_get_buffer(sample);
        timestamp = GST_BUFFER_PTS(buffer);
        if (gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ)))
        {
            // g_print("FRAME BYTES: %s\n", (info.data));
            g_print("TIMESTAMP: %lu\n", timestamp);
        }

        for (count = 0; (s == NATS_OK) && (count < 1); count++)
        {
            s = natsConnection_Publish(conn, subj, (const void *)(info.data), info.size);
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

        // Destroy all our objects to avoid report of memory leak
        natsStatistics_Destroy(stats);
        natsConnection_Destroy(conn);
        natsOptions_Destroy(opts);

        // To silence reports of memory still in used with valgrind
        nats_Close();

        gst_buffer_unmap(buffer, &info);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

