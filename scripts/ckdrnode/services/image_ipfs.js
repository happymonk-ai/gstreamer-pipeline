const { connect, consumerOpts, createInbox, StringCodec } = require('nats');
const fs = require("fs");
var jpeg = require('jpeg-js');
const { create } = require('ipfs-http-client')


//device_stream

async function consumer() {

  // to create a connection to a nats-server:
  const nc = await connect({ servers: "nats://216.48.189.5:4222" });
  console.log("Server connected\n");

  // create a codec
  const sc = StringCodec();

  const jsm = await nc.jetstreamManager();

  // create a jetstream client:
  const js = nc.jetstream();

  const ipfs = create('/ip4/127.0.0.1/tcp/5002/http')

  await ipfs.files.mkdir('/images')

  const opts = consumerOpts();
  opts.durable("me");
  opts.manualAck();
  opts.ackExplicit();
  opts.deliverTo(createInbox());

  let sub = await js.subscribe("stream.*.frame", opts);
  const done = (async () => {
    for await (const m of sub) {
      var frameData = Buffer.from(m.data);
      console.log(frameData);
      var rawImageData = {
        data: frameData,
        width: "1024",
        height: "1024",
      };
      var jpegImageData = jpeg.encode(rawImageData, 100);
      console.log(jpegImageData);

      const imgdata = fs.writeFileSync("image" + m.seq + ".jpg", jpegImageData.data);

      await ipfs.files.write(
        '/images/image'  + m.seq +  '.jpg',
        imgdata,
        { create: true })

      m.ack();
    }
  })();


}


consumer()