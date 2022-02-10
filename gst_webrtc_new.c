// gcc gst_webrtc_sol.c -o gst_webrtc_sol `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libnats gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0`

#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include "/home/nivetheni/nats.c/examples/examples.h"
#include <gst/app/gstappsink.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

/* For signalling */
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include <string.h>

enum AppState
{
  APP_STATE_UNKNOWN = 0,
  APP_STATE_ERROR = 1,          /* generic error */
  SERVER_CONNECTING = 1000,
  SERVER_CONNECTION_ERROR,
  SERVER_CONNECTED,             /* Ready to register */
  SERVER_REGISTERING = 2000,
  SERVER_REGISTRATION_ERROR,
  SERVER_REGISTERED,            /* Ready to call a peer */
  SERVER_CLOSED,                /* server connection closed by us or the server */
  PEER_CONNECTING = 3000,
  PEER_CONNECTION_ERROR,
  PEER_CONNECTED,
  PEER_CALL_NEGOTIATING = 4000,
  PEER_CALL_STARTED,
  PEER_CALL_STOPPING,
  PEER_CALL_STOPPED,
  PEER_CALL_ERROR,
};

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData
{
  gchar *id;
  gchar *type;
  gchar *url;
  gchar *peerid; 
} CustomData;

static GMainLoop *loop;
static GstBus *bus;
static GstMessage *msg;
static GstElement *pipe1, *webrtc1[50], *app_sink[50];
static GObject *send_channel, *receive_channel;

static SoupWebsocketConnection *ws_conn[50];
static enum AppState app_state[50];
static const gchar *peer_id = "3322";
static const gchar *server_url = "wss://webrtc.nirbheek.in:8443";
static gboolean disable_ssl = FALSE;
static gboolean remote_is_offerer = FALSE;

static GOptionEntry entries[] = {
  {"peer-id", 0, 0, G_OPTION_ARG_STRING, &peer_id,
      "String ID of the peer to connect to", "ID"},
  {"server", 0, 0, G_OPTION_ARG_STRING, &server_url,
      "Signalling server to connect to", "URL"},
  {"disable-ssl", 0, 0, G_OPTION_ARG_NONE, &disable_ssl, "Disable ssl", NULL},
  {"remote-offerer", 0, 0, G_OPTION_ARG_NONE, &remote_is_offerer,
      "Request that the peer generate the offer and we'll answer", NULL},
  {NULL},
};

static gboolean connect_to_websocket_server_async (CustomData *data);
static void on_offer_received (GstSDPMessage *sdp, CustomData *data);

static gboolean
cleanup_and_quit_loop (const gchar * msg, enum AppState state, CustomData *data)
{
  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  if (msg)
    g_printerr ("%s\n", msg);
  if (state > 0)
    app_state[integer] = state;

  if (ws_conn[integer]) {
    if (soup_websocket_connection_get_state (ws_conn[integer]) ==
        SOUP_WEBSOCKET_STATE_OPEN)
      /* This will call us again */
      soup_websocket_connection_close (ws_conn[integer], 1000, "");
    else
      g_object_unref (ws_conn[integer]);
  }

  if (loop) {
    g_main_loop_quit (loop);
    loop = NULL;
  }

  /* To allow usage as a GSourceFunc */
  return G_SOURCE_REMOVE;
}

static gchar *
get_string_from_json_object (JsonObject * object)
{
  JsonNode *root;
  JsonGenerator *generator;
  gchar *text;

  /* Make it the root node */
  root = json_node_init_object (json_node_alloc (), object);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  text = json_generator_to_data (generator, NULL);

  /* Release everything */
  g_object_unref (generator);
  json_node_free (root);
  return text;
}

static void
handle_media_stream (GstPad * pad, GstElement * pipe, const char *convert_name,
    const char *sink_name)
{
  GstPad *qpad;
  GstElement *q, *conv, *resample, *sink;
  GstPadLinkReturn ret;

  g_print ("Trying to handle stream with %s ! %s", convert_name, sink_name);

  q = gst_element_factory_make ("queue", NULL);
  g_assert_nonnull (q);
  conv = gst_element_factory_make (convert_name, NULL);
  g_assert_nonnull (conv);
  sink = gst_element_factory_make (sink_name, NULL);
  g_assert_nonnull (sink);

  if (g_strcmp0 (convert_name, "audioconvert") == 0) {
    /* Might also need to resample, so add it just in case.
     * Will be a no-op if it's not required. */
    resample = gst_element_factory_make ("audioresample", NULL);
    g_assert_nonnull (resample);
    gst_bin_add_many (GST_BIN (pipe), q, conv, resample, sink, NULL);
    gst_element_sync_state_with_parent (q);
    gst_element_sync_state_with_parent (conv);
    gst_element_sync_state_with_parent (resample);
    gst_element_sync_state_with_parent (sink);
    gst_element_link_many (q, conv, resample, sink, NULL);
  } else {
    gst_bin_add_many (GST_BIN (pipe), q, conv, sink, NULL);
    gst_element_sync_state_with_parent (q);
    gst_element_sync_state_with_parent (conv);
    gst_element_sync_state_with_parent (sink);
    gst_element_link_many (q, conv, sink, NULL);
  }

  qpad = gst_element_get_static_pad (q, "sink");

  ret = gst_pad_link (pad, qpad);
  g_assert_cmphex (ret, ==, GST_PAD_LINK_OK);
}

static void
on_incoming_decodebin_stream (GstElement * decodebin, GstPad * pad,
    GstElement * pipe)
{
  GstCaps *caps;
  const gchar *name;

  if (!gst_pad_has_current_caps (pad)) {
    g_printerr ("Pad '%s' has no caps, can't do anything, ignoring\n",
        GST_PAD_NAME (pad));
    return;
  }

  caps = gst_pad_get_current_caps (pad);
  name = gst_structure_get_name (gst_caps_get_structure (caps, 0));

  if (g_str_has_prefix (name, "video")) {
    handle_media_stream (pad, pipe, "videoconvert", "autovideosink");
  } else if (g_str_has_prefix (name, "audio")) {
    handle_media_stream (pad, pipe, "audioconvert", "autoaudiosink");
  } else {
    g_printerr ("Unknown pad %s, ignoring", GST_PAD_NAME (pad));
  }
}

static void
on_incoming_stream (GstElement * webrtc, GstPad * pad, GstElement * pipe)
{
  GstElement *decodebin;
  GstPad *sinkpad;

  g_print("On-incoming-stream\n");

  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
    return;

  decodebin = gst_element_factory_make ("decodebin", NULL);
  g_signal_connect (decodebin, "pad-added",
      G_CALLBACK (on_incoming_decodebin_stream), pipe);
  gst_bin_add (GST_BIN (pipe), decodebin);
  gst_element_sync_state_with_parent (decodebin);

  sinkpad = gst_element_get_static_pad (decodebin, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);
}

static void
send_ice_candidate_message (GstElement * webrtc G_GNUC_UNUSED, guint mlineindex,
    gchar * candidate, CustomData *data)
{
  gchar *text;
  JsonObject *ice, *msg;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  if (app_state[integer] < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop ("Can't send ICE, not in call", APP_STATE_ERROR, data);
    return;
  }

  ice = json_object_new ();
  json_object_set_string_member (ice, "candidate", candidate);
  json_object_set_int_member (ice, "sdpMLineIndex", mlineindex);
  msg = json_object_new ();
  json_object_set_object_member (msg, "ice", ice);
  text = get_string_from_json_object (msg);
  json_object_unref (msg);

  soup_websocket_connection_send_text (ws_conn[integer], text);
  g_free (text);
}

static void
send_sdp_to_peer (GstWebRTCSessionDescription * desc, CustomData *data)
{
  gchar *text;
  JsonObject *msg, *sdp;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  if (app_state[integer] < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop ("Can't send SDP to peer, not in call",
        APP_STATE_ERROR, data);
    return;
  }

  text = gst_sdp_message_as_text (desc->sdp);
  sdp = json_object_new ();

  if (desc->type == GST_WEBRTC_SDP_TYPE_OFFER) {
    g_print ("Sending offer:\n%s\n", text);
    json_object_set_string_member (sdp, "type", "offer");
  } else if (desc->type == GST_WEBRTC_SDP_TYPE_ANSWER) {
    g_print ("Sending answer:\n%s\n", text);
    json_object_set_string_member (sdp, "type", "answer");
  } else {
    g_assert_not_reached ();
  }

  json_object_set_string_member (sdp, "sdp", text);
  g_free (text);

  msg = json_object_new ();
  json_object_set_object_member (msg, "sdp", sdp);
  text = get_string_from_json_object (msg);
  json_object_unref (msg);

  soup_websocket_connection_send_text (ws_conn[integer], text);
  g_free (text);
}

/* Offer created by our pipeline, to be sent to the peer */
static void
on_offer_created (GstPromise * promise, gpointer user_data)
{
  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;

  CustomData *data = user_data;
  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);


  g_print("On-offer-created\n");

  g_assert_cmphex (app_state[integer], ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (webrtc1[integer], "set-local-description", offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  send_sdp_to_peer (offer, data);
  gst_webrtc_session_description_free (offer);
}

static void
on_negotiation_needed (GstElement * element, CustomData *data)
{
  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  app_state[integer] = PEER_CALL_NEGOTIATING;

  g_print("On-negotiation-needed\n");

  if (remote_is_offerer) {
    gchar *msg = g_strdup_printf ("OFFER_REQUEST");
    soup_websocket_connection_send_text (ws_conn[integer], msg);
    g_free (msg);
  } else {
    GstPromise *promise;
    promise =
        gst_promise_new_with_change_func (on_offer_created, data, NULL);;
    g_signal_emit_by_name (webrtc1[integer], "create-offer", NULL, promise);
  }
}

#define STUN_SERVER " stun-server=stun://stun.l.google.com:19302 "
#define RTP_CAPS_OPUS "application/x-rtp,media=audio,encoding-name=OPUS,payload="
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="

static void
data_channel_on_error (GObject * dc, CustomData *data)
{
  cleanup_and_quit_loop ("Data channel error", 0, data);
}

static void
data_channel_on_open (GObject * dc, CustomData *data)
{
  GBytes *bytes = g_bytes_new ("data", strlen ("data"));
  g_print ("data channel opened\n");
  g_signal_emit_by_name (dc, "send-string", "Hi! from GStreamer");
  g_signal_emit_by_name (dc, "send-data", bytes);
  g_bytes_unref (bytes);
}

static void
data_channel_on_close (GObject * dc, CustomData *data)
{
  cleanup_and_quit_loop ("Data channel closed", 0, data);
}

static void
data_channel_on_message_string (GObject * dc, gchar * str, CustomData *data)
{
  g_print ("Received data channel message: %s\n", str);
}

static void
connect_data_channel_signals (GObject * data_channel, CustomData *data)
{
  g_signal_connect (data_channel, "on-error",
      G_CALLBACK (data_channel_on_error), data);
  g_signal_connect (data_channel, "on-open", G_CALLBACK (data_channel_on_open),
      NULL);
  g_signal_connect (data_channel, "on-close",
      G_CALLBACK (data_channel_on_close), data);
  g_signal_connect (data_channel, "on-message-string",
      G_CALLBACK (data_channel_on_message_string), data);
}

static void
on_data_channel (GstElement * webrtc, GObject * data_channel,
    CustomData *data)
{
  connect_data_channel_signals (data_channel, data);
  receive_channel = data_channel;
}

static void
on_ice_gathering_state_notify (GstElement * webrtcbin, GParamSpec * pspec,
    gpointer user_data)
{
  GstWebRTCICEGatheringState ice_gather_state;
  const gchar *new_state = "unknown";

  g_object_get (webrtcbin, "ice-gathering-state", &ice_gather_state, NULL);
  switch (ice_gather_state) {
    case GST_WEBRTC_ICE_GATHERING_STATE_NEW:
      new_state = "new";
      break;
    case GST_WEBRTC_ICE_GATHERING_STATE_GATHERING:
      new_state = "gathering";
      break;
    case GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE:
      new_state = "complete";
      break;
  }
  g_print ("ICE gathering state changed to %s\n", new_state);
}

static gboolean
start_pipeline (CustomData *data)
{
  GstStateChangeReturn ret;
  GError *error = NULL;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  gchar *webrtc_tmp, *gst_string;

  webrtc_tmp = g_strdup_printf ("sendrecv-%s", data->id);

  gst_string = g_strdup_printf("webrtcbin bundle-policy=max-bundle name=%s stun-server=stun://stun.l.google.com:19302 rtspsrc location=%s latency=0 ! rtph265depay ! h265parse ! decodebin ! videoconvert ! queue ! vp8enc deadline=1 ! rtpvp8pay ! queue ! application/x-rtp,media=video,encoding-name=VP8,payload= 96 ! %s.", webrtc_tmp, data->url, webrtc_tmp);
  
  pipe1 = gst_parse_launch (gst_string, &error);
  
  if (error) {
      g_printerr ("Failed to parse launch: %s\n", error->message);
      g_error_free (error);
      goto err;
  }

  webrtc1[integer] = gst_bin_get_by_name (GST_BIN (pipe1), webrtc_tmp);
  g_assert_nonnull (webrtc1[integer]);

  /* This is the gstwebrtc entry point where we create the offer and so on. It
   * will be called when the pipeline goes to PLAYING. */
  g_signal_connect (webrtc1[integer], "on-negotiation-needed",
      G_CALLBACK (on_negotiation_needed), data);
  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */
  g_signal_connect (webrtc1[integer], "on-ice-candidate",
      G_CALLBACK (send_ice_candidate_message), data);
  g_signal_connect (webrtc1[integer], "notify::ice-gathering-state",
      G_CALLBACK (on_ice_gathering_state_notify), data);

  gst_element_set_state (pipe1, GST_STATE_READY);

  g_signal_emit_by_name (webrtc1[integer], "create-data-channel", "channel", NULL,
      &send_channel);
  if (send_channel) {
    g_print ("Created data channel\n");
    connect_data_channel_signals (send_channel, data);
  } else {
    g_print ("Could not create data channel, is usrsctp available?\n");
  }

  g_signal_connect (webrtc1[integer], "on-data-channel", G_CALLBACK (on_data_channel),
      data);
  /* Incoming streams will be exposed via this signal */
  g_signal_connect (webrtc1[integer], "pad-added", G_CALLBACK (on_incoming_stream),
      pipe1);

  /* Lifetime is the same as the pipeline itself */
  gst_object_unref (webrtc1[integer]);

  g_print ("Starting pipeline\n");
  ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto err;

  g_free(gst_string);
  g_free(webrtc_tmp);

  return TRUE;

err:
  if (pipe1)
    g_clear_object (&pipe1);
  if (webrtc1[integer])
    webrtc1[integer] = NULL;
  return FALSE;
}

static gboolean
register_with_server (CustomData *data)
{
  gchar *hello;
  gchar *our_id;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  if (soup_websocket_connection_get_state (ws_conn[integer]) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  our_id = data->peerid;
  g_print ("**************************************************************Registering id %s with server***************************************************************\n", our_id);
  app_state[integer] = SERVER_REGISTERING;

  /* Register with the server with a random integer id. Reply will be received
   * by on_server_message() */
  hello = g_strdup_printf ("HELLO %s", our_id);
  soup_websocket_connection_send_text (ws_conn[integer], hello);
  g_free (hello);

  return TRUE;
}

static void
on_server_closed (SoupWebsocketConnection * conn G_GNUC_UNUSED,
    CustomData *data)
{
  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  app_state[integer] = SERVER_CLOSED;

  g_print("*****************************************Server closed*******************************************************\n");

  connect_to_websocket_server_async (data);

//   cleanup_and_quit_loop ("Server connection closed", 0);
}

/* Answer created by our pipeline, to be sent to the peer */
static void
on_answer_created (GstPromise * promise, gpointer user_data)
{
  GstWebRTCSessionDescription *answer = NULL;
  const GstStructure *reply;

  CustomData *data = user_data;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  g_print("On-answer-created\n");
  g_assert_cmphex (app_state[integer], ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "answer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (webrtc1[integer], "set-local-description", answer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send answer to peer */
  send_sdp_to_peer (answer, data);
  gst_webrtc_session_description_free (answer);
}

static void
on_offer_set (GstPromise * promise, gpointer user_data)
{
  CustomData *data = user_data;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  g_print("On-offer-set\n");
  gst_promise_unref (promise);
  promise = gst_promise_new_with_change_func (on_answer_created, NULL, NULL);
  g_signal_emit_by_name (webrtc1[integer], "create-answer", NULL, promise);
}

static void
on_offer_received (GstSDPMessage *sdp, CustomData *data)
{

  g_print("On-offer-received\n");
  GstWebRTCSessionDescription *offer = NULL;
  GstPromise *promise;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  offer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_OFFER, sdp);
  g_assert_nonnull (offer);

  /* Set remote description on our pipeline */
  {
    promise = gst_promise_new_with_change_func (on_offer_set, NULL, NULL);
    g_signal_emit_by_name (webrtc1[integer], "set-remote-description", offer,
        promise);
  }
  gst_webrtc_session_description_free (offer);
}

/* One mega message handler for our asynchronous calling mechanism */
static void
on_server_message (SoupWebsocketConnection * conn, SoupWebsocketDataType type,
    GBytes * message, CustomData *data)
{
  gchar *text;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %d", integer);

  switch (type) {
    case SOUP_WEBSOCKET_DATA_BINARY:
      g_printerr ("Received unknown binary message, ignoring\n");
      return;
    case SOUP_WEBSOCKET_DATA_TEXT:{
      gsize size;
      const gchar *data = g_bytes_get_data (message, &size);
      /* Convert to NULL-terminated string */
      text = g_strndup (data, size);
      break;
    }
    default:
      g_assert_not_reached ();
  }

  g_print("\nReceived : %s\n", text);

  /* Server has accepted our registration, we are ready to send commands */
  if (g_strcmp0 (text, "HELLO") == 0) {
    if (app_state[integer] != SERVER_REGISTERING) {
      cleanup_and_quit_loop ("ERROR: Received HELLO when not registering",
          APP_STATE_ERROR, data);
      goto out;
    }
    app_state[integer] = SERVER_REGISTERED;
    g_print ("Registered with server\n");

    /* Call has been setup by the server, now we can start negotiation */
  } else if (g_strcmp0 (text, "OFFER_REQUEST") == 0) {
    if (app_state[integer] != SERVER_REGISTERED) {
      cleanup_and_quit_loop ("ERROR: Received OFFER_REQUEST when not calling",
          PEER_CONNECTION_ERROR, data);
      goto out;
    }

    g_print("Got offer request, starting the pipeline\n");

    app_state[integer] = PEER_CONNECTED;
    /* Start negotiation (exchange SDP and ICE candidates) */
    if (!start_pipeline (data))
      cleanup_and_quit_loop ("ERROR: failed to start pipeline",
          PEER_CALL_ERROR, data);
    /* Handle errors */
  } else if (g_str_has_prefix (text, "ERROR")) {
    switch (app_state[integer]) {
      case SERVER_CONNECTING:
        app_state[integer] = SERVER_CONNECTION_ERROR;
        break;
      case SERVER_REGISTERING:
        app_state[integer] = SERVER_REGISTRATION_ERROR;
        break;
      case PEER_CONNECTING:
        app_state[integer] = PEER_CONNECTION_ERROR;
        break;
      case PEER_CONNECTED:
      case PEER_CALL_NEGOTIATING:
        app_state[integer] = PEER_CALL_ERROR;
        break;
      default:
        app_state[integer] = APP_STATE_ERROR;
    }
    cleanup_and_quit_loop (text, 0, data);
    /* Look for JSON messages containing SDP and ICE candidates */
  } else {
    JsonNode *root;
    JsonObject *object, *child;
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, text, -1, NULL)) {
      g_printerr ("Unknown message '%s', ignoring", text);
      g_object_unref (parser);
      goto out;
    }

    root = json_parser_get_root (parser);
    if (!JSON_NODE_HOLDS_OBJECT (root)) {
      g_printerr ("Unknown json message '%s', ignoring", text);
      g_object_unref (parser);
      goto out;
    }

    object = json_node_get_object (root);
    /* Check type of JSON message */
    if (json_object_has_member (object, "sdp")) {
      int ret;
      GstSDPMessage *sdp;
      const gchar *text, *sdptype;
      GstWebRTCSessionDescription *answer;

      g_assert_cmphex (app_state[integer], ==, PEER_CALL_NEGOTIATING);

      child = json_object_get_object_member (object, "sdp");

      if (!json_object_has_member (child, "type")) {
        cleanup_and_quit_loop ("ERROR: received SDP without 'type'",
            PEER_CALL_ERROR, data);
        goto out;
      }

      sdptype = json_object_get_string_member (child, "type");
      /* In this example, we create the offer and receive one answer by default,
       * but it's possible to comment out the offer creation and wait for an offer
       * instead, so we handle either here.
       *
       * See tests/examples/webrtcbidirectional.c in gst-plugins-bad for another
       * example how to handle offers from peers and reply with answers using webrtcbin. */
      text = json_object_get_string_member (child, "sdp");
      ret = gst_sdp_message_new (&sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);
      ret = gst_sdp_message_parse_buffer ((guint8 *) text, strlen (text), sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);

      if (g_str_equal (sdptype, "answer")) {
        g_print ("Received answer:\n%s\n", text);
        answer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER,
            sdp);
        g_assert_nonnull (answer);

        /* Set remote description on our pipeline */
        {
          GstPromise *promise = gst_promise_new ();
          g_signal_emit_by_name (webrtc1[integer], "set-remote-description", answer,
              promise);
          gst_promise_interrupt (promise);
          gst_promise_unref (promise);
        }
        app_state[integer] = PEER_CALL_STARTED;
      } else {
        g_print ("Received offer:\n%s\n", text);
        on_offer_received (sdp, data);
      }

    } else if (json_object_has_member (object, "ice")) {
      const gchar *candidate;
      gint sdpmlineindex;

      child = json_object_get_object_member (object, "ice");
      candidate = json_object_get_string_member (child, "candidate");
      sdpmlineindex = json_object_get_int_member (child, "sdpMLineIndex");

      /* Add ice candidate sent by remote peer */
      g_signal_emit_by_name (webrtc1[integer], "add-ice-candidate", sdpmlineindex,
          candidate);
    } else {
      g_printerr ("Ignoring unknown JSON message:\n%s\n", text);
    }
    g_object_unref (parser);
  }

out:
  g_free (text);
}

static void
on_server_connected (SoupSession * session, GAsyncResult * res, CustomData *data,
    SoupMessage * msg)
{
  GError *error = NULL;

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  g_print("PEERID: %s\n", data->peerid);

  ws_conn[integer] = soup_session_websocket_connect_finish (session, res, &error);
  if (error) {
    cleanup_and_quit_loop (error->message, SERVER_CONNECTION_ERROR, data);
    g_error_free (error);
    return;
  }

  g_assert_nonnull (ws_conn[integer]);

  app_state[integer] = SERVER_CONNECTED;
  g_print ("Connected to signalling server\n");

  g_signal_connect (ws_conn[integer], "closed", G_CALLBACK (on_server_closed), data);
  g_signal_connect (ws_conn[integer], "message", G_CALLBACK (on_server_message), data);

  /* Register with the server so it knows about us and can accept commands */
  register_with_server (data);
}

/*
 * Connect to the signalling server. This is the entrypoint for everything else.
 */
static gboolean
connect_to_websocket_server_async (CustomData *data)
{
  SoupLogger *logger;
  SoupMessage *message;
  SoupSession *session;
  const char *https_aliases[] = { "wss", NULL };

  int integer = (int) strtol ( data->peerid, NULL, 10 );

  session =
      soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, !disable_ssl,
      SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
      //SOUP_SESSION_SSL_CA_FILE, "/etc/ssl/certs/ca-bundle.crt",
      SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

  logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);

  message = soup_message_new (SOUP_METHOD_GET, server_url);

  g_print ("Connecting to server...\n");

  /* Once connected, we will register */
  soup_session_websocket_connect_async (session, message, NULL, NULL, NULL,
      (GAsyncReadyCallback) on_server_connected, data);
  app_state[integer] = SERVER_CONNECTING;
}

static GstFlowReturn new_sample(GstAppSink *sink)
{
    GstSample *sample;
    GstBuffer *buffer;
    GstBuffer *buffer_bytes;
    GstMapInfo info;
    GstMapInfo map;

    gchar *subj = g_strdup_printf("device::stream::1");

    natsConnection *conn = NULL;
    natsStatistics *stats = NULL;
    natsOptions *opts = NULL;
    natsStatus s;
    int dataLen = 0;

    printf("Sending messages to subject '%s'\n", subj);
    s = natsConnection_Connect(&conn, opts);
    // s = natsConnection_ConnectTo(&conn, "nats://164.52.213.244:4222");

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
        g_free(subj);

        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}

static gboolean create_pipeline(CustomData *data)
{
    GstStateChangeReturn ret;
    GError *error = NULL;
    gchar *sink_tmp, *gst_string;

    int integer = (int) strtol ( data->peerid, NULL, 10 );

    sink_tmp = g_strdup_printf ("sink-%s", data->id);

    g_print("SINK NAME: %s\n PEERID = %d\n", sink_tmp, integer);

    gst_string = g_strdup_printf("rtspsrc location=%s latency=0 ! rtph265depay ! h265parse ! decodebin ! videoconvert ! videoscale ! video/x-raw, format=GRAY8, width = 1024, height = 1024 ! appsink name=%s", data->url, sink_tmp);
    pipe1 = gst_parse_launch (gst_string, &error);

    if (error) {
        g_printerr ("Failed to parse launch: %s\n", error->message);
        g_error_free (error);
        goto err;
    }

    app_sink[integer] = gst_bin_get_by_name (GST_BIN (pipe1), sink_tmp);
    g_assert_nonnull (app_sink[integer]);

    g_object_set(app_sink[integer], "emit-signals", TRUE, NULL);
    g_signal_connect(app_sink[integer], "new-sample", G_CALLBACK(new_sample), app_sink[integer]);

    ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    g_free(gst_string);
    g_free(sink_tmp);
    return TRUE;

    err:
      g_print ("State change failure\n");
      if (pipe1)
      {
          gst_object_unref (pipe1);
      }
      return FALSE;
}

static gboolean add_device(CustomData *data)
{
    g_print("************************************************************A device has been received************************************************************\n");
    if(!create_pipeline(data))
    {
      g_print("Cannot connect with the server\n");
      return FALSE;
    }
    if(!connect_to_websocket_server_async(data))
    {
      g_print("Cannot connect with the server\n");
      return FALSE;
    }
}

static gboolean initial_pipeline()
{
    GstStateChangeReturn ret;
    GError *error = NULL;

    pipe1 = gst_parse_launch ("fakesrc ! queue ! fakesink", &error);

    if (error) {
        g_printerr ("Failed to parse launch: %s\n", error->message);
        g_error_free (error);
        goto err;
    }

    g_print ("Starting pipeline, not transmitting yet\n");

    ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        goto err;
    }
    return TRUE;

    err:
      g_print ("State change failure\n");
      if (pipe1)
      {
          gst_object_unref (pipe1);
      }
      return FALSE;
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

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("we received an error!\n");
      gst_message_parse_error(message, &err, &debug_info);
      g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(message->src), err->message);
      g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");

      g_clear_error(&err);
      g_free(debug_info);
      break;
    case GST_MESSAGE_EOS:
      g_print ("#####################################################we reached EOS#################################################\n");
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


static gboolean
check_plugins (void)
{
  int i;
  gboolean ret;
  GstPlugin *plugin;
  GstRegistry *registry;
  const gchar *needed[] = { "opus", "vpx", "nice", "webrtc", "dtls", "srtp",
    "rtpmanager", "videotestsrc", "audiotestsrc", NULL
  };

  registry = gst_registry_get ();
  ret = TRUE;
  for (i = 0; i < g_strv_length ((gchar **) needed); i++) {
    plugin = gst_registry_find_plugin (registry, needed[i]);
    if (!plugin) {
      g_print ("Required gstreamer plugin '%s' not found\n", needed[i]);
      ret = FALSE;
      continue;
    }
    gst_object_unref (plugin);
  }
  return ret;
}

int
main (int argc, char *argv[])
{
  GOptionContext *context;
  GError *error = NULL;

  context = g_option_context_new ("- gstreamer webrtc sendrecv demo");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("Error initializing: %s\n", error->message);
    return -1;
  }

  if (!check_plugins ())
    return -1;

  /* Disable ssl when running a localhost server, because
   * it's probably a test server with a self-signed certificate */
  {
    GstUri *uri = gst_uri_from_string (server_url);
    if (g_strcmp0 ("localhost", gst_uri_get_host (uri)) == 0 ||
        g_strcmp0 ("127.0.0.1", gst_uri_get_host (uri)) == 0)
      disable_ssl = TRUE;
    gst_uri_unref (uri);
  }

  loop = g_main_loop_new (NULL, FALSE);

  if (!initial_pipeline ()) 
  {
    g_printerr("ERROR: Failed to start pipeline");
    return -1;
  }

  bus = gst_element_get_bus(pipe1);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) cb_message,
      pipe1);

  CustomData data0, data1;

  data0.id = "Hu87350klxkn";
  data0.url = "rtsp://192.168.29.92:8554//test1";
  data0.type = "H.265";
  data0.peerid = "0001";

  if (!add_device (&data0)) 
  {
    g_printerr("ERROR: Failed to create pipeline");
    return -1;
  }

  sleep(5);

  data1.id = "Uuiwuqasdvsaa";
  data1.url = "rtsp://192.168.29.92:8554//test2";
  data1.type = "H.265";
  data1.peerid = "0002";

  if (!add_device (&data1)) 
  {
    g_printerr("ERROR: Failed to create pipeline");
    return -1;
  }

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  if (pipe1) {
    gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_NULL);
    g_print ("Pipeline stopped\n");
    gst_object_unref (pipe1);
  }

  return 0;
}