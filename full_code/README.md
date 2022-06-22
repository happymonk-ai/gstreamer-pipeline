TO COMPILE THE PROGRAM:<br>
gcc full_pipeline.c -o full_pipeline -lgstnet-1.0 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0 json-c libnats`<br><br>

TO RUN:<br>
./full_pipeline<br><br>

FOR HLS STREAMING<br>
The 'main.js' is for starting a http server, to expose the files generated in hls streams

TO INSTALL GSTREAMER NECESSARY PACKAGE:<br><br>
https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c<br><br>

TO INSTALL THE RTSP SERVER PACKAGE:<br><br>
sudo apt-get install libgstrtspserver-1.0-dev gstreamer1.0-rtsp<br><br>

TO INSTALL NATS:<br><br>
https://github.com/nats-io/nats.c
