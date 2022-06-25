#include <gst/gst.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <gst/app/gstappsink.h>
#include <zconf.h>


static GstBus *bus;
static GstMessage *msg; 
static GMainLoop *loop;
static gboolean terminate = FALSE;

// GstElement *bin;
GstElement *pipeline;
GstElement *source;
GstElement *identity;
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

gchar *url[50];

static gboolean start_pipeline();
static gboolean add_device(gchar *device_id, gchar *device_url);
static gboolean start_stream(gchar *device_id, gchar *device_url);
static gboolean start_stream_2(gchar *device_id, gchar *device_url);
static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data);
static GstFlowReturn new_sample(GstAppSink *sink, gpointer user_data);
static void cb_message (GstBus *bus, GstMessage *message, gpointer user_data);
static GstFlowReturn eos_sample(GstAppSink *sink, gpointer data);
static gboolean re_add_device(gpointer data);
static void handoff_callback (GstElement * identity, GstBuffer * buffer, gpointer pad);



int main (int argc, char *argv[])
{
    gst_init(&argc, &argv);

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

    /* Inside Nats Consumer */
    gchar *device_id = "Hu87350klxkn";
    gchar *device_type = "H.265";
    gchar *device_url = "rtsp://192.168.29.92:8554//test1";
    url[0] = "rtsp://192.168.29.92:8554//test1";
    

    if(!add_device(device_id, device_url))
    {
        g_printerr("ERROR: Failed to add the device to the pipeline\n");
        gst_object_unref(pipeline);
        return -1;
    }
    sleep(20);
    if(!add_device("jsis872792e", "rtsp://192.168.29.92:8564//test2"))
    {
        g_printerr("ERROR: Failed to add the device to the pipeline\n");
        gst_object_unref(pipeline);
        return -1;
    }

    g_main_loop_run (loop);

    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
    g_print ("Pipeline has been stopped\n");

    gst_object_unref (pipeline);
    gst_message_unref (msg);
    gst_object_unref(bus);
    g_free(device_id);
    g_free(device_type);
    g_free(device_url);

    return 0;
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
  const GstStructure *msg_structure;
  const gchar *msg_name;
  GstStateChangeReturn ret;
  gchar *ID, *element_name, *ident_tmp, *depay_tmp, *parse_tmp, *tee_tmp, *app_q_tmp, *decode_tmp, *convert_tmp, *scale_tmp, *filter_tmp, *sink_tmp, *file_q_tmp, *mux_tmp, *fake_tmp;



  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("we received an error!\n");
      gst_message_parse_error(message, &err, &debug_info);
      g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(message->src), err->message);
      g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");

      ID = gst_object_get_name(message->src);
      source = gst_bin_get_by_name(GST_BIN (pipeline), ID);
      element_name = gst_element_get_name(source);
      g_print("The source element name is %s\n", element_name);
      g_free(ID);

      ident_tmp = g_strdup_printf ("identity-%s", element_name);
      depay_tmp = g_strdup_printf ("depay-%s", element_name);
      parse_tmp = g_strdup_printf ("parse-%s", element_name);
      tee_tmp = g_strdup_printf ("tee-%s", element_name);
      app_q_tmp = g_strdup_printf ("app_q-%s", element_name);
      decode_tmp = g_strdup_printf ("decode-%s", element_name);
      convert_tmp = g_strdup_printf ("convert-%s", element_name);
      scale_tmp = g_strdup_printf ("scale-%s", element_name);
      filter_tmp = g_strdup_printf ("filter-%s", element_name);
      sink_tmp = g_strdup_printf ("sink-%s", element_name);
      file_q_tmp = g_strdup_printf ("file_q-%s", element_name);
      fake_tmp = g_strdup_printf ("fake-%s", element_name);
    //   g_free(element_name);

      identity = gst_bin_get_by_name(GST_BIN (pipeline), ident_tmp);
    //   g_free (ident_tmp);
      depay = gst_bin_get_by_name(GST_BIN (pipeline), depay_tmp);
    //   g_free (depay_tmp);
      parse = gst_bin_get_by_name(GST_BIN (pipeline), parse_tmp);
    //   g_free (parse_tmp);
      tee = gst_bin_get_by_name(GST_BIN (pipeline), tee_tmp);
    //   g_free (tee_tmp);
      app_queue = gst_bin_get_by_name(GST_BIN (pipeline), app_q_tmp);
    //   g_free (app_q_tmp);
      decode = gst_bin_get_by_name(GST_BIN (pipeline), decode_tmp);
    //   g_free (decode_tmp);
      convert = gst_bin_get_by_name(GST_BIN (pipeline), convert_tmp);
    //   g_free (convert_tmp);
      scale = gst_bin_get_by_name(GST_BIN (pipeline), scale_tmp);
    //   g_free (scale_tmp);
      filter = gst_bin_get_by_name(GST_BIN (pipeline), filter_tmp);
    //   g_free (filter_tmp);
      app_sink = gst_bin_get_by_name(GST_BIN (pipeline), sink_tmp);
    //   g_free (sink_tmp);
      file_queue = gst_bin_get_by_name(GST_BIN (pipeline), file_q_tmp);
    //   g_free (file_q_tmp);
      fakesink = gst_bin_get_by_name(GST_BIN (pipeline), fake_tmp);
    //   g_free (fake_tmp);

      /* Set pipeline branch to PLAYING */
      gst_element_set_state (GST_ELEMENT (source), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (identity), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (depay), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (parse), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (tee), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (app_queue), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (decode), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (convert), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (scale), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (filter), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (app_sink), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (file_queue), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (fakesink), GST_STATE_NULL);

      /* Remove the elements */
      gst_bin_remove (GST_BIN (pipeline), source);
      gst_bin_remove (GST_BIN (pipeline), identity);
      gst_bin_remove (GST_BIN (pipeline), depay);
      gst_bin_remove (GST_BIN (pipeline), parse);
      gst_bin_remove (GST_BIN (pipeline), tee);
      gst_bin_remove (GST_BIN (pipeline), app_queue);
      gst_bin_remove (GST_BIN (pipeline), decode);
      gst_bin_remove (GST_BIN (pipeline), convert);
      gst_bin_remove (GST_BIN (pipeline), scale);
      gst_bin_remove (GST_BIN (pipeline), filter);
      gst_bin_remove (GST_BIN (pipeline), app_sink);
      gst_bin_remove (GST_BIN (pipeline), file_queue);
      gst_bin_remove (GST_BIN (pipeline), fakesink);


      if(!add_device(element_name, "rtsp://192.168.29.92:8564//test2"))
      {
          g_printerr("ERROR: Failed to add the device to the pipeline\n");
          gst_object_unref(pipeline);
        //   return -1;
      }

      g_clear_error(&err);
      g_free(debug_info);
    //   g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("we reached EOS\n");
    //   g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_APPLICATION: 

      msg_structure = gst_message_get_structure (message);
      msg_name = gst_structure_get_name(msg_structure);

      g_print("The message name is %s\n", msg_name);
      g_print("Reconnecting the pipeline.......\n");

      source = gst_bin_get_by_name(GST_BIN (pipeline), msg_name);
      element_name = gst_element_get_name(source);
      g_print("The source element name is %s\n", element_name);

      ident_tmp = g_strdup_printf ("identity-%s", element_name);
      depay_tmp = g_strdup_printf ("depay-%s", element_name);
      parse_tmp = g_strdup_printf ("parse-%s", element_name);
      tee_tmp = g_strdup_printf ("tee-%s", element_name);
      app_q_tmp = g_strdup_printf ("app_q-%s", element_name);
      decode_tmp = g_strdup_printf ("decode-%s", element_name);
      convert_tmp = g_strdup_printf ("convert-%s", element_name);
      scale_tmp = g_strdup_printf ("scale-%s", element_name);
      filter_tmp = g_strdup_printf ("filter-%s", element_name);
      sink_tmp = g_strdup_printf ("sink-%s", element_name);
      file_q_tmp = g_strdup_printf ("file_q-%s", element_name);
      fake_tmp = g_strdup_printf ("fake-%s", element_name);
    //   g_free(element_name);

      identity = gst_bin_get_by_name(GST_BIN (pipeline), ident_tmp);
    //   g_free (ident_tmp);
      depay = gst_bin_get_by_name(GST_BIN (pipeline), depay_tmp);
    //   g_free (depay_tmp);
      parse = gst_bin_get_by_name(GST_BIN (pipeline), parse_tmp);
    //   g_free (parse_tmp);
      tee = gst_bin_get_by_name(GST_BIN (pipeline), tee_tmp);
    //   g_free (tee_tmp);
      app_queue = gst_bin_get_by_name(GST_BIN (pipeline), app_q_tmp);
    //   g_free (app_q_tmp);
      decode = gst_bin_get_by_name(GST_BIN (pipeline), decode_tmp);
    //   g_free (decode_tmp);
      convert = gst_bin_get_by_name(GST_BIN (pipeline), convert_tmp);
    //   g_free (convert_tmp);
      scale = gst_bin_get_by_name(GST_BIN (pipeline), scale_tmp);
    //   g_free (scale_tmp);
      filter = gst_bin_get_by_name(GST_BIN (pipeline), filter_tmp);
    //   g_free (filter_tmp);
      app_sink = gst_bin_get_by_name(GST_BIN (pipeline), sink_tmp);
    //   g_free (sink_tmp);
      file_queue = gst_bin_get_by_name(GST_BIN (pipeline), file_q_tmp);
    //   g_free (file_q_tmp);
      fakesink = gst_bin_get_by_name(GST_BIN (pipeline), fake_tmp);
    //   g_free (fake_tmp);

      /* Set pipeline branch to PLAYING */
      gst_element_set_state (GST_ELEMENT (source), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (identity), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (depay), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (parse), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (tee), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (app_queue), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (decode), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (convert), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (scale), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (filter), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (app_sink), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (file_queue), GST_STATE_NULL);
      gst_element_set_state (GST_ELEMENT (fakesink), GST_STATE_NULL);

      /* Set pipeline branch to PLAYING */
      gst_element_set_state (GST_ELEMENT (source), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (identity), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (depay), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (parse), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (tee), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (app_queue), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (decode), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (convert), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (scale), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (filter), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (app_sink), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (file_queue), GST_STATE_READY);
      gst_element_set_state (GST_ELEMENT (fakesink), GST_STATE_READY);

      /* Set pipeline branch to PLAYING */
      gst_element_set_state (GST_ELEMENT (source), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (identity), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (depay), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (parse), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (tee), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (app_queue), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (decode), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (convert), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (scale), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (filter), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (app_sink), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (file_queue), GST_STATE_PLAYING);
      gst_element_set_state (GST_ELEMENT (fakesink), GST_STATE_PLAYING);
      
      

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

static gboolean add_device(gchar *device_id, gchar *device_url)
{
    g_print("************************************************************A device has been received************************************************************\n");
    if(!start_stream(device_id, device_url))
    {
        return FALSE;
    }
}

static gboolean start_stream(gchar *device_id, gchar *device_url)
{
    g_print("The device ID is %s\n", device_id);

    int ret;
    gint handler_id = 0;
    gchar *bin_tmp, *ident_tmp, *depay_tmp, *parse_tmp, *tee_tmp, *app_q_tmp, *decode_tmp, *convert_tmp, *scale_tmp, *filter_tmp, *sink_tmp, *file_q_tmp, *mux_tmp, *fake_tmp;
    GstPad *tee_app_pad, *tee_file_pad, *queue_app_pad, *queue_file_pad, *dep_sink_pad;
    GstStream *dep_stream;
    
    // bin_tmp = g_strdup_printf ("bin-%s", device_id);
    ident_tmp = g_strdup_printf ("identity-%s", device_id);
    depay_tmp = g_strdup_printf ("depay-%s", device_id);
    parse_tmp = g_strdup_printf ("parse-%s", device_id);
    tee_tmp = g_strdup_printf ("tee-%s", device_id);
    app_q_tmp = g_strdup_printf ("app_q-%s", device_id);
    decode_tmp = g_strdup_printf ("decode-%s", device_id);
    convert_tmp = g_strdup_printf ("convert-%s", device_id);
    scale_tmp = g_strdup_printf ("scale-%s", device_id);
    filter_tmp = g_strdup_printf ("filter-%s", device_id);
    sink_tmp = g_strdup_printf ("sink-%s", device_id);
    file_q_tmp = g_strdup_printf ("file_q-%s", device_id);
    fake_tmp = g_strdup_printf ("fake-%s", device_id);

    // bin = gst_bin_new(bin_tmp);

    /* Create the elements */
    source = gst_element_factory_make("rtspsrc", device_id);
    identity = gst_element_factory_make("identity", ident_tmp);
    g_free (ident_tmp);
    depay = gst_element_factory_make("rtph265depay", depay_tmp);
    g_free (depay_tmp);
    parse = gst_element_factory_make("h265parse", parse_tmp);
    g_free (parse_tmp);
    tee = gst_element_factory_make("tee", tee_tmp);
    g_free (tee_tmp);
    app_queue = gst_element_factory_make("queue", app_q_tmp);
    g_free (app_q_tmp);
    decode = gst_element_factory_make("avdec_h265", decode_tmp);
    g_free (decode_tmp);
    convert = gst_element_factory_make("videoconvert", convert_tmp);
    g_free (convert_tmp);
    scale = gst_element_factory_make("videoscale", scale_tmp);
    g_free (scale_tmp);
    filter = gst_element_factory_make("capsfilter", filter_tmp);
    g_free (filter_tmp);
    app_sink = gst_element_factory_make("appsink", sink_tmp);
    g_free (sink_tmp);
    file_queue = gst_element_factory_make("queue", file_q_tmp);
    g_free (file_q_tmp);
    fakesink = gst_element_factory_make("fakesink", fake_tmp);
    g_free (fake_tmp);

    if (!source || !identity || !depay || !parse || !tee || !app_queue || !decode || !convert || !scale || !filter || !app_sink || !file_queue || !fakesink)
    {
        g_printerr("Not all elements could be created.\n");
        return FALSE;
    }

    /* must add elements to pipeline before linking them */
    gst_bin_add_many(GST_BIN(pipeline), source, identity, depay, parse, tee, app_queue, decode, convert, scale, filter, app_sink, file_queue, fakesink, NULL);
    if (!gst_element_link_many(identity, depay, parse, tee, NULL) || !gst_element_link_many(app_queue, decode, convert, scale, filter, app_sink, NULL) || !gst_element_link_many(file_queue, fakesink, NULL))
    {
        g_printerr("Elements could not be linked.\n");
        return FALSE;
    }
    else
    {
        g_print("Elements are linked successfully.\n");
    }

    tee_app_pad = gst_element_get_request_pad(tee, "src_%u");
    g_print("Obtained request pad %s for appsink branch.\n", gst_pad_get_name (tee_app_pad));
    tee_file_pad = gst_element_get_request_pad(tee, "src_%u");
    g_print("Obtained request pad %s for file(fakesink) branch.\n", gst_pad_get_name (tee_file_pad));

    queue_app_pad = gst_element_get_static_pad(app_queue, "sink");
    queue_file_pad = gst_element_get_static_pad(file_queue, "sink");

    if (gst_pad_link(tee_app_pad, queue_app_pad) != GST_PAD_LINK_OK ||
        gst_pad_link(tee_file_pad, queue_file_pad) != GST_PAD_LINK_OK) {
        g_printerr("Tee could not be linked.\n");
        gst_element_release_request_pad (tee, tee_app_pad);
        gst_element_release_request_pad (tee, tee_file_pad);
        gst_object_unref (tee_app_pad);
        gst_object_unref (tee_file_pad);
        return FALSE;
    }
    else
    {
        g_print("Tee is linked successfully\n");
        gst_object_unref(queue_app_pad);
        gst_object_unref(queue_file_pad);
    }

    GstCaps *filtercaps = gst_caps_new_simple("video/x-raw",
                                              "format", G_TYPE_STRING, "GRAY8",
                                              "width", G_TYPE_INT, 512,
                                              "height", G_TYPE_INT, 512,
                                              NULL);

    g_object_set(G_OBJECT(filter), "caps", filtercaps, NULL);
    gst_caps_unref(filtercaps);

    g_object_set(source, "location", "rtsp://192.168.29.92:8554//test1", NULL);

    /* Connect to the pad-added signal */
    g_signal_connect(source, "pad-added", G_CALLBACK(pad_added_handler), identity);

    /* Set pipeline branch to PLAYING */
    gst_element_set_state (GST_ELEMENT (source), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (identity), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (depay), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (parse), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (tee), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (app_queue), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (decode), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (convert), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (scale), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (filter), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (app_sink), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (file_queue), GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (fakesink), GST_STATE_PLAYING);

    g_object_set(app_sink, "emit-signals", TRUE, NULL);
    g_signal_connect(app_sink, "new-sample", G_CALLBACK(new_sample), source);
    g_signal_connect(app_sink, "eos", G_CALLBACK(eos_sample), source);

    
    return TRUE;
}


/* This function will be called by the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *new_pad, gpointer user_data) {

    g_print("The Source Pad Handler is called\n");

    GstPad *ident_sink_pad = gst_element_get_static_pad (user_data, "sink");
    g_assert_nonnull (ident_sink_pad);
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;
    
    g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));
    
    /* If our converter is already linked, we have nothing to do here */
    if (gst_pad_is_linked (ident_sink_pad)) {
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
    ret = gst_pad_link (new_pad, ident_sink_pad);
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
  gst_object_unref (ident_sink_pad);
}

static GstFlowReturn new_sample(GstAppSink *sink, gpointer data)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstBuffer *buffer_bytes;
    GstMapInfo info;
    GstMapInfo map;

    printf("The stream has started\n");

    gchar *element_name = gst_element_get_name(data);
    g_print("The element name is %s\n", element_name);
    g_free(element_name);

    const char *subj = "frame_topic";

    natsConnection *conn = NULL;
    natsStatistics *stats = NULL;
    natsOptions *opts = NULL;
    natsStatus s;
    int dataLen = 0;

    printf("Sending messages to subject '%s'\n", subj);
    s = natsConnection_Connect(&conn, opts);

    if (s == NATS_OK)
        s = natsStatistics_Create(&stats);

    if (s == NATS_OK)
        start = nats_Now();

    /* Retrieve the sample */
    sample = gst_app_sink_pull_sample(sink);

    if (sample)
    {

        buffer = gst_sample_get_buffer(sample);
        if (gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ)))
        {
            // g_print("%s ", (info.data));
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

static GstFlowReturn eos_sample(GstAppSink *sink, gpointer data)
{
    gchar *element_name = gst_element_get_name(data);
    g_print("The element name is %s\n", element_name);

    g_printerr("The appsink-%s has reached EOS\n", element_name);

    g_print("Posting the custom message\n");
    GstStructure *custom_structure;
    GstMessage *custom_message;

    custom_structure = gst_structure_new_empty(element_name);
    custom_message = gst_message_new_application(NULL, custom_structure);

    if (!gst_bus_post(bus, custom_message))
    {
        g_printerr("Cannot post message to bus\n");
    }
    g_print("The message is posted in the bus\n");   
    
    return GST_FLOW_OK;
} 
