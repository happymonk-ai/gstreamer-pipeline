// gcc rtsp_server_mp4.c -o rtsp_server_mp4 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0`

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include<stdio.h>
#include<stdlib.h>
#include <stdlib.h>
#include <pthread.h>
#include <zconf.h>

struct arg_struct
{
  char *deviceid;
  char *url;
  char *port;
  char *endpoint;
};

struct pair
{
	char *str1;
	char *str2;
	char *str3;
	char *str4;
};

struct pair pairs[5] = {
	{"/stream1", "/home/nivetheni/nats.c/device_stream/video23.mp4", "8554", "haKDBkjhadlk"},
	{"/stream2", "/home/nivetheni/nats.c/device_stream/test_video.mp4", "8564", "ioalkjNmahnKL"}};


static gboolean
timeout(GstRTSPServer *server)
{
	GstRTSPSessionPool *pool;

	pool = gst_rtsp_server_get_session_pool(server);
	gst_rtsp_session_pool_cleanup(pool);
	g_object_unref(pool);

	return TRUE;
}

/* called when a stream has received an RTCP packet from the client */
static void
on_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  GST_INFO ("source %p in session %p is active", source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    g_print ("structure: %s\n", sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

static void
on_sender_ssrc_active (GObject * session, GObject * source,
    GstRTSPMedia * media)
{
  GstStructure *stats;

  GST_INFO ("source %p in session %p is active", source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    g_print ("Sender stats:\nstructure: %s\n", sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

/* signal callback when the media is prepared for streaming. We can get the
 * session manager for each of the streams and connect to some signals. */
static void
media_prepared_cb (GstRTSPMedia * media)
{
  guint i, n_streams;

  n_streams = gst_rtsp_media_n_streams (media);

  GST_INFO ("media %p is prepared and has %u streams", media, n_streams);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GObject *session;

    stream = gst_rtsp_media_get_stream (media, i);
    if (stream == NULL)
      continue;

    session = gst_rtsp_stream_get_rtpsession (stream);
    GST_INFO ("watching session %p on stream %u", session, i);

    g_signal_connect (session, "on-ssrc-active",
        (GCallback) on_ssrc_active, media);
    g_signal_connect (session, "on-sender-ssrc-active",
        (GCallback) on_sender_ssrc_active, media);
  }
}

static void
media_configure_cb (GstRTSPMediaFactory * factory, GstRTSPMedia * media)
{
  /* connect our prepared signal so that we can see when this media is
   * prepared for streaming */
  g_signal_connect (media, "prepared", (GCallback) media_prepared_cb, factory);
}

void *server_function(void *arguments)
{

  GMainLoop *loop;

  static GstRTSPServer *server;
  static GstRTSPMountPoints *mounts;
  static GstRTSPMediaFactory *factory;

  struct arg_struct *args = (struct arg_struct *)arguments;
  char *device_id = args->deviceid;
  char *device_url = args->url;
  char *port = args->port;
  char *endpt = args->endpoint;

  printf("DEVICE ID: %s\n", args->deviceid);

  loop = g_main_loop_new (NULL, FALSE);

  g_print("Starting RTSP Server Streaming for device id: %s\n", device_id);

  /* create a server instance */
  server = gst_rtsp_server_new ();
  g_object_set(server, "service", port, NULL);
  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines. 
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  gchar *gst_string, *src_tmp, *dem_tmp, *dec_tmp, *enc_tmp;

  src_tmp = g_strdup_printf ("src-%s", device_id);
  dem_tmp = g_strdup_printf ("dem-%s", device_id);

  gst_string = g_strdup_printf("(filesrc location=%s name=%s ! qtdemux name=%s %s. ! queue ! rtph264pay pt=96 name=pay0 %s. ! queue ! rtpmp4apay pt=97 name=pay1 )", device_url, src_tmp, dem_tmp, dem_tmp, dem_tmp);
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, gst_string);

  g_signal_connect (factory, "media-configure", (GCallback) media_configure_cb,
      factory);

  gst_rtsp_media_factory_set_shared (factory, TRUE);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory (mounts, endpt, factory);

  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  gst_rtsp_server_attach (server, NULL);

  /* start serving */
  g_print("stream ready at rtsp://127.0.0.1:%s/%s\n", port, endpt);
  g_main_loop_run (loop);

  g_free(src_tmp);
  g_free(dem_tmp);
  g_free(gst_string);
}

int main(int argc, gchar *argv[])
{
	gst_init(&argc, &argv);

	pthread_t thread[50];
	int err;
	struct arg_struct *args = malloc(sizeof(struct arg_struct));

	for (int i = 0; i < 2; i++)
	{
		args->endpoint = pairs[i].str1;
		args->url = pairs[i].str2;
		args->port = pairs[i].str3;
		args->deviceid = pairs[i].str4;

		err = pthread_create(&thread[i], NULL, server_function, args);

		if (err)
		{
			printf("An error occured: %d\n", err);
			return 1;
		}
		sleep(3);
	}

	pthread_exit(0);

    return 0;
}