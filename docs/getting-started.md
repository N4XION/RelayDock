# Getting started

This page takes you from a fresh install to your first stream on two platforms.

## 1. Open the dock

In OBS Studio, open the Docks menu and choose RelayDock. Drag the dock where you want it. It behaves like any other OBS dock.

The first time, RelayDock asks you to review six short documents. Choose Review now, scroll each one to its end, tick the box and continue. This happens once.

## 2. Add a destination

1. Choose + Add Platform and pick a platform.
2. Paste your stream key into the Stream key field. The field shows dots as you type.
3. Choose Add.

Where to find the key:

| Platform | Guide |
| --- | --- |
| Twitch | [twitch.md](twitch.md) |
| YouTube | [youtube.md](youtube.md) |
| Facebook | [facebook.md](facebook.md) |
| TikTok | [tiktok.md](tiktok.md) |
| Your own server | [custom-rtmp.md](custom-rtmp.md) |

RelayDock saves the key in Windows Credential Manager. It never writes it to a file and never shows it again.

Each destination appears as a card. Add a second one the same way.

## 3. Check before you go live

Choose the stethoscope button in the toolbar. The preflight check looks at your scene, your audio, each destination's settings and key, your encoders and your upload budget. It reports READY, WARNING or FAILED and tells you what to fix.

Tell RelayDock your upload speed once, under Settings, Network. Then the check can warn you before you start more streams than your connection carries. Every destination needs its own upload.

## 4. Start

- Start one destination with the play button on its card.
- Start every ticked destination with Start All Enabled.

A card shows CONNECTING, then LIVE with the bitrate, dropped frames and time. If a destination fails, its card says what failed, what RelayDock knows about it and what to check. The other destinations keep streaming.

While a destination is connecting, live or reconnecting, RelayDock asks Windows to keep the PC and the display awake, the way OBS does for its own stream. The request ends when the last destination stops.

## 5. Stop

- Stop one destination with the stop button on its card.
- Stop everything with Stop All.

A destination that takes long to stop shows STOPPING. Press its button again to cut it off at once.

Closing OBS ends every stream. When you close OBS while a destination is active, RelayDock asks first: Close OBS or Keep streaming. The question follows the OBS setting "Show active outputs warning on exit" under Settings, Advanced. With that setting off, OBS closes without asking.

## What the cards show

| Status | Meaning |
| --- | --- |
| READY | The destination can start. |
| SETUP NEEDED | Something is missing, usually the stream key. The card says what. |
| OFF | The box is unticked. Start All Enabled skips it. You can still start it by hand. |
| CONNECTING | RelayDock is connecting. |
| LIVE | The stream is running. |
| TEST STREAM | A Twitch test stream that viewers do not see. |
| RECONNECTING | The connection dropped. RelayDock is trying again, with longer waits each time. |
| STOPPING | The stream is shutting down. |
| FAILED | The destination stopped with an error. The card explains it. |

The three dots on a card open more actions: Edit, Duplicate, Test connection, Reconnect, Move up, Move down and Remove. Drag a card by its grip to reorder. With the keyboard, Alt+Up and Alt+Down move the focused card and Enter opens its editor.

## One encoder for several destinations

Encoding video is the expensive part of streaming. When two destinations want exactly the same video, RelayDock encodes once and sends the result to both. The card says "Shared with" when that happens, and Settings, Streaming shows the plan before you start.

The performance mode decides how much sharing you get:

| Mode | What it does |
| --- | --- |
| Potato | 720p at 30 FPS, fastest preset, one shared encoder. For a weak PC or a heavy game. |
| Balanced | Up to 1080p at 60 FPS. Destinations on the same canvas get the same settings and share. |
| Quality | Each destination uses its platform's own limit. Different limits need separate encoders. |
| Custom | Every destination uses exactly the values you saved. |

Lock a setting in a destination's editor to keep your own value whatever the mode says. [performance.md](performance.md) explains the modes, locks and automatic optimisation.

## Horizontal and vertical at the same time

Set a destination's Shape to Vertical in its Video settings. It then streams 9:16 from RelayDock's vertical canvas while your other destinations stream 16:9.

A new vertical layout shows your OBS picture, centre-cropped. Open the layout editor from the destination's Video settings or from Settings, Vertical Layout to arrange sources yourself. The picture is never stretched: an item either fills its box and gets cropped, or fits inside it.

## Next

- [performance.md](performance.md): modes, locks, automatic optimisation, Prevent Game Lag.
- [appearance.md](appearance.md): themes, layouts and the look of the dock.
- [security.md](security.md): how keys are stored and what RelayDock sends.
- [troubleshooting.md](troubleshooting.md): when something does not work.
