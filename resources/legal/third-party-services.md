# Third-Party Services Notice

Version 1.3. Last updated 5 October 2026.

This document is a draft written by the RelayDock contributors. No lawyer has reviewed it. Have it reviewed by a qualified lawyer before you rely on it for commercial use.

## 1. RelayDock is independent

RelayDock is not affiliated with, endorsed by or sponsored by Twitch, TikTok, YouTube, Google, Facebook, Meta, the OBS Project or any other company. Twitch, TikTok, YouTube, Facebook and OBS Studio are names and trademarks of their owners. RelayDock uses those names, and the logos of Twitch, TikTok, YouTube and Facebook, only to mark which service a destination or a chat message belongs to. The logos are trademarks of their owners as well. You can switch them off under Settings, Appearance.

## 2. You deal with each platform directly

When you stream to a platform, your stream goes from your computer to that platform. Your use of the platform is governed by that platform's terms of service, community guidelines, privacy policy and streaming rules, not by RelayDock. Read them. You are responsible for following them.

Platforms decide who may stream. They can require a minimum age, a number of followers, a verified account, a waiting period or their own approval before they give you a stream key. RelayDock cannot grant or restore that access.

## 3. Rules that matter when you stream to several platforms

Some platforms set conditions for streaming to other services at the same time. Twitch, for example, publishes simulcasting rules. On the date of this document they ask that viewers on Twitch get an experience at least as good as on other platforms, that you do not direct your community to leave Twitch during the stream, and that you do not merge other platforms' chat into your Twitch stream. Platforms change such rules. Check the current rules of every platform you stream to. An exclusivity agreement you signed with a platform can forbid simulcasting altogether.

## 4. Platforms change

Platforms change their server addresses, limits, formats and rules without telling RelayDock. A destination that works today can stop working after such a change until RelayDock is updated or you adjust your settings. The limits RelayDock applies come from each platform's public documentation, checked on the dates recorded in the project's research notes. They can be out of date.

To stream, RelayDock connects to each platform the way OBS Studio does, with a server address and a stream key over RTMP or RTMPS. It does not sign in to your platform accounts for that.

## 5. Chat

RelayDock reads chat only from platforms you set it up for under Settings, Chat, and only through interfaces those platforms publish.

- Twitch. You sign in on twitch.tv, and Twitch gives RelayDock a permission to read the chat of your channel. Twitch's terms and its developer agreement apply. You can end the permission at any time on twitch.tv under Settings, Connections.
- YouTube. RelayDock uses YouTube API Services with an API key that you create yourself in a Google Cloud project of your own. By using this feature you agree to be bound by the YouTube Terms of Service, at https://www.youtube.com/t/terms. The Google Privacy Policy, at https://policies.google.com/privacy, applies to the requests. Your key has a daily amount of requests that Google sets.
- TikTok and Facebook. RelayDock reads no chat from them. TikTok publishes no interface for it, and Facebook's needs an approval that RelayDock does not have.

The chat dock is for you, not for your viewers. Twitch's simulcasting rules, on the date of this document, do not allow showing the chat of other platforms on your Twitch stream. Do not put the dock into a scene that goes to Twitch.

## 6. Custom servers

Custom RTMP and Custom RTMPS destinations connect to a server you name. You are responsible for knowing who runs that server and what it does with your stream.

## 7. GitHub

RelayDock's source code, releases and issue tracker are hosted on GitHub. RelayDock contacts GitHub for the update check: each time OBS Studio starts, unless you switch that off under Settings, Updates, and when you click Check for updates. When you choose Update now, RelayDock also downloads the installer of the newer version from GitHub. GitHub's terms and privacy statement apply to these contacts and to your use of the project pages.

## 8. Software RelayDock builds on

RelayDock runs inside OBS Studio and uses the Qt libraries that OBS Studio provides. Their licences are listed on the Open Source Licenses page.

## 9. No responsibility for third parties

The RelayDock contributors do not control third-party services and are not responsible for their availability, their decisions about your account, their handling of your data or changes they make.
