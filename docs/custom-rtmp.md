# Custom RTMP and RTMPS

Use a custom destination for any service or server that gives you an RTMP or RTMPS address and that RelayDock has no preset for: another platform, your own streaming server, or a second PC.

RelayDock is tested against its own RTMP test server. It is not tested against any particular third-party service through a custom destination.

## Set it up

1. In RelayDock choose + Add Platform, then Custom RTMP or Custom RTMPS.
2. Enter the Server URL.
3. Enter the Stream key, if the server uses one.
4. Choose Add.

The two kinds differ in one way:

| Kind | Address | Connection |
| --- | --- | --- |
| Custom RTMP | `rtmp://host/app` | Not encrypted. The stream key travels in the clear. |
| Custom RTMPS | `rtmps://host:443/app` | Encrypted with TLS. |

Use Custom RTMPS whenever the server offers it. With plain RTMP, anyone who can watch your network traffic can read the key.

## The address

A streaming address has two parts. Services often show them as one line:

```
rtmp://live.example.net/app/abcd-1234-efgh
```

Split it at the last slash:

- Server URL: `rtmp://live.example.net/app`
- Stream key: `abcd-1234-efgh`

Keep the key out of the Server URL. OBS writes server addresses to its log, and RelayDock can only protect what you enter in the Stream key field.

RelayDock accepts `rtmp://` and `rtmps://` addresses. Without a port it uses 1935 for RTMP and 443 for RTMPS.

## User name and password

Some servers ask for a login. Tick "The server asks for a user name and password" and fill in both. RelayDock saves the password in Windows Credential Manager, like a stream key.

## No key

Some servers need no key. Leave the field empty. RelayDock shows a note and starts anyway.

## Settings

A custom server has no published limits, so RelayDock applies none beyond what the performance mode picks. If your server needs specific values, set them in the destination's Video settings and lock them.

Custom destinations may use H.264, HEVC or AV1 encoders, when OBS has them and its RTMP output accepts the codec. Most servers expect H.264.

## Test

Test connection, in the editor and in the card's menu, checks that the server accepts a connection on its port. It does not check the key and does not perform the TLS handshake. Start the stream to find out whether the server accepts your key.

## When it fails

| The card says | Check |
| --- | --- |
| ... rejected the connection | The stream key and the application name at the end of the Server URL. |
| ... could not connect to the server | The host name and port in the Server URL, and that the server is running. |
| ... lost its connection | Your network and the server. RelayDock reconnects by itself with growing waits. |
