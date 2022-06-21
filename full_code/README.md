TO COMPILE THE PROGRAM:<br>
gcc full_pipeline.c -o full_pipeline -lgstnet-1.0 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0 json-c libnats`<br><br>

TO RUN:<br>
./full_pipeline
