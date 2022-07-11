#include "/home/nivetheni/nats.c/examples/examples.h"
#include "./rtspServer.c"
#include <json.h>
#include <string.h>

static void
_jsPubErr(jsCtx *js, jsPubAckErr *pae, void *closure)
{
    int *errors = (int *)closure;

    printf("Error: %u - Code: %u - Text: %s\n", pae->Err, pae->ErrCode, pae->ErrText);
    printf("Original message: %.*s\n", natsMsg_GetDataLength(pae->Msg), natsMsg_GetData(pae->Msg));

    *errors = (*errors + 1);
}

/* To fetch the key in the json object */
struct json_object *find_something(struct json_object *jobj, const char *key)
{
    struct json_object *tmp;

    json_object_object_get_ex(jobj, key, &tmp);

    return tmp;
}

/* To remove extra characters in the strings */
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
    free(video_path);
    free(stream_endpt);
    free(video_type);

    // // Need to destroy the message!
    natsMsg_Destroy(msg);
}