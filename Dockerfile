
FROM ubuntu:22.04
RUN mkdir /app
WORKDIR /app
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
    clang \
    wget
RUN apt-get install -y python3-gi python3-gst-1.0 libgirepository1.0-dev libcairo2-dev gir1.2-gstreamer-1.0 gir1.2-gtk-3.0 python3-pip
RUN pip install --upgrade wheel pip setuptools
#RUN apt-get install -yqq libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav
RUN apt-get install -yqq libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
#RUN wget https://gstreamer.freedesktop.org/src/gstreamer/gstreamer-1.22.0.tar.xz --no-check-certificate
#RUN tar -xf gstreamer-1.22.0.tar.xz
#RUN cd gstreamer-1.22.0
#RUN ls -a
#RUN mkdir build
#RUN pip3 install meson
#RUN which meson && meson --version
#RUN apt-get update && apt-get install flex -y
#RUN apt-get update && apt-get install bison -y
#RUN apt-get update && apt-get -y install ninja-build
#RUN cd /app/gstreamer-1.22.0 && meson build -Dintrospection=disabled
#RUN cd /app/gstreamer-1.22.0/build && ninja && ninja install
# ENV CONDA_DIR /opt/conda
# RUN wget --quiet https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh -O ~/miniconda.sh && /bin/bash ~/miniconda.sh -b -p /opt/conda
# ENV PATH=$CONDA_DIR/bin:$PATH
# RUN conda config --add channels conda-forge
# RUN conda config --set channel_priority strict
# RUN conda install gst-plugins-bad
# RUN conda install gst-plugins-good
# RUN conda install gst-plugins-ugly
# RUN conda install gst-plugins-base
# RUN conda install gst-libav
RUN cd /app
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
RUN apt-get update && apt-get -y install libnats-dev
RUN cd full_code && gcc full_pipeline_camera.c -o full_pipeline -dotenv -lgstnet-1.0 $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0 json-c libnats)
#RUN cd full_code && ls .
RUN apt-get install -y gstreamer-1.0 gstreamer1.0-dev
RUN apt-get install -y git autoconf automake libtool
COPY . .
RUN pip uninstall pycairo
RUN pip install -r requirements.txt
RUN pip install pycairo PyGObject
ENV PORT=8554
EXPOSE 8554/tcp
#CMD [ "./full_code/full_pipeline" ]
