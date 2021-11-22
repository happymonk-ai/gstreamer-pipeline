#include <gst/gst.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <msgpack.h>
#include "./nats.c/examples/examples.h"
#include <gst/app/gstappsink.h>

typedef struct _CustomData
{
  gboolean is_live;
  GstElement *pipeline, *app_sink;;
  GMainLoop *loop;
} CustomData;

/* The appsink has received a buffer */
static GstFlowReturn new_sample (GstAppSink *sink, CustomData *data) {
  GstSample *sample;
  GstBuffer *buffer;
  GstBuffer *buffer_bytes;
	GstMapInfo info;
  GstMapInfo map;

  /* Nats is connected */
  const char  *subj   = "frame-topic1";

  natsConnection  *conn  = NULL;
  natsStatistics  *stats = NULL;
  natsOptions     *opts  = NULL;
  natsStatus      s;
  int             dataLen=0;

  printf("Sending messages to subject '%s'\n", subj);
  s = natsConnection_ConnectTo(&conn, "nats://164.52.213.244:4222");

  if (s == NATS_OK)
      s = natsStatistics_Create(&stats);

  if (s == NATS_OK)
      start = nats_Now();

  /* Retrieve the sample */
	sample = gst_app_sink_pull_sample(sink);

  if (sample) {

    buffer = gst_sample_get_buffer(sample);
    gst_buffer_map( buffer, &info, (GstMapFlags)(GST_MAP_READ) );

    buffer_bytes = gst_buffer_new_wrapped(info.data, info.size);
    gst_buffer_map( buffer_bytes, &map, (GstMapFlags)(GST_MAP_READ) );
    g_print("%s", map.data);

    /* Publishing the frames to nats */
    for (count = 0; (s == NATS_OK) && (count < 1); count++){
        s = natsConnection_Publish(conn, subj, (const void*) info.data, info.size);
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

static void
cb_message (GstBus * bus, GstMessage * msg, CustomData * data)
{

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *debug;

      gst_message_parse_error (msg, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      gst_element_set_state (data->pipeline, GST_STATE_READY);
      g_main_loop_quit (data->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      gst_element_set_state (data->pipeline, GST_STATE_READY);
      g_main_loop_quit (data->loop);
      break;
    
  }
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline;
  GstBus *bus;
  GstStateChangeReturn ret;
  GMainLoop *main_loop;
  CustomData data;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));

  /* Build the pipeline */
  pipeline =
      gst_parse_launch
      ("rtspsrc location=rtsp://172.26.130.124:8554/test1 latency=0 ! queue ! rtph265depay ! h265parse ! decodebin ! videoconvert ! videoscale ! video/x-raw, width=160, height=120, format=GRAY8 ! appsink name=sink" ,
      NULL);
  bus = gst_element_get_bus (pipeline);

  /* Configure appsink */
  data.app_sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  g_object_set (data.app_sink, "emit-signals", TRUE, NULL);
  g_signal_connect (data.app_sink, "new-sample", G_CALLBACK (new_sample), &data);

  /* Start playing */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return -1;
  } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
    data.is_live = TRUE;
  }

  main_loop = g_main_loop_new (NULL, FALSE);
  data.loop = main_loop;
  data.pipeline = pipeline;

  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (cb_message), &data);

  g_main_loop_run (main_loop);

  /* Free resources */
  g_main_loop_unref (main_loop);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
