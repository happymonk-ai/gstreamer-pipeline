PACKAGES TO BE INSTALLED: <br><br>

GSTREAMER PACKAGE<br>
a) https://lifestyletransfer.com/how-to-install-gstreamer-on-ubuntu/<br>
(or)<br>
https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c<br><br>

GSTREAMER RTSP SERVER PACKAGE<br>
b) sudo apt-get install libgstrtspserver-1.0-dev gstreamer1.0-rtsp<br><br>



1. RUN rtsp_camera_server.c file <br>
gcc rtsp_camera_server.c -o rtsp_camera_server -lgstnet-1.0 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0` <br><br>

2. RUN hls_stream.c <br>
gcc hls_stream.c -o hls_stream `pkg-config --cflags --libs gstreamer-1.0`

