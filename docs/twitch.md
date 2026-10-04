# Twitch

Facts on this page come from Twitch's own help pages and were checked on 2026-10-04. Twitch can change them. [research/platform-requirements.md](research/platform-requirements.md) lists every source.

## Set it up

1. In RelayDock choose + Add Platform, Twitch.
2. Get your stream key: on twitch.tv open the Creator Dashboard, Settings, Stream, and copy the Primary Stream key.
3. Paste it into the Stream key field and choose Add.

The Server list starts with Default (RTMPS, encrypted). Twitch routes that address to an ingest server near you. Pick a regional server only when you have a reason to.

Your Twitch key does not expire. Twitch issues a new one when you reset it or change your password.

## What Twitch accepts

| Setting | Value |
| --- | --- |
| Video | H.264, up to 1920x1080 at 60 FPS |
| Video bitrate | Up to 6000 Kbps |
| Audio | AAC, up to 160 Kbps |
| Keyframe interval | 2 seconds |
| Rate control | CBR |

RelayDock keeps unlocked settings inside these limits. If you lock a value above a limit, RelayDock warns you and uses your value.

Twitch offers 2K and HEVC only through its Enhanced Broadcasting feature in OBS itself. RelayDock streams one standard H.264 stream.

## Test without going live

Twitch is the one platform where RelayDock can run a real test stream that viewers do not see.

Open the three dots on the card and choose Start test stream. RelayDock adds Twitch's `?bandwidthtest=true` flag to the key. The card shows TEST STREAM. Twitch checks your key and receives your video, but your channel stays offline.

Test connection, also in that menu, only checks that the server answers.

## Vertical video

Twitch plays 16:9 on the most devices. You can set a Twitch destination to Vertical, and RelayDock warns you when you do. Twitch's own vertical feature, Dual Format, works through Enhanced Broadcasting in OBS and is separate from RelayDock.

## Streaming to other platforms at the same time

Twitch's Terms of Service allow simulcasting under conditions. On the date above they ask that:

- the Twitch stream is at least as good as the stream you send elsewhere,
- you do not send your Twitch community to another service during the stream,
- you do not combine other platforms' chat or activity into your Twitch stream.

An exclusivity agreement with Twitch can forbid simulcasting altogether. Read the current rules yourself.

RelayDock helps with the first point. The preflight check warns you when another horizontal destination gets a larger picture or a higher frame rate than Twitch.

## When it fails

| The card says | Check |
| --- | --- |
| Twitch rejected the connection | The stream key. Reset it in the Creator Dashboard and enter the new one. |
| Twitch could not connect to the server | Your internet connection. Try the Default server. |
| Twitch lost its connection | Your upload. RelayDock reconnects by itself with growing waits. |

Twitch Inspector at inspector.twitch.tv shows what Twitch received, including test streams.
