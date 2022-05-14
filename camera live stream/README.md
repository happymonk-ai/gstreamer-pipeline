1. RUN rtsp_camera_server.c file <br>
gcc rtsp_camera_server.c -o rtsp_camera_server -lgstnet-1.0 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0` <br><br>

2. RUN hls_stream.c <br>
gcc hls_stream.c -o hls_stream `pkg-config --cflags --libs gstreamer-1.0`

