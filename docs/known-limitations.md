# Known limitations

What RelayDock does not do, and where it behaves in a way you might not expect.

## What has and has not been tested

RelayDock's automated tests stream to a test server on the same PC. They prove that RelayDock connects, encodes, shares encoders, reconnects, recovers and shuts down the way this documentation says.

They do not prove that Twitch, YouTube, Facebook or TikTok accept the stream. Testing that needs a real account on each platform, and a stream to a real platform cannot run in an automated test. [testing.md](testing.md) shows, per platform, what has been checked and by whom.

Encrypted RTMPS connections use the same OBS output as plain RTMP, with TLS handled by OBS. The local tests use plain RTMP only, because a local TLS server would need a certificate installed into Windows.

## Platforms

- RelayDock streams over RTMP and RTMPS. It has no SRT, RIST, WHIP or HLS output.
- RelayDock cannot fetch your stream key, set a title or category, show viewer numbers or press Go live for you. You do those on the platform. It signs in to Twitch only to read chat, and only when you ask.
- Twitch's Enhanced Broadcasting, 2K, HEVC and Dual Format are features of OBS Studio itself and work only through OBS's own Stream settings. RelayDock sends Twitch one standard H.264 stream.
- Only Twitch has a hidden test stream. For the other platforms, use the platform's own private or test mode.
- Test connection checks that a server accepts a TCP connection. It cannot check a stream key and does not perform the TLS handshake.
- Platforms change their limits and server addresses without notice. RelayDock's values are from the date shown under Settings, Platforms.

## Chat

- The chat dock reads Twitch and YouTube. It reads no chat from TikTok or Facebook. TikTok publishes no interface for live comments, and Meta's needs an approval that RelayDock does not have. [chat.md](chat.md) says what you can do for TikTok inside TikTok's rules.
- Twitch chat needs an application id. A RelayDock that comes without one asks you to register an application with Twitch, free of charge, and to enter its Client ID.
- YouTube chat needs an API key of your own, because YouTube forbids keys in open-source programs. It also needs the link to the stream, and a YouTube stream has a new link every time.
- Google gives an API key a daily amount of requests. A long stream with a short pause between requests can use it up. RelayDock then stops reading until you connect again.
- YouTube comments arrive in batches, a few seconds after they were written. Twitch comments arrive at once. The list is in the order of arrival, so a YouTube comment can stand below a Twitch comment that was written after it.
- RelayDock shows chat. It cannot write to chat, delete comments or ban viewers.
- Emotes show as their names. Badges are shown as "host" and "mod" only.
- The dock is for you. Twitch's simulcasting rules do not allow showing the chat of other platforms on your Twitch stream.
- The chat readers are tested against stand-ins for the two platforms. Nobody has used them with the real Twitch or the real YouTube yet.

## Video

- A destination's frame rate is the OBS frame rate divided by a whole number. From 60 FPS you can have 60, 30, 20, 15, 12 or 10. From 30 FPS you cannot have 60.
- A destination's size is never larger than its canvas and always has the canvas's aspect ratio. RelayDock corrects a size that would stretch the picture.
- When a destination encodes at a divided frame rate, OBS still writes the full OBS frame rate into the stream's metadata. The frames themselves arrive at the divided rate. A server that reads only the metadata shows the higher number. RelayDock's test server measures arrival times and confirms the real rate.
- Destinations share an encoder only when their video settings are exactly equal.
- A change of bitrate applies while live when OBS reports that the encoder can do that. Settings, Encoder shows it for each encoder on your PC. A change of resolution, frame rate or encoder needs a new encoder, so the destination reconnects.
- HEVC and AV1 are offered only where both OBS's RTMP output and the platform accept them: YouTube and custom destinations. Most servers expect H.264.
- OBS's AMD encoder can pick a faster preset than the one requested when it judges the graphics chip too slow. The OBS log shows what it used.
- A bitrate is a target. An encoder has a lowest quality it can go to, and a picture that needs more bits than the target at that quality goes out above the target. On the development PC the AMD hardware encoder sent about 28 Mbps of full-screen random noise at 1080p and 60 FPS for a target of 6000 Kbps. It held the target with a plain still picture, and x264 held it with the noise. [performance-results.md](performance-results.md) has the numbers. A live card shows what the destination sends. If that is far above what you set, lower the size or the frame rate.

## Vertical video

- All vertical destinations share one canvas size.
- A source that appears in both the OBS scene and a vertical layout is drawn twice, which costs graphics time.
- The Program item shows what OBS has in Program, including transitions. It cannot show one scene while Program shows another. Add the scene itself as an item for that.
- Layouts are saved in the OBS scene collection. Another scene collection has its own layouts.

## Audio

- A destination sends one OBS audio track. Multi-track audio in one stream is not supported.
- Audio is encoded once per group of destinations that share a video encoder and audio settings.

## Performance readings

- Graphics load comes from Windows performance counters and shows the busiest engine of the busiest adapter. Some systems do not provide it. The dock then shows a dash.
- OBS reports a high rendering lag for the first seconds after it starts. That is OBS starting up.
- RelayDock does not measure your upload speed. The network panel compares what your destinations need with the speed you entered.

## Windows and OBS

- Windows 10 and 11, 64-bit, only. Tested on Windows 11. Nobody has tested Windows 10.
- OBS Studio 32.0.0 or newer. Tested with 32.0.4 and 32.2.2.
- No 32-bit Windows and no Windows 7 or 8. OBS Studio ended both with its version 28. The last OBS Studio for them, 27.2.4, has no function for a second picture, which RelayDock needs for vertical video, and uses an older toolkit.
- OBS Studio 28 to 31 are not supported yet. RelayDock uses functions that arrived in OBS Studio 30.0, 30.1 and 31.0. A build for OBS Studio 30.1 and newer is possible and not done.
- On a laptop that runs on battery, Windows can ignore the request to stay awake. On the development laptop it accepted the request and still reported that nothing needed the system.
- A portable OBS does not read `C:\ProgramData`, so the installer does not serve it. Use the ZIP. See [manual-installation.md](manual-installation.md).
- While a destination is connecting, live or waiting to reconnect, OBS greys out its video settings and the PC stays awake. Both end when the last destination stops.
- The release files are not code-signed, so Windows SmartScreen may warn about the installer. [installation.md](installation.md) explains how to verify the download.

## Other

- The interface is in English. The locale system is in place, and translations are welcome.
- The logos of the platforms are trademarks of their owners. TikTok's terms ask for its written permission before its logo is used, and the project has not obtained one. Settings, Appearance switches the logos off, and the badges then show letters.
- RelayDock never updates itself. It tells you when a newer release exists and opens the installer's download in your browser. You close OBS and run it.
- GitHub answers 60 update checks an hour for one internet address. On a shared address, such as a VPN, that amount can be used up by others. The check then says so, and works again within the hour.
- RelayDock does not record. Use OBS for recording.
- The clipboard is shared by every program on your PC. While a copied key is on it, other programs can read it.
