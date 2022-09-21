FROM ubuntu:22.04
RUN mkdir /app
WORKDIR /app
ENV LD_LIBRARY_PATH=/usr/local/lib
RUN ln -snf /usr/share/zoneinfo/$CONTAINER_TIMEZONE /etc/localtime && echo $CONTAINER_TIMEZONE > /etc/timezone
RUN apt-get update && apt-get install -y tzdata

RUN apt-get update -yqq && apt-get install -yqq --no-install-recommends \
    apt-transport-https \
    build-essential \
    apt-utils \
    libssl-dev \
    pkg-config \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-rtsp \
    vim \
    git \
    cmake \
    gcc \
    clang
#RUN apt-get install -yqq libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav
RUN apt-get install -yqq libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
## setup nats-c ##
RUN apt-get -yqq install protobuf-compiler libprotobuf-dev libprotoc-dev
RUN apt-get -yqq install libxml2-dev libgeos++-dev libpq-dev libbz2-dev libtool automake

ADD . /app/

RUN cd ./nats.c/protobuf-c/ && ./autogen.sh && ./configure && make && make install && cd ../..
RUN cd ./nats.c/build/ && cmake .. && make install
#############################
## setup json-c ##
#RUN git clone https://github.com/json-c/json-c.git
RUN cd json-c &&  mkdir build && cmake -DCMAKE_INSTALL_PREFIX=build . && make all test install
RUN cp /app/json-c/build/lib/pkgconfig/json-c.pc /usr/local/lib/pkgconfig/
RUN ln -s /app/json-c/build/lib/libjson-c.so /usr/local/lib/libjson-c.so.5
# Build dotenv-c
RUN cd dotenv-c && mkdir -p build && cd build && cmake .. && cmake --build . && make install && cd ..
#RUN gcc js_mp4.c -o  js_mp4 $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libnats json-c)
RUN cd full_code && gcc js_office.c -o full_pipeline -lgstnet-1.0 $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0 json-c libnats)
#RUN cd full_code && ls .
ENV PORT=8554
EXPOSE 8554/tcp
#CMD [ "./full_code/full_pipeline" ]
