# Streaming Disclaimer

Version 1.0. Last updated 4 October 2026.

This document is a draft written by the RelayDock contributors. No lawyer has reviewed it. Have it reviewed by a qualified lawyer before you rely on it for commercial use.

## 1. Live streaming can fail

A live stream depends on your computer, your encoder, your network, your internet provider and the platform, all at the same moment. Any of them can fail. RelayDock cannot prevent a stream from dropping frames, losing quality, disconnecting or ending. Do not rely on RelayDock alone for a broadcast where a failure would cause serious loss.

## 2. Streaming to several platforms multiplies the load

Each destination needs its own upload. Three destinations at 6000 Kbps need about 18 Mbps of upload plus a margin. RelayDock estimates this and warns you when it exceeds the upload speed you entered, but it does not measure your connection.

Each separate video encode uses your processor or graphics chip. RelayDock shares one encode between destinations whose settings match exactly. Destinations with different settings need their own encode, which can slow down your game or cause OBS to miss frames.

## 3. What automatic optimisation does

Automatic optimisation follows fixed rules. It is not artificial intelligence. When a measurement such as dropped frames or encoder lag stays high, it proposes a lower bitrate, frame rate or resolution. In Suggest mode you decide. In Automatic mode RelayDock applies changes that need no reconnect, and applies changes that briefly reconnect a destination only if you allowed that. A change lowers the quality of the affected streams. RelayDock never changes a setting you locked.

Prevent Game Lag makes those rules react earlier and restore quality later. It reduces the load RelayDock adds. It cannot make a game run well on hardware that cannot run it.

## 4. Tests and previews

Test connection checks that a server answers. It cannot check your stream key, because a platform checks the key only when a stream starts. On Twitch, Test stream sends a real stream that Twitch does not show to viewers. Other platforms may show your stream to viewers as soon as it connects. Use the platform's own private or test mode when you want to try a stream without an audience.

## 5. Your content

You are responsible for everything you stream: video, audio, music, chat overlays, alerts and anything visible on your screen. Make sure you have the right to stream it and that no private information is visible. RelayDock sends what OBS produces and does not review it.

## 6. Recordings and delay

RelayDock sends a live stream. It does not record. A platform may publish, record or archive your stream under its own rules. A stream delay you set in RelayDock applies to that destination only and does not remove content that was already sent.

## 7. Stopping a stream

Stopping a destination in RelayDock ends the connection from your computer. Some platforms keep the broadcast open for a while or until you end it on the platform itself. Check the platform to confirm that your broadcast has ended.

## 8. Costs

RelayDock is free. Streaming uses large amounts of data. Your internet provider may charge for data or limit it. Those costs are yours.
