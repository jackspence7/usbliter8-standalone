# usbliter8-standalone

A standalone tethered-boot box for Face ID iPhones, built on top of
[usbliter8](#about-usbliter8). Flash it once onto a **Pico 2 (RP2350)**, wire
up a cut Lightning cable, and from then on booting a tethered phone is just
"plug it in and follow the light" - no laptop, no PC, no other software
running anywhere.

> **This is not a jailbreak/downgrade tool by itself - it's for *after* you've
> already done that part.** Before this is useful to you, you need to have
> already used `surrealra1n` (or an equivalent tool) **with a computer** to
> pwn your phone and restore/downgrade it to your target unsigned iOS
> version at least once. That one-time process is what generates the
> personalized `iBSS.boot` file this box needs (see step 5 of
> [TUTORIAL.md](TUTORIAL.md)). What this project replaces is the *routine*
> tethered reboot you'd otherwise need a computer for every time afterward -
> it does not perform the initial pwn-and-downgrade for you. If you flash
> this without ever generating your own `iBSS.boot`, it'll still pwn the
> phone into DFU on its own, but then just sits there with nothing to boot,
> since it has no image to send.

## Why this exists / what's different

The normal tethered-boot flow (as documented by usbliter8/surrealra1n) needs a
computer in the loop **every single time you boot**: run a script on a Mac/PC,
put the phone into DFU by hand (timing-sensitive - it's very easy to
accidentally land in Recovery Mode instead), physically swap the phone
between the computer and the Pico mid-process, then run the exploit and boot
commands from the computer.

This project collapses all of that onto the Pico 2 itself:

- **One device does everything.** The exploit and your device's patched boot
  image both live in the Pico's own flash. There's no script to run, no
  drivers to install, nothing else plugged in.
- **No manual DFU timing.** Tethered phones sit in Recovery Mode after a
  reboot. The box detects that on its own, sends the phone the same
  USB "reboot" trigger a computer-based tool would send while walking you
  through a short button hold with LED cues - so you don't have to eyeball a
  screen and hope you released a button at the right millisecond.
- **No cable swapping.** The phone stays plugged into the same cable the
  entire time: Recovery Mode -> DFU -> exploited -> booted.
- **Self-healing.** If a step fails, the box notices and retries on its own -
  no need to unplug anything or restart a script.

In short: instead of "run this tool on your computer, watch the screen, time
some button presses, swap some cables," it's **plug the phone in and watch
one light**.

## LED quick reference

The onboard LED is single-color (just the Pico's own green LED) - there's no
color-coding, only breathing vs. blink-rate vs. solid vs. off:

| LED behavior | What it means | What to do |
|---|---|---|
| Slow breathing (fades in/out) | Waiting for a device | Plug the phone in |
| Fast blink | Phone seen in Recovery Mode - hold **Volume Down + Side** now | Hold both buttons |
| Fast blink continues, then... | (automatic) reboot command sent | Keep holding both buttons |
| Slow blink | Reboot triggered | Release **Side**, keep holding **Volume Down** |
| Back to slow breathing, briefly | Phone found in DFU, settling | Wait (about a second) |
| Fast blink again | Running the exploit (well under a second), then sending the boot image | Wait |
| Solid on | Boot image sent - phone should boot | Done |
| Off for a few seconds, then the box restarts itself | Something failed | It retries automatically; redo the DFU hold if asked again |

The exploit itself is quick, but sending the ~2MB boot image over DFU in small
chunks is not - expect the "sending the boot image" part of that fast blink to
run for **a minute or two** before the LED goes solid. That's normal, not a
hang.

Note the "hold the buttons" fast blink and the "running/sending" fast blink
look the same - the sequence makes it unambiguous (the button-hold blink only
happens right after Recovery Mode is detected; the exploit/upload blink only
happens after DFU is confirmed), but don't rely on the blink alone to tell
them apart.

Full details, plus exact build/flash/first-run instructions, are in
**[TUTORIAL.md](TUTORIAL.md)**.

## What's actually in this repo

Because usbliter8 doesn't publish a license (see below), this repo does **not**
bundle usbliter8's own source. Instead it ships:

- `patches/` - unified diffs against vanilla usbliter8, for the files this
  project modifies (`main.c`, `led.c`, `led.h`, `CMakeLists.txt`).
- `new-files/` - files with no usbliter8 equivalent, fully original to this
  project (`autoboot.c`, `autoboot.h`, `make_boot_uf2.py`).

You get usbliter8 itself separately and apply these on top. See
[TUTORIAL.md](TUTORIAL.md) for exactly how.

**Nothing device-specific is in this repo.** No boot images, no ECIDs, no
IPSWs, no SHSH blobs. The patched iBoot image (`iBSS.boot`) that actually lets
your phone boot is personalized per-device (tied to your phone's ECID) and
per-firmware-version, and can only legitimately be generated for *your own*
device by a tool like `surrealra1n`. You combine it with the firmware yourself
using `make_boot_uf2.py` - see the tutorial.

## Hardware

- A Pico 2 (RP2350) or compatible RP2350 board.
- A cut Lightning cable soldered to the board's GPIO12 (D+), GPIO13 (D-), VBUS
  and GND (D+/D- pin numbers are configurable if your board layout differs -
  see usbliter8's own README for board options).
- A Face ID iPhone whose bootrom is vulnerable to usbliter8 (check usbliter8's
  supported-device list).

## About usbliter8

[usbliter8](https://ps.tc/pages/blog-usbliter8.html) is a tethered bootrom
exploit for Apple A12, S4/S5 and A13 SoCs, by the Paradigm Shift team
(`@__gsch` and `@hdesk` for the bug and exploitation, plus further
post-exploitation/development work). It uses a Raspberry Pi Pico-family board
running [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB) (by
sekigon-gonnoc) as a bit-banged USB host to reach the bug.

**Licensing note:** as of this writing, usbliter8's own repository has no
LICENSE file and no license text anywhere in its README - it's effectively
all-rights-reserved by its authors by default. That's why this repo ships only
patches and original new files rather than a full mirror of usbliter8's
source. If you're the usbliter8 authors and want this handled differently
(e.g. an explicit license, or a full mirror with attribution), please open an
issue.

## Credits

- **Paradigm Shift** (`@__gsch`, `@hdesk`) - usbliter8 itself, the bootrom
  exploit this all runs on top of.
- **sekigon-gonnoc** - [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB),
  the PIO-based USB host library usbliter8 (and this project) build on.
- **surrealra1n** (pwnerblu) - the tethered-restore tool used to get a
  personalized boot image for your device in the first place. The
  Recovery-Mode-to-DFU trigger in this project was discovered by studying its
  `dfu_helper_a11` routine and reverse-engineering the underlying "reboot"
  command's USB protocol (which itself comes from **libirecovery** /
  libimobiledevice, a long-public part of the jailbreak toolchain).

## Disclaimer

This is for jailbreaking/researching a device you own. It permanently loses
Face ID/passcode support on the affected iOS version, and the resulting boot
is tethered (a reboot needs this box again). Use at your own risk.
