// gcc rtsp_server_camera.c -o rtsp_server_camera `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0`

#include <gst/gst.h>
#include <gst/net/gstnettimeprovider.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <pthread.h>
#include <zconf.h>

#define PORT "8090"

struct arg_struct
{
  char *deviceid;
  char *url;
  char *endpoint;
};

struct pair
{
  char *str1;
  char *str2;
  char *str3;
};

struct pair pairs[5] = {
    {"/stream1", "rtsp://admin:Admin@1234@192.168.1.103:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif", "haKDBkjhadlk"},
    {"/stream2", "rtsp://admin:Admin@1234@192.168.1.105:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif", "ioalkjNmahnKL"},
    {"/stream3", "rtsp://admin:Admin@1234@192.168.1.107:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif", "kkksajhajufsd"},
    {"/stream4", "rtsp://admin:Admin@1234@192.168.1.102:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif", "ppoapdakjabsx"},
    {"/stream5", "rtsp://admin:Admin@1234@192.168.1.101:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif", "aaqqhabsnnaxv"},};

GMainLoop *loop;
static GstRTSPServer *server;
static GstRTSPMountPoints *mounts;
static GstRTSPMediaFactory *factory;
static GstClock *global_clock;

#define TEST_TYPE_RTSP_MEDIA_FACTORY (test_rtsp_media_factory_get_type())
#define TEST_TYPE_RTSP_MEDIA (test_rtsp_media_get_type())

GType test_rtsp_media_get_type(void);

typedef struct TestRTSPMediaClass TestRTSPMediaClass;
typedef struct TestRTSPMedia TestRTSPMedia;

struct TestRTSPMediaClass
{
  GstRTSPMediaClass parent;
};

struct TestRTSPMedia
{
  GstRTSPMedia parent;
};

static gboolean custom_setup_rtpbin(GstRTSPMedia *media, GstElement *rtpbin);

G_DEFINE_TYPE(TestRTSPMedia, test_rtsp_media, GST_TYPE_RTSP_MEDIA);

static void
test_rtsp_media_class_init(TestRTSPMediaClass *test_klass)
{
  GstRTSPMediaClass *klass = (GstRTSPMediaClass *)(test_klass);
  klass->setup_rtpbin = custom_setup_rtpbin;
}

static void
test_rtsp_media_init(TestRTSPMedia *media)
{
}

static gboolean
custom_setup_rtpbin(GstRTSPMedia *media, GstElement *rtpbin)
{
  g_object_set(rtpbin, "ntp-time-source", 3, NULL);
  return TRUE;
}

static gboolean server_function(char *endpt, char *device_url, char *device_id, GstRTSPServer *server)
{

  g_print("Starting RTSP Server Streaming for device id: %s\n", device_id);

  global_clock = gst_system_clock_obtain();
  gst_net_time_provider_new(global_clock, NULL, 8090);

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points(server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  gchar *gst_string, *src_tmp, *dep_tmp, *par_tmp;

  src_tmp = g_strdup_printf("src-%s", device_id);
  dep_tmp = g_strdup_printf("dep-%s", device_id);
  par_tmp = g_strdup_printf("par-%s", device_id);

  gst_string = g_strdup_printf("(rtspsrc location=%s name=%s drop-on-latency=true is-live=true latency=0 ! rtpjitterbuffer drop-on-latency=true ! queue ! rtph265depay name=%s ! h265parse name=%s ! queue ! rtph265pay name=pay0 pt=96 )", device_url, src_tmp, dep_tmp, par_tmp);
  factory = gst_rtsp_media_factory_new();
  gst_rtsp_media_factory_set_launch(factory, gst_string);

  gst_rtsp_media_factory_set_shared(factory, TRUE);
  gst_rtsp_media_factory_set_media_gtype(factory, TEST_TYPE_RTSP_MEDIA);
  gst_rtsp_media_factory_set_clock(factory, global_clock);

  /* attach the test factory to the /test url */
  gst_rtsp_mount_points_add_factory(mounts, endpt, factory);

  /* don't need the ref to the mapper anymore */
  g_object_unref(mounts);

  /* attach the server to the default maincontext */
  gst_rtsp_server_attach(server, NULL);

  /* start serving */
  g_print("stream ready at rtsp://127.0.0.1:%s/%s\n", PORT, endpt);

  g_free(gst_string);

  return TRUE;
}

int main(int argc, gchar *argv[])
{
  gst_init(&argc, &argv);

  loop = g_main_loop_new(NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new();
  g_object_set(server, "service", PORT, NULL);

  for (int i = 0; i < 5; i++)
  {
    args->endpoint = pairs[i].str1;
    args->url = pairs[i].str2;
    args->deviceid = pairs[i].str3;

    if (!server_function(args->endpoint, args->url, args->deviceid, server))
    {
      g_printerr("Cannot add the stream\n");
    }
  }

  return 0;
}