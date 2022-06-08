#include <gst/gst.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

static GMainLoop *loop;
GstElement *pipeline;
GstBus *bus;
GstMessage *msg;

static gboolean hls_function(char *file_path, char *file_url, char *file_name)
{

  GstStateChangeReturn ret;
  GError *error = NULL;

  g_print("\nStarting the hls streaming\n");

  gchar *gst_str1, *gst_str2, *gst_str3;

  gst_str1 = g_strdup_printf("rtspsrc location=%s ! rtph264depay ! avdec_h264 ! clockoverlay ! videoconvert ! videoscale ! video/x-raw,width=1080, height=1080 ! x264enc bitrate=512 ! video/x-h264,profile=\"high\" ! hlssink2 playlist-root=http://216.48.189.5:8060 playlist-location=%s/%s.m3u8 location=%s", file_url, file_path, file_name, file_path);

  gst_str2 = "/segment.%05d.ts target-duration=15 max-files=30 playlist-length=30";

  gst_str3 = (char *) malloc(1 + strlen(gst_str1)+ strlen(gst_str2) );

  strcpy(gst_str3, gst_str1);

  strcat(gst_str3, gst_str2);

  // g_print("%s", gst_str3);

  /* Build the pipeline */
  pipeline = gst_parse_launch(gst_str3, &error);

  /* Start playing */
  gst_element_set_state(pipeline, GST_STATE_PLAYING);

  if (error)
  {
    g_printerr("Failed to parse launch: %s\n", error->message);
    g_error_free(error);
    goto err;
  }

  ret = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
  {
    goto err;
  }
  // g_free(gst_str1);
  // g_free(gst_str2);
  g_free(gst_str3);
  return TRUE;

err:
  g_print("State change failure\n");
  if (pipeline)
  {
    gst_object_unref(pipeline);
  }
  return FALSE;
}

static gboolean initial_pipeline()
{
  GstStateChangeReturn ret;
  GError *error = NULL;

  pipeline = gst_parse_launch("fakesrc ! queue ! fakesink", &error);

  if (error)
  {
    g_printerr("Failed to parse launch: %s\n", error->message);
    g_error_free(error);
    goto err;
  }

  g_print("Starting pipeline, not transmitting yet\n");

  ret = gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
  {
    goto err;
  }
  return TRUE;

err:
  g_print("State change failure\n");
  if (pipeline)
  {
    gst_object_unref(pipeline);
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

int main(int argc, gchar *argv[])
{
  gst_init(&argc, &argv);
  
  char *val = getenv("HLS_LIMIT");

  int x = atoi(val);

  char *file_path, *file_url, *file_name;

  loop = g_main_loop_new(NULL, FALSE);

  if (!initial_pipeline())
  {
    g_printerr("ERROR: Failed to start pipeline");
    return -1;
  }

  bus = gst_element_get_bus(pipeline);
  gst_bus_add_signal_watch(bus);
  g_signal_connect(bus, "message", (GCallback)cb_message,
                   pipeline);

  for (int i = 1; i <= x; i++)
  {
    file_path = g_strdup_printf("/app/streams/stream%d", i);
    mkdir(file_path, 0777);
    file_url = g_strdup_printf("rtsp://216.48.189.5:8090//stream%d", i);
    file_name = g_strdup_printf("%d", i);

    if (!hls_function(file_path, file_url, file_name))
    {
      g_printerr("Cannot start the stream\n");
    }
  }
  g_main_loop_run(loop);

  if (pipeline)
  {
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);
    g_print("Pipeline stopped\n");
    gst_object_unref(pipeline);
  }

  /* Free resources */
  g_main_loop_unref(loop);
  gst_message_unref(msg);
  gst_object_unref(bus);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
  return 0;
}
