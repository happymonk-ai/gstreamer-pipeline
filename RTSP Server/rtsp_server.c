// gcc rtsp_server.c -o rtsp_server `pkg-config --cflags --libs gstreamer-1.0 libnats gstreamer-rtsp-server-1.0`

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <stdio.h>
#include <stdlib.h>
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
	{"/stream1", "rtsp://nivetheni:Chandrika5@192.168.29.64/Streaming/Channels/101?transportmode=unicast&profile=Profile_1", "8554", "haKDBkjhadlk"},
	{"/stream2", "rtsp://nivetheni:Chandrika5@192.168.29.65/Streaming/Channels/101?transportmode=unicast&profile=Profile_1", "8564", "ioalkjNmahnKL"},
	{"/stream3", "rtsp://nivetheni:Chandrika5@192.168.29.64/Streaming/Channels/101?transportmode=unicast&profile=Profile_1", "8574", "opoSILKJS"},
	{"/stream4", "rtsp://nivetheni:Chandrika5@192.168.29.65/Streaming/Channels/101?transportmode=unicast&profile=Profile_1", "8584", "ACvqSAAYWQ"},
	{"/stream5", "rtsp://nivetheni:Chandrika5@192.168.29.64/Streaming/Channels/101?transportmode=unicast&profile=Profile_1", "8594", "Naejhqlkejqw"}};

static char *nats_url = "nats://164.52.213.244:4222";
static natsConnection      *conn = NULL;
static natsSubscription    *sub  = NULL;
static natsStatus          s;
static volatile bool       done  = false;
static const char *subject = "device.*";
static const char *queueGroup = "device::stream::crud";

static natsStatistics *stats = NULL;

void *serverStream_interpipesrc (void *arguments) 
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
  gchar *gst_string, *src_tmp, *dep_tmp, *par_tmp;

  src_tmp = g_strdup_printf ("src-%s", device_id);
  dep_tmp = g_strdup_printf ("dep-%s", device_id);
  par_tmp = g_strdup_printf ("par-%s", device_id);

  gst_string = g_strdup_printf("(rtspsrc location=%s name=%s ! rtph265depay name=%s ! h265parse name=%s ! rtph265pay name=pay0 pt=96 )", device_url, src_tmp, dep_tmp, par_tmp);
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, gst_string);

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
  g_free(dep_tmp);
  g_free(par_tmp);
  g_free(gst_string);
}


static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
	pthread_t thread[5];
	int err;

    printf("Received msg: %s - %.*s\n",
           natsMsg_GetSubject(msg),
           natsMsg_GetDataLength(msg),
           natsMsg_GetData(msg));

    struct arg_struct *args = malloc(sizeof(struct arg_struct));
    args->deviceid = malloc(20);
    strcpy(args->deviceid, natsMsg_GetData(msg));
    int i = atoi(args->deviceid);

    args->endpoint = pairs[i].str1;
	args->url = pairs[i].str2;
	args->port = pairs[i].str3;

    err = pthread_create(&thread[i], NULL, serverStream_interpipesrc, args);

	if (err)
	{
		printf("An error occured: %d\n", err);
	}
    
    // Need to destroy the message!

    natsMsg_Destroy(msg);
}

int main(int argc, gchar *argv[])
{
	gst_init(&argc, &argv);

	printf("Listening on queue group %s\n", queueGroup);

	// Creates a connection to the NATS URL
    s = natsConnection_Connect(&conn, opts);
    // s = natsConnection_ConnectTo(&conn, nats_url);
    if (s == NATS_OK)
    {
        s = natsConnection_QueueSubscribe(&sub, conn, subject, queueGroup, onMsg, (void*) &done);
    }
    if (s == NATS_OK)
    {
        for (;!done;) {
            nats_Sleep(100);
        }
    }

	// Anything that is created need to be destroyed
    natsSubscription_Destroy(sub);
    natsConnection_Destroy(conn);

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    pthread_exit(0);
	return 0;
}
