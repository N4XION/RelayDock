# Facebook

Facts on this page come from Meta's own help and developer pages and were checked on 2026-10-04. Meta can change them. [research/platform-requirements.md](research/platform-requirements.md) lists every source.

## Before you start

Facebook asks for an account that is at least 60 days old, and a Page or a professional mode profile with at least 100 followers.

## Set it up

1. In RelayDock choose + Add Platform, Facebook.
2. Get your stream key: open facebook.com/live/create, choose where to post, choose Go live, select a video source and pick Streaming software. Copy the Stream key.
3. Paste it into the Stream key field and choose Add.

Facebook accepts encrypted RTMPS only. RelayDock uses:

```
rtmps://rtmp-api.facebook.com:443/rtmp/
```

## The key works once, unless you say otherwise

A standard Facebook stream key is valid for one stream. If you preview and then stop, you cannot resume on the same key. For the next stream you need a new key: open the destination's editor, choose Replace, and paste it.

To keep one key, switch on Persistent stream key under Advanced settings in Live Producer. Switching it on gives you a new key, and that key then works for every broadcast. Meta advises refreshing a persistent key that is older than seven days.

If Facebook rejects the connection, an old key is the most common reason.

## What Facebook accepts

| Setting | Value |
| --- | --- |
| Video | H.264, up to 1920x1080 at 60 FPS |
| Video bitrate | Up to 9000 Kbps |
| Audio | AAC, up to 256 Kbps |
| Keyframe interval | 2 seconds recommended, 4 seconds at most |

Meta's documentation describes horizontal video for this kind of stream, so RelayDock offers no Vertical shape for Facebook.

## Test without an audience

RelayDock cannot send a hidden test stream to Facebook by itself. Facebook has its own ways:

- Pages and professional mode profiles: switch on the test broadcast option on the Go live card, then choose Start test.
- Profiles: set the audience to Only me before you start.

Live Producer shows a preview once RelayDock connects. You go public when you choose Go live there.

Test connection in the card's menu checks only that the server answers.

## When it fails

| The card says | Check |
| --- | --- |
| Facebook rejected the connection | The stream key. Get a new one from Live Producer, or switch on the persistent key. |
| Facebook could not connect to the server | Your internet connection, and that your network allows outgoing connections on port 443. |
| Facebook lost its connection | Your upload. RelayDock reconnects by itself with growing waits. Meta says an encoder on a persistent key does not reconnect by itself after a broadcast ended. |

When you stop the stream in RelayDock, end the broadcast in Live Producer too.
