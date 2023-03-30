import sys
import traceback
import argparse
import os
from dotenv import load_dotenv

import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject, GLib  # noqa:F401,F402

# Initializes Gstreamer, it's variables, paths
Gst.init()
load_dotenv()

def on_message(bus: Gst.Bus, message: Gst.Message, loop: GLib.MainLoop):
    mtype = message.type
    """
        Gstreamer Message Types and how to parse
        https://lazka.github.io/pgi-docs/Gst-1.0/flags.html#Gst.MessageType
    """
    if mtype == Gst.MessageType.EOS:
        print("End of stream")
        loop.quit()

    elif mtype == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        print(err, debug)
        loop.quit()

    elif mtype == Gst.MessageType.WARNING:
        err, debug = message.parse_warning()
        print(err, debug)

    return True

def launchHLSstream(camID, rtsp_URL, path, camera_type):
    
    print(camID, rtsp_URL, path)
    
    if(camera_type == 'H.264'):
        gst_str = 'rtspsrc name=m_rtspsrc_{ID} ! rtph264depay name=m_depay_{ID} ! mpegtsmux name=m_mux_{ID} ! hlssink name=m_sink_{ID}'.format(ID = camID)
    if(camera_type == 'H.265'):
        gst_str = 'rtspsrc name=m_rtspsrc_{ID} ! rtph265depay name=m_depay_{ID} ! mpegtsmux name=m_mux_{ID} ! hlssink name=m_sink_{ID}'.format(ID = camID)
        
    
    # Create the empty pipeline
    pipeline = Gst.parse_launch(gst_str)

    # source params
    source = pipeline.get_by_name('m_rtspsrc_{ID}'.format(ID = camID))
    source.set_property('latency', 0)
    source.set_property('location', rtsp_URL)
    source.set_property('protocols', 'tcp')
    source.set_property('drop-on-latency', 'true')

    # depay params
    depay = pipeline.get_by_name('m_depay_{ID}'.format(ID = camID))
    
    # mux params
    mux = pipeline.get_by_name('m_mux_{ID}'.format(ID = camID))

    # sink params
    sink = pipeline.get_by_name('m_sink_{ID}'.format(ID = camID))

    # Location of the playlist to write
    sink.set_property('playlist-root', 'https://hls.ckdr.co.in/live/stream{device_id}'.format(device_id = camID))
    # Location of the playlist to write
    sink.set_property('playlist-location', '{file_path}/{file_name}.m3u8'.format(file_path = path, file_name = camID))
    # Location of the file to write
    sink.set_property('location', '{file_path}/segment.%01d.ts'.format(file_path = path))
    # The target duration in seconds of a segment/file. (0 - disabled, useful for management of segment duration by the streaming server)
    sink.set_property('target-duration', 10)
    # Length of HLS playlist. To allow players to conform to section 6.3.3 of the HLS specification, this should be at least 3. If set to 0, the playlist will be infinite.
    sink.set_property('playlist-length', 3)
    # Maximum number of files to keep on disk. Once the maximum is reached,old files start to be deleted to make room for new ones.
    sink.set_property('max-files', 6)
    
    if not source or not sink or not pipeline or not depay or not mux:
        print("Not all elements could be created.")

    # Start playing
    ret = pipeline.set_state(Gst.State.PLAYING)
    if ret == Gst.StateChangeReturn.FAILURE:
        print("Unable to set the pipeline to the playing state.") 
    
def startHLSstream():
    # Gst.Pipeline https://lazka.github.io/pgi-docs/Gst-1.0/classes/Pipeline.html
    # https://lazka.github.io/pgi-docs/Gst-1.0/functions.html#Gst.parse_launch
    pipeline = Gst.parse_launch('fakesrc ! queue ! fakesink')

    # https://lazka.github.io/pgi-docs/Gst-1.0/classes/Bus.html
    bus = pipeline.get_bus()

    # allow bus to emit messages to main thread
    bus.add_signal_watch()

    # Start pipeline
    pipeline.set_state(Gst.State.PLAYING)

    # Init GObject loop to handle Gstreamer Bus Events
    loop = GLib.MainLoop()

    # Add handler to specific signal
    # https://lazka.github.io/pgi-docs/GObject-2.0/classes/Object.html#GObject.Object.connect
    bus.connect("message", on_message, loop)
    
    for i in range (1,7):
        file_path = './streams/stream{id}'.format(id = i)
        # checking if the directory streams exist or not for storing the HLS files
        if not os.path.exists(file_path):
            # if the demo_folder directory is not present then create it.
            os.makedirs(file_path)
        # fetch rtsp url from .env file
        rtsp_url = os.getenv('RTSP_URL_{id}'.format(id=i))
        cam_type = os.getenv('TYPE_{id}'.format(id=i))
        launchHLSstream(camID = i, rtsp_URL = rtsp_url, path = file_path, camera_type = cam_type)

    try:
        loop.run()
    except Exception:
        traceback.print_exc()
        loop.quit()

    # Stop Pipeline
    pipeline.set_state(Gst.State.NULL)
    

if __name__ == "__main__":
    startHLSstream()
