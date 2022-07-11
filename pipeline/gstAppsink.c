#include <gst/gst.h>
#include <gst/app/gstappsink.h>

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

            int len = strlen(nats_str);

            // g_print("STRING: %s\n", nats_str);

            // g_print("LENGTH: %d\n", len);

            for (count = 0; (s == NATS_OK) && (count < 1); count++)
            {
                s = js_PublishAsync(js, subj1, (const void *)(info.data), info.size, NULL);
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