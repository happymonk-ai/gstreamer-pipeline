FROM bitnami/minideb:latest
WORKDIR /app
COPY ./full_code .
RUN apt-get update && apt-get install -y tzdata
## installing gstreamer necessary packages
RUN apt-get install -yqq libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-doc gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
## Installing RTSP server packages
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
## Installing NATS
RUN git clone https://github.com/Microsoft/vcpkg.git && ./vcpkg/bootstrap-vcpkg.sh && vcpkg install cnats
## Installing protobuf-c
RUN git clone https://github.com/protobuf-c/protobuf-c.git && cd protobuf-c && ./configure && make && make install && cd ..
## Installing protobuf
RUN sudo apt-get install g++ bazel && git clone https://github.com/protocolbuffers/protobuf.git && cd protobuf && git submodule update --init --recursive && cd ..
## Installing json-c
RUN git clone git://github.com/json-c/json-c.git && cd json-c && git branch -r && git checkout -b json-c-0.14 origin/json-c-0.14 && mkdir build cmake -DCMAKE_INSTALL_PREFIX=build . && make all test install && cd ..
## Compiling rtsp_server.c ##
RUN gcc full_pipeline.c -o full_pipeline -lgstnet-1.0 pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0 json-c libnats
#############################
EXPOSE 8060
