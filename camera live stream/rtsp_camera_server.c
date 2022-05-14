// gcc rtsp_camera_server.c -o rtsp_camera_server -lgstnet-1.0 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0`

#include <gst/gst.h>
#include <gst/net/gstnettimeprovider.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <pthread.h>
#include <zconf.h>

#define PORT "8554"

static GMainLoop *loop;
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

static gboolean server_function(char *endpt, char *device_url, char *device_id)
{

    g_print("Starting RTSP Server Streaming for device id: %s\n", device_id);

    global_clock = gst_system_clock_obtain();
    gst_net_time_provider_new(global_clock, NULL, 8554);

    /* get the mount points for this server, every server has a default object
     * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points(server);

    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */

    gchar *gst_string;

    gst_string = g_strdup_printf("(rtspsrc location=%s name=src-%s drop-on-latency=true is-live=true latency=0 ! rtpjitterbuffer drop-on-latency=true ! queue ! rtph265depay name=dep-%s ! h265parse name=parse-%s ! queue ! rtph265pay name=pay0 pt=96 )", device_url, device_id, device_id, device_id);
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

static char *mkrndstr(size_t length)
{

    static char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
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

int main(int argc, gchar *argv[])
{
    gst_init(&argc, &argv);

    char *endpt, *location, *device_id;

    loop = g_main_loop_new(NULL, FALSE);

    /* create a server instance */
    server = gst_rtsp_server_new();

    g_object_set(server, "service", PORT, NULL);

    for (int i = 1; i <= 3; i++)
    {
        endpt = g_strdup_printf("/stream%d", i);
        location = g_strdup_printf("rtsp://admin:Admin@1234@192.168.1.10%d:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif", i);
        device_id = mkrndstr(10);

        printf("%s %s %s", endpt, location, device_id);

        if (!server_function(endpt, location, device_id))
        {
            g_printerr("Cannot add the stream\n");
        }
    }

    g_main_loop_run(loop);

    g_main_loop_unref(loop);

    return 0;
}