TO INSTALL THE RTSP SERVER PACKAGE: </br>
sudo apt-get install libgstrtspserver-1.0-dev gstreamer1.0-rtsp </br></br>

FOR COMPILING THE C CODE RUN: </br>
gcc rtsp_server_mp4.c -o rtsp_server_mp4 `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0` </br></br>

TO RUN THE CODE: </br>
./rtsp_server_mp4 </br>
