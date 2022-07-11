#include <gst/gst.h>

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