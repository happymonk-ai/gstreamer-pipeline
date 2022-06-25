TO INSTALL GSTREAMER NECESSARY PACKAGE:<br>
https://lifestyletransfer.com/how-to-install-gstreamer-on-ubuntu/<br>
(or)<br>
https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c<br>

COMPILATION OF SINGLE STREAM:<br>
gcc stream.c -o stream `pkg-config --cflags --libs gstreamer-1.0`

COMPILATION OF MULTI STREAM:<br>
gcc multi_stream.c -o multi_stream `pkg-config --cflags --libs gstreamer-1.0`

TO RUN SINGLE STREAM:<br>
./stream


TO RUN MULTI STREAM:<br>
./multi_stream
