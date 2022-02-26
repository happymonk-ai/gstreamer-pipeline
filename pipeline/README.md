TO INSTALL GSTREAMER NECESSARY PACKAGE:<br>
https://lifestyletransfer.com/how-to-install-gstreamer-on-ubuntu/<br>
(or)<br>
https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c<br><br>


TO INSTALL NATS:<br>
https://github.com/nats-io/nats.c<br><br>

TO INSTALL JSON-C:<br>
https://lynxbee.com/how-to-compile-json-c-json-implementation-in-c/<br><br>

TO COMPILE:<br>
gcc gstreamer_mp4.c -o gstreamer_mp4 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libnats json-c`<br><br>

TO RUN :<br>
GST_DEBUG="GST_TRACER:7" GST_TRACERS="stats;rusage;log;latency;leaks" GST_DEBUG_FILE=trace.log ./gstreamer_mp4
