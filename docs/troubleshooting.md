# Troubleshooting

Start with the card. When a destination fails, its card says what failed, what RelayDock knows and what to check. Then run the preflight check (the stethoscope button). It finds most setup problems before you go live.

## RelayDock does not appear in OBS

1. Open the Docks menu. RelayDock is listed there once the plugin loaded. A new dock starts hidden.
2. If it is not listed, open Help, Log Files, View Current Log and search for `RelayDock`.
   - No line at all: OBS did not find the plugin. Check the folder. See [manual-installation.md](manual-installation.md). A portable OBS does not read `C:\ProgramData`.
   - A line about a newer OBS version: update OBS Studio to 32.0.0 or newer.
3. Reinstall RelayDock with OBS closed.

## The dock only shows "Before your first stream"

RelayDock asks you to review six documents once. Choose Review now. Each page unlocks its box when you reach the end of the text, and Continue when you tick the box.

## A destination will not start

| The card says | What to do |
| --- | --- |
| ... has no stream key | Open the editor and paste the key. |
| ... has an invalid server URL | Check the Server URL. It must start with `rtmp://` or `rtmps://`. |
| ... rejected the connection | The key is wrong or expired, or the address is wrong. Get a new key. Facebook and TikTok keys expire. |
| ... could not connect to the server | Check the Server URL for typing mistakes, then your internet connection. A firewall or your network can block the port. |
| ... lost its connection | Your upload or the platform. RelayDock reconnects by itself when the destination allows it. |
| ... has no video encoder to use | OBS offers no encoder the stream can use. Check that OBS itself can stream. Update your graphics driver. |
| ... could not create its video encoder | The graphics driver refused the settings. Pick another encoder in the Video settings, or lower the resolution. |
| ... cannot use the selected encoder settings | Pick a different encoder or a lower resolution. |
| ... stopped because the video encoder failed | Update your graphics driver, or switch to the x264 encoder. |
| ... cannot stream the HDR color format OBS is set to | Set OBS to an SDR colour format under OBS Settings, Advanced. |

One destination failing never stops the others.

Test connection in the card's menu tells you whether the server answers at all:

| Test connection says | Meaning |
| --- | --- |
| The server answers | The name resolves and the port accepts connections. The key is not checked. |
| RelayDock could not find the server | The host name does not exist. Check the Server URL. |
| The server refused the connection | The server is online but nothing listens on that port. |
| The server did not answer | No reply in time. A firewall or your network may block the port. |

It cannot check the key. Only a real start can.

## The stream stutters or drops frames

Look at the Performance panel in the dock while you stream.

| Reading | Meaning | What helps |
| --- | --- | --- |
| Dropped frames is high | Your upload cannot carry the streams. | Lower the bitrate, stop a destination, or use a wired connection. Enter your upload speed under Settings, Network so RelayDock warns you beforehand. |
| Encoder lag is high | The encoder cannot keep up. | Use a hardware encoder, a faster preset, a lower resolution or frame rate, or Potato Mode. Make destinations share one encoder (Balanced or Potato mode, fewer locks). |
| Rendering lag is high | The graphics chip is too busy to draw every frame. | Cap your game's frame rate, lower the OBS canvas or frame rate, close other programs that use the graphics chip. |

Rendering lag is normal for the first seconds after OBS starts.

Automatic optimisation can make these changes for you. See [performance.md](performance.md).

## My game lags when I stream

1. Switch to Potato Mode in the Performance panel.
2. Tick Prevent Game Lag.
3. Unlock resolution, frame rate and bitrate on your destinations, so they all share one encoder.
4. If your PC has a hardware encoder (NVIDIA NVENC, AMD AMF, Intel Quick Sync), set Encoder to Automatic.

RelayDock cannot make a game run well on hardware that cannot run it. It can only keep its own load small.

## A destination keeps reconnecting

RelayDock retries with a growing wait: your first wait, then one and a half times longer each time, up to 15 minutes, for as many attempts as the destination's Advanced settings allow.

- All destinations reconnect together: your internet connection.
- One destination reconnects: that platform, or its key was used elsewhere. Most platforms accept one connection per key.

Reconnect in the card's menu drops the connection and starts again at once.

## Destinations do not share an encoder

They share only when their video settings match exactly. Settings, Streaming shows the plan. Common reasons for a split:

- a locked resolution, frame rate, bitrate or encoder on one of them,
- one is vertical and one horizontal,
- Quality mode, where platforms with different limits get different bitrates,
- Custom mode with different saved values.

## Windows could not save the stream key

RelayDock tells you when Windows Credential Manager refuses a key. It then keeps the key in memory until OBS closes, so you can stream now.

Open Control Panel, Credential Manager and check that you can add a Windows credential by hand. A full or damaged credential store, or a policy on a managed PC, can block it.

## The vertical preview is empty

An item whose source no longer exists in OBS stays empty. The layout editor names the missing source. Remove the item, or add a source with that name to OBS.

## Copy Key does nothing

When another program holds the clipboard, or a policy blocks it, Windows refuses the copy. RelayDock then says so and copies nothing. Close clipboard tools and try again.

## Get help

1. Open Settings, Diagnostics and choose Save report. The report contains no stream key and no password. Read it before you share it.
2. Open an issue with the bug report template and attach the report.

Never post a stream key. For a security problem, follow `SECURITY.md` and do not open a public issue.
