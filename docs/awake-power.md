# Awake power (X4 Pro)

Between page turns the reader used to idle awake at 80 MHz, drawing about 28 mA. On the X4 Pro
the idle loop now light-sleeps in short slices instead, like the stock firmware's "Power saving"
option. This note covers what the code does and why each guard exists.

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
| `light` | the frontlight is lit and its PWM would stop in light sleep (never on the X4 Pro, see below) |
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

## The frontlight through a nap

The X4 Pro frontlight is two LEDC channels (cool GPIO8, warm GPIO9) at 25 kHz with 10-bit duty.
The Arduino 3.x core already clocks every LEDC timer from the 40 MHz crystal on the S3
(`esp32-hal-ledc.c`, `LEDC_DEFAULT_CLK`; the shipped image's `clock_source` is 11,
`SOC_MOD_CLK_XTAL`). The timer divisor is 40 MHz / (25 kHz x 1024) = 1.5625, exactly 25 kHz.

Light sleep powers the crystal down by default, which is what stops the PWM. While the light is
lit, `HalFrontlight` holds a reference on the IDF's "digital peripheral needs XTAL in light sleep"
sub-mode (`esp_sleep_sub_mode_config(ESP_SLEEP_DIG_USE_XTAL_MODE)`, the same call the IDF LEDC
driver makes for its own keep-alive channels). It takes the reference before the first nonzero duty
and returns it after the zero one (light off, or on at brightness 0), so dark naps keep their lower
floor. The GPIO8/9 pads keep their LEDC drive in sleep (`gpio_sleep_sel_dis`). Nothing about the
waveform changes: same clock, same divisor, same duty values (`test/frontlight_pwm` pins every level
at every warmth).

Deep sleep powers the crystal down whatever the sub-mode says, and `startDeepSleep()` still parks
both pads LOW through `ledcDetach()`. The request COUNT is another matter: the IDF keeps it in RTC
slow memory, which a deep-sleep wake does not reload (`esp_sleep_internal.h`: "kept after deep sleep
wakeup"). A lit sleep that kept its reference would leave every dark nap after the wake holding the
crystal, with nothing on STATE to show it. So `startDeepSleep()` returns the reference first
(`HalFrontlight::releaseForDeepSleep()`), and `HalFrontlight::begin()` zeroes the count on the X4 Pro
(`esp_sleep_sub_mode_force_disable`; this HAL is its only user on this image) before taking one.

Holding the crystal costs a little more per nap than a dark nap. The oscillator is the smaller part:
with the sub-mode set, the S3 sleep also keeps the digital rail at 1.1 V instead of its lower sleep
level and leaves the bias and current monitor on (`rtc_sleep.c`). GUESSED at 1-2 mA, against the
20+ mA an awake idle loop draws. At 1 % the LEDs draw almost nothing, so the ledger's lit windows
against its dark windows (mV/h) measure it.

One physical path to flicker is left. A lit reader used to draw a steady current; it now swings
between the nap floor and the awake draw at the nap rate, about 19 Hz. With a constant-current LED
driver that is invisible. If the LED current followed the battery or 3.3 V rail it would show as a
shimmer rather than a step, so the eye check looks for shimmer at 1 % and around 50 %.

`HalFrontlight::survivesLightSleep()` says whether this holds. It is false on other boards, and it
would turn false if a core ever clocked LEDC from APB; the guard then reports `light` again.

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
when the line is written. STATE reports `ls lsn lsw lsms lsblk mhz radio fls xtal flrun`:

- `fls=1`: a lit light keeps napping (`lsblk=light` only when it does not);
- `xtal`: the IDF's live light-sleep crystal request count, 1 while lit at a nonzero duty, else 0;
- `flrun=<ran>/<checked>` (dev builds): lit naps of at least 45 ms whose PWM was measured running.
  Before each lit nap the lit channel's LEDC overflow counter is reset with its limit at 1024 PWM
  periods (40.96 ms at 25 kHz); after the nap its raw flag says whether the timer kept counting. A
  frozen PWM would count only the awake entry and exit (well under 1024), so `ran` stays near 0.

On the bench, `LSFORCE <1..45>` (dev builds) lets the naps run for that many seconds even with the
bench cable attached or a charger in; every other guard still applies. The USB link drops for the
window. Afterwards the chip stays awake 5 s so the host re-enumerates. If the port does not come
back, one key press on the reader brings it back.

### Bench check for the lit naps

1. Light on at 1 %. STATE: `fls=1 xtal=1`.
2. `LSFORCE 20`, reconnect after about 25 s. STATE: `lsn` up by roughly 18 per forced second,
   `lsblk` never `light`, `flrun` ran equal to checked (and checked grown by roughly the naps).
3. Repeat at about 50 % and at a warmth of 50 % or more (1 % is then the warm channel alone).
4. Light off. STATE: `xtal=0`, `flrun` unchanged by a further `LSFORCE 10`.
5. Light on, sleep with the power button, wake, light off. STATE: `xtal=0` (the count did not
   survive the deep sleep).
6. Eye check, blind A/B at 1 % and about 50 %: forced windows (`LSFORCE 45`) against awake windows
   in an order Nick is not told, looking for a step or a shimmer. Optionally film the panel at
   240 fps slow motion in both states.
7. Real use: `pwr` lines with `fl` set and no cable should read `ls` at 850 per mille or more.

## Not done yet

- Waking on the GT911 INT line (GPIO10): its polarity during sleep is unproven.
- Light sleep with the frontlight on is built, but it still needs Nick's eye check at 1 % and
  the ledger numbers from a lit reading session.
- Panel power-off (POF) after each page.
- A USB host plugged in while the battery is full raises no STAT edge. It enumerates on the next key
  press, which opens the 3 s window. A host that suspends looks unplugged after 2 s.
