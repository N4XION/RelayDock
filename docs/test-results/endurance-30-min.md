# Endurance run, 30 minutes

Run on 2026-10-04 with `tests/integration/Test-Endurance.ps1 -Minutes 30`. Every number is measured.

- RelayDock build 1.0.0-rc.1+9.54512d94d
- PC: AMD Ryzen 3 5300U with Radeon Graphics, 8 threads
- OBS Studio 32.2.2, canvas 1280x720 at 30 FPS, scrolling random noise
- Three destinations to a test server on the same PC: two horizontal ones on one shared encoder, one vertical one

| Destination | Size | Live | Video received | Frames dropped | Reconnects | Sessions at the server |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Sink a | 1280x720 | 1801 s | 645 MB | 0 of 54053 | 0 | 1 |
| Sink b | 1280x720 | 1801 s | 645 MB | 0 of 54053 | 0 | 1 |
| Sink tall | 1080x1920 | 1801 s | 968 MB | 0 of 54051 | 0 | 1 |

- OBS memory: 145 MB at minute 5, 138 MB at the end, peak 187 MB (-4.9 percent change)
- OBS processor use: 2.0 percent on average (2.0 in the first half, 2.0 in the second)
- Rendering lag 0.00 percent, encoder lag 0.00 percent of all frames
- Longest stall of the OBS interface: 437 ms
- OBS exit: code 0, crash reports 0, memory leaks reported 1 (OBS reports 1 without any plugin)
- Checks: 28 of 28 passed
