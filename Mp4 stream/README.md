TO INSTALL GSTREAMER NECESSARY PACKAGE:<br>
https://lifestyletransfer.com/how-to-install-gstreamer-on-ubuntu/<br>
(or)<br>
https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c<br><br>

TO INSTALL NATS:<br>
https://github.com/nats-io/nats.c<br><br>

TO COMPILE THE CODE:<br>
gcc js_mp4.c -o js_mp4 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libnats json-c`<br><br>

TO RUN THE CODE:<br>
./gstreamer_mp4<br><br>
