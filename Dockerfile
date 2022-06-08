FROM bitnami/minideb:latest
RUN mkdir /app
WORKDIR /app
RUN ln -snf /usr/share/zoneinfo/$CONTAINER_TIMEZONE /etc/localtime && echo $CONTAINER_TIMEZONE > /etc/timezone
RUN apt-get update && apt-get install -y tzdata

RUN apt-get update -yqq && apt-get install -yqq --no-install-recommends \
    build-essential \
    pkg-config \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-rtsp \
    vim \
    git \
    cmake \
    gcc \
    clang
RUN apt-get install -yqq libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-doc gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
ADD . /app/
## Compiling rtsp_server.c ##
RUN gcc hls_stream.c -o hls_stream $(pkg-config --cflags --libs gstreamer-1.0)
#############################
EXPOSE 8060
