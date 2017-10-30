# GStreamer Synchronised Network Playback

This is a library to help application developers easily write applications
where multiple devices connected to a network need to play back media in sync.

Use cases include multi-room audio playback, video walls, and any other
situation where it is required that possibly heteregenous devices on a network
need to playback the same audio/video stream.

There is a talk about this library at:

  * https://gstconf.ubicast.tv/videos/synchronised-playback-made-easy/

You can read more at:

  * An introduction to `gst-sync-server`: https://arunraghavan.net/2016/11/gstreamer-and-synchronisation-made-easy/

  * Building a video wall: https://arunraghavan.net/2016/12/synchronised-playback-and-video-walls/

  * Measuring synchronisation: https://arunraghavan.net/2017/01/quantifying-synchronisation-oscilloscope-edition/

## Examples

There is an example server and client in the `examples` directory. Once you've
built the project, just run `examples/test-server --help` and
`examples/text-client --help` to see how you can run these.

The example server expects a playlist file. The playlist file is a simple text
line with each line containing a URI, a space, and the length of the media at
that URI in nanoseconds (or -1 if it is unknown). An example might look like:

```
file:///some/local/foo.mp4 123456789
http://myhttpserver/bar.mkv -1
udp://192.168.0.0.1:5004 -1
```

The config file that can be passed to a server is a serialised
[`GVariant`](https://developer.gnome.org/glib/stable/glib-GVariant.html). These
are programmatically created using
[`g_variant_print`](https://developer.gnome.org/glib/stable/glib-GVariant.html#g-variant-print).

This example configuration was used to scale a video and play it across two
displays, after cropping to adjust for bezels.

```
{
  "client1": <{
    "crop": <{
      "right": 973
    }>,
    "offset": <{
      "left": 449
    }>,
    "scale": <{
      "width": 1280,
      "height": 720
    }>
  }>,
  "client2": <{
    "crop": <{
      "left": 973
    }>,
    "offset": <{
      "right": 449
    }>,
    "scale": <{
      "width": 1280,
      "height": 720
    }>
  }>
}
```
