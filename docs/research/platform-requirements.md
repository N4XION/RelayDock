# Platform requirements research

Access date for every source: 2026-10-04. Platforms change these limits without notice. Re-check every row before each RelayDock release.

How to read the tables:

- official: the platform's own help centre, developer docs, legal pages or live endpoints.
- unofficial: anything else, such as the OBS Studio service list or a third-party guide.
- no official source: the official pages checked say nothing on this point. This file does not guess a value.

The number in front of each fact matches the research question: 1 protocols and ports, 2 server URLs, 3 where to find the URL and key, 4 eligibility, 5 video, 6 vertical video, 7 audio, 8 test modes, 9 disconnects, 10 simulcast policy.

## Twitch

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| 1. Protocols | RTMP and RTMPS. The ingest endpoint returns an `rtmp://` template (`url_template`) and an `rtmps://` template (`url_template_secure`) for every server. | https://ingest.twitch.tv/ingests | official |
| 1. Plain RTMP accepted | Yes. The developer docs and the Stream Key FAQ both give the broadcast URL as `rtmp://`. | https://dev.twitch.tv/docs/video-broadcast/ | official |
| 1. Ports | Twitch states no port number. The URL templates carry none. | Checked: https://ingest.twitch.tv/ingests and https://dev.twitch.tv/docs/video-broadcast/reference/ | no official source |
| 1. Generic RTMP port | IANA registers TCP 1935 to the RTMP server product (service name macromedia-fcs). This is a protocol default, not a Twitch statement. | https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml?search=1935 | unofficial |
| 2. URL format | `rtmp://<ingest-server>/app/<stream-key>[?bandwidthtest=true]` | https://dev.twitch.tv/docs/video-broadcast/ | official |
| 2. Default URL | The first entry in the ingest list is named Default: `rtmp://ingest.global-contribute.live-video.net/app/{stream_key}` and `rtmps://ingest.global-contribute.live-video.net/app/{stream_key}`. | https://ingest.twitch.tv/ingests | official |
| 2. Ingest list endpoint | `GET https://ingest.twitch.tv/ingests`, no authentication. Each entry has `_id`, `availability`, `default`, `name`, `url_template`, `url_template_secure` and `priority`. The reference page documents `url_template` and marks `availability`, `default` and `priority` as reserved for internal use. It does not mention `url_template_secure`. | https://dev.twitch.tv/docs/video-broadcast/reference/ | official |
| 2. Regional servers | 14 entries on the access date: `ingest.global-contribute.live-video.net` (Default), `aps20.contribute.live-video.net` (Sydney), `aps10.contribute.live-video.net` (Singapore), `apn10.contribute.live-video.net` (Tokyo), `apn20.contribute.live-video.net` (Seoul), `sae10.contribute.live-video.net` (São Paulo), `usw20.contribute.live-video.net` (Oregon), `aps30.contribute.live-video.net` (Mumbai), `use20.contribute.live-video.net` (Ohio), `use10.contribute.live-video.net` (N. Virginia), `eun10.contribute.live-video.net` (Stockholm), `euc10.contribute.live-video.net` (Frankfurt), `euw10.contribute.live-video.net` (Ireland), `euw30.contribute.live-video.net` (Paris). Twitch's help page says it selects endpoints for your network path, so fetch the list at run time. | https://ingest.twitch.tv/ingests | official |
| 2. Recommended ingest page | Shows the ten best endpoints for your connection, in `rtmp://` form. | https://help.twitch.tv/s/twitch-ingest-recommendation | official |
| 2. Primary and backup URLs | Twitch documents no primary and backup pair. | Checked: https://dev.twitch.tv/docs/video-broadcast/reference/ | no official source |
| 3. Where to find the key | Creator Dashboard > Settings > Stream. Twitch assigns the key. Apps can also call the Get Stream Key API. | https://help.twitch.tv/s/article/twitch-stream-key-faq | official |
| 3. Key persistence | Persistent. You do not re-enter it for each stream. You can reset it at any time, and Twitch resets it when you change your password. Guest keys for authorized streamers look like `live_sub_[streamkey]` and stay valid when you reset your own key. | https://help.twitch.tv/s/article/twitch-stream-key-faq | official |
| 4. Eligibility | Twitch states no follower or account-age minimum. Its FAQ says you need a stable connection and an encoder. Only Affiliates and Partners can stream in 2K. Every streamer can use Dual Format. | https://help.twitch.tv/s/article/how-do-i-stream-faq and https://help.twitch.tv/s/article/stream-quality | official |
| 5. Video codec | H.264 (AVC) for a standard RTMP stream. HEVC only for 2K (1440p) through Enhanced Broadcasting. Twitch says it offers no 4K and no AV1. | https://help.twitch.tv/s/article/stream-quality | official |
| 5. Recommended settings | 1080p60: 1920x1080, 6000 kbps, 60 or 50 fps. 1080p30: 1920x1080, 4500 kbps, 25 or 30 fps. 720p60: 1280x720, 4500 kbps, 60 or 50 fps. 720p30: 1280x720, 3000 kbps, 25 or 30 fps. | https://help.twitch.tv/s/article/broadcasting-guidelines | official |
| 5. Maximum video bitrate | 6000 kbps for a standard stream. Twitch says higher bitrates cause instability. | https://help.twitch.tv/s/article/guide-to-broadcast-health | official |
| 5. Enhanced Broadcasting bitrates | Twitch sets these automatically. Total bandwidth ranges from 1.5 to 10.5 Mbps. A single rendition ranges from 200 Kbps (160p AVC) to 6 Mbps (1080p AVC). In 2K mode the top values are 9 Mbps for 1440p HEVC and 7.5 Mbps for 1080p AVC. | https://help.twitch.tv/s/article/enhanced-broadcasting and https://help.twitch.tv/s/article/stream-quality | official |
| 5. Keyframe interval | 2 seconds | https://help.twitch.tv/s/article/broadcasting-guidelines | official |
| 5. B-frames | 2 in the NVENC settings. The x264 settings give no B-frame value. | https://help.twitch.tv/s/article/broadcasting-guidelines | official |
| 5. Rate control | CBR. Twitch advises against VBR. | https://help.twitch.tv/s/article/broadcasting-guidelines | official |
| 5. Profile and level | Main or High for x264. Twitch states no level. | https://help.twitch.tv/s/article/broadcasting-guidelines | official |
| 5. Encoder preset | NVENC: Quality. x264: veryfast to medium. | https://help.twitch.tv/s/article/broadcasting-guidelines | official |
| 5. Maximum stream length | 48 hours | https://help.twitch.tv/s/article/broadcasting-guidelines | official |
| 6. Vertical video | A single RTMP stream can use another aspect ratio, but Twitch says 16:9 has the best device support. Twitch delivers vertical video through Dual Format, which sends one horizontal track and one 9:16 track together. Dual Format needs Enhanced Broadcasting (OBS Studio 32.0.0 or newer plus a vertical plugin). Vertical canvas: 1080x1920. Typical vertical output: 720x1280 or 1080x1920. | https://help.twitch.tv/s/article/dual-format-vertical-video and https://help.twitch.tv/s/article/guide-to-using-twitch-inspector | official |
| 7. Audio | AAC-LC, stereo or mono. 96 kbps recommended for maximum compatibility. 160 kbps maximum. Any sampling frequency. | https://help.twitch.tv/s/article/broadcasting-guidelines | official |
| 8. Test without going live | Append `?bandwidthtest=true` to the stream key. The developer docs say the parameter disables live viewing. The Inspector page says you do not appear online and Twitch sends no notifications. | https://dev.twitch.tv/docs/video-broadcast/ and https://inspector.twitch.tv/ | official |
| 8. Twitch Inspector | Web tool at https://inspector.twitch.tv. Log in with Twitch. It lists your streams from the last 7 days, bandwidth tests included, with bitrate, frame rate, resolution, codec, AVC level and keyframe interval. | https://help.twitch.tv/s/article/guide-to-using-twitch-inspector | official |
| 9. Disconnect behaviour | With Disconnect Protection on, viewers see a slate for up to 90 seconds while you reconnect. If you do not reconnect in 90 seconds, the stream ends. You enable it on the Stream Settings page. Twitch lists OBS Studio, Streamlabs OBS, XSplit, Twitch Studio and the Twitch mobile app as supported. | https://help.twitch.tv/s/article/Disconnect-Protection | official |
| 9. Disconnect without that setting | Twitch states no reconnect window. It says reconnection behaviour may depend on the encoder. | Checked: https://help.twitch.tv/s/article/broadcasting-guidelines | no official source |
| 10. Simulcast policy | You may simulcast to any other live service if you meet three conditions. One: Twitch viewers get an experience at least as good as on other platforms, including your engagement in chat. Two: "You do not provide links, or otherwise direct your community, to leave Twitch" (Twitch Terms of Service, section 11). Three: you do not use third-party services that merge activity from other platforms, such as combined chat, into your Twitch stream. Terms last modified 08/12/2026. | https://legal.twitch.com/legal/terms-of-service/ | official |
| 10. Simulcast FAQ | The rules apply to every streamer without an exclusivity agreement. Twitch warns you before it enforces. Sending Twitch a smaller or lower-quality picture than other platforms breaks the rules. Chat-combining tools for your own private use are allowed. Profile links to other platforms are allowed, but you cannot promote a concurrent stream elsewhere. | https://help.twitch.tv/s/article/simulcasting-guidelines | official |
| 10. Simulcast with 2K | Allowed. Twitch suggests setting Maximum Video Tracks to at least 3 or 4 to avoid encoder overload. | https://help.twitch.tv/s/article/stream-quality | official |
| OBS service defaults | Keyframe 2. Max video bitrate 6000. Max audio bitrate 320. x264 option `scenecut=0`. H.264 only. Stream key page https://dashboard.twitch.tv/settings/stream. The OBS audio cap of 320 is higher than Twitch's stated 160 kbps maximum. | https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/rtmp-services/data/services.json | unofficial |

## YouTube

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| 1. Protocols | RTMP and RTMPS for encoders. HLS and DASH also exist. YouTube recommends RTMPS. | https://support.google.com/youtube/answer/2853702 | official |
| 1. Plain RTMP accepted | Yes. The Live Control Room shows the plain RTMP URL by default. Click the lock icon in the Stream URL field to get the RTMPS URL. | https://support.google.com/youtube/answer/10364924 | official |
| 1. Ports | RTMPS must connect to port 443. The help page suggests adding `:443` to the URL if you get an SSL error. YouTube states no RTMP port. | https://developers.google.com/youtube/v3/live/guides/rtmps-ingestion | official |
| 1. TLS detail | Send SNI in the TLS handshake with the ingest hostname. A cleartext connection to an RTMPS server times out. | https://developers.google.com/youtube/v3/live/guides/rtmps-ingestion | official |
| 2. Server URL format | `<protocol>://<server>/<path>`. YouTube's public docs do not print the hostname. The help page uses the placeholder `rtmps://exampleYouTubeServer.com:443/stream`. Copy the real Stream URL from the Live Control Room. | https://support.google.com/youtube/answer/10364924 | official |
| 2. Primary and backup URLs (API) | The liveStreams resource returns `cdn.ingestionInfo.ingestionAddress`, `backupIngestionAddress`, `rtmpsIngestionAddress`, `rtmpsBackupIngestionAddress` and `streamName`. Join them as `STREAM_URL/STREAM_NAME`. `cdn.ingestionType` accepts `dash`, `hls` and `rtmp`, and `rtmp` covers RTMPS. Page updated 2026-09-14. | https://developers.google.com/youtube/v3/live/docs/liveStreams | official |
| 2. Server URLs in OBS | Primary `rtmps://a.rtmps.youtube.com:443/live2`. Backup `rtmps://b.rtmps.youtube.com:443/live2?backup=1`. Legacy RTMP primary `rtmp://a.rtmp.youtube.com/live2`. Legacy RTMP backup `rtmp://b.rtmp.youtube.com/live2?backup=1`. | https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/rtmp-services/data/services.json | unofficial |
| 2. Regional servers | YouTube publishes no regional ingest list. | Checked: https://developers.google.com/youtube/v3/live/guides/rtmps-ingestion | no official source |
| 2. Backup stream rule | Primary and backup streams must match in resolution, codecs, profile, bitrate, frame rate, keyframe frequency and audio settings. | https://support.google.com/youtube/answer/3006768 | official |
| 3. Where to find the URL and key | YouTube Studio > Create > Go Live > Stream tab > Stream settings. Copy Stream URL and Stream key. | https://support.google.com/youtube/answer/2907883 | official |
| 3. Key persistence | The default key persists. Returning streamers get their previous settings and stream key back, so the encoder needs no update. Custom stream keys are reusable. Reuse settings copies the key to a scheduled stream. Owners and managers can reset a key in the Stream tab. | https://support.google.com/youtube/answer/2907883 and https://support.google.com/youtube/answer/9854503 | official |
| 3. Stream limits | 10 active streams per channel and 3 active streams per stream key. | https://support.google.com/youtube/answer/2474026 | official |
| 4. Eligibility | Verified channel. No live streaming restrictions in the past 90 days. At least 16 years old. The first enable request takes up to 24 hours. YouTube states no subscriber minimum for encoder streams. Mobile streaming has separate requirements. | https://support.google.com/youtube/answer/2474026 | official |
| 5. Video codecs | H.264, H.265 (HEVC) and AV1, listed with protocol RTMP/RTMPS. HDR needs HEVC. AV1 does not support HDR. | https://support.google.com/youtube/answer/2853702 | official |
| 5. Codec conflict | The developer protocol comparison page (updated 2026-09-14) lists only H.264 for RTMP and RTMPS. | https://developers.google.com/youtube/v3/live/guides/ingestion-protocol-comparison | official |
| 5. Bitrates for H.264, minimum to recommended | 2160p60: 14 to 50 Mbps. 2160p30: 11 to 42. 1440p60: 8 to 34. 1440p30: 7 to 21. 1080p60: 6 to 17. 1080p30: 5 to 14. 720p60: 3 to 8. 720p30: 3 to 8. 480p30: 0.4 to 4. 360p30: 0.4 to 4. | https://support.google.com/youtube/answer/2853702 | official |
| 5. Bitrates for AV1 and H.265, minimum to recommended | 2160p60: 10 to 35 Mbps. 2160p30: 8 to 30. 1440p60: 6 to 24. 1440p30: 5 to 15. 1080p60: 4 to 12. 1080p30: 4 to 10. 720p60: 2 to 6. 720p30: 2 to 6. 480p30: 0.3 to 3. 360p30: 0.3 to 3. | https://support.google.com/youtube/answer/2853702 | official |
| 5. Hard maximum bitrate | YouTube gives minimum and recommended values only. It states no hard cap. | Checked: https://support.google.com/youtube/answer/2853702 | no official source |
| 5. Maximum resolution and frame rate | 4K (2160p) at up to 60 fps. 4K streams cannot use the low latency option. Ultra low latency does not support resolutions above 1080p. | https://support.google.com/youtube/answer/2853702 and https://developers.google.com/youtube/v3/live/docs/liveBroadcasts | official |
| 5. Resolution detection | YouTube detects resolution and frame rate automatically by default. Manual resolution needs a custom stream key with manual settings turned on. API values for `cdn.resolution`: 240p, 360p, 480p, 720p, 1080p, 1440p, 2160p, variable. For `cdn.frameRate`: 30fps, 60fps, variable. | https://support.google.com/youtube/answer/2853702 and https://developers.google.com/youtube/v3/live/docs/liveStreams | official |
| 5. Keyframe interval | 2 seconds recommended. Do not exceed 4 seconds. | https://support.google.com/youtube/answer/2853702 | official |
| 5. B-frames and GOP | 2 B-frames, 1 reference frame, CABAC, progressive scan, closed GOP. YouTube does not support interlaced video. | https://support.google.com/youtube/answer/2853702 and https://support.google.com/youtube/answer/3006768 | official |
| 5. Rate control | CBR | https://support.google.com/youtube/answer/2853702 | official |
| 5. Profile and level | YouTube names no profile or level. Its error list says a wrong codec profile raises an error. | Checked: https://support.google.com/youtube/answer/3006768 | no official source |
| 5. Other video settings | Square pixels. Rec. 709 and 8-bit for SDR. 10-bit for HDR. AV1 at 3840x2160 and above needs at least 2 tile columns. | https://support.google.com/youtube/answer/2853702 | official |
| 6. Vertical video | Supported. A stream that is taller than wide opens full screen on mobile and shows in the Shorts feed. From the Live Control Room, streams go out in 16:9 and 9:16 together by default, and the vertical version is a centre crop. A third-party encoder can send its own vertical feed on a second stream key over RTMP(S). You cannot add the vertical format after the stream starts. YouTube states no vertical pixel dimensions. | https://support.google.com/youtube/answer/2474026 | official |
| 7. Audio | AAC or MP3. Stereo: 44.1 kHz, 128 kbps. 5.1 surround: AAC only, 48 kHz, 384 kbps. YouTube requires exactly one audio stream. | https://support.google.com/youtube/answer/2853702 and https://support.google.com/youtube/answer/3006768 | official |
| 8. Default go-live behaviour | In the default Stream tab flow, YouTube creates the watch page, goes live and notifies subscribers as soon as your encoder starts. | https://support.google.com/youtube/answer/2907883 | official |
| 8. Test without going public | Set the stream to private or unlisted. For a scheduled stream, start the encoder, wait for the preview in the Live Control Room, then click Go live when ready. The API has a testing state that only the broadcaster sees through a monitor stream. YouTube has no stream-key test flag. | https://support.google.com/youtube/answer/12917058 and https://support.google.com/youtube/answer/2907883 and https://developers.google.com/youtube/v3/live/docs/liveBroadcasts | official |
| 9. Disconnect behaviour | With auto-stop on, the broadcast stops around one minute after your encoder stops sending. Auto-start and auto-stop are settings in the Live Control Room and fields in the API (`enableAutoStart`, `enableAutoStop`). | https://developers.google.com/youtube/v3/live/docs/liveBroadcasts and https://support.google.com/youtube/answer/9854503 | official |
| 9. Disconnect with auto-stop off | YouTube states no reconnect window. | Checked: https://support.google.com/youtube/answer/2853835 | no official source |
| 9. Archive limit | YouTube archives streams under 12 hours automatically. | https://support.google.com/youtube/answer/2907883 | official |
| 10. Simulcast policy | No restriction. YouTube's help page says you can go live with the same content on several platforms at once and gives setup advice. | https://support.google.com/youtube/answer/16404722 | official |
| OBS service defaults | Keyframe 2. Max video bitrate 51000. Max audio bitrate 160. Codecs H.264, HEVC and AV1. Stream key page https://www.youtube.com/live_dashboard. | https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/rtmp-services/data/services.json | unofficial |

## Facebook

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| 1. Protocols | RTMPS. Meta says live broadcasts must use RTMPS. | https://developers.facebook.com/documentation/live-video-api/overview | official |
| 1. Plain RTMP accepted | The docs conflict. The overview, the reference and the Business Help pages require RTMPS. The API still returns an `rtmp://` field (`stream_url`) next to `secure_stream_url`, and the Broadcasting guide lists an RTMP or RTMPS encoder as a prerequisite. Use RTMPS only. | https://developers.facebook.com/documentation/live-video-api/guides/streaming | official |
| 1. Ports | 443 appears in the official sample URL `rtmps://rtmp-pc.facebook.com:443/rtmp/LIVE_VIDEO_ID?...`. Meta makes no other port statement. | https://developers.facebook.com/documentation/live-video-api/guides/streaming | official |
| 2. Server URL format | Server `rtmps://rtmp-api.facebook.com/rtmp/`. The stream key is everything after `/rtmp/`. | https://developers.facebook.com/documentation/live-video-api/getting-started | official |
| 2. Other hostnames in official samples | `rtmp-pc.facebook.com:443`, `live-api.facebook.com`, `rtmp.facebook.com` and `rtmps.facebook.com`. These are documentation samples. Use the URL Facebook returns for each broadcast. | https://developers.facebook.com/documentation/live-video-api/guides/streaming and https://developers.facebook.com/documentation/live-video-api/backup_stream | official |
| 2. Default server in OBS | `rtmps://rtmp-api.facebook.com:443/rtmp/` | https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/rtmp-services/data/services.json | unofficial |
| 2. Backup stream | Optional. In Live Producer: Advanced settings > Backup stream. In the API: `enable_backup_ingest=true` returns `secure_stream_url` plus `secure_stream_secondary_urls`. Facebook switches to the backup when the primary fails. | https://www.facebook.com/business/help/767179794442688 and https://developers.facebook.com/documentation/live-video-api/backup_stream | official |
| 2. Regional servers | Meta publishes no server list. The Live Ingests tool (https://www.facebook.com/live/ingests/) and the Speed Test API (`GET /traffic_speedtest`) find the best ingest server. The API returns a target token that you pass to get an optimised stream URL. | https://developers.facebook.com/documentation/live-video-api/guides/speed-test | official |
| 3. Where to find the URL and key | Go to facebook.com/live/create > Choose where to post > Go live > Select a video source > Streaming software. Copy Server URL and Stream key. | https://www.facebook.com/help/587160588142067 | official |
| 3. Standard stream key | Valid for one stream only. After your encoder connects you have up to five hours to go live. If you preview and then stop, you cannot resume on the same key. | https://www.facebook.com/business/help/165076674943644 and https://www.facebook.com/business/help/767179794442688 | official |
| 3. Persistent stream key | Advanced Settings > Persistent stream key. Turning it on changes the key. You can reuse it for every broadcast, one live video at a time. After a preview or broadcast ends, the encoder does not reconnect by itself. The Help Centre advises refreshing keys older than 7 days. | https://www.facebook.com/business/help/767179794442688 and https://www.facebook.com/help/587160588142067 and https://www.facebook.com/help/1534561009906955 | official |
| 3. API stream URLs | Use the URL within 24 hours of creation. Once used, you can stream to it for up to 8 hours. | https://developers.facebook.com/documentation/live-video-api/overview | official |
| 4. Eligibility | Account at least 60 days old. Page or professional mode profile with at least 100 followers. Page streams need Facebook access or task access to create content. | https://www.facebook.com/business/help/165076674943644 | official |
| 5. Video codec | H.264 only. Level 4.1 up to 1080p30. Level 4.2 for 1080p60. The Live API can reject other formats. | https://developers.facebook.com/documentation/live-video-api/reference | official |
| 5. Resolution and bitrate ranges | 1080p60 (1920x1080): 4,500 to 9,000 Kbps. 1080p30 (1920x1080): 3,000 to 6,000. 720p60 (1280x720): 2,250 to 6,000. 720p30 (1280x720): 1,500 to 4,000. 480p30 (854x480): 600 to 2,000. 360p (640x360): 400 to 1,000. Page updated Apr 16, 2026. | https://developers.facebook.com/documentation/live-video-api/reference | official |
| 5. Maximum | 1080p at 60 fps is the top listed format. Business Help adds a maximum recommended bitrate of 15 Mbps. | https://www.facebook.com/business/help/162540111070395 | official |
| 5. Keyframe interval | 2 seconds recommended. Do not exceed 4 seconds. | https://developers.facebook.com/documentation/live-video-api/reference | official |
| 5. Rate control and scan | CBR. Progressive scan. Do not change settings during a broadcast. | https://www.facebook.com/business/help/162540111070395 and https://developers.facebook.com/documentation/live-video-api/reference | official |
| 5. Profile, B-frames and GOP | The spec pages give no profile or B-frame value. The Automatic Encoder Configuration API sample for 1080p30 at 6000 kbps returns High profile, level 4.1, a fixed closed GOP of 60 frames, 3 B-frames, 3 reference frames, CBR and a 12000 kb buffer. The endpoint `GET /video_encoder_settings` needs no token. Page updated Oct 19, 2020. | https://developers.facebook.com/documentation/live-video-api/guides/automatic-encoder-configuration-api | official |
| 5. Maximum stream length | 8 hours. Facebook ends the stream at the limit with no grace period. Some approved partners get about 12 hours. | https://www.facebook.com/help/1534561009906955 | official |
| 5. Concurrent broadcasts | 5 active broadcasts per Page. 2 per profile. | https://www.facebook.com/business/help/1680276075344587 | official |
| 6. Vertical video | Meta makes no statement on 9:16 from an encoder. It says to aim for 16:9 and that it may not support a ratio too far from 16:9. The developer FAQ says the default is 16:9 and that Facebook infers the ratio from the stream. | https://developers.facebook.com/documentation/live-video-api/reference and https://developers.facebook.com/documentation/live-video-api/support | official |
| 7. Audio | AAC-LC. 44.1 or 48 kHz. 128 kbps preferred, 256 kbps maximum. Stereo. Audio is mandatory: a video-only stream ends. | https://developers.facebook.com/documentation/live-video-api/reference and https://developers.facebook.com/documentation/live-video-api/support | official |
| 8. Test without going public | On the Go live card, turn on the test broadcast toggle, then click Start test. Pages and professional mode profiles only. People with Facebook access to the Page can see a Page test. Only you can see a profile test. You cannot schedule a test. Profiles can also pick the Only me audience. | https://www.facebook.com/business/help/870559976652093 and https://www.facebook.com/business/help/167417030499767 | official |
| 8. Preview | Live Producer shows a preview once your encoder connects, and you go public when you click Go live. With the API and `status=LIVE_NOW`, the post goes live as soon as data arrives. | https://www.facebook.com/business/help/165076674943644 and https://developers.facebook.com/documentation/live-video-api/guides/streaming | official |
| 9. Disconnect behaviour | You have 2 to 3 minutes to reconnect to the original stream URL. After that you need a new key and URL. The API reports a stream timeout after 4 seconds without data. | https://developers.facebook.com/documentation/live-video-api/support and https://developers.facebook.com/documentation/live-video-api/guides/streaming | official |
| 9. Encoder end signal | A Live Producer setting lets your encoder end the stream. It does not apply when a backup stream is on. | https://www.facebook.com/business/help/698535488117624 | official |
| 10. Simulcast policy | Meta states no rule on streaming to other platforms at the same time. Its Live policies ban looping video, static images and third-party ads such as pre-roll or mid-roll. | https://www.facebook.com/business/help/859112078191288 and https://developers.facebook.com/documentation/live-video-api/support | official |
| OBS service defaults | Keyframe 2. Profile main. Max fps 60. Max video bitrate 9000. Max audio bitrate 128. H.264 only. Caps by format: 1080p60 9000, 1080p30 6000, 720p60 6000, 720p30 4000, 852x480 at 60 fps 3000, 852x480 at 30 fps 2000, 640x360 at 60 fps 1500, 640x360 at 30 fps 1000. | https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/rtmp-services/data/services.json | unofficial |

## TikTok

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| 1. Protocols | RTMP. TikTok's own guides tell you to copy a stream key and an RTMP URL. | https://www.tiktok.com/creator-academy/article/live-best-practices-publishers | official |
| 1. RTMPS and ports | TikTok states nothing on RTMPS or port numbers. | Checked: https://www.tiktok.com/creator-academy/article/live-best-practices-publishers | no official source |
| 2. Server URL | TikTok publishes no server URL, ingest list, regional list or backup URL. It shows the RTMP URL with the key when you set up a LIVE. | Checked: https://www.tiktok.com/creator-academy/article/live-best-practices-publishers | no official source |
| 3. Where to find the URL and key, desktop | Click Go LIVE, enter a title of up to 32 characters, select the option that stops the LIVE from ending automatically, start, then copy the stream key and RTMP URL. End the LIVE from the TikTok desktop page as well. Page updated Jun 22, 2026. | https://www.tiktok.com/creator-academy/article/live-best-practices-publishers | official |
| 3. Where to find the URL and key, app | TikTok app > + > Transfer to PC/Mac > Go LIVE. The app then shows the RTMP server and stream key. Guide dated 27/02/2023, marked for the United Kingdom. | https://seller-uk.tiktok.com/university/essay?knowledge_id=7738055662569218&default_language=en-GB | official |
| 3. Where to find the URL and key, third-party guide | tiktok.com > Go LIVE > livecenter.tiktok.com/producer > Go LIVE > pick a category and title > Save & Go LIVE. Server URL and Stream key appear at the bottom of the dashboard. | https://restream.io/learn/platforms/how-to-find-tiktok-stream-key/ | unofficial |
| 3. Key validity | Keys expire after a period of inactivity. TikTok says to generate them no more than one hour before the stream. | https://www.tiktok.com/creator-academy/article/live-best-practices-publishers | official |
| 3. Key validity, third-party guide | The key changes each time you log out of TikTok. | https://restream.io/learn/platforms/how-to-find-tiktok-stream-key/ | unofficial |
| 4. LIVE eligibility | At least 18 years old. 1,000 followers, which TikTok says may vary across regions. TikTok can ask you to confirm your age. | https://support.tiktok.com/en/live-gifts-wallet/tiktok-live/what-is-tiktok-live and https://www.tiktok.com/support/faq_detail?id=7581820708762753548&category=web_tiktok_live | official |
| 4. LIVE Studio eligibility | Gaming creators: 1,000 followers. Non-gaming creators: 10,000 followers. Article dated Sep 4, 2024. Access requirements vary by country or region. A trial needs at least 25 minutes of LIVE twice in the first week. | https://www.tiktok.com/live/creators/en-US/article/tiktok-live-studio-access_en-US and https://www.tiktok.com/live/creators/en-US/article/faqs-of-live-studio_en-US | official |
| 4. Stream key for third-party encoders | TikTok publishes no global rule. Its general pages describe the stream key flow but state no separate threshold. One regional page (TikTok Shop Thailand, 07/09/2023) limits OBS to sellers with at least 10,000 followers who ask TikTok to activate it. | https://seller-th.tiktok.com/university/essay?knowledge_id=2819052385093378&lang=en | official |
| 4. Stream key access, third-party guide | Not every account has RTMP access. You may have to apply for it. | https://restream.io/learn/platforms/how-to-find-tiktok-stream-key/ | unofficial |
| 5. Video requirements for RTMP ingest | TikTok publishes no ingest spec for codec, maximum bitrate, keyframe interval, B-frames, profile or level. | Checked: https://www.tiktok.com/creator-academy/article/live-best-practices-publishers and https://www.tiktok.com/live/creators/en-US/article/configure-live-settings-in-live-studio-en-US | no official source |
| 5. OBS settings in the TikTok Shop guide (UK, 2023) | Canvas 1080x1920 for 9:16. 25 fps (PAL) recommended. Video bitrate around 2500 Kbps. x264 or a hardware encoder. Wired connection of at least 20 Mbps. | https://seller-uk.tiktok.com/university/essay?knowledge_id=7738055662569218&default_language=en-GB | official |
| 5. OBS settings in the TikTok Shop LIVE Manager guide (US, 05/13/2026) | Base and output 1080x1920. 30 fps. CBR. 5400 Kbps. Keyframe interval 2. CPU usage preset very fast. Bicubic downscale. These settings feed OBS into LIVE Manager as a virtual camera. That path uses no stream key, so this is not an RTMP spec. | https://seller-us.tiktok.com/university/essay?knowledge_id=3360165320984366&lang=en | official |
| 5. LIVE Studio guidance | Viewers can watch at 60 fps or lower. Use at least 30 fps for gaming and 24 fps for lifestyle content. Pick 1080P or 720P+ on a good network. TikTok says 1920x1080 or higher may be unnecessary because most viewers are on mobile. | https://www.tiktok.com/live/creators/en-US/article/configure-live-settings-in-live-studio-en-US | official |
| 6. Vertical video | TikTok's own OBS guides configure 9:16 at 1080x1920. The Creator Academy says desktop streaming also allows landscape. | https://seller-us.tiktok.com/university/essay?knowledge_id=3360165320984366&lang=en and https://www.tiktok.com/creator-academy/article/live-best-practices-publishers | official |
| 7. Audio | 160 kbps, the OBS default, is acceptable. 256 kbps is also common. TikTok states no codec, sample rate or channel layout. | https://seller-uk.tiktok.com/university/essay?knowledge_id=7738055662569218&default_language=en-GB | official |
| 8. Test without going public | TikTok documents no test mode for RTMP. It says users see the LIVE only once your encoder starts sending. TikTok Shop LIVE Manager (US) has a Desktop Practice Mode, which is not an RTMP path. | https://www.tiktok.com/creator-academy/article/live-best-practices-publishers and https://seller-us.tiktok.com/university/essay?knowledge_id=3360165320984366&lang=en | official |
| 9. Disconnect behaviour | TikTok states no reconnect window. The go-live dialog has an option that stops the LIVE from ending automatically. Opening the profile on another device during a LIVE interrupts and ends it. You must end the LIVE on TikTok as well as in your encoder. | https://www.tiktok.com/creator-academy/article/live-best-practices-publishers | official |
| 10. Simulcast policy | TikTok makes no statement for or against streaming to other platforms at the same time. It says it does not change your traffic based on the streaming tool you use (LIVE Studio or OBS). | https://www.tiktok.com/live/creators/en-US/article/faqs-of-live-studio_en-US | official |
| OBS service list | OBS ships no TikTok entry. | https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/rtmp-services/data/services.json | unofficial |

## Unverified or unofficial items

Twitch

- Port numbers. No official page states them. The templates carry no port.
- H.264 level. Twitch states none.
- What happens above 6000 kbps. Twitch names 6000 as the maximum and warns of instability. It does not say whether the ingest rejects the stream.
- Reconnect window without Disconnect Protection. Twitch states none.
- Account checks before you can stream, such as a verified email, phone or two-factor login. The official two-factor page only requires it for Affiliates and Partners. Third-party copies of older help text say Twitch requires it to broadcast. Not confirmed.
- Whether the Default entry routes to the nearest server. The endpoint only names it Default.
- `url_template_secure` comes back from the live endpoint but the reference page does not document it.
- OBS caps Twitch audio at 320 kbps. Twitch's own page says 160 kbps maximum.
- One x264 block on the Broadcasting Guidelines page lists 1980x1080. The other blocks list 1920x1080.

YouTube

- Ingest hostnames. Only the OBS service list has them. YouTube's public docs do not.
- RTMP port. YouTube states none.
- Hard maximum bitrate. YouTube states none. The OBS cap of 51000 kbps is unofficial.
- H.264 profile and level. YouTube states none.
- HEVC and AV1 over RTMP. The Help Center lists them. The developer comparison page lists H.264 only. Neither page names Enhanced RTMP.
- Vertical resolution. YouTube states none.
- Reconnect window with auto-stop off. YouTube states none.
- Audio channels. The error list says only mono or stereo work. The encoder settings page allows 5.1 with AAC.
- Mobile streaming requirements fall outside this scope. Not checked.

Facebook

- The exact Server URL that Live Producer shows. It needs a login, so nobody checked it here.
- The Live Ingests tool needs a login. Not checked.
- https://www.facebook.com/policies_live/ returned a login wall. Not read.
- https://www.facebook.com/help/1534561009906956 (video format guidelines) returned a page-not-available message.
- https://developers.facebook.com/docs/live-video-api/faq and https://developers.facebook.com/docs/graph-api/reference/live-video/ returned page not found. The FAQ content now sits on the support page.
- 9:16 from an encoder. Meta makes no statement.
- Whether plain RTMP still connects. The docs conflict and nobody tested it here.
- H.264 profile and B-frames. Only the 2020 encoder configuration sample gives values (High, 3 B-frames). OBS uses main, which is unofficial.
- The 15 Mbps maximum recommended bitrate sits above the 9,000 Kbps top of the 1080p60 range. Meta does not say which one the ingest enforces.
- Simulcast. Meta states no rule and no explicit permission.
- The 2 to 3 minute reconnect window comes from the developer FAQ only. The Business Help pages do not repeat it.

TikTok

- Server URL, hostnames, ports and RTMPS support. TikTok publishes none. Third-party guides disagree on the hostname, so take the URL from the user.
- RTMP ingest spec: video codec, maximum bitrate, keyframe interval, B-frames, profile, level, audio codec, sample rate, channels. TikTok publishes none.
- Who gets a stream key for third-party encoders. TikTok publishes no global rule. The 1,000 follower figure is for LIVE in general and varies by region. The 10,000 follower figures apply to LIVE Studio for non-gaming creators and to a 2023 Thailand seller page.
- Community claims that TikTok does not confirm: stream key access depends on per-account approval, creators get it through agencies or creator networks, TikTok can revoke it, and a key lasts one session or two hours. These came from search summaries of third-party guides and creator videos. This research did not open those pages one by one.
- LIVE Producer at https://livecenter.tiktok.com/producer redirects to a login page. Not checked.
- TikTok Community Guidelines at https://www.tiktok.com/community-guidelines/en/accounts-features did not load. A direct fetch returned only the page title and the browser timed out twice. This research did not read its LIVE rules, so the simulcast row rests on the other TikTok pages only.
- https://support.streamyard.com/hc/en-us/articles/360051974452-Create-a-Live-Stream-on-TikTok returned HTTP 403.
- https://support.tiktok.com/en/live-gifts-wallet/tiktok-live/going-live no longer holds an article. It lists topics only.
- Test mode for RTMP. TikTok documents none.
- Reconnect window. TikTok states none.
- The TikTok Shop OBS guides date from 2023 and are marked for single regions (United Kingdom, Malaysia, Thailand). The 2026 United States guide covers the virtual camera path only.

Fetch notes

- help.twitch.tv, the TikTok help sites and the Meta help pages need JavaScript. A direct fetch returned an error or an empty page, so the values here come from a browser session on the access date.
- Two Twitch help URLs returned not found: https://help.twitch.tv/s/article/twitch-ingest-recommendation and https://help.twitch.tv/s/article/guide-to-broadcast-health-and-using-twitch-inspector. The working URLs are in the Twitch table.
- The Twitch Inspector test-stream text sits in the page source of https://inspector.twitch.tv/. The rendered page shows it only after login.
- The OBS service list values come from the master branch on the access date (format version 5, 79 services).
