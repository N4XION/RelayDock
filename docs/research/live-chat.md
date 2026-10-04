# Live chat research

Access date for every source: 2026-10-04. Platforms change their interfaces and terms without notice. Check every row again before you build on it.

The question: can RelayDock show the comments of every platform in one merged timeline, and stay inside its own rules? Those rules are: only interfaces the platform publishes, no cost for the user, no RelayDock server, and no secret in the source code.

RelayDock cannot read chat today. This page records what each platform allows. It is not a plan.

How to read the tables:

- official: the platform's own developer docs, help pages or legal pages.
- unofficial: anything else, such as a forum post, an archive copy or another project's page.
- not verified: the pages checked do not answer the point. This file does not guess.

## Result

| Platform | Published interface for live chat | Sign-in without a secret in the source | Review by the platform | Result |
| --- | --- | --- | --- | --- |
| Twitch | Yes, EventSub over WebSocket | Yes, the device code flow | None | Possible. Each user signs in once, and again after 30 days without use. |
| YouTube | Yes, polling or a streaming connection | Yes, an API key or a browser sign-in with PKCE | Verification once an app has more than 100 users | Possible only when each user brings an own API key or an own Google Cloud project. YouTube's policy bans credentials in open-source projects. |
| Facebook | Yes, polling or server-sent events | Yes, the device login with a client token | App Review and Business Verification for anyone without a role on the app | Possible only when each user creates an own Meta app and keeps it in development mode. |
| TikTok | No | No | No route | Not possible. The only way known breaks TikTok's terms and depends on a paid third party. |

All four in one timeline is not possible inside RelayDock's rules. Twitch and YouTube are.

## Twitch

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| Interface | The EventSub subscription type `channel.chat.message`, version 1, over a WebSocket at `wss://eventsub.wss.twitch.tv/ws`. | https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/#channelchatmessage and https://dev.twitch.tv/docs/eventsub/handling-websocket-events/ | official |
| IRC | Still listed as active, with no shutdown date. Only IRC over a WebSocket without TLS ended, on 2025-08-15. | https://dev.twitch.tv/docs/chat/irc-migration/ and https://dev.twitch.tv/docs/product-lifecycle/ | official |
| Authorisation | A user access token with the scope `user:read:chat`. The scopes `user:bot` and `channel:bot` matter only for app access tokens, and those do not work for WebSocket subscriptions. | https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/#channelchatmessage and https://dev.twitch.tv/docs/eventsub/manage-subscriptions/ | official |
| Sign-in | The device code grant flow: `https://id.twitch.tv/oauth2/device`, then `https://id.twitch.tv/oauth2/token`. It supports public clients, which have no client secret. An access token lasts 4 hours. A refresh token works once and expires after 30 days without use. Twitch suggests the public client type for programs that run on the user's PC. A public client gets no other flow. | https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/ | official |
| Setup | Register an application in the developer console, with an account that has two-factor authentication. There is no review. A client id is public. The page states no fee. | https://dev.twitch.tv/docs/authentication/register-app/ | official |
| Limits | Per user token: 3 WebSocket connections, 300 subscriptions on each, and a total cost of 10. A subscription the user authorised costs 0. Subscribe within 10 seconds of the welcome message. A dropped connection replays nothing. | https://dev.twitch.tv/docs/eventsub/manage-subscriptions/ and https://dev.twitch.tv/docs/eventsub/handling-websocket-events/ | official |
| Reading without sign-in | An anonymous IRC login with the name `justinfan` followed by digits works, according to posts on Twitch's developer forum from 2016 and 2019. Twitch's IRC guide does not mention it. | https://discuss.dev.twitch.com/t/possible-to-connect-to-twitch-irc-anonymously/5921 and https://discuss.dev.twitch.com/t/anonymous-connection-to-twitch-chat/20392 | unofficial |

## YouTube

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| Interface | `liveChatMessages.list` polls over HTTPS and returns `pollingIntervalMillis`, the wait before the next request. `liveChatMessages.streamList` holds a server-streaming gRPC connection to `youtube.googleapis.com:443` and resumes with `nextPageToken`. | https://developers.google.com/youtube/v3/live/docs/liveChatMessages/list and https://developers.google.com/youtube/v3/live/docs/liveChatMessages/streamList | official |
| Authorisation | The streaming guide accepts an OAuth 2.0 token or an API key. The chat id comes from `videos.list`, field `liveStreamingDetails.activeLiveChatId`. So an API key and the video id read the chat of a public broadcast without a sign-in. With OAuth, `liveBroadcasts.list` with `mine=true` finds the user's own broadcast under the scope `youtube.readonly`. | https://developers.google.com/youtube/v3/live/streaming-live-chat and https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/list | official |
| Sign-in from a desktop program | A redirect to `http://127.0.0.1` on a free port, with PKCE. Google says it does not treat the client secret of an installed app as a secret. | https://developers.google.com/youtube/v3/guides/auth/installed-apps and https://developers.google.com/identity/protocols/oauth2/native-app | official |
| Credentials in open source | The YouTube API Services Developer Policies, section III.D, list what an API client must not do with its credentials. The list ends with "or embed your API Credentials in open source projects". The same section requires one API project per API client and bans undocumented interfaces and scraping. | https://developers.google.com/youtube/terms/developer-policies | official |
| How OBS Studio handles it | OBS takes its YouTube client id and secret as build variables. They are not in its source tree. | https://raw.githubusercontent.com/obsproject/obs-studio/master/frontend/cmake/feature-youtube.cmake | official, the OBS source |
| Does a build variable, or one project per user, satisfy the policy | The policy pages do not say. | Checked: https://developers.google.com/youtube/terms/developer-policies | not verified |
| Quota | 10,000 units per project per day, reset at midnight Pacific Time. The cost table lists `liveChatMessages.list`, `videos.list` and `liveBroadcasts.list` at 1 unit each. `streamList` is not in the table, and its own page gives no cost. More quota needs a compliance audit. The page states no fee and no duration for it. | https://developers.google.com/youtube/v3/determine_quota_cost and https://developers.google.com/youtube/v3/guides/quota_and_compliance_audits | official |
| Quota, other reports | Two open-source chat tools assume 5 units per list call or per streaming connection. That conflicts with the cost table. | https://github.com/Milzstream/OBS-Multi-Chat/issues/6 and https://github.com/Voidscape-Development/Social-Feed/pull/2 | unofficial |
| What the quota allows | At 1 unit per request and one request every 5 seconds, one stream uses 720 units an hour. A project's daily quota then lasts about 14 hours of streaming. One shared project would give all users together those 14 hours. An own project gives each user 14 hours alone. | Calculated from the row above | calculated |
| Verification | An unverified app has a cap of 100 users and shows a warning screen. In Testing status its refresh tokens last 7 days. Verification needs a homepage, a privacy policy and a verified domain, and typically takes 3 to 5 business days. | https://developers.google.com/identity/protocols/oauth2/production-readiness/sensitive-scope-verification and https://support.google.com/cloud/answer/15549945 | official |
| Is `youtube.readonly` a sensitive scope | Third-party sources say so. Google's scope list was not checked. | No official page checked | not verified |

## Facebook

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| Interface | Poll `GET /{live-video-id}/comments`, or open server-sent events on `GET https://streaming-graph.facebook.com/{live-video-id}/live_comments`. The page was updated on 2 July 2026. | https://developers.facebook.com/documentation/live-video-api/interact-with-viewers and https://developers.facebook.com/docs/graph-api/reference/live-video/comments | official |
| Rate of the event stream | The dedicated reference page now redirects to the overview. An archive copy from 2023 lists the `comment_rate` values `one_per_two_seconds`, `ten_per_second` and `one_hundred_per_second`. | https://web.archive.org/web/20230905065405/https://developers.facebook.com/docs/graph-api/server-sent-events/endpoints/live-comments/ | unofficial, an archive copy |
| Profile or Page | The archive copy says a video on a profile or in a group needs a user token with `user_videos`, and a video on a Page needs a Page token. | Same archive copy | unofficial, an archive copy |
| Permissions for a Page | `pages_read_user_content` reads the comments of users on a Page. It depends on `pages_show_list`. | https://developers.facebook.com/documentation/development/permissions | official |
| Names of commenters on a personal profile video | The pages checked do not say whether they come back. | Checked: the permissions reference and the Live Video API pages | not verified |
| Sign-in without a secret | Facebook Login for Devices sends the app id and the client token to `/device/login` and `/device/login_status`. Tokens last up to 60 days. Meta says a client token is not a secret. | https://developers.facebook.com/documentation/facebook-login/for-devices and https://developers.facebook.com/documentation/facebook-login/guides/access-tokens | official |
| Page permissions through the device login | The page limits the device login to permissions approved in Login Review. Whether Page permissions qualify is not stated. | Checked: https://developers.facebook.com/documentation/facebook-login/for-devices | not verified |
| The other desktop flow | An embedded web view with `response_type=token` and the redirect `https://www.facebook.com/connect/login_success.html`. Exchanging a code for a token needs the app secret. There is no PKCE. | https://developers.facebook.com/documentation/facebook-login/guides/advanced/manual-flow | official |
| Development mode | Only people with a role on the app can grant it permissions, and they can grant any permission without review. Everyone else needs Advanced Access, which takes App Review and Business Verification of a business. The pages state no fee and no duration. | https://developers.facebook.com/documentation/development/build-and-test/app-modes and https://developers.facebook.com/docs/graph-api/overview/access-levels/ and https://developers.facebook.com/documentation/development/release/business-verification | official |
| Limits | 200 calls per hour for an app, times the number of its users. | https://developers.facebook.com/docs/graph-api/overview/rate-limiting | official |
| Reading without sign-in | No published route. Meta's terms, section 3.2, ban automated collection without permission. | https://www.facebook.com/legal/terms | official |

## TikTok

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| Published interface | None. TikTok for Developers lists Login Kit, Share Kit, Content Posting API, Display API, Research API, Embed Videos, Data Portability API, Green Screen Kit, Commercial Content API and TikTok Minis. Neither the product page nor the navigation of the docs mentions live streams. No scope covers live streams or comments. | https://developers.tiktok.com/ and https://developers.tiktok.com/docs/en/welcome and https://developers.tiktok.com/doc/tiktok-api-scopes | official |
| Other software with TikTok chat | Streamlabs Desktop shows TikTok chat under a commercial partnership. Meld Studio added TikTok chat in April 2026 and does not name the interface. | https://streamlabs.com/platforms/tiktok and https://meldstudio.co/blog/tiktok-chat-is-now-in-meld-studio-2/ | unofficial |
| How to apply for that access | TikTok publishes no way. | Checked: https://developers.tiktok.com/ | not verified |
| The unofficial route | TikTok-Live-Connector, a Node.js library under a modified AGPL licence, reads TikTok's internal Webcast service. Its README calls the project reverse engineering and not ready for production, and warns that TikTok can change the protocol without notice. A third party, Euler Stream, signs every connection. | https://github.com/zerodytrash/TikTok-Live-Connector | unofficial |
| Cost of the signing service | The free tier allows 2,500 requests a day. The next tier costs US$50 a month. The library's README says not to ship an API key in a desktop program and to issue tokens from a server of your own. | https://www.eulerstream.com/pricing and https://github.com/zerodytrash/TikTok-Live-Connector | unofficial, the vendor's page |
| TikTok's terms | They ban automated scripts that collect information, and they ban reverse engineering. The Developer Terms allow automated access only as documented. | https://www.tiktok.com/legal/page/row/terms-of-service/en and https://www.tiktok.com/legal/page/us/terms-of-service/en and https://www.tiktok.com/legal/page/global/tik-tok-developer-terms-of-service/en | official |

The unofficial route breaks three of RelayDock's rules at once: it is not a published interface, it needs a server or a paid service, and it breaks the platform's terms.

## Gifts and paid events

A gift, a paid message or a new subscription is an event next to the comments. This table says where a published interface reports them.

| Platform | Interface | What it reports | Source URL | Official? |
| --- | --- | --- | --- | --- |
| Twitch | EventSub `channel.chat.notification`, version 1, scope `user:read:chat` | Events that appear in chat. Its `notice_type` values include `sub`, `resub`, `sub_gift`, `community_sub_gift`, `gift_paid_upgrade`, `prime_paid_upgrade`, `pay_it_forward`, `raid`, `announcement`, `bits_badge_tier` and `charity_donation`. | https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/ | official |
| Twitch | EventSub `channel.cheer` and `channel.bits.use`, version 1, scope `bits:read` | Bits. | https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/ | official |
| Twitch | EventSub `channel.subscribe`, `channel.subscription.gift` and `channel.subscription.message`, version 1, scope `channel:read:subscriptions` | New subscriptions, gift subscriptions and resubscription messages. | https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/ | official |
| YouTube | The chat feed itself. The field `snippet.type` names the event. | `superChatEvent`, `superStickerEvent`, `newSponsorEvent`, `memberMilestoneChatEvent`, `membershipGiftingEvent`, `giftMembershipReceivedEvent`, and `giftEvent` for a gift paid with Jewels. The details carry the amount and the currency, the membership level, or the name of the gift and its number of Jewels. | https://developers.google.com/youtube/v3/live/docs/liveChatMessages | official |
| Facebook | None found for Stars | The Live Video API page documents comments and reactions only, each with a polling address and a streaming address. A search of developers.facebook.com found no interface that reports Stars. | https://developers.facebook.com/documentation/live-video-api/interact-with-viewers | not verified |
| TikTok | None | Gifts travel in the same internal service as the comments. Only the unofficial route reads them. | See the TikTok table above | official |

So gift events are available where the chat is: on Twitch and YouTube, through the same sign-in or key. Twitch needs the two extra scopes for Bits and subscriptions.

## Existing tools

| Tool | Licence | How it reads chat | Source URL |
| --- | --- | --- | --- |
| Social Stream Ninja | GPL-3.0 | A browser extension, or a desktop app, reads the chat pages you have open. The page `dock.html?session=ID` loads as an OBS dock. Messages pass through `wss://io.socialstream.ninja`. The desktop app can use a relay on your own PC instead, at `ws://127.0.0.1:3003`. For TikTok it captures the page or uses a WebSocket mode that needs signing. | https://github.com/steveseguin/social_stream and https://raw.githubusercontent.com/steveseguin/social_stream/main/api.md and https://socialstream.ninja/docs/guides.html |
| Casterlabs Caffeinated | Free to use, all rights reserved | Twitch, YouTube, Kick and TikTok. No Facebook. Its widgets run as OBS browser sources. It does not say how it reads TikTok. | https://github.com/Casterlabs/caffeinated and https://casterlabs.co/ |
| AxelChat | Free to use, proprietary, with open-source widgets | Lists Facebook and TikTok. Offers OBS widgets and a WebSocket connection. It does not say how it reads them. | https://github.com/3dproger/AxelChat |

Reading a Facebook or TikTok chat page with a script runs against the terms in the tables above. A person who uses such a tool decides that for their own accounts.

## What OBS Studio offers

| Fact | Value | Source URL | Official? |
| --- | --- | --- | --- |
| Custom browser docks | Since OBS Studio 24.0, a dock can show any web address. A platform's own pop-out chat page works there, one dock per platform, with nothing merged. | https://github.com/obsproject/obs-studio/releases/tag/24.0.0 | official |
| A browser inside a plugin's dock | The obs-browser plugin exposes `QCef::create_widget` through `obs_browser_init_panel()` in `panel/browser-panel.hpp`. `obs_frontend_add_dock_by_id`, since OBS Studio 30.0, docks a widget. The header is internal. The frontend API reference does not mention it. | https://raw.githubusercontent.com/obsproject/obs-browser/master/panel/browser-panel.hpp and https://docs.obsproject.com/reference-frontend-api | official, an internal header |

## Pages that did not open

- The text of the Twitch Developer Services Agreement. The page returned its navigation only.
- The Streamlabs support article on TikTok chat. HTTP 403.
- `obsproject.com/kb/custom-browser-docks`. HTTP 404.
- Meta's current reference page for `live_comments`. It redirects to the overview.
