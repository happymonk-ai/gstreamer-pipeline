TO INSTALL THE RTSP SERVER PACKAGE: </br>
sudo apt-get install libgstrtspserver-1.0-dev gstreamer1.0-rtsp </br></br>

TO INSTALL GSTREAMER NECESSARY PACKAGE: <br>
https://lifestyletransfer.com/how-to-install-gstreamer-on-ubuntu/ <br>
(or)<br>
https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c<br><br>

FOR COMPILING THE MP4 SERVER CODE RUN: </br>
gcc mp4_server.c -o mp4_server `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0` </br></br>

TO RUN THE CODE: </br>
GST_DEBUG="GST_TRACER:7" GST_TRACERS="stats;rusage;log;latency;leaks" GST_DEBUG_FILE=trace.log ./mp4_server </br>

FOR COMPILING THE DEVICE SERVER CODE RUN: </br>
gcc device_server.c -o device_server `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0` </br></br>

TO RUN THE CODE: </br>
GST_DEBUG="GST_TRACER:7" GST_TRACERS="stats;rusage;log;latency;leaks" GST_DEBUG_FILE=trace.log ./device_server </br>

