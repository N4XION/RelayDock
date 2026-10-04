# Third-party licences

RelayDock is free software under the GNU General Public License, version 2 or later. See `LICENSE`.

This file lists the software RelayDock contains, builds on or is built with, and the licence of each.

## Included in the RelayDock plugin

### Lucide icons

Version 1.51.0. https://lucide.dev

Used for the icons in RelayDock's interface. The files are in `resources/icons`. Six of them (`ui-chevron-down`, `ui-chevron-up` and `ui-check`, each in a light and a dark variant) are Lucide icons with a fixed stroke colour.

```
ISC License

Copyright (c) 2026 Lucide Icons and Contributors

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

---

The following Lucide icons are derived from the Feather project:

airplay, alert-circle, alert-octagon, alert-triangle, aperture, arrow-down-circle, arrow-down-left, arrow-down-right, arrow-down, arrow-left-circle, arrow-left, arrow-right-circle, arrow-right, arrow-up-circle, arrow-up-left, arrow-up-right, arrow-up, at-sign, calendar, cast, check, chevron-down, chevron-left, chevron-right, chevron-up, chevrons-down, chevrons-left, chevrons-right, chevrons-up, circle, clipboard, clock, code, columns, command, compass, corner-down-left, corner-down-right, corner-left-down, corner-left-up, corner-right-down, corner-right-up, corner-up-left, corner-up-right, crosshair, database, divide-circle, divide-square, dollar-sign, download, external-link, feather, frown, hash, headphones, help-circle, info, italic, key, layout, life-buoy, link-2, link, loader, lock, log-in, log-out, maximize, meh, minimize, minimize-2, minus-circle, minus-square, minus, monitor, moon, more-horizontal, more-vertical, move, music, navigation-2, navigation, octagon, pause-circle, percent, plus-circle, plus-square, plus, power, radio, rss, search, server, share, shopping-bag, sidebar, smartphone, smile, square, table-2, tablet, target, terminal, trash-2, trash, triangle, tv, type, upload, x-circle, x-octagon, x-square, x, zoom-in, zoom-out

The MIT License (MIT) (for the icons listed above)

Copyright (c) 2013-present Cole Bemis

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Platform logos from Simple Icons

https://simpleicons.org

Used for the badges of Twitch, YouTube, TikTok and Facebook. The files are in `resources/brands`. Simple Icons releases its collection under CC0 1.0 Universal. The logos themselves are trademarks of their owners.

```
Platform logos
==============

The four files in this folder are the logos of Twitch, YouTube, TikTok and Facebook.

Where they come from
  Simple Icons, https://simpleicons.org, files icons/twitch.svg, icons/youtube.svg,
  icons/tiktok.svg and icons/facebook.svg, fetched on 2026-10-05.
  Simple Icons releases its collection under CC0 1.0 Universal.
  RelayDock added fill="currentColor" to each file, so it can draw a logo in its platform's
  colour. Nothing else was changed.

Whose they are
  The logos are trademarks of their owners. A licence for the drawing is not a licence for
  the trademark.
    Twitch and the Glitch logo      Twitch Interactive, Inc.
    YouTube and the YouTube icon    Google LLC
    TikTok and the TikTok logo      ByteDance Ltd. or its affiliates
    Facebook and the "f" logo       Meta Platforms, Inc.

How RelayDock uses them
  Only to mark which platform a destination or a chat message belongs to, at the size of a
  small badge, in the colours the owners use themselves:
    Twitch      white on Twitch purple #9146FF
    YouTube     red #FF0000 on white
    TikTok      white on black
    Facebook    blue #0866FF on white
  RelayDock is not affiliated with, endorsed by or sponsored by any of these companies.

The owners' rules
  Twitch      https://brand.twitch.com
  YouTube     https://brand.youtube
  Facebook    https://www.meta.com/brand/resources/facebook/logo/
  TikTok      TikTok's terms ask for its written permission before its logo is used.
              The RelayDock project has not obtained one.

Building RelayDock without a logo
  Delete the file here and its line in resources/relaydock.qrc. The badge then shows the
  platform's initials, as it does for a custom server.
```

### JSON for Modern C++ (nlohmann/json)

Version 3.12.0. https://github.com/nlohmann/json

Used to read and write RelayDock's settings. The files are in `third_party/nlohmann`.

```
MIT License 

Copyright (c) 2013-2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### OBS plugin template build files

https://github.com/obsproject/obs-plugintemplate

RelayDock's CMake files under `cmake/` are adapted from the OBS Project's plugin template. Copyright (C) the OBS Project and its contributors. GNU General Public License, version 2 or later, the same licence as RelayDock. See `LICENSE`.

## Used by RelayDock, not included in it

### OBS Studio

https://obsproject.com

RelayDock is a plugin for OBS Studio and links to its `libobs` and `obs-frontend-api` libraries. Copyright (C) the OBS Project and its contributors. GNU General Public License, version 2 or later. OBS Studio is installed separately. No part of it is in the RelayDock download.

### Qt 6

https://www.qt.io

RelayDock uses the Qt libraries that OBS Studio ships and loads. Copyright (C) The Qt Company Ltd and other contributors. Available under the GNU Lesser General Public License version 3 and the GNU General Public License. No Qt library is in the RelayDock download.

## Used to build and test RelayDock, not included in the plugin

### doctest

Version 2.5.3. https://github.com/doctest/doctest

Used for RelayDock's automated tests. The files are in `third_party/doctest`.

```
The MIT License (MIT)

Copyright (c) 2016-2023 Viktor Kirilov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Inno Setup

https://jrsoftware.org/isinfo.php

Used to build the Windows installer. The installer contains Inno Setup's setup program. Copyright (C) Jordan Russell and Martijn Laan. Inno Setup License: https://jrsoftware.org/files/is/license.txt

## Trademarks

Twitch, TikTok, YouTube, Facebook and OBS Studio, and their logos, are trademarks of their owners. RelayDock uses the names and the logos to mark which service a destination or a chat message belongs to. RelayDock is not affiliated with their owners.
