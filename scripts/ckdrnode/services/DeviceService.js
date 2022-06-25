const onvif = require('node-onvif');
const { nanoid } = require('nanoid');
const { connect, StringCodec } = require('nats');

let username = 'nivetheni'
let password = 'Chandrika5'


async function publish(jstr1) {
    const nc = await connect({ servers: "nats://216.48.189.5:4222" });

    nc.publish("device.add.stream", Buffer.from(jstr1));
    console.log("Published\n");
}

async function mp4_pub() {

    for (let i = 4; i <= 4; i++) {
        let path = "/home/nivetheni/1080p_videos/" + i + ".mp4"
        let hls_path = "/home/nivetheni/nats.c/hlsstream/stream" + i
        let jobj = { "device_id": i, "device_url": path, "type": "video", "location": {"latitude": "12.972442", "longitude": "77.580643"} };
        let jstr = JSON.stringify(jobj)
        jobj.stream_endpt = "/video" + i
        let jstr1 = JSON.stringify(jobj)
        console.log((jstr1))
        setTimeout(publish, 2000, jstr1)
    }

}

async function device_pub() {
    // to create a connection to a nats-server:
    const nc = await connect({ servers: "nats://216.48.189.5:4222" });

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
                    device_info['device_id'] = "device_" + endpt;
                    device_info['type'] = "stream";
                    device_info['location'] = {"latitude": "12.972442", "longitude": "77.580643"};
                    device_info['stream_endpt'] = ("/stream" + endpt);
                    const url = new URL(info.xaddrs[0]);
                    device_info['hostname'] = url.hostname;
                    device_info['device_url'] = device.getUdpStreamUrl();
                    var str = device_info['device_url'];
                    var result = str.substring(0, 7) + username + ":" + password + "@" + str.substring(7);
                    device_info['device_url'] = result;
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