TO INSTALL THE RTSP SERVER PACKAGE: </br>
sudo apt-get install libgstrtspserver-1.0-dev gstreamer1.0-rtsp </br></br>

TO INSTALL NATS.C : </br>
https://github.com/nats-io/nats.c </br></br>

FOR COMPILING THE C CODE RUN: </br>
gcc rtsp_server.c -o rtsp_server `pkg-config --cflags --libs gstreamer-1.0 libnats gstreamer-rtsp-server-1.0` </br></br>

TO RUN THE CODE: </br>
./rtsp_server </br>

