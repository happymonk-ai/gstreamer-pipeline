const { connect, consumerOpts, createInbox, StringCodec} = require('nats');
const fs = require("fs");
const { create } = require('ipfs-http-client')
var GifEncoder = require('gif-encoder');

const client = create('/ip4/127.0.0.1/tcp/5002/http')
var var1 = 0;

async function gif_convert(array){
    console.log(array.length);
    var gif = new GifEncoder(512, 512);
    var file = require('fs').createWriteStream('img'+var1+'.gif');

    gif.read(100000000);
    gif.pipe(file);
    gif.writeHeader();
    // gif.addFrame(array[0]);
    gif.addFrame(array[0]);
    gif.addFrame(array[1]);
    gif.addFrame(array[2]);
    gif.addFrame(array[3]);
    gif.addFrame(array[4]);
    gif.addFrame(array[5]);
    gif.finish();
    
    const res = await client.add('img'+var1+'.gif')
    console.log('Uploaded to IPFS: \n', res)
    var1 = var1 + 1;
    
}

async function consumer() {

    // to create a connection to a nats-server:
    const nc = await connect({ servers: "nats://216.48.189.5:4222" });
    console.log("Server connected\n");

    // create a codec
    const sc = StringCodec();

    const jsm = await nc.jetstreamManager();

    // create a jetstream client:
    const js = nc.jetstream();

    const opts = consumerOpts();
    opts.durable("me");
    opts.manualAck();
    opts.ackExplicit();
    opts.deliverTo(createInbox());

    let arr_buf = [];

    let sub = await js.subscribe("frame1", opts);
    const done = (async () => {

        for await (const m of sub) {

            const buffer = Buffer.from(m.data);
            if(arr_buf.length <= 5){
                arr_buf.push(buffer);
            }
            else{
                await gif_convert(arr_buf);
                arr_buf = [];
            }

            m.ack();
        }

    })();

    // The iterator completed
    await done;


}



consumer()
