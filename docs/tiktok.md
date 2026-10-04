# TikTok

TikTok publishes less about streaming from a PC than the other platforms. This page says what is known, where it comes from and what is not documented. Checked on 2026-10-04. [research/platform-requirements.md](research/platform-requirements.md) lists every source.

## You need access from TikTok

TikTok decides who may go LIVE and who may stream from a PC with a server URL and stream key. For LIVE in general it asks that you are at least 18 years old and have 1,000 followers, and it says the number can differ between regions. It publishes no worldwide rule for stream key access, and not every account has it. RelayDock cannot give you that access and cannot work around it.

You have access when TikTok shows you a server URL and a stream key. If it does not, ask TikTok.

## Set it up

1. Get the server URL and the stream key from TikTok. On the TikTok website, choose Go LIVE, enter a title, select the option that keeps the LIVE from ending automatically, start, and copy the stream key and the RTMP URL. In the TikTok app, an older TikTok guide describes +, Transfer to PC/Mac, Go LIVE. What you see depends on your account and region.
2. In RelayDock choose + Add Platform, TikTok.
3. Paste the Server URL into the Server URL field.
4. Paste the Stream key into the Stream key field and choose Add.

TikTok publishes no fixed server list, so RelayDock has none. You always enter the address TikTok gives you.

TikTok says its keys expire after a period of inactivity and tells you to generate them no more than one hour before the stream. So before each stream, get fresh values, open the destination's editor, update the Server URL, choose Replace and paste the new key.

When you stop, end the LIVE on TikTok as well.

## Settings

TikTok documents no hard limits for this kind of stream. Its creator guides suggest:

| Setting | Value |
| --- | --- |
| Shape | Vertical, 9:16 |
| Size | 1080x1920 |
| Frame rate | 30 FPS |
| Video bitrate | About 5400 Kbps |
| Keyframe interval | 2 seconds |

A new TikTok destination in RelayDock starts as Vertical with these values as its ceiling. They are suggestions from TikTok's guides, not guaranteed limits. If TikTok shows you other values, use those and lock them.

## The vertical picture

TikTok destinations stream from RelayDock's vertical canvas. A new layout shows your whole OBS picture as a band across the middle of the 9:16 frame, so nothing is cut off. Open the layout editor from the destination's Video settings to change that: set Scaling to Fill to zoom in and crop the sides, or place your camera, game and other sources yourself. See [getting-started.md](getting-started.md).

## Test without an audience

RelayDock cannot send a hidden test stream to TikTok. Test connection in the card's menu checks only that the server answers.

## When it fails

| The card says | Check |
| --- | --- |
| TikTok rejected the connection | The Server URL and Stream key. Get fresh ones from TikTok. They expire. |
| TikTok could not connect to the server | The Server URL. Copy it again, complete and without spaces. |
| TikTok lost its connection | Your upload. RelayDock reconnects by itself with growing waits. |
