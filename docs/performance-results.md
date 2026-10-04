# RelayDock performance measurements

Measured on 2026-10-04 with `tests/integration/Test-Performance.ps1`. Every number comes from a real run. Nothing is estimated.

- RelayDock build 1.0.0-rc.1+9.54512d94d
- PC: AMD Ryzen 3 5300U with Radeon Graphics, 4 cores, 8 threads, 7 GB memory
- Graphics: AMD Radeon(TM) Graphics
- OBS Studio 32.2.2, canvas 1920x1080 at 60 FPS
- Picture: scrolling random noise, the hardest picture there is to compress. One case uses a still picture of plain colours instead and says so.
- Each streaming case: 10 seconds of warm-up, then 30 seconds measured
- Streams went to a test server on the same PC

| Case | OBS processor | OBS memory | Video encoders | Rendering lag | Encoder lag | Dropped frames | Asked for (Kbps) | Sent (Kbps) | Encoder |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| OBS alone, idle | 0.2% | 137 MB | 0 | - | - | - |  |  |  |
| OBS with RelayDock, idle | 0.4% | 144 MB | 0 | - | - | - |  |  |  |
| 1 destination, plain still picture | 2.3% | 161 MB | 1 | 0.0% | 0.0% | 0.0% | 6160 | 6054 | h264_texture_amf |
| 1 destination(s), shared encoder | 3.0% | 172 MB | 1 | 0.0% | 0.0% | 0.0% | 6160 | 28448 | h264_texture_amf |
| 2 destination(s), shared encoder | 3.1% | 172 MB | 1 | 0.0% | 0.0% | 0.0% | 6160, 6160 | 28478, 28478 | h264_texture_amf |
| 3 destination(s), shared encoder | 3.5% | 173 MB | 1 | 0.0% | 0.0% | 0.0% | 6160, 6160, 6160 | 28468, 28468, 28468 | h264_texture_amf |
| 4 destination(s), shared encoder | 4.3% | 174 MB | 1 | 0.0% | 0.0% | 0.0% | 6160, 6160, 6160, 6160 | 28570, 28570, 28570, 28570 | h264_texture_amf |
| 4 destinations, 4 encoders | 14.1% | 415 MB | 4 | 66.3% | 91.5% | 0.0% | 6160, 5660, 5160, 4660 | 5398, 4933, 4520, 4138 | h264_texture_amf |
| 2 horizontal and 1 vertical | 4.1% | 190 MB | 2 | 0.0% | 0.0% | 0.0% | 6160, 6160, 6160 | 28454, 28454, 6039 | h264_texture_amf |
| 3 destinations, potato mode | 2.4% | 175 MB | 1 | 0.0% | - | 0.0% | 2660, 2628, 2660 | 2508, 2508, 2508 | h264_texture_amf |
| 3 destinations, quality mode | 3.6% | 187 MB | 2 | 0.0% | 0.0% | 0.0% | 6160, 9128, 9160 | 28488, 28452, 28452 | h264_texture_amf |
| 3 destinations, x264, shared | 30.7% | 511 MB | 1 | 0.0% | 0.0% | 0.0% | 6160, 6160, 6160 | 6029, 6029, 6029 | obs_x264 |
| 3 destinations, x264, separate | 72.5% | 1242 MB | 3 | 0.0% | 97.4% | 0.0% | 6160, 5660, 5160 | 5372, 4941, 4480 | obs_x264 |

OBS processor is the share of all processor threads that the OBS process used. Rendering lag, encoder lag and dropped frames are shares of the frames in the measured interval. "Asked for" is the video bitrate plus the audio bitrate of each destination.

## Cases that sent more than they were asked for

In these cases a destination sent more than one and a half times its bitrate:

- 1 destination(s), shared encoder, encoder `h264_texture_amf`: asked for 6160 Kbps, sent 28448 Kbps
- 2 destination(s), shared encoder, encoder `h264_texture_amf`: asked for 6160, 6160 Kbps, sent 28478, 28478 Kbps
- 3 destination(s), shared encoder, encoder `h264_texture_amf`: asked for 6160, 6160, 6160 Kbps, sent 28468, 28468, 28468 Kbps
- 4 destination(s), shared encoder, encoder `h264_texture_amf`: asked for 6160, 6160, 6160, 6160 Kbps, sent 28570, 28570, 28570, 28570 Kbps
- 2 horizontal and 1 vertical, encoder `h264_texture_amf`: asked for 6160, 6160, 6160 Kbps, sent 28454, 28454, 6039 Kbps
- 3 destinations, quality mode, encoder `h264_texture_amf`: asked for 6160, 9128, 9160 Kbps, sent 28488, 28452, 28452 Kbps

An encoder has a lowest quality it can go to. When a picture needs more bits than the bitrate allows even at that quality, the encoder sends more than it was asked for. Random noise at a large size and a high frame rate is such a picture. Compare these rows with the rows of the same encoder that stayed at their bitrate: the still picture, a smaller size, or a lower frame rate.
