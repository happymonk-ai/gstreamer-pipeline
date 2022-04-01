TO INSTALL GSTREAMER NECESSARY PACKAGE:<br>
https://lifestyletransfer.com/how-to-install-gstreamer-on-ubuntu/<br>
(or)<br>
https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c<br>

FOR COMPILATION:<br>
gcc hls_stream.c -o hls_stream `pkg-config --cflags --libs gstreamer-1.0`

TO RUN:<br>
./hls_stream
