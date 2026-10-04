# Performance

Streaming to several platforms costs two things: encoding and upload. RelayDock keeps encoding cheap by sharing encoders, and tells you about upload before you start. This page explains the controls.

Measured numbers from a real run are in [performance-results.md](performance-results.md). They show what the features on this page cost and save on one PC.

## Why sharing an encoder matters

Encoding video is the expensive part. Two destinations that want exactly the same video need only one encode. RelayDock then feeds both connections from that one encoder.

Sharing saves processing. It does not save upload. Every destination sends its own copy of the stream.

Destinations share when all of these are equal: canvas (horizontal or vertical), size, frame rate, encoder, bitrate, rate control, preset, profile, keyframe interval, B-frames and encoder options. RelayDock never bends one destination's settings towards another's behind your back. Making settings equal is what the performance mode is for.

Settings, Streaming shows which destinations will share before you start. A live card says "Shared with".

## Performance modes

The mode picks resolution, frame rate, bitrate, encoder and preset for every setting you have not locked.

| Mode | Size and frame rate | Sharing | Extra |
| --- | --- | --- | --- |
| Potato | At most 720p at 30 FPS | Every unlocked destination on a canvas gets the same settings, so they share one encoder. | Fastest encoder preset. Cheaper scaling filter. Measurements every two seconds instead of every second. No interface animation. No graphics load reading. |
| Balanced | At most 1080p at 60 FPS | The same as Potato. | |
| Quality | Up to each platform's limit, at most 60 FPS | Each destination uses its own platform's limits. Destinations with different limits get different settings and separate encoders. | Slower, better encoder preset. |
| Custom | Your saved values | Only where your saved values match. | |

In Potato and Balanced, destinations that agree on canvas, size, frame rate and encoder all take the lowest bitrate among them. With Twitch in the group, nothing goes above Twitch's 6000 Kbps.

RelayDock's starting bitrates, before platform limits:

| Size | Frame rate | Potato | Balanced | Quality |
| --- | --- | ---: | ---: | ---: |
| 2160p | above 45 FPS | 12000 | 20000 | 35000 |
| 2160p | up to 45 FPS | 9000 | 14000 | 25000 |
| 1440p | above 45 FPS | 6000 | 9000 | 13000 |
| 1440p | up to 45 FPS | 4500 | 6500 | 9000 |
| 1080p | above 45 FPS | 4500 | 6000 | 9000 |
| 1080p | up to 45 FPS | 3500 | 4500 | 6000 |
| 720p | above 45 FPS | 3500 | 4500 | 6000 |
| 720p | up to 45 FPS | 2500 | 3000 | 4000 |
| 480p | above 45 FPS | 2000 | 2500 | 3000 |
| 480p | up to 45 FPS | 1200 | 1500 | 2000 |

All values in Kbps. These are RelayDock's own starting points, not platform rules.

Presets per mode:

| Encoder | Potato | Balanced | Quality |
| --- | --- | --- | --- |
| x264 | superfast | veryfast | faster |
| NVIDIA NVENC | p3 | p5 | p6 |
| AMD AMF | speed | balanced | quality |
| Intel Quick Sync | TU7 | TU4 | TU2 |

With Encoder set to Automatic, RelayDock picks a hardware H.264 encoder when your PC has one (NVENC, then AMF, then Quick Sync) and x264 otherwise.

## Locks

Each destination can lock resolution, frame rate, bitrate and encoder in its editor.

- Locked: RelayDock always uses your saved value. The mode and automatic optimisation never change it. A locked value above a platform's limit gets a warning, and your value is used.
- Unlocked: the mode picks the value, inside the platform's limits. Automatic optimisation may lower it.

A lock on one destination can keep it from sharing an encoder with the others. The preflight check tells you when Potato Mode ends up with more than two encoders for that reason.

The aspect ratio is always kept. RelayDock corrects a saved size that does not match the canvas shape, so the picture is never stretched.

## Automatic optimisation

RelayDock watches three things while you stream, each as a percentage over the last ten seconds:

| Measurement | Meaning |
| --- | --- |
| Dropped frames | Frames a destination discarded because its connection could not carry them. |
| Encoder lag | Frames an encoder skipped because it could not keep up. |
| Rendering lag | Frames OBS could not draw in time. Usually the graphics chip is busy with your game. |

These are fixed rules, not artificial intelligence.

| Rule | Value |
| --- | --- |
| A measurement has to stay above its limit for | 20 seconds without a break |
| Limits | 3 percent for dropped frames, encoder lag and rendering lag |
| It counts as fine again when it falls to | half the limit |
| After a change, nothing else is proposed for | 60 seconds |
| Quality returns, one step at a time, after | 5 minutes without a problem |
| If a step up brings the problem back, the next attempt waits | twice as long, up to 30 minutes |

What it changes:

- Dropped frames: the bitrate, in steps of 100, 85, 70, 55 and 40 percent. This applies while live when the encoder supports it, with no reconnect.
- Encoder lag or rendering lag: the frame rate down to 30 first, then the size to 720p, then to 540p. These need a new encoder, so the destination reconnects for a moment.

Destinations that share an encoder change together.

What it never changes: a locked setting, and a destination whose "Let automatic optimisation adjust this destination" box is unticked.

Three modes, under Settings, Automatic Optimization:

| Mode | What happens |
| --- | --- |
| Off | Nothing is proposed and nothing changes. |
| Suggest changes | A suggestion appears in the dock with Apply, Ignore and Lock Setting. Nothing changes until you choose. This is the default. |
| Automatic | Changes that need no reconnect are applied and shown in the dock. Changes that need a reconnect are applied only when you allowed that. Otherwise they are suggestions. |

Ignore dismisses a suggestion for ten minutes. Lock Setting locks that setting on the destinations it names, restores your saved value and ends the suggestions for it.

When every stream has stopped, all reductions end. The next stream starts at full quality.

## Prevent Game Lag

Tick it in the Performance panel. Automatic optimisation then:

- reacts after 12 seconds instead of 20,
- uses half the limits for encoder lag and rendering lag (1.5 percent),
- also treats sustained processor load above 88 percent as a reason to act,
- waits 10 minutes instead of 5 before it raises quality again.

It needs automatic optimisation set to Suggest changes or Automatic. It reduces the load RelayDock adds. It cannot make a game run well on hardware that cannot run it.

## The preflight check

The stethoscope button checks, without connecting anywhere:

- OBS video is running and your scene shows a video source,
- OBS has audio that is not muted,
- each destination's address, key and settings,
- values above a platform's limit,
- two destinations with the same server and key,
- a key that is only held in memory,
- missing sources in a vertical layout,
- Twitch getting a smaller picture than another horizontal destination,
- how many encoders will run,
- whether OBS already misses frames,
- your upload budget.

It reports READY, WARNING or FAILED. Start All Enabled runs it first. A WARNING lets you start anyway. A FAILED result blocks Start All Enabled, and you can still start single destinations from their cards.

## Upload

Enter your upload speed under Settings, Network. RelayDock adds up what the enabled destinations need (video plus audio plus about 4 percent for the transport) and compares:

- within the safe share (75 percent unless you change it): fine,
- above the safe share: a warning,
- above your upload speed: the preflight check fails.

RelayDock runs no speed test and never measures your line. While you stream, the network panel also shows what the destinations send right now.

## What RelayDock costs when you are not streaming

RelayDock reads a handful of counters once per second. The vertical canvas is not rendered unless a vertical destination streams or the layout editor is open. [performance-results.md](performance-results.md) has the measured idle cost.
