# YouTube

Facts on this page come from YouTube's own help pages and were checked on 2026-10-04. YouTube can change them. [research/platform-requirements.md](research/platform-requirements.md) lists every source.

## Before you start

YouTube asks for a verified channel with no live streaming restrictions in the past 90 days, and you must be at least 16 years old. The first time you enable live streaming, YouTube takes up to 24 hours to switch it on. Do it well before your stream.

## Set it up

1. In RelayDock choose + Add Platform, YouTube.
2. Get your stream key: open YouTube Studio, choose Create, Go Live, then the Stream tab. Copy the Stream key from Stream settings.
3. Paste it into the Stream key field and choose Add.

RelayDock uses YouTube's encrypted RTMPS server by default:

```
rtmps://a.rtmps.youtube.com:443/live2
```

## You can be live as soon as RelayDock connects

With Auto-start on, YouTube puts your broadcast live the moment the encoder connects. RelayDock cannot send a hidden test stream to YouTube, because YouTube has no test flag for a stream key.

To try a stream without an audience, set the visibility to Private or Unlisted in YouTube Studio before you start. To choose the moment yourself, switch Auto-start off there, start the stream in RelayDock, wait for the preview in the Live Control Room and then choose Go live.

## What YouTube accepts

| Setting | Value |
| --- | --- |
| Video | H.264, HEVC or AV1, up to 3840x2160 at 60 FPS |
| Video bitrate | Up to 51000 Kbps |
| Keyframe interval | 2 seconds recommended, 4 seconds at most |
| Audio | AAC |

RelayDock streams over RTMP, the way OBS does. Its unlocked settings follow the performance mode, so a YouTube destination in Balanced mode gets the same settings as your other destinations and shares their encoder. Quality mode lets YouTube use more than Twitch's 6000 Kbps.

## Vertical video

YouTube accepts vertical video. Set the destination's Shape to Vertical. See [getting-started.md](getting-started.md).

## When it fails

| The card says | Check |
| --- | --- |
| YouTube rejected the connection | The stream key, and that live streaming is enabled on your channel. |
| The stream is live in RelayDock but YouTube shows nothing | YouTube Studio. With Auto-start off you have to choose Go live there. |
| YouTube could not connect to the server | Your internet connection, and that your network allows outgoing connections on port 443. |
| YouTube lost its connection | Your upload. RelayDock reconnects by itself with growing waits. |

When you stop the stream in RelayDock, check YouTube Studio. With Auto-stop on, YouTube ends the broadcast about a minute after the video stops arriving. With Auto-stop off, the broadcast stays open until you end it there.
