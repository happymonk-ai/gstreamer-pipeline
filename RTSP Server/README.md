TO INSTALL THE RTSP SERVER PACKAGE:
sudo apt-get install libgstrtspserver-1.0-dev gstreamer1.0-rtsp

TO INSTALL NATS.C :
https://github.com/nats-io/nats.c

FOR COMPILING THE C CODE RUN:
gcc rtsp_server.c -o rtsp_server `pkg-config --cflags --libs gstreamer-1.0 libnats gstreamer-rtsp-server-1.0`

TO RUN THE CODE:
./rtsp_server

