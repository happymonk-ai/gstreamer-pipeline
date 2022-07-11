#include <gst/gst.h>

static GstElement *pipe1, *app_sink;

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