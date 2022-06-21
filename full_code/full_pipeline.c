// gcc full_pipeline.c -o full_pipeline -lgstnet-1.0 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0 json-c libnats`
// python -m http.server 8080 --bind 127.0.0.1

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include "test-replay-server.h"
#include <gst/net/gstnettimeprovider.h>
#include <stdio.h>
#include <stdlib.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <gst/net/gstnet.h>
#include <json.h>
#include <string.h>
#include <gst/app/gstappsink.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory.h>

#define PORT "8554"
#define PORT1 "8090"

static GMainLoop *loop;
static GstRTSPServer *server;
static GstRTSPMountPoints *mounts;
static GstRTSPMediaFactory *factory;
static GstClock *global_clock;
static GstClock *net_clock;
static gchar *clock_server = "0.0.0.0";
static gint clock_port = 8554;

static GstBus *bus;
static GstMessage *msg;
static GMainLoop *loop;
static GstElement *pipe1, *app_sink;

static char *nats_url = "nats://216.48.189.5:4222";
static natsConnection *conn = NULL;
static natsSubscription *sub = NULL;
static natsStatus s;
static volatile bool done = false;
static const char *subject = "device.*.stream";
static const char *queueGroup = "device::stream::crud";
static jsCtx *js = NULL;
static jsOptions jsOpts;
static jsErrCode jerr = 0;
volatile int errors = 0;

static natsStatistics *stats = NULL;

const char *stream1 = "device_stream";
const char *subject1 = "stream.*.frame";

struct arg_struct
{
    char *id;
    char *url;
    const char *json_str;
};

static GError *error = NULL;
static gchar *service;
static gchar *uri = NULL;
static gint64 num_loops = -1;

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

GST_DEBUG_CATEGORY_STATIC(replay_server_debug);
#define GST_CAT_DEFAULT (replay_server_debug)

static GstStaticCaps raw_video_caps = GST_STATIC_CAPS("video/x-raw");
static GstStaticCaps raw_audio_caps = GST_STATIC_CAPS("audio/x-raw");

static GList
    *
    gst_rtsp_media_factory_replay_get_demuxers(GstRTSPMediaFactoryReplay *
                                                   factory);
static GList
    *
    gst_rtsp_media_factory_replay_get_payloaders(GstRTSPMediaFactoryReplay *
                                                     factory);
static GList
    *
    gst_rtsp_media_factory_replay_get_decoders(GstRTSPMediaFactoryReplay *
                                                   factory);

static void
_jsPubErr(jsCtx *js, jsPubAckErr *pae, void *closure)
{
    int *errors = (int *)closure;

    printf("Error: %u - Code: %u - Text: %s\n", pae->Err, pae->ErrCode, pae->ErrText);
    printf("Original message: %.*s\n", natsMsg_GetDataLength(pae->Msg), natsMsg_GetData(pae->Msg));

    *errors = (*errors + 1);
}

/*
 * In real life the operations done here are way more complicated, but it's
 * only an example.
 */
struct json_object *find_something(struct json_object *jobj, const char *key)
{
    struct json_object *tmp;

    json_object_object_get_ex(jobj, key, &tmp);

    return tmp;
}

static char *removeChar(char *str, char charToRemmove)
{
    int i, j;
    int len = strlen(str);
    for (i = 0; i < len; i++)
    {
        if (str[i] == charToRemmove)
        {
            for (j = i; j < len; j++)
            {
                str[j] = str[j + 1];
            }
            len--;
            i--;
        }
    }

    return str;
}

typedef struct
{
    GstPad *srcpad;
    gulong block_id;
} GstReplayBinPad;

static void
gst_replay_bin_pad_unblock_and_free(GstReplayBinPad *pad)
{
    if (pad->srcpad && pad->block_id)
    {
        GST_DEBUG_OBJECT(pad->srcpad, "Unblock");
        gst_pad_remove_probe(pad->srcpad, pad->block_id);
        pad->block_id = 0;
    }

    gst_clear_object(&pad->srcpad);
    g_free(pad);
}

/* NOTE: this bin implementation is almost completely taken from rtsp-media-factory-uri
 * but this example doesn't use the GstRTSPMediaFactoryURI object so that
 * we can handle events and messages ourselves.
 * Specifically,
 * - Handle segment-done message for looping given source
 * - Drop all incoming seek event because client seek is not implemented
 *   and do initial segment seeking on no-more-pads signal
 */
struct _GstReplayBin
{
    GstBin parent;

    gint64 num_loops;

    GstCaps *raw_vcaps;
    GstCaps *raw_acaps;

    guint pt;

    /* without ref */
    GstElement *uridecodebin;
    GstElement *inner_bin;

    /* holds ref */
    GstRTSPMediaFactoryReplay *factory;

    GMutex lock;

    GList *srcpads;
};

static void gst_replay_bin_dispose(GObject *object);
static void gst_replay_bin_finalize(GObject *object);
static void gst_replay_bin_handle_message(GstBin *bin, GstMessage *message);

static gboolean autoplug_continue_cb(GstElement *dbin, GstPad *pad,
                                     GstCaps *caps, GstReplayBin *self);
static void pad_added_cb(GstElement *dbin, GstPad *pad, GstReplayBin *self);
static void no_more_pads_cb(GstElement *uribin, GstReplayBin *self);

#define gst_replay_bin_parent_class bin_parent_class
G_DEFINE_TYPE(GstReplayBin, gst_replay_bin, GST_TYPE_BIN);

static void
gst_replay_bin_class_init(GstReplayBinClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBinClass *bin_class = GST_BIN_CLASS(klass);

    gobject_class->dispose = gst_replay_bin_dispose;
    gobject_class->finalize = gst_replay_bin_finalize;

    bin_class->handle_message = GST_DEBUG_FUNCPTR(gst_replay_bin_handle_message);
}

static void
gst_replay_bin_init(GstReplayBin *self)
{
    self->raw_vcaps = gst_static_caps_get(&raw_video_caps);
    self->raw_acaps = gst_static_caps_get(&raw_audio_caps);

    self->uridecodebin = gst_element_factory_make("uridecodebin", NULL);
    if (!self->uridecodebin)
    {
        GST_ERROR_OBJECT(self, "uridecodebin is unavailable");
        return;
    }

    /* our bin will dynamically expose payloaded pads */
    self->inner_bin = gst_bin_new("dynpay0");
    gst_bin_add(GST_BIN_CAST(self), self->inner_bin);
    gst_bin_add(GST_BIN_CAST(self->inner_bin), self->uridecodebin);

    g_signal_connect(self->uridecodebin, "autoplug-continue",
                     G_CALLBACK(autoplug_continue_cb), self);
    g_signal_connect(self->uridecodebin, "pad-added",
                     G_CALLBACK(pad_added_cb), self);
    g_signal_connect(self->uridecodebin, "no-more-pads",
                     G_CALLBACK(no_more_pads_cb), self);

    self->pt = 96;

    g_mutex_init(&self->lock);
}

static void
gst_replay_bin_dispose(GObject *object)
{
    GstReplayBin *self = GST_REPLAY_BIN(object);

    GST_DEBUG_OBJECT(self, "dispose");

    gst_clear_caps(&self->raw_vcaps);
    gst_clear_caps(&self->raw_acaps);
    gst_clear_object(&self->factory);

    if (self->srcpads)
    {
        g_list_free_full(self->srcpads,
                         (GDestroyNotify)gst_replay_bin_pad_unblock_and_free);
        self->srcpads = NULL;
    }

    G_OBJECT_CLASS(bin_parent_class)->dispose(object);
}

static void
gst_replay_bin_finalize(GObject *object)
{
    GstReplayBin *self = GST_REPLAY_BIN(object);

    g_mutex_clear(&self->lock);

    G_OBJECT_CLASS(bin_parent_class)->finalize(object);
}

static gboolean
send_eos_foreach_srcpad(GstElement *element, GstPad *pad, gpointer user_data)
{
    GST_DEBUG_OBJECT(pad, "Sending EOS to downstream");
    gst_pad_push_event(pad, gst_event_new_eos());

    return TRUE;
}

static void
gst_replay_bin_do_segment_seek(GstElement *element, GstReplayBin *self)
{
    gboolean ret;

    ret = gst_element_seek(element, 1.0, GST_FORMAT_TIME,
                           GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_SEGMENT,
                           GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, -1);

    if (!ret)
    {
        GST_WARNING_OBJECT(self, "segment seeking failed");
        gst_element_foreach_src_pad(element,
                                    (GstElementForeachPadFunc)send_eos_foreach_srcpad, NULL);
    }
}

static void
gst_replay_bin_handle_message(GstBin *bin, GstMessage *message)
{
    GstReplayBin *self = GST_REPLAY_BIN(bin);

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_SEGMENT_DONE)
    {
        gboolean next_loop = TRUE;

        GST_DEBUG_OBJECT(self, "Have segment done message");

        g_mutex_lock(&self->lock);
        if (self->num_loops != -1)
        {
            self->num_loops--;

            if (self->num_loops < 1)
                next_loop = FALSE;
        }

        if (next_loop)
        {
            /* Send seek event from non-streaming thread */
            gst_element_call_async(GST_ELEMENT_CAST(self->uridecodebin),
                                   (GstElementCallAsyncFunc)gst_replay_bin_do_segment_seek, self, NULL);
        }
        else
        {
            gst_element_foreach_src_pad(GST_ELEMENT_CAST(self->uridecodebin),
                                        (GstElementForeachPadFunc)send_eos_foreach_srcpad, NULL);
        }

        g_mutex_unlock(&self->lock);
    }

    GST_BIN_CLASS(bin_parent_class)->handle_message(bin, message);
}

static GstElementFactory *
find_payloader(GstReplayBin *self, GstCaps *caps)
{
    GList *list;
    GstElementFactory *factory = NULL;
    gboolean autoplug_more = FALSE;
    GList *demuxers = NULL;
    GList *payloaders = NULL;

    demuxers = gst_rtsp_media_factory_replay_get_demuxers(self->factory);

    /* first find a demuxer that can link */
    list = gst_element_factory_list_filter(demuxers, caps, GST_PAD_SINK, FALSE);

    if (list)
    {
        GstStructure *structure = gst_caps_get_structure(caps, 0);
        gboolean parsed = FALSE;
        gint mpegversion = 0;

        if (!gst_structure_get_boolean(structure, "parsed", &parsed) &&
            gst_structure_has_name(structure, "audio/mpeg") &&
            gst_structure_get_int(structure, "mpegversion", &mpegversion) &&
            (mpegversion == 2 || mpegversion == 4))
        {
            /* for AAC it's framed=true instead of parsed=true */
            gst_structure_get_boolean(structure, "framed", &parsed);
        }

        /* Avoid plugging parsers in a loop. This is not 100% correct, as some
         * parsers don't set parsed=true in caps. We should do something like
         * decodebin does and track decode chains and elements plugged in those
         * chains...
         */
        if (parsed)
        {
            GList *walk;
            const gchar *klass;

            for (walk = list; walk; walk = walk->next)
            {
                factory = GST_ELEMENT_FACTORY(walk->data);
                klass = gst_element_factory_get_metadata(factory,
                                                         GST_ELEMENT_METADATA_KLASS);
                if (strstr(klass, "Parser"))
                    /* caps have parsed=true, so skip this parser to avoid loops */
                    continue;

                autoplug_more = TRUE;
                break;
            }
        }
        else
        {
            /* caps don't have parsed=true set and we have a demuxer/parser */
            autoplug_more = TRUE;
        }

        gst_plugin_feature_list_free(list);
    }

    if (autoplug_more)
        /* we have a demuxer, try that one first */
        return NULL;

    payloaders = gst_rtsp_media_factory_replay_get_payloaders(self->factory);

    /* no demuxer try a depayloader */
    list = gst_element_factory_list_filter(payloaders,
                                           caps, GST_PAD_SINK, FALSE);

    if (list == NULL)
    {
        GList *decoders =
            gst_rtsp_media_factory_replay_get_decoders(self->factory);
        /* no depayloader, try a decoder, we'll get to a payloader for a decoded
         * video or audio format, worst case. */
        list = gst_element_factory_list_filter(decoders,
                                               caps, GST_PAD_SINK, FALSE);

        if (list != NULL)
        {
            /* we have a decoder, try that one first */
            gst_plugin_feature_list_free(list);
            return NULL;
        }
    }

    if (list != NULL)
    {
        factory = GST_ELEMENT_FACTORY_CAST(list->data);
        g_object_ref(factory);
        gst_plugin_feature_list_free(list);
    }

    return factory;
}

static gboolean
autoplug_continue_cb(GstElement *dbin, GstPad *pad, GstCaps *caps,
                     GstReplayBin *self)
{
    GstElementFactory *factory;

    GST_DEBUG_OBJECT(self, "found pad %s:%s of caps %" GST_PTR_FORMAT,
                     GST_DEBUG_PAD_NAME(pad), caps);

    if (!(factory = find_payloader(self, caps)))
        goto no_factory;

    /* we found a payloader, stop autoplugging so we can plug the
     * payloader. */
    GST_DEBUG_OBJECT(self, "found factory %s",
                     gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));
    gst_object_unref(factory);

    return FALSE;

no_factory:
{
    /* no payloader, continue autoplugging */
    GST_DEBUG_OBJECT(self, "no payloader found for caps %" GST_PTR_FORMAT,
                     caps);
    return TRUE;
}
}

static GstPadProbeReturn
replay_bin_sink_probe(GstPad *pad, GstPadProbeInfo *info,
                      GstReplayBin *self)
{
    GstPadProbeReturn ret = GST_PAD_PROBE_OK;

    if (GST_IS_EVENT(GST_PAD_PROBE_INFO_DATA(info)))
    {
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

        switch (GST_EVENT_TYPE(event))
        {
        case GST_EVENT_SEEK:
            /* Ideally this shouldn't happen because we are responding
             * seeking query with non-seekable */
            GST_DEBUG_OBJECT(pad, "Drop seek event");
            ret = GST_PAD_PROBE_DROP;
            break;
        default:
            break;
        }
    }
    else if (GST_IS_QUERY(GST_PAD_PROBE_INFO_DATA(info)))
    {
        GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);

        switch (GST_QUERY_TYPE(query))
        {
        case GST_QUERY_SEEKING:
        {
            /* FIXME: client seek is not implemented */
            gst_query_set_seeking(query, GST_FORMAT_TIME, FALSE, 0,
                                  GST_CLOCK_TIME_NONE);
            ret = GST_PAD_PROBE_HANDLED;
            break;
        }
        case GST_QUERY_SEGMENT:
            /* client seeking is not considered in here */
            gst_query_set_segment(query,
                                  1.0, GST_FORMAT_TIME, 0, GST_CLOCK_TIME_NONE);
            ret = GST_PAD_PROBE_HANDLED;
            break;
        default:
            break;
        }
    }

    return ret;
}

static GstPadProbeReturn
replay_bin_src_block(GstPad *pad, GstPadProbeInfo *info, GstReplayBin *self)
{
    GST_DEBUG_OBJECT(pad, "Block pad");

    return GST_PAD_PROBE_OK;
}

static void
pad_added_cb(GstElement *dbin, GstPad *pad, GstReplayBin *self)
{
    GstElementFactory *factory;
    GstElement *payloader;
    GstCaps *caps;
    GstPad *sinkpad, *srcpad, *ghostpad;
    GstPad *dpad = pad;
    GstElement *convert;
    gchar *padname, *payloader_name;
    GstElement *inner_bin = self->inner_bin;
    GstReplayBinPad *bin_pad;

    GST_DEBUG_OBJECT(self, "added pad %s:%s", GST_DEBUG_PAD_NAME(pad));

    /* ref to make refcounting easier later */
    gst_object_ref(pad);
    padname = gst_pad_get_name(pad);

    /* get pad caps first, then call get_caps, then fail */
    if ((caps = gst_pad_get_current_caps(pad)) == NULL)
        if ((caps = gst_pad_query_caps(pad, NULL)) == NULL)
            goto no_caps;

    /* check for raw caps */
    if (gst_caps_can_intersect(caps, self->raw_vcaps))
    {
        /* we have raw video caps, insert converter */
        convert = gst_element_factory_make("videoconvert", NULL);
    }
    else if (gst_caps_can_intersect(caps, self->raw_acaps))
    {
        /* we have raw audio caps, insert converter */
        convert = gst_element_factory_make("audioconvert", NULL);
    }
    else
    {
        convert = NULL;
    }

    if (convert)
    {
        gst_bin_add(GST_BIN_CAST(inner_bin), convert);
        gst_element_sync_state_with_parent(convert);

        sinkpad = gst_element_get_static_pad(convert, "sink");
        gst_pad_link(pad, sinkpad);
        gst_object_unref(sinkpad);

        /* unref old pad, we reffed before */
        gst_object_unref(pad);

        /* continue with new pad and caps */
        pad = gst_element_get_static_pad(convert, "src");
        if ((caps = gst_pad_get_current_caps(pad)) == NULL)
            if ((caps = gst_pad_query_caps(pad, NULL)) == NULL)
                goto no_caps;
    }

    if (!(factory = find_payloader(self, caps)))
        goto no_factory;

    gst_caps_unref(caps);

    /* we have a payloader now */
    GST_DEBUG_OBJECT(self, "found payloader factory %s",
                     gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));

    payloader_name = g_strdup_printf("pay_%s", padname);
    payloader = gst_element_factory_create(factory, payloader_name);
    g_free(payloader_name);
    if (payloader == NULL)
        goto no_payloader;

    g_object_set(payloader, "pt", self->pt, NULL);
    self->pt++;

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(payloader),
                                     "buffer-list"))
        g_object_set(payloader, "buffer-list", TRUE, NULL);

    /* add the payloader to the pipeline */
    gst_bin_add(GST_BIN_CAST(inner_bin), payloader);
    gst_element_sync_state_with_parent(payloader);

    /* link the pad to the sinkpad of the payloader */
    sinkpad = gst_element_get_static_pad(payloader, "sink");
    gst_pad_link(pad, sinkpad);
    gst_object_unref(pad);

    /* Add pad probe to handle events */
    gst_pad_add_probe(sinkpad,
                      GST_PAD_PROBE_TYPE_EVENT_UPSTREAM | GST_PAD_PROBE_TYPE_QUERY_UPSTREAM,
                      (GstPadProbeCallback)replay_bin_sink_probe, self, NULL);
    gst_object_unref(sinkpad);

    /* block data for initial segment seeking */
    bin_pad = g_new0(GstReplayBinPad, 1);

    /* Move ownership of pad to this struct */
    bin_pad->srcpad = gst_object_ref(dpad);
    bin_pad->block_id =
        gst_pad_add_probe(dpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
                          (GstPadProbeCallback)replay_bin_src_block, self, NULL);
    g_mutex_lock(&self->lock);
    self->srcpads = g_list_append(self->srcpads, bin_pad);
    g_mutex_unlock(&self->lock);

    /* now expose the srcpad of the payloader as a ghostpad with the same name
     * as the uridecodebin pad name. */
    srcpad = gst_element_get_static_pad(payloader, "src");
    ghostpad = gst_ghost_pad_new(padname, srcpad);
    gst_object_unref(srcpad);
    g_free(padname);

    gst_pad_set_active(ghostpad, TRUE);
    gst_element_add_pad(inner_bin, ghostpad);

    return;

    /* ERRORS */
no_caps:
{
    GST_WARNING("could not get caps from pad");
    g_free(padname);
    gst_object_unref(pad);
    return;
}
no_factory:
{
    GST_DEBUG("no payloader found");
    g_free(padname);
    gst_caps_unref(caps);
    gst_object_unref(pad);
    return;
}
no_payloader:
{
    GST_ERROR("could not create payloader from factory");
    g_free(padname);
    gst_caps_unref(caps);
    gst_object_unref(pad);
    return;
}
}

static void
gst_replay_bin_do_initial_segment_seek(GstElement *element,
                                       GstReplayBin *self)
{
    gboolean ret;
    GstQuery *query;
    gboolean seekable;

    query = gst_query_new_seeking(GST_FORMAT_TIME);
    ret = gst_element_query(element, query);

    if (!ret)
    {
        GST_WARNING_OBJECT(self, "Cannot query seeking");
        gst_query_unref(query);
        goto done;
    }

    gst_query_parse_seeking(query, NULL, &seekable, NULL, NULL);
    gst_query_unref(query);

    if (!seekable)
    {
        GST_WARNING_OBJECT(self, "Source is not seekable");
        ret = FALSE;
        goto done;
    }

    ret = gst_element_seek(element, 1.0, GST_FORMAT_TIME,
                           GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_FLUSH,
                           GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, -1);

    if (!ret)
        GST_WARNING_OBJECT(self, "segment seeking failed");

done:
    /* Unblock all pads then */
    g_mutex_lock(&self->lock);
    if (self->srcpads)
    {
        g_list_free_full(self->srcpads,
                         (GDestroyNotify)gst_replay_bin_pad_unblock_and_free);
        self->srcpads = NULL;
    }
    g_mutex_unlock(&self->lock);

    if (!ret)
    {
        GST_WARNING_OBJECT(self, "Sending eos to all pads");
        gst_element_foreach_src_pad(element,
                                    (GstElementForeachPadFunc)send_eos_foreach_srcpad, NULL);
    }
}

static void
no_more_pads_cb(GstElement *uribin, GstReplayBin *self)
{
    GST_DEBUG_OBJECT(self, "no-more-pads");
    gst_element_no_more_pads(GST_ELEMENT_CAST(self->inner_bin));

    /* Flush seeking from streaming thread might not be good idea.
     * Do this from another (non-streaming) thread */
    gst_element_call_async(GST_ELEMENT_CAST(self->uridecodebin),
                           (GstElementCallAsyncFunc)gst_replay_bin_do_initial_segment_seek,
                           self, NULL);
}

static GstElement *
gst_replay_bin_new(const gchar *uri, gint64 num_loops,
                   GstRTSPMediaFactoryReplay *factory, const gchar *name)
{
    GstReplayBin *self;

    g_return_val_if_fail(uri != NULL, NULL);
    g_return_val_if_fail(GST_IS_RTSP_MEDIA_FACTORY(factory), NULL);

    if (!name)
        name = "GstRelayBin";

    self = GST_REPLAY_BIN(g_object_new(GST_TYPE_REPLAY_BIN,
                                       "name", name, NULL));

    if (!self->uridecodebin)
    {
        gst_object_unref(self);
        return NULL;
    }

    g_object_set(self->uridecodebin, "uri", uri, NULL);
    self->factory = g_object_ref(factory);
    self->num_loops = num_loops;

    return GST_ELEMENT_CAST(self);
}

struct _GstRTSPMediaFactoryReplay
{
    GstRTSPMediaFactory parent;

    gchar *uri;

    GList *demuxers;
    GList *payloaders;
    GList *decoders;

    gint64 num_loops;
};

enum
{
    PROP_0,
    PROP_URI,
    PROP_NUM_LOOPS,
};

#define DEFAULT_NUM_LOOPS (-1)

static void gst_rtsp_media_factory_replay_get_property(GObject *object,
                                                       guint propid, GValue *value, GParamSpec *pspec);
static void gst_rtsp_media_factory_replay_set_property(GObject *object,
                                                       guint propid, const GValue *value, GParamSpec *pspec);
static void gst_rtsp_media_factory_replay_finalize(GObject *object);

static GstElement
    *
    gst_rtsp_media_factory_replay_create_element(GstRTSPMediaFactory *
                                                     factory,
                                                 const GstRTSPUrl *url);

typedef struct
{
    GList *demux;
    GList *payload;
    GList *decode;
} FilterData;

static gboolean
payloader_filter(GstPluginFeature *feature, FilterData *self);

#define gst_rtsp_media_factory_replay_parent_class parent_class
G_DEFINE_TYPE(GstRTSPMediaFactoryReplay,
              gst_rtsp_media_factory_replay, GST_TYPE_RTSP_MEDIA_FACTORY);

static void
gst_rtsp_media_factory_replay_class_init(GstRTSPMediaFactoryReplayClass
                                             *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstRTSPMediaFactoryClass *mf_class = GST_RTSP_MEDIA_FACTORY_CLASS(klass);

    gobject_class->get_property = gst_rtsp_media_factory_replay_get_property;
    gobject_class->set_property = gst_rtsp_media_factory_replay_set_property;
    gobject_class->finalize = gst_rtsp_media_factory_replay_finalize;

    g_object_class_install_property(gobject_class, PROP_URI,
                                    g_param_spec_string("uri", "URI",
                                                        "The URI of the resource to stream", NULL,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_NUM_LOOPS,
                                    g_param_spec_int64("num-loops", "Num Loops",
                                                       "The number of loops (-1 = infinite)", -1, G_MAXINT64,
                                                       DEFAULT_NUM_LOOPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    mf_class->create_element =
        GST_DEBUG_FUNCPTR(gst_rtsp_media_factory_replay_create_element);
}

static void
gst_rtsp_media_factory_replay_init(GstRTSPMediaFactoryReplay *self)
{
    FilterData data = {
        NULL,
    };

    /* get the feature list using the filter */
    gst_registry_feature_filter(gst_registry_get(), (GstPluginFeatureFilter)payloader_filter, FALSE, &data);

    /* sort */
    self->demuxers =
        g_list_sort(data.demux, gst_plugin_feature_rank_compare_func);
    self->payloaders =
        g_list_sort(data.payload, gst_plugin_feature_rank_compare_func);
    self->decoders =
        g_list_sort(data.decode, gst_plugin_feature_rank_compare_func);

    self->num_loops = DEFAULT_NUM_LOOPS;
}

static void
gst_rtsp_media_factory_replay_get_property(GObject *object, guint propid,
                                           GValue *value, GParamSpec *pspec)
{
    GstRTSPMediaFactoryReplay *self = GST_RTSP_MEDIA_FACTORY_REPLAY(object);

    switch (propid)
    {
    case PROP_URI:
        g_value_take_string(value, self->uri);
        break;
    case PROP_NUM_LOOPS:
        g_value_set_int64(value, self->num_loops);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propid, pspec);
    }
}

static void
gst_rtsp_media_factory_replay_set_property(GObject *object, guint propid,
                                           const GValue *value, GParamSpec *pspec)
{
    GstRTSPMediaFactoryReplay *self = GST_RTSP_MEDIA_FACTORY_REPLAY(object);

    switch (propid)
    {
    case PROP_URI:
        g_free(self->uri);
        self->uri = g_value_dup_string(value);
        break;
    case PROP_NUM_LOOPS:
        self->num_loops = g_value_get_int64(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propid, pspec);
    }
}

static void
gst_rtsp_media_factory_replay_finalize(GObject *object)
{
    GstRTSPMediaFactoryReplay *self = GST_RTSP_MEDIA_FACTORY_REPLAY(object);

    g_free(self->uri);

    gst_plugin_feature_list_free(self->demuxers);
    gst_plugin_feature_list_free(self->payloaders);
    gst_plugin_feature_list_free(self->decoders);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static GstElement *
gst_rtsp_media_factory_replay_create_element(GstRTSPMediaFactory *factory,
                                             const GstRTSPUrl *url)
{
    GstRTSPMediaFactoryReplay *self = GST_RTSP_MEDIA_FACTORY_REPLAY(factory);

    return gst_replay_bin_new(self->uri, self->num_loops, self,
                              "GstRTSPMediaFactoryReplay");
}

static gboolean
payloader_filter(GstPluginFeature *feature, FilterData *data)
{
    const gchar *klass;
    GstElementFactory *fact;
    GList **list = NULL;

    /* we only care about element factories */
    if (G_UNLIKELY(!GST_IS_ELEMENT_FACTORY(feature)))
        return FALSE;

    if (gst_plugin_feature_get_rank(feature) < GST_RANK_MARGINAL)
        return FALSE;

    fact = GST_ELEMENT_FACTORY_CAST(feature);

    klass = gst_element_factory_get_metadata(fact, GST_ELEMENT_METADATA_KLASS);

    if (strstr(klass, "Decoder"))
        list = &data->decode;
    else if (strstr(klass, "Demux"))
        list = &data->demux;
    else if (strstr(klass, "Parser") && strstr(klass, "Codec"))
        list = &data->demux;
    else if (strstr(klass, "Payloader") && strstr(klass, "RTP"))
        list = &data->payload;

    if (list)
    {
        GST_LOG("adding %s", GST_OBJECT_NAME(fact));
        *list = g_list_prepend(*list, gst_object_ref(fact));
    }

    return FALSE;
}

static GList *
gst_rtsp_media_factory_replay_get_demuxers(GstRTSPMediaFactoryReplay *factory)
{
    return factory->demuxers;
}

static GList *
gst_rtsp_media_factory_replay_get_payloaders(GstRTSPMediaFactoryReplay *
                                                 factory)
{
    return factory->payloaders;
}

static GList *
gst_rtsp_media_factory_replay_get_decoders(GstRTSPMediaFactoryReplay *factory)
{
    return factory->decoders;
}

static GstRTSPMediaFactory *
gst_rtsp_media_factory_replay_new(const gchar *uri, gint64 num_loops)
{
    GstRTSPMediaFactory *factory;

    factory =
        GST_RTSP_MEDIA_FACTORY(g_object_new(GST_TYPE_RTSP_MEDIA_FACTORY_REPLAY, "uri", uri, "num-loops", num_loops,
                                            NULL));

    return factory;
}

static gboolean hls_server_device(char *file_name, char *file_url, char *file_path)
{

    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("\nStarting the hls streaming\n");

    gchar *gst_str1, *gst_str2, *gst_str3;

    gst_str1 = g_strdup_printf("rtspsrc location=%s ! queue ! rtph265depay ! h265parse ! mpegtsmux ! hlssink playlist-location=%s/%s.m3u8 location=%s", file_url, file_path, file_name, file_path);

    gst_str2 = "/segment.%05d.ts target-duration=15  max-files=15 playlist-length=30";

    gst_str3 = (char *)malloc(1 + strlen(gst_str1) + strlen(gst_str2));

    strcpy(gst_str3, gst_str1);

    strcat(gst_str3, gst_str2);

    /* Build the pipeline */
    pipe1 = gst_parse_launch(gst_str3, &error);

    /* Start playing */
    gst_element_set_state(pipe1, GST_STATE_PLAYING);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    g_free(gst_str3);
    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
    }
    return FALSE;
}

static gboolean hls_server_mp4(char *file_name, char *file_url, char *file_path)
{

    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("\nStarting the hls streaming\n");

    gchar *gst_str1, *gst_str2, *gst_str3;

    gst_str1 = g_strdup_printf("rtspsrc location=%s ! rtph264depay ! avdec_h264 ! clockoverlay ! videoconvert ! videoscale ! video/x-raw,width=1080, height=1080 ! x264enc bitrate=512 ! video/x-h264,profile=\"high\" ! hlssink2 playlist-location=%s/%s.m3u8 location=%s", file_url, file_path, file_name, file_path);

    gst_str2 = "/segment.%05d.ts target-duration=15 max-files=30 playlist-length=30";

    gst_str3 = (char *)malloc(1 + strlen(gst_str1) + strlen(gst_str2));

    strcpy(gst_str3, gst_str1);

    strcat(gst_str3, gst_str2);

    /* Build the pipeline */
    pipe1 = gst_parse_launch(gst_str3, &error);

    /* Start playing */
    gst_element_set_state(pipe1, GST_STATE_PLAYING);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }

    g_free(gst_str3);
    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
    }
    return FALSE;
}

static GstFlowReturn new_sample(GstAppSink *sink, gpointer user_data)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo info;
    GstClockTime timestamp;
    struct json_object *jobj, *video_id, *jobj1;
    const gchar *id, *jstr, *json_str;
    gchar *nats_str;

    json_str = user_data;

    jobj = json_tokener_parse(json_str);
    video_id = find_something(jobj, "device_id");

    id = json_object_to_json_string(video_id);

    gchar *subj1 = g_strdup_printf("stream.%s.frame", id);

    printf("Sending messages to subject '%s'\n", subj1);

    /* Retrieve the sample */
    sample = gst_app_sink_pull_sample(sink);

    if (sample)
    {
        buffer = gst_sample_get_buffer(sample);
        timestamp = GST_BUFFER_PTS(buffer);

        if (gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ)))
        {
            jobj1 = json_tokener_parse(json_str);

            json_object_object_add(jobj1, "frame_in_bytes", json_object_new_string(info.data));

            json_object_object_add(jobj1, "frame_timestamp", json_object_new_double(timestamp));

            jstr = json_object_to_json_string_ext(jobj1, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);

            nats_str = malloc(strlen(jstr) + 1);

            strcpy(nats_str, jstr);

            for (count = 0; (s == NATS_OK) && (count < 1); count++)
            {
                s = js_PublishAsync(js, subj1, (const void *)(nats_str), info.size, NULL);
            }

            if (s == NATS_OK)
            {
                jsPubOptions jsPubOpts;

                jsPubOptions_Init(&jsPubOpts);
                // Let's set it to 30 seconds, if getting "Timeout" errors,
                // this may need to be increased based on the number of messages
                // being sent.
                jsPubOpts.MaxWait = 1000000000;
                s = js_PublishAsyncComplete(js, &jsPubOpts);
            }

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
        }

        g_free(jobj1);
        g_free(nats_str);
        gst_buffer_unmap(buffer, &info);
        gst_sample_unref(sample);

        return GST_FLOW_OK;
    }
    g_free(video_id);
    g_free(jobj);
    return GST_FLOW_ERROR;
}

static gboolean add_device(char *id, char *url, char *json_str)
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("Starting the camera streaming pipeline for %s\n", id);

    gchar *gst_str, *sink_name;

    sink_name = g_strdup_printf("sink-%s", id);

    gst_str = g_strdup_printf("rtspsrc location=%s latency=40 is-live=true name=%s ntp-time-source=3 buffer-mode=4 ntp-sync=TRUE ! rtph265depay name=depay-%s ! h265parse name=parse-%s ! decodebin name=decode-%s ! videoconvert name=convert-%s ! videoscale name=scale-%s ! video/x-raw, format=GRAY8, width = 1024, height = 1024 ! appsink name=%s", url, id, id, id, id, id, id, sink_name);

    pipe1 = gst_parse_launch(gst_str, &error);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }
    else
    {
        g_print("The pipeline launched successfully\n");
    }

    /*checking whether the appsink is null*/
    app_sink = gst_bin_get_by_name(GST_BIN(pipe1), sink_name);
    g_assert_nonnull(app_sink);

    /*callback function to fetch the frames from sample*/
    g_object_set(app_sink, "emit-signals", TRUE, NULL);
    g_signal_connect(app_sink, "new-sample", G_CALLBACK(new_sample), json_str);

    /*playing the pipeline*/
    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }

    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
    }
    return FALSE;
}

static gboolean add_mp4(char *id, char *location, char *json_str)
{

    GstStateChangeReturn ret;
    GError *error = NULL;

    g_print("Starting the mp4 streaming pipeline for %s\n", id);

    gchar *gst_str, *sink_name;

    sink_name = g_strdup_printf("sink-%s", id);

    /* gstreamer pipeline for fetching the frames from appsink*/
    gst_str = g_strdup_printf("filesrc location=%s ! decodebin name=decode-%s ! queue ! videoconvert name=convert-%s ! videoscale name=scale-%s ! videorate max-rate=30 ! video/x-raw, framerate=30/1, format=GRAY8, width = 1024, height = 1024 ! queue ! appsink name=%s", location, id, id, id, sink_name);

    /*launching the pieline*/
    pipe1 = gst_parse_launch(gst_str, &error);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }
    else
    {
        g_print("The pipeline launched successfully\n");
    }

    /*checking whether the appsink is null*/
    app_sink = gst_bin_get_by_name(GST_BIN(pipe1), sink_name);
    g_assert_nonnull(app_sink);

    /*callback function to fetch the frames from sample*/
    g_object_set(app_sink, "emit-signals", TRUE, NULL);
    g_signal_connect(app_sink, "new-sample", G_CALLBACK(new_sample), json_str);

    /*playing the pipeline*/
    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }

    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
    }
    return FALSE;
}

static gboolean camera_server(char *endpt, char *device_url, char *device_id, struct json_object *json_obj)
{
    gchar *server_str_1, *file_path_1, *hls_str_1;
    const char *json_str_1;
    char *new_json_str_1;

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

    server_str_1 = g_strdup_printf("rtsp://127.0.0.1:%s/%s", PORT, endpt);

    hls_str_1 = g_strdup_printf("http://127.0.0.1:%s/%s/%s.m3u8", PORT1, endpt, device_id);

    json_object_object_add(json_obj, "rtsp_url", json_object_new_string(server_str_1));

    json_object_object_add(json_obj, "hls_url", json_object_new_string(hls_str_1));

    json_object_object_del(json_obj, "stream_endpt");

    /* converting object to string*/
    json_str_1 = json_object_to_json_string(json_obj);

    new_json_str_1 = malloc(strlen(json_str_1) + 1);

    strcpy(new_json_str_1, json_str_1);

    if (!add_device(device_id, device_url, new_json_str_1))
    {
        g_printerr("Cannot add stream to Jetstream!\n");
    }

    file_path_1 = g_strdup_printf("/app/streams/%s", device_id);
    mkdir(file_path_1, 0777);

    if (!hls_server_device(device_id, server_str_1, file_path_1))
    {
        g_printerr("Cannot add stream to HLS Server!\n");
    }

    g_free(gst_string);

    return TRUE;
}

static gboolean video_server(char *endpt, char *device_url, char *device_id, struct json_object *json_obj)
{
    gchar *server_str, *file_path, *hls_str;
    const char *json_str;
    char *new_json_str;

    GST_DEBUG_CATEGORY_INIT(replay_server_debug, "replay-server", 0,
                            "RTSP replay server");

    g_print("Starting RTSP Server Streaming for device id: %s\n", device_id);

    uri = gst_filename_to_uri(device_url, NULL);

    /* get the mount points for this server, every server has a default object
     * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points(server);

    factory = gst_rtsp_media_factory_replay_new(uri, num_loops);
    g_free(uri);

    gst_rtsp_media_factory_set_shared(factory, TRUE);

    /* attach the test factory to the /test url */
    gst_rtsp_mount_points_add_factory(mounts, endpt, factory);

    /* don't need the ref to the mapper anymore */
    g_object_unref(mounts);

    /* attach the server to the default maincontext */
    gst_rtsp_server_attach(server, NULL);

    /* start serving */
    g_print("stream ready at rtsp://127.0.0.1:%s/%s\n", PORT, endpt);

    server_str = g_strdup_printf("rtsp://127.0.0.1:%s/%s", PORT, endpt);

    hls_str = g_strdup_printf("http://127.0.0.1:%s/%s/%s.m3u8", PORT1, endpt, device_id);

    /* adding the stream url to json object*/
    json_object_object_add(json_obj, "rtsp_url", json_object_new_string(server_str));

    json_object_object_add(json_obj, "hls_url", json_object_new_string(hls_str));

    json_object_object_del(json_obj, "device_url");

    json_object_object_del(json_obj, "stream_endpt");

    /* converting object to string*/
    json_str = json_object_to_json_string(json_obj);

    new_json_str = malloc(strlen(json_str) + 1);

    strcpy(new_json_str, json_str);

    g_print("FINAL JSON: %s", new_json_str);

    /*adding the stream to nats publishing*/
    if (!add_mp4(device_id, device_url, new_json_str))
    {
        g_printerr("Cannot add stream to Jetstream!\n");
    }

    /*create path for storing hls streams*/
    file_path = g_strdup_printf("/app/streams/%s", endpt);
    mkdir(file_path, 0777);

    /*add the stream to HLS server*/
    if (!hls_server_mp4(device_id, server_str, file_path))
    {
        g_printerr("Cannot add stream to HLS Server!\n");
    }

    return TRUE;
}

static gboolean start_pipeline()
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    pipe1 = gst_parse_launch("fakesrc ! queue ! fakesink", &error);

    if (error)
    {
        g_printerr("Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        goto err;
    }

    g_print("Starting pipeline, not transmitting yet\n");

    ret = gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    return TRUE;

err:
    g_print("State change failure\n");
    if (pipe1)
    {
        gst_object_unref(pipe1);
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

static void
onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
{
    printf("Received msg: %s - %.*s\n",
           natsMsg_GetSubject(msg),
           natsMsg_GetDataLength(msg),
           natsMsg_GetData(msg));

    struct json_object *jobj, *video_id, *video_path, *stream_endpt, *video_type;
    const char *id, *path, *endpt, *type;
    char *new_id, *new_path, *new_endpt, *new_type;

    /* Tokenizing the json string */
    jobj = json_tokener_parse(natsMsg_GetData(msg));
    /* Fetching the respective field's value*/
    video_id = find_something(jobj, "device_id");
    video_path = find_something(jobj, "device_url");
    stream_endpt = find_something(jobj, "stream_endpt");
    video_type = find_something(jobj, "type");

    /*converting the value to string */
    id = json_object_to_json_string_ext(video_id, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
    path = json_object_to_json_string_ext(video_path, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
    endpt = json_object_to_json_string_ext(stream_endpt, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
    type = json_object_to_json_string_ext(video_type, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);

    /*convert const char* to char* */
    new_id = malloc(strlen(id) + 1);
    new_path = malloc(strlen(path) + 1);
    new_endpt = malloc(strlen(endpt) + 1);
    new_type = malloc(strlen(type) + 1);

    strcpy(new_id, id);
    strcpy(new_path, path);
    strcpy(new_endpt, endpt);
    strcpy(new_type, type);

    new_id = removeChar(new_id, '"');
    new_path = removeChar(new_path, '\\');
    new_path = removeChar(new_path, '"');
    new_endpt = removeChar(new_endpt, '\\');
    new_endpt = removeChar(new_endpt, '"');
    new_type = removeChar(new_type, '"');

    /* fetching the type of operation need to be performed*/
    char *natsSubject = strchr(natsMsg_GetSubject(msg), '.');

    char natsSubj[50];

    strcpy(natsSubj, natsSubject);

    switch (natsSubj[1])
    {
    case 'a':
        printf("Adding the device stream\n");
        if (strcmp(new_type, "video") == 0)
        {
            /*adding the stream to RTSP mp4 server*/
            if (!video_server(new_endpt, new_path, new_id, jobj))
            {
                g_printerr("Cannot add the mp4 stream to RTSP Server\n");
            }
        }
        if (strcmp(new_type, "stream") == 0)
        {
            /*adding the stream to RTSP camera server*/
            if (!camera_server(new_endpt, new_path, new_id, jobj))
            {
                g_printerr("Cannot add the mp4 stream to RTSP Server\n");
            }
        }
        break;
    case 'r':
        printf("Removing the device stream\n");
        break;
    case 'u':
        printf("Updating the device stream\n");
        break;
    default:
        break;
    }

    free(jobj);
    free(video_id);
    // free(video_path);
    // free(stream_endpt);
    free(video_type);

    // // Need to destroy the message!
    natsMsg_Destroy(msg);
}

int main(int argc, gchar *argv[])
{
    /* Initializing the gstreamer */
    gst_init(&argc, &argv);

    struct json_object *init_json1, *init_json2;

    init_json1 = json_object_new_object();
    init_json2 = json_object_new_object();

    /* Initializing the main loop */
    loop = g_main_loop_new(NULL, FALSE);

    /* Starting the pipeline with fakesrc and sink */
    if (!start_pipeline())
    {
        g_printerr("ERROR: Failed to start pipeline");
        gst_object_unref(pipe1);
        return -1;
    }

    /* Adding bus to the pipeline */
    bus = gst_element_get_bus(pipe1);
    gst_bus_add_signal_watch(bus);
    g_signal_connect(bus, "message", (GCallback)cb_message, pipe1);

    /* create a RTSP server instance */
    server = gst_rtsp_server_new();

    /* assigning port */
    g_object_set(server, "service", PORT, NULL);

    // Creates a connection to the NATS URL
    s = natsConnection_ConnectTo(&conn, nats_url);
    printf("Connected to Nats\n");

    if (s == NATS_OK)
        s = jsOptions_Init(&jsOpts);

    if (s == NATS_OK)
    {
        jsOpts.PublishAsync.ErrHandler = _jsPubErr;
        jsOpts.PublishAsync.ErrHandlerClosure = (void *)&errors;
        s = natsConnection_JetStream(&js, conn, &jsOpts);
        printf("Connected to Jetstream\n");
    }

    if (s == NATS_OK)
    {
        jsStreamInfo *si = NULL;

        // First check if the stream already exists.
        s = js_GetStreamInfo(&si, js, stream1, NULL, &jerr);
        if (s == NATS_NOT_FOUND)
        {
            jsStreamConfig cfg;

            // Initialize the configuration structure.
            jsStreamConfig_Init(&cfg);
            cfg.Name = stream1;
            // Set the subject
            cfg.Subjects = (const char *[1]){subject1};
            cfg.SubjectsLen = 1;
            // Make it a memory stream.
            cfg.Storage = js_MemoryStorage;
            // Add the stream,
            s = js_AddStream(&si, js, &cfg, NULL, &jerr);
            printf("The stream is added\n");
        }

        if (s == NATS_OK)
        {
            printf("Stream %s has %" PRIu64 " messages (%" PRIu64 " bytes)\n",
                   si->Config->Name, si->State.Msgs, si->State.Bytes);

            // Need to destroy the returned stream object.
            jsStreamInfo_Destroy(si);
        }
    }

    if (s == NATS_OK)
    {
        s = natsStatistics_Create(&stats);
        printf("Stats are created\n");
    }

    if (s == NATS_OK)
    {
        start = nats_Now();
    }

    // printf("Listening on queue group %s\n", queueGroup);

    // if (s == NATS_OK)
    // {
    //     s = natsConnection_QueueSubscribe(&sub, conn, subject, queueGroup, onMsg, (void *)&done);
    // }
    for (int i = 4; i <= 8; i++)
    {
        char *new_path = g_strdup_printf("/home/avishek/sample_videos/mp4_videos/%d.mp4", i);
        char *new_id = g_strdup_printf("%d", i);
        char *new_endpt = g_strdup_printf("/video%d", i);

        json_object_object_add(init_json1, "device_id", json_object_new_string(new_id));
        json_object_object_add(init_json1, "device_url", json_object_new_string(new_path));
        json_object_object_add(init_json1, "type", json_object_new_string("video"));
        json_object_object_add(init_json2, "latitude", json_object_new_string("11.342423"));
        json_object_object_add(init_json2, "longitude", json_object_new_string("77.728165"));
        const char *jstr2 = json_object_to_json_string_ext(init_json2, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY);
        json_object_object_add(init_json1, "geo-location", json_object_new_string(jstr2));

        if (!video_server(new_endpt, new_path, new_id, init_json1))
        {
            g_printerr("Cannot add the mp4 stream to RTSP Server\n");
        }
        sleep(5);
    }

    /* starting the loop */
    g_main_loop_run(loop);

    /* on any error */
    gst_element_set_state(GST_ELEMENT(pipe1), GST_STATE_NULL);
    g_print("Pipeline has been stopped\n");

    // If there was an error, print a stack trace and exit
    if (s != NATS_OK)
    {
        nats_PrintLastErrorStack(stderr);
        exit(2);
    }

    // Destroy all our objects to avoid report of memory leak
    jsCtx_Destroy(js);
    natsConnection_Destroy(conn);
    natsSubscription_Destroy(sub);
    g_main_loop_unref(loop);

    // To silence reports of memory still in used with valgrind
    nats_Close();

    gst_object_unref(pipe1);
    gst_message_unref(msg);
    gst_object_unref(bus);

    return 0;
}