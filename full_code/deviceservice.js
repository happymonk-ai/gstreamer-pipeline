const onvif = require('node-onvif');
const { nanoid } = require('nanoid');
const { connect, StringCodec } = require('nats');

let username = 'nivetheni'
let password = 'Chandrika5'

async function device_pub() {
    // to create a connection to a nats-server:
    const nc = await connect({ servers: "nats://216.48.181.154:5222" });

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
                    device_info['location'] = {"latitude": "12.972442", "longitude": "77.580643"};
                    device_info['device_url'] = device.getUdpStreamUrl();
                    // var str = device_info['device_url'];
                    // var result = str.substring(0, 7) + username + ":" + password + "@" + str.substring(7);
                    // device_info['device_url'] = result;
                    device_info_str = JSON.stringify(device_info);
                    console.log(Buffer.from(device_info_str))
                    console.log(device_info_str, null, '  ');
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

device_pub()