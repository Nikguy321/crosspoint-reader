# Testing and Debugging

CrossPoint runs on real hardware, so debugging usually combines local build checks and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Flash and monitor

Flash firmware:

```sh
pio run --target upload
```

Open serial monitor:

```sh
pio device monitor
```

Optional enhanced monitor:

```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
```

### Bench console (x4pro dev builds)

The `x4pro` environment builds with `-DCROSSPOINT_BENCH_CONSOLE=1`, a console
on the USB serial port that lets a computer press keys, touch the screen, take
screenshots and add books to the SD card. Release environments leave it out.

```sh
python3 scripts/x4bench.py state
python3 scripts/x4bench.py key down
python3 scripts/x4bench.py tap 240 400
python3 scripts/x4bench.py shot /tmp/screen.png
python3 scripts/x4bench.py push ~/books /Books
python3 scripts/x4bench.py sleep
```

- Uploads are add-only: there is no overwrite, delete or rename verb.
- While a computer is attached (USB SOF frames, not just a charger), the
  inactivity auto-sleep waits. The power button still sleeps the reader, and a
  sleeping reader cannot be woken over USB.
- `KEY power` refuses a press that would sleep the reader unless `force` is given.
- Close any other serial monitor first: the tool opens the port exclusively.
- `python3 scripts/x4bench.py --help` lists every verb and key name. Self-test:
  `python3 scripts/x4bench_selftest.py`.
- `python3 scripts/x4bench.py card <name> --shot /tmp/card.png` shows a sleep-screen card
  exactly as the sleep screen draws it, without sleeping, and screenshots it; the next key
  or tap returns. See [Sleep screen cards](../sleep-screen-cards.md).

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#7-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)
