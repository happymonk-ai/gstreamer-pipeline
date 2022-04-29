const onvif = require('node-onvif');
const { nanoid } = require('nanoid');
const { connect } = require('nats');

let username = 'nivetheni'
let password = 'Chandrika5'

async function mp4_pub() {

    const nc = await connect({ servers: "nats://216.48.189.5:4222" });

    for (let i = 1; i <= 2; i++) {
        let url = "rtsp://216.48.189.5:8090//stream" + i
        let path = "/home/nivetheni/1080p_videos/" + i + ".mp4"
        let jobj = { "video_id": nanoid(20), "video_url": url, "video_path": path, "type": "video" };
        let jstr = JSON.stringify(jobj)
        console.log("Publishing to topic device.new")
        console.log((jstr))
        // nc.publish("device.new", Buffer.from(jstr));
        jobj.stream_endpt = "/stream" + i
        let jstr1 = JSON.stringify(jobj)
        console.log((jstr1))
        nc.publish("device.add.stream", Buffer.from(jstr1));
        console.log("Published\n");
    }

}

async function device_pub() {
    // to create a connection to a nats-server:
    const nc = await connect({ servers: "nats://164.52.213.244:4222" });

    console.log('Start the discovery process.');
    // Find the ONVIF network cameras.
    // It will take about 3 seconds.
    onvif.startProbe().then((device_info_list) => {
        let number = device_info_list.length
        if (number == 0) {
            console.log(number + ' devices were found.')
        }
        else {
            console.log(device_info_list.length + ' devices were found.');
            let decrement = number - 1
            // Show the device name and the URL of the end point.
            device_info_list.forEach((info) => {
                // Create an OnvifDevice object
                let device = new onvif.OnvifDevice({
                    xaddr: info.xaddrs[0],
                    user: username,
                    pass: password
                });
                // Initialize the OnvifDevice object
                device.init().then((device_info) => {
                    let endpt = number - decrement
                    decrement = decrement - 1
                    device_info['id'] = nanoid(20);
                    device_info['type'] = "stream";
                    device_info['stream_endpt'] = ("/stream" + endpt);
                    const url = new URL(info.xaddrs[0]);
                    device_info['hostname'] = url.hostname;
                    device_info['stream_url'] = device.getUdpStreamUrl();
                    var str = device_info['stream_url'];
                    var result = str.substring(0, 7) + username + ":" + password + "@" + str.substring(7);
                    device_info['stream_url'] = result;
                    device_info_str = JSON.stringify(device_info);
                    console.log(Buffer.from(device_info_str))
                    console.log(device_info_str, null, '  ');
                    // device_info_buffer = Buffer.from(device_info_str);
                    nc.publish("device.add.stream", Buffer.from(device_info_str));

                }).catch((error) => {
                    console.error(error);
                });
            })
        }

    }).catch((error) => {
        console.error(error);
    });
}

mp4_pub()
// device_pub()