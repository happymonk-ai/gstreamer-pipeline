FROM ubuntu:20.04
RUN mkdir /app
WORKDIR /app
RUN apt-get update && apt-get install -y tzdata

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    pkg-config \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstrtspserver-1.0-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-rtsp \
    gstreamer1.0-plugins-ugly

# Copy the server script to the container
COPY . .

# Compile the server script
RUN gcc rtsp_mp4_server.c -o rtsp_mp4_server `pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0`

# Expose the necessary ports
EXPOSE 8554

# Run the server script inside the container
CMD ["./rtsp_mp4_server"]
