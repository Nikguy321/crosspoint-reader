#!/usr/bin/env python3
"""check_radio_power.py - every radio start goes through RadioPower, which holds the clock.

Nick's rule (the WiPhone lesson: a CPU downshift under a live radio dropped its link): when a
radio is needed, the reader brings itself up to a clock that supports it first, and neither
downclocks nor light-sleeps until the radio is off. The mechanism:

  src/network/RadioPower.cpp   the ONE owner of every radio start. Each start calls
                               lockForStart() -> powerManager.acquireRadioLock() BEFORE the
                               WiFi call; the lock is released only once the radio is off.
  lib/hal/HalPowerManager.cpp  setPowerSaving(), refreshWaitBegin() and lightSleep() all refuse
                               while the radio lock is held or Wi-Fi is up.
  lib/hal/PowerPolicy.h        the pure decisions (test/power_policy pins them).

So this fails on:
  BANNED    a radio-start spelling anywhere in src/ or lib/ except RadioPower.cpp:
            WiFi.mode(<anything but WIFI_OFF / WIFI_MODE_NULL>), WiFi.begin(, WiFi.softAP(,
            WiFi.softAPConfig(, WiFi.config(, WiFi.setDNS(, WiFi.beginSmartConfig(,
            WiFi.beginWPSConfig(, WiFiMulti, WiFi.scanNetworks(, WiFi.enableSTA(, WiFi.enableAP(,
            WiFi.reconnect(, WiFi.STA/AP.begin|create|connect(, esp_wifi_init/start/connect(,
            esp_now_init(, btStart(, esp_bt_controller_init/enable(, esp_bluedroid_enable(,
            NimBLEDevice::init(, BLEDevice::init(, and #include of the SDK's own radio users
            (NearbyTransfer, BleKeyboardHost) - they start radios behind the wrapper's back.
            A clock change or sleep (setCpuFrequencyMhz(, esp_light_sleep_start(,
            esp_pm_configure(, rtc_clk_cpu_freq_set_config() anywhere but HalPowerManager.cpp,
            and the radio lock's acquire/release anywhere but RadioPower.cpp / HalPowerManager.
  CONTRACTS where the guards must be, stated positively (deleting a guard spells nothing
            banned): every radio-start call inside RadioPower.cpp follows lockForStart() in the
            same function; lockForStart() takes the radio lock; the lock is released only in
            releaseIfOff(), only once WiFi reads off; HalPowerManager's three clock/sleep paths
            ask the radio and act on the answer (the clock drops, and the light sleep starts,
            only past the guard); every other clock change there restores the full clock;
            PowerPolicy's decisions refuse on the radio.
  MUTATIONS each of those guards removed from the real source, one at a time: the check must
            then fail (a guard deleted with the suite still green is the failure this exists for).

  AUTO-LOCATE "Update location when syncing" (src/network/AutoLocate.cpp) rides a sync's Wi-Fi
            and must never ask the internet-address lookup: the file names no ipwho.is /
            parseIpWhoisResponse / FixSource::Ip / IP_ACCURACY, and every GeolocateClient::request
            in it is passed BEACONDB_URL, which is beaconDB's geolocate endpoint. Mutated too.

Comments and string literals are blanked first. A self-test runs first, so a pattern that has
quietly stopped matching fails loudly.

    python3 scripts/check_radio_power.py
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCAN_DIRS = [ROOT / "src", ROOT / "lib"]
# Generated glyph tables (17 MB, no code): scanning them is most of the run time.
SKIP_DIRS = [ROOT / "lib" / "EpdFont" / "builtinFonts"]
OWNER = ROOT / "src" / "network" / "RadioPower.cpp"
POWER_CPP = ROOT / "lib" / "hal" / "HalPowerManager.cpp"
POWER_H = ROOT / "lib" / "hal" / "HalPowerManager.h"
POLICY_H = ROOT / "lib" / "hal" / "PowerPolicy.h"
OWNER_REL = "src/network/RadioPower.cpp"
POWER_CPP_REL = "lib/hal/HalPowerManager.cpp"
POWER_H_REL = "lib/hal/HalPowerManager.h"

OFF_ARG = r"(?:WIFI_OFF|WIFI_MODE_NULL)"
BANNED = [
    (re.compile(r"\bWiFi\s*\.\s*mode\s*\(\s*(?!" + OFF_ARG + r"\s*\))"), "WiFi.mode(<a radio-on mode>)"),
    (re.compile(r"\bWiFi\s*\.\s*begin\s*\("), "WiFi.begin()"),
    (re.compile(r"\bWiFi\s*\.\s*softAP\s*\("), "WiFi.softAP()"),
    (re.compile(r"\bWiFi\s*\.\s*softAPConfig\s*\("), "WiFi.softAPConfig() (starts the AP interface)"),
    (re.compile(r"\bWiFi\s*\.\s*(?:config|setDNS)\s*\("), "WiFi.config()/setDNS() (starts the STA interface)"),
    (re.compile(r"\bWiFi\s*\.\s*begin(?:SmartConfig|WPSConfig)\s*\("), "WiFi.beginSmartConfig()/beginWPSConfig()"),
    (re.compile(r"\bWiFiMulti\b"), "WiFiMulti (its run() starts the radio)"),
    (re.compile(r"\bWiFi\s*\.\s*scanNetworks\s*\("), "WiFi.scanNetworks()"),
    (re.compile(r"\bWiFi\s*\.\s*enable(?:STA|AP)\s*\("), "WiFi.enableSTA()/enableAP()"),
    (re.compile(r"\bWiFi\s*\.\s*reconnect\s*\("), "WiFi.reconnect()"),
    (re.compile(r"\bWiFi\s*\.\s*(?:STA|AP)\s*\.\s*(?:begin|create|connect)\s*\("), "WiFi.STA/AP start"),
    (re.compile(r"\besp_wifi_(?:init|start|connect)\s*\("), "a bare esp_wifi_init/start/connect()"),
    (re.compile(r"\besp_now_init\s*\("), "esp_now_init()"),
    (re.compile(r"\bbtStart\s*\("), "btStart()"),
    (re.compile(r"\besp_bt_controller_(?:init|enable)\s*\("), "esp_bt_controller_init/enable()"),
    (re.compile(r"\besp_bluedroid_enable\s*\("), "esp_bluedroid_enable()"),
    (re.compile(r"\b(?:NimBLEDevice|BLEDevice)\s*::\s*init\s*\("), "a BLE stack init"),
]
# Allowed only in the named files (repo-relative); banned everywhere else.
CLOCK_OWNERS = {POWER_CPP_REL}
LOCK_OWNERS = {OWNER_REL, POWER_CPP_REL, POWER_H_REL}
RESTRICTED = [
    (re.compile(r"\bsetCpuFrequencyMhz\s*\("), "setCpuFrequencyMhz() (the clock is HalPowerManager's)", CLOCK_OWNERS),
    (re.compile(r"\besp_light_sleep_start\s*\("), "esp_light_sleep_start() (HalPowerManager::lightSleep only)",
     CLOCK_OWNERS),
    (re.compile(r"\besp_pm_configure\s*\("), "esp_pm_configure() (automatic clock scaling)", CLOCK_OWNERS),
    (re.compile(r"\brtc_clk_cpu_freq_set_config(?:_fast)?\s*\("), "a raw CPU clock change", CLOCK_OWNERS),
    (re.compile(r"\b(?:acquire|release)RadioLock\s*\("), "the radio lock taken or released outside RadioPower",
     LOCK_OWNERS),
]

# Checked on the raw text (the blanking pass empties <...> include paths' neighbours, not these).
BANNED_INCLUDES = [
    (re.compile(r'#\s*include\s*[<"](?:NearbyTransfer|BleKeyboardHost)\.h[>"]'),
     "an SDK radio user (it starts its radio behind RadioPower)"),
]


_TOKENS = re.compile(
    r"//[^\n]*"
    r"|/\*.*?(?:\*/|\Z)"
    r"|(?<![\w])R\"(?P<delim>[^()\\ ]{0,16})\(.*?\)(?P=delim)\""
    r"|\"(?:\\.|[^\"\\\n])*\"?"
    r"|'(?:\\.|[^'\\\n])*'?",
    re.S,
)


def strip_code(t: str) -> str:
    """Blank comments and string/char literals, keeping offsets and line numbers."""
    return _TOKENS.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), t)


# Every BANNED / RESTRICTED spelling contains one of these; a file with none skips the regexes
# (their leading \b makes each a full scan of the file).
NEEDLES = ("WiFi", "esp_wifi_", "esp_now_init", "btStart", "esp_bt_controller_", "esp_bluedroid_enable",
           "BLEDevice", "setCpuFrequencyMhz", "esp_light_sleep_start", "esp_pm_configure",
           "rtc_clk_cpu_freq_set_config", "RadioLock")


def findings(text: str, rel: str = ""):
    """Banned spellings in one file; rel (repo-relative path) unlocks the RESTRICTED owners."""
    hits = []
    for rx, what in BANNED_INCLUDES:
        for m in rx.finditer(text):
            hits.append((text.count("\n", 0, m.start()) + 1, what))
    if not any(n in text for n in NEEDLES):
        return sorted(hits)
    code = strip_code(text)
    for rx, what in BANNED:
        for m in rx.finditer(code):
            hits.append((code.count("\n", 0, m.start()) + 1, what))
    for rx, what, owners in RESTRICTED:
        if rel in owners:
            continue
        for m in rx.finditer(code):
            hits.append((code.count("\n", 0, m.start()) + 1, what))
    return sorted(hits)


def match_close(code: str, i: int) -> int:
    open_ch = code[i]
    close_ch = ")" if open_ch == "(" else "}"
    depth = 0
    for j in range(i, len(code)):
        if code[j] == open_ch:
            depth += 1
        elif code[j] == close_ch:
            depth -= 1
            if depth == 0:
                return j
    return -1


def function_body(code: str, name: str):
    """The body text of the DEFINITION of `name`, or None (a call followed by `{` is skipped)."""
    rx = re.compile(r"(?<![\w:~.>])" + re.escape(name) + r"\s*\(")
    for m in rx.finditer(code):
        close = match_close(code, m.end() - 1)
        if close < 0:
            continue
        k = close + 1
        while k < len(code) and code[k] not in "{;":
            k += 1
        if k >= len(code) or code[k] != "{":
            continue
        between = code[close + 1:k].strip()
        if between and not re.fullmatch(r"(?:const|override|noexcept|final|\s)*", between):
            continue
        end = match_close(code, k)
        if end > 0:
            return code[k:end + 1]
    return None


CONTROL = {"if", "for", "while", "switch", "catch", "return", "sizeof", "defined"}


def function_spans(code: str):
    """(name, body_start, body_end) of every function definition in code."""
    spans = []
    for m in re.finditer(r"(?<![\w.>])([A-Za-z_][\w:]*)\s*\(", code):
        if m.group(1).split("::")[-1] in CONTROL:
            continue
        close = match_close(code, m.end() - 1)
        if close < 0:
            continue
        k = close + 1
        while k < len(code) and code[k] not in "{;=,)":
            k += 1
        if k >= len(code) or code[k] != "{":
            continue
        if not re.fullmatch(r"(?:const|override|noexcept|final|\s)*", code[close + 1:k]):
            continue
        end = match_close(code, k)
        if end > 0:
            spans.append((m.group(1), k, end + 1))
    return spans


def enclosing(spans, pos: int):
    """The innermost function span containing pos, or None."""
    best = None
    for s in spans:
        if s[1] <= pos < s[2] and (best is None or s[1] > best[1]):
            best = s
    return best


def lock_before(body: str, call_rx: str) -> bool:
    """lockForStart() is called before the first `call_rx` in body."""
    lock = re.search(r"\blockForStart\s*\(\s*\)", body)
    call = re.search(call_rx, body)
    return bool(lock and call and lock.start() < call.start())


def contract_failures(owner: str, power: str, policy: str):
    """Positive contracts; each entry is a human-readable failure."""
    fails = []
    o = strip_code(owner)
    starts = [
        ("mode", r"\bWiFi\s*\.\s*mode\s*\(\s*m\s*\)"),
        ("begin", r"\bWiFi\s*\.\s*begin\s*\("),
        ("softAP", r"\bWiFi\s*\.\s*softAP\s*\("),
        ("scanNetworks", r"\bWiFi\s*\.\s*scanNetworks\s*\("),
    ]
    for name, call in starts:
        body = function_body(o, name)
        if body is None:
            fails.append(f"RadioPower.cpp: no definition of {name}()")
        elif not lock_before(body, call):
            fails.append(f"RadioPower.cpp: {name}() must call lockForStart() before its WiFi call")
    # Every radio start in the owner, including one added later under a new name.
    spans = function_spans(o)
    for rx, what in BANNED:
        for m in rx.finditer(o):
            fn = enclosing(spans, m.start())
            line = o.count("\n", 0, m.start()) + 1
            if fn is None:
                fails.append(f"RadioPower.cpp:{line}: {what} outside any function")
            elif not re.search(r"\blockForStart\s*\(\s*\)", o[fn[1]:m.start()]):
                fails.append(f"RadioPower.cpp:{line}: {what} in {fn[0]}() without lockForStart() before it")
    body = function_body(o, "lockForStart")
    if body is None or not re.search(r"\bpowerManager\s*\.\s*acquireRadioLock\s*\(", body):
        fails.append("RadioPower.cpp: lockForStart() must call powerManager.acquireRadioLock()")
    body = function_body(o, "releaseIfOff")
    if body is None or not re.search(
            r"if\s*\(\s*WiFi\s*\.\s*getMode\s*\(\s*\)\s*==\s*WIFI_MODE_NULL\s*\)\s*"
            r"powerManager\s*\.\s*releaseRadioLock\s*\(", body):
        fails.append("RadioPower.cpp: releaseIfOff() must release only once WiFi.getMode() is WIFI_MODE_NULL")
    for m in re.finditer(r"\breleaseRadioLock\s*\(", o):
        fn = enclosing(spans, m.start())
        if fn is None or fn[0] != "releaseIfOff":
            fails.append(f"RadioPower.cpp:{o.count(chr(10), 0, m.start()) + 1}: releaseRadioLock() outside "
                         "releaseIfOff() (the lock goes only once the radio reads off)")

    p = strip_code(power)
    for fn in ("HalPowerManager::setPowerSaving", "HalPowerManager::refreshWaitBegin"):
        body = function_body(p, fn)
        if body is None or not re.search(r"\bmayDownclock\s*\(\s*\{[^}]*radioLock[^}]*WiFi\s*\.\s*getMode", body):
            fails.append(f"HalPowerManager.cpp: {fn}() must ask power_policy::mayDownclock(radio lock, WiFi mode)")
    body = function_body(p, "HalPowerManager::lightSleep")
    if body is None or not re.search(r"in\s*\.\s*radio\s*=\s*\{[^}]*radioLock[^}]*WiFi\s*\.\s*getMode", body):
        fails.append("HalPowerManager.cpp: lightSleep() must feed the radio lock and WiFi mode to the policy")
    body = function_body(p, "HalPowerManager::acquireRadioLock")
    if body is None or not re.search(r"\bsetCpuMhzLocked\s*\(\s*normalFreq\s*\)", body):
        fails.append("HalPowerManager.cpp: acquireRadioLock() must restore the full clock (setCpuMhzLocked(normalFreq))")

    # The guards must gate the action, not just be asked.
    body = function_body(p, "HalPowerManager::setPowerSaving")
    if body is not None:
        guard = re.search(r"if\s*\([^;{]*\bmayDownclock\s*\(\s*\{[^}]*\}\s*\)\s*\)\s*\{\s*enabled\s*=\s*false\s*;\s*\}",
                          body)
        drop = re.search(r"\bsetCpuFrequencyMhz\s*\(\s*LOW_POWER_FREQ\s*\)", body)
        if not guard or not drop or drop.start() < guard.end():
            fails.append("HalPowerManager.cpp: setPowerSaving() must force enabled = false on a Lock or radio "
                         "before it can drop the clock")
        if len(re.findall(r"\benabled\s*=(?!=)", body)) != 1:
            fails.append("HalPowerManager.cpp: setPowerSaving() may assign `enabled` only in its guard")
    body = function_body(p, "HalPowerManager::refreshWaitBegin")
    if body is not None:
        ask = re.search(r"\bmayDownclock\s*\(", body)
        drop = re.search(r"\bsetCpuFrequencyMhz\s*\(", body)
        if not ask or not drop or drop.start() < ask.start() or not re.search(r"\bif\s*\(", body[:ask.start()]):
            fails.append("HalPowerManager.cpp: refreshWaitBegin() must drop the clock only inside its radio check")
    body = function_body(p, "HalPowerManager::lightSleep")
    if body is not None:
        verdict = re.search(r"\bblock\s*=\s*power_policy\s*::\s*lightSleepBlock\s*\(\s*in\s*\)", body)
        gate = re.search(r"if\s*\(\s*block\s*!=\s*power_policy\s*::\s*Block\s*::\s*None\s*\)\s*return\s+false\s*;",
                         body)
        sleep = re.search(r"\besp_light_sleep_start\s*\(", body)
        if not (verdict and gate and sleep and verdict.end() <= gate.start() and gate.end() <= sleep.start()):
            fails.append("HalPowerManager.cpp: lightSleep() must return on any power_policy block before "
                         "esp_light_sleep_start()")
    pspans = function_spans(p)
    allowed_clock = {"HalPowerManager::setPowerSaving", "HalPowerManager::refreshWaitBegin",
                     "HalPowerManager::setCpuMhzLocked"}
    for m in re.finditer(r"\bsetCpuFrequencyMhz\s*\(", p):
        fn = enclosing(pspans, m.start())
        if fn is None or fn[0] not in allowed_clock:
            fails.append(f"HalPowerManager.cpp:{p.count(chr(10), 0, m.start()) + 1}: setCpuFrequencyMhz() outside "
                         "setPowerSaving/refreshWaitBegin/setCpuMhzLocked")
    for m in re.finditer(r"\besp_light_sleep_start\s*\(", p):
        fn = enclosing(pspans, m.start())
        if fn is None or fn[0] != "HalPowerManager::lightSleep":
            fails.append("HalPowerManager.cpp: esp_light_sleep_start() outside lightSleep()")
    for m in re.finditer(r"\bsetCpuMhzLocked\s*\(\s*([^)]*)\)", p):
        arg = m.group(1).strip()
        fn = enclosing(pspans, m.start())
        if fn is not None and fn[0] == "HalPowerManager::setCpuMhzLocked":
            continue
        if arg.startswith("const ") or arg.startswith("int "):
            continue  # the definition / declaration
        if arg not in ("normalFreq", "pm.normalFreq"):
            fails.append(f"HalPowerManager.cpp:{p.count(chr(10), 0, m.start()) + 1}: setCpuMhzLocked({arg}) - "
                         "only the full clock goes through it (drops ask the radio first)")

    y = strip_code(policy)
    body = function_body(y, "lightSleepBlock")
    if body is None or not re.search(r"if\s*\(\s*radioActive\s*\(\s*in\s*\.\s*radio\s*\)\s*\)\s*return\s+Block::Radio",
                                     body):
        fails.append("PowerPolicy.h: lightSleepBlock() must return Block::Radio when radioActive()")
    body = function_body(y, "mayDownclock")
    if body is None or not re.search(r"return\s*!\s*radioActive\s*\(", body):
        fails.append("PowerPolicy.h: mayDownclock() must be !radioActive()")
    body = function_body(y, "radioActive")
    if body is None or not re.search(r"radioLocked\s*\|\|\s*r\s*\.\s*wifiModeOn", body):
        fails.append("PowerPolicy.h: radioActive() must be radioLocked || wifiModeOn")
    return fails


AUTOLOCATE = ROOT / "src" / "network" / "AutoLocate.cpp"
AUTOLOCATE_IP = re.compile(r"ipwho|FixSource\s*::\s*Ip\b|IP_ACCURACY", re.I)
BEACONDB_DEF = 'constexpr const char* BEACONDB_URL = "https://api.beacondb.net/v1/geolocate";'


def autolocate_failures(text: str):
    """AutoLocate.cpp asks beaconDB only (never the internet-address lookup)."""
    rel = "src/network/AutoLocate.cpp"
    fails = []
    if AUTOLOCATE_IP.search(text):
        fails.append(f"{rel}: names the internet-address lookup (auto-locate is beaconDB only)")
    if BEACONDB_DEF not in text:
        fails.append(f"{rel}: BEACONDB_URL is not beaconDB's geolocate endpoint")
    urls = re.findall(r"GeolocateClient\s*::\s*request\s*\(\s*([^,\s)]+)", strip_code(text))
    if not urls or any(u != "BEACONDB_URL" for u in urls):
        fails.append(f"{rel}: a GeolocateClient::request not passed BEACONDB_URL ({', '.join(urls) or 'none'})")
    return fails


def self_test():
    must_hit = [
        "WiFi.mode(WIFI_STA);", "WiFi.mode (WIFI_AP);", "WiFi.mode(m);", "WiFi.begin(ssid, pw);",
        "WiFi.begin();", "WiFi.softAP(a, b);", "WiFi.scanNetworks(true);", "WiFi.enableSTA(true);",
        "WiFi.reconnect();", "WiFi.STA.begin();", "WiFi.AP.create(cfg);", "esp_wifi_start();",
        "esp_wifi_init(&cfg);", "esp_wifi_connect();", "btStart();", "esp_bt_controller_enable(m);",
        "NimBLEDevice::init(\"x\");", "esp_now_init();", "#include <NearbyTransfer.h>",
        "WiFi.softAPConfig(ip, gw, sn);", "WiFi.config(ip, gw, sn);", "WiFi.setDNS(a);",
        "WiFi.beginSmartConfig();", "WiFi.beginWPSConfig();", "WiFiMulti multi;",
        "setCpuFrequencyMhz(80);", "esp_light_sleep_start();", "esp_pm_configure(&c);",
        "powerManager.releaseRadioLock();", "powerManager.acquireRadioLock();",
    ]
    must_pass = [
        "WiFi.mode(WIFI_OFF);", "WiFi.mode(WIFI_MODE_NULL);", "WiFi.getMode();", "RadioPower::mode(WIFI_STA);",
        "// WiFi.begin(ssid);", "/* WiFi.softAP(x) */", 'LOG("WiFi.begin(")', "WiFi.disconnect(false);",
        "esp_wifi_stop();", "WiFi.scanComplete();", "WiFi.scanDelete();", "RadioPower::begin(a);",
    ]
    # The clock and lock owners may spell what nobody else may.
    must_pass_in = [
        (POWER_CPP_REL, "setCpuFrequencyMhz(80);"), (POWER_CPP_REL, "esp_light_sleep_start();"),
        (OWNER_REL, "powerManager.releaseRadioLock();"), (POWER_H_REL, "void acquireRadioLock();"),
    ]
    must_hit_in = [
        ("src/main.cpp", "setCpuFrequencyMhz(80);"), (OWNER_REL, "setCpuFrequencyMhz(80);"),
        ("src/main.cpp", "powerManager.releaseRadioLock();"), (POWER_CPP_REL, "WiFi.begin(a);"),
    ]
    bad = [s for s in must_hit if not findings(s)] + [s for s in must_pass if findings(s)]
    bad += [f"{r}: {s}" for r, s in must_pass_in if findings(s, r)]
    bad += [f"{r}: {s}" for r, s in must_hit_in if not findings(s, r)]
    if bad:
        print("check_radio_power self-test FAILED on:", *bad, sep="\n  ")
        return False
    return True


def mutations(owner: str, power: str, policy: str):
    """(description, owner, power, policy) with one guard removed each."""
    out = []

    def drop_nth(text, needle, n):
        idx = -1
        for _ in range(n + 1):
            idx = text.find(needle, idx + 1)
            if idx < 0:
                return None
        return text[:idx] + text[idx + len(needle):]

    lock_calls = owner.count("lockForStart();")
    for i in range(lock_calls):
        m = drop_nth(owner, "lockForStart();", i)
        if m is not None:
            out.append((f"RadioPower lockForStart() call #{i + 1} removed", m, power, policy))
    out.append(("acquireRadioLock() removed from lockForStart()",
                owner.replace("powerManager.acquireRadioLock();", "", 1), power, policy))
    out.append(("releaseIfOff releases unconditionally",
                owner.replace("if (WiFi.getMode() == WIFI_MODE_NULL) powerManager.releaseRadioLock();",
                              "powerManager.releaseRadioLock();", 1), power, policy))
    out.append(("setPowerSaving ignores the radio",
                owner, power.replace("lockCount != 0 || !power_policy::mayDownclock", "lockCount != 0 || !true", 1),
                policy))
    out.append(("refresh wait ignores the radio",
                owner, power.replace("&&\n      power_policy::mayDownclock", "&&\n      true", 1), policy))
    out.append(("lightSleep ignores the radio",
                owner, power.replace("in.radio = {radioLock, WiFi.getMode() != WIFI_MODE_NULL};", "", 1), policy))
    out.append(("acquireRadioLock leaves the clock low",
                owner, power.replace("setCpuMhzLocked(normalFreq);\n  isLowPower = false;", "isLowPower = false;", 1),
                policy))
    out.append(("policy sleeps under a radio",
                owner, power, policy.replace("if (radioActive(in.radio)) return Block::Radio;", "", 1)))
    out.append(("policy downclocks under a radio",
                owner, power, policy.replace("return !radioActive(r);", "return true;", 1)))
    out.append(("policy forgets Wi-Fi mode",
                owner, power, policy.replace("r.radioLocked || r.wifiModeOn", "r.radioLocked", 1)))
    out.append(("lightSleep ignores the policy verdict",
                owner, power.replace("if (block != power_policy::Block::None) return false;", "", 1), policy))
    out.append(("setPowerSaving re-enables after its guard",
                owner, power.replace("    enabled = false;\n  }\n", "    enabled = false;\n  }\n  enabled = true;\n", 1),
                policy))
    out.append(("begin() releases the lock under a live radio",
                owner.replace("const wl_status_t status = WiFi.begin(ssid, passphrase);",
                              "const wl_status_t status = WiFi.begin(ssid, passphrase);\n  powerManager.releaseRadioLock();",
                              1), power, policy))
    out.append(("stop() releases the lock with the driver still initialised",
                owner.replace("const esp_err_t err = esp_wifi_stop();",
                              "const esp_err_t err = esp_wifi_stop();\n  powerManager.releaseRadioLock();", 1),
                power, policy))
    out.append(("a new RadioPower start without the lock",
                owner.replace("bool ranThisBoot()",
                              "bool extraAp() { return WiFi.softAP(\"x\", nullptr, 1, false, 1); }\n\nbool ranThisBoot()",
                              1), power, policy))
    out.append(("a clock drop inside lightSleep()",
                owner, power.replace("bool HalPowerManager::lightSleep(HalGPIO& gpio, const LightSleepContext& ctx) {",
                                     "bool HalPowerManager::lightSleep(HalGPIO& gpio, const LightSleepContext& ctx) {\n"
                                     "  setCpuFrequencyMhz(40);", 1), policy))
    out.append(("acquireRadioLock sets the refresh-wait clock",
                owner, power.replace("setCpuMhzLocked(normalFreq);\n  isLowPower = false;",
                                     "setCpuMhzLocked(REFRESH_WAIT_FREQ);\n  isLowPower = false;", 1), policy))
    out.append(("a second light sleep outside lightSleep()",
                owner, power.replace("void HalPowerManager::refreshWaitEnd() {",
                                     "void HalPowerManager::refreshWaitEnd() {\n  esp_light_sleep_start();", 1), policy))
    return out


def main() -> int:
    if not self_test():
        return 1

    failed = False
    for d in SCAN_DIRS:
        for path in sorted(d.rglob("*")):
            if path.suffix not in (".cpp", ".h", ".hpp", ".c", ".ino"):
                continue
            if any(s in path.parents for s in SKIP_DIRS):
                continue
            rel = path.relative_to(ROOT).as_posix()
            text = path.read_text(encoding="utf-8", errors="replace")
            for line, what in findings(text, rel):
                if path == OWNER and not any(what == w for _, w, _ in RESTRICTED):
                    continue  # the owner's starts are checked by the contracts
                print(f"{rel}:{line}: {what} (see scripts/check_radio_power.py)")
                failed = True

    owner = OWNER.read_text(encoding="utf-8")
    power = POWER_CPP.read_text(encoding="utf-8")
    policy = POLICY_H.read_text(encoding="utf-8")
    for f in contract_failures(owner, power, policy):
        print(f)
        failed = True

    autolocate = AUTOLOCATE.read_text(encoding="utf-8")
    for f in autolocate_failures(autolocate):
        print(f)
        failed = True
    ip_request = "GeolocateClient::request(IPWHOIS_URL, nullptr"
    auto_muts = [
        ("auto-locate asks the address lookup", autolocate.replace("GeolocateClient::request(BEACONDB_URL,", ip_request + ",", 1)),
        ("auto-locate parses an address answer",
         autolocate.replace("geolocate::parseBeaconDbResponse(", "geolocate::parseIpWhoisResponse(", 1)),
        ("auto-locate's URL points elsewhere", autolocate.replace("api.beacondb.net/v1/geolocate", "ipwho.is/", 1)),
    ]
    for desc, text in auto_muts:
        if text == autolocate:
            print(f"mutation did not apply (the source changed; update the check): {desc}")
            failed = True
        elif not autolocate_failures(text):
            print(f"mutation NOT caught: {desc}")
            failed = True

    muts = mutations(owner, power, policy)
    for desc, o, p, y in muts:
        if (o, p, y) == (owner, power, policy):
            print(f"mutation did not apply (the source changed; update the check): {desc}")
            failed = True
        elif not contract_failures(o, p, y):
            print(f"mutation NOT caught: {desc}")
            failed = True

    if failed:
        return 1
    print(f"check_radio_power: OK ({len(muts) + len(auto_muts)} mutations caught)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
