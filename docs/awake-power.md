# Awake power (X4 Pro)

Between page turns, with the frontlight off, the reader used to idle awake at 80 MHz, drawing
about 28 mA. On the X4 Pro the idle loop now light-sleeps in short slices instead, like the stock
firmware's "Power saving" option. This note covers what the code does and why each guard exists.

## Idle light sleep

After `HalPowerManager::idlePowerSavingMs()` with no input, the main loop drops the CPU to 80 MHz.
That is 1 s on the X4 Pro and 3 s on the other boards, whose 10 MHz step also changes APB. On the
X4 Pro, each idle pass then calls `HalPowerManager::lightSleep()` while holding the RenderLock. That
function naps for one `LIGHT_SLEEP_SLICE_MS` (50 ms) timer slice. The slice keeps the touch panel on
the same 50 ms poll it always had, since the GT911 has no wake line yet. A page key (GPIO0 or GPIO7),
the power key (GPIO3) or the charger STAT line going active (GPIO21) ends a nap early. After a key or
STAT wake the chip stays awake for 3 s, so a second tap lands on a running loop and a USB host has
time to enumerate.

The loop only naps when none of the guards in `lib/hal/PowerPolicy.h` applies. They are checked in
this order, and the first one that applies is what STATE `lsblk=` reports:

| `lsblk` | Why it blocks |
|---|---|
| `off` | bench `LS off` (RAM only, until the next boot) |
| `board` | not an X4 Pro |
| `lock` | a `HalPowerManager::Lock` is held (a render, deep-sleep prep) |
| `radio` | the radio lock is held, or Wi-Fi is up by any route |
| `host` | a computer on USB: the USB pad powers down in light sleep |
| `charging` | charger STAT active |
| `light` | the frontlight is lit: LEDC stops in light sleep |
| `input` | a key or touch contact is down, or a debounce is in progress |
| `activity` | the activity asks for `preventAutoSleep()` or `skipLoopDelay()` |
| `postwake` | inside the 3 s after a key or STAT wake |
| `render` | a render is requested, queued, or taken but not yet drawn |
| `refused` | every guard passed, but `esp_light_sleep_start()` returned an error |

After each nap the code:

- disarms every wake source;
- hands the RTC_PERIPH power domain back to AUTO, because arming a GPIO wake pins that domain ON,
  and the deep sleep must keep the configuration its overnight drain was measured with;
- counts the nap for the ledger.

## Radios and the clock

Nick's rule, learned on the WiPhone, is that a radio never runs under a lowered clock. Every radio
start goes through `src/network/RadioPower.cpp`, which takes the radio lock *before* the radio comes
up. That lock:

- puts the CPU at full clock, even over an 80 MHz refresh wait already in progress;
- stops the idle downclock and the light sleep.

The lock is released only in `releaseIfOff()`, once `WiFi.getMode()` reads `WIFI_MODE_NULL`. That
happens after `WIFI_OFF`, after `RadioPower::off()`, or after a start that failed before the driver
came up. `RadioPower::stop()` turns only the RF off for a result screen. The driver stays
initialised, so the lock stays held until the activity's exit restart. Every network activity's exit
restarts the reader if a radio ran in that boot (`RadioPower::ranThisBoot()`), so Wi-Fi is never
left idling in the background. Nothing starts a radio at boot.

`scripts/check_radio_power.py` is also the ctest `check_radio_power`. It fails in any of these
cases:

- a radio start appears outside RadioPower;
- a clock change or light sleep appears outside `HalPowerManager.cpp`;
- a guard stops gating the action it protects.

It proves it can still catch these by removing each guard from the real source, one at a time.

## E-paper refresh wait

The CPU runs at 80 MHz through the panel's BUSY wait, through the `EpdBus` busy-wait hooks that
`HalDisplay::begin()` installs on the X4 Pro. APB stays at 80 MHz, so SPI, I2C and LEDC timing are
unchanged. The wait keeps full clock while a radio is up.

## Measuring it

`/sleep.log` gets a `pwr` line every 5 minutes while the reader is awake. The fields are listed in
`src/util/PowerLedger.h`. `fl`, `wifi`, `usb` and `host` cover the whole window, so a window counts
as dark or unplugged only if it was dark or unplugged all the way through. `mhz` is a snapshot taken
when the line is written. STATE reports `ls lsn lsw lsms lsblk mhz radio`.

## Not done yet

- Waking on the GT911 INT line (GPIO10): its polarity during sleep is unproven.
- Light sleep with the frontlight on: it needs LEDC on the XTAL clock, proven flicker-free first.
- Panel power-off (POF) after each page.
- A USB host plugged in while the battery is full raises no STAT edge. It enumerates on the next key
  press, which opens the 3 s window. A host that suspends looks unplugged after 2 s.
