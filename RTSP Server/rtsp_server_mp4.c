// gcc rtsp_server_mp4.c -o rtsp_server_mp4 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0`

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include<stdio.h>
#include<stdlib.h>
#include <stdlib.h>
#include <pthread.h>
#include <zconf.h>

#define PORT "8090"

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

struct pair pairs[50] = {
	{"/stream1", "/home/wajoud/yolov5/sample_videos/traffic.mp4", "haKDBkjhadlk"},
	{"/stream2", "/home/wajoud/yolov5/Trespass_People17.mp4", "ioalkjNmahnKL"},
	{"/stream3", "/home/wajoud/yolov5/society_entrance.mp4", "ooasasjbjabdhua"},
	{"/stream4", "/home/wajoud/yolov5/Trespass People_6.mp4", "rrtarsytahgbj"},
	{"/stream5", "/home/wajoud/yolov5/Robbery.mp4", "bbsnabsagaae"},
	{"/stream6", "/home/wajoud/yolov5/railway_station.mp4", "xccxheqoiueo"}};

GMainLoop *loop;
static GstRTSPServer *server;
static GstRTSPMountPoints *mounts;
static GstRTSPMediaFactory *factory;


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

static gboolean server_function(char *endpt, char *device_url, char *device_id, GstRTSPServer *server)
{

  g_print("Starting RTSP Server Streaming for device id: %s\n", device_id);

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines. 
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  gchar *gst_string;

  gst_string = g_strdup_printf("(filesrc location=%s name=src-%s ! qtdemux name=dem-%s dem-%s. ! queue ! rtph264pay pt=96 name=pay0 dem-%s. ! queue ! rtpmp4apay pt=97 name=pay1 )", device_url, device_id, device_id, device_id, device_id);
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
  g_print("stream ready at rtsp://216.48.189.5:%s/%s\n", PORT, endpt);

  g_free(gst_string);
	
  return TRUE;
}

int main(int argc, gchar *argv[])
{
	gst_init(&argc, &argv);
	
	struct arg_struct *args = malloc(sizeof(struct arg_struct));

	loop = g_main_loop_new (NULL, FALSE);

        /* create a server instance */
	server = gst_rtsp_server_new ();
	g_object_set(server, "service", PORT, NULL);


	for (int i = 0; i < 6; i++)
	{
		args->endpoint = pairs[i].str1;
		args->url = pairs[i].str2;
		args->deviceid = pairs[i].str3;

		if (!server_function(args->endpoint, args->url, args->deviceid, server)){
			g_printerr("Cannot add the stream\n");
		}
	}
	g_main_loop_run (loop);
	
	return 0;
}
