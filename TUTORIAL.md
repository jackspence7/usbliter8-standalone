# Tutorial: building a standalone tethered-boot box

This walks through the whole thing end to end: getting usbliter8, applying
this project's patches, building the firmware, getting a personalized boot
image for your specific phone, combining the two, flashing it, and using the
finished box.

**Read this before you start:** steps 1-4 below (get usbliter8, patch it,
build it, wire the hardware) are a one-time setup for the box itself. Step 5
- getting your own personalized boot image - assumes you have **already**
pwned your phone and downgraded/restored it to your target unsigned iOS
version at least once, using `surrealra1n` (or an equivalent tool) **with a
computer**, the normal way. This project doesn't do that part for you and
isn't a substitute for it - it only takes the boot image that process
produces and lets you replay the resulting tethered boot without a computer
from then on. If you haven't done the initial pwn-and-downgrade yet, go do
that first with `surrealra1n`'s own instructions, then come back here for
step 5 onward.

## 0. What you'll need

- A Raspberry Pi Pico 2 (RP2350), or another RP2350 board usbliter8 supports.
- A spare Lightning cable you're OK cutting open, plus basic soldering gear.
- A Mac or Linux machine for the one-time setup (building the firmware and
  generating your device's boot image). You will **not** need this machine
  again once the box is built - that's the whole point.
- [CMake](https://cmake.org/), the [Pico SDK](https://github.com/raspberrypi/pico-sdk),
  [picotool](https://github.com/raspberrypi/picotool) and the
  [ARM GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).
  usbliter8's own README pins exact versions it was tested with - match those
  if you hit build issues.
- Python 3 (for `make_boot_uf2.py`).
- A Face ID iPhone on the vendor's supported-device list, currently in (or
  restorable to) an unsigned/tethered iOS version via a tool like
  [surrealra1n](https://github.com/pwnerblu/surrealra1n).

## 1. Get usbliter8

usbliter8's own repo has no license file, so it isn't bundled here - go get it
from its authors/official source. Once you have it, you should have a
directory that builds on its own following usbliter8's README (a `src/`-style
folder with `main.c`, `led.c`, `bus.c`, `usb.c`, `exploit.c`, the `pio_usb/`
library, `boards/`, etc.).

Make a working copy of that source tree - you're about to modify it.

## 2. Apply this project's changes

From inside your usbliter8 copy:

```
cd path/to/usbliter8

# files this project modifies:
patch -p1 < path/to/usbliter8-standalone/patches/main.c.patch
patch -p1 < path/to/usbliter8-standalone/patches/led.c.patch
patch -p1 < path/to/usbliter8-standalone/patches/led.h.patch
patch -p1 < path/to/usbliter8-standalone/patches/CMakeLists.txt.patch

# files this project adds (no usbliter8 equivalent):
cp path/to/usbliter8-standalone/new-files/autoboot.c .
cp path/to/usbliter8-standalone/new-files/autoboot.h .
cp path/to/usbliter8-standalone/new-files/make_boot_uf2.py .
```

If a patch fails to apply, usbliter8's upstream `main.c`/`led.c`/`led.h` may
have moved on since this was written - open the `.patch` file and apply the
same idea by hand, it's a small diff.

## 3. Build the firmware

Same as usbliter8's own build instructions, just make sure you're building
this patched copy:

```
cmake -S . -B build -DPICO_BOARD=pico2 \
  -DPICO_SDK_PATH=/path/to/pico-sdk \
  -DPICO_TOOLCHAIN_PATH=/path/to/arm-toolchain
cmake --build build -j8
```

This produces `build/usbliter8.uf2` - the firmware alone (exploit + the
standalone-boot logic + the Recovery->DFU trigger). It doesn't have a boot
image baked in yet, so on its own it'll pwn the phone and then just sit there
("success (no boot image), spinning forever" in the serial log). That's
expected - the next steps add your device's boot image.

## 4. Wire the hardware

Cut a Lightning cable and solder its VBUS, GND, D+ and D- wires to the Pico's
GPIO12 (D+), GPIO13 (D-), VBUS and GND pins (or whatever pins your board
definition uses - check `boards/<your-board>.cmake`). Keep the Lightning-end
side of the cable short. **D+/D- can end up swapped depending on the cable**;
if the serial log shows `opened EP0` repeating forever with a phone plugged
in and never progresses, that's the symptom - swap them.

Flash `build/usbliter8.uf2` to the Pico now (hold BOOTSEL, plug in, drag the
file onto the mass-storage drive that appears) so you can use it in the next
step.

## 5. Get YOUR OWN personalized boot image

This is the part that can't be shared or reused between devices. Using
`surrealra1n` (or an equivalent tool) with a computer, in the loop like
normal:

1. Follow `surrealra1n`'s own instructions to pwn and tethered-restore your
   phone to your target unsigned iOS version, using this Pico (now flashed
   with the plain firmware from step 4) as the pwn hardware.
2. When it succeeds, the tool saves a patched iBoot image, typically at a path
   like `boot/<your-device-model>/<ios-version>/iBSS.boot`. That file is keyed
   to your device's ECID and firmware version - **do not share it, post it, or
   commit it anywhere**.

This step only needs to happen once per device/iOS-version combination.

## 6. Combine firmware + your boot image

```
python3 make_boot_uf2.py \
  build/usbliter8.uf2 \
  /path/to/your/iBSS.boot \
  usbliter8-standalone-final.uf2
```

This writes your `iBSS.boot` into unused flash after the firmware, with a
small header (magic + size + CRC32) that `autoboot.c` checks on boot.

## 7. Flash the final image

Hold BOOTSEL on the Pico, plug it in, and drag
`usbliter8-standalone-final.uf2` onto the drive that appears. This replaces
the plain firmware from step 4 with the full standalone build.

## 8. Using the box

From here on, no computer is involved:

1. Plug the phone into the box's Lightning cable.
2. The onboard LED breathes while it's waiting for a device.
3. If your phone is sitting in **Recovery Mode** (which a tethered phone will
   be, on its own, after any reboot):
   - LED starts **blinking fast**. Hold **Volume Down + Side** on the phone
     now.
   - After a couple of seconds it sends the phone a "reboot" command over
     USB - you don't need to time anything yourself, the box does it.
   - LED switches to a **slow blink**. Release **Side**, keep holding
     **Volume Down** until the LED changes again (a few more seconds).
   - If the phone doesn't land in DFU (screen shows the Apple logo instead of
     staying black), the box will notice it's back in Recovery Mode and just
     retries the whole sequence automatically - no need to power-cycle
     anything.
4. Once the phone is confirmed in DFU, the LED goes back to a brief breathing
   pause (~1s, letting the phone settle), then blinks fast again while it
   runs the exploit (well under a second) and then sends the boot image.
   Sending the boot image is the slow part - it's a couple of megabytes going
   over DFU in small chunks, so expect **a minute or two** of fast blinking
   before the LED turns **solid on**. That's normal, not a hang - just wait
   it out.
5. The phone boots.

The LED is single-color (no color-coding, just the Pico's own onboard LED) -
everything above is communicated through breathing vs. blink speed vs. solid
vs. off. Note that the "hold the buttons" fast blink in step 3 and the
"running the exploit / sending the image" fast blink in step 4 look the same;
the sequence they happen in is what disambiguates them, not the blink itself.

If it ever fails outright, the LED goes off for a few seconds and the Pico
reboots itself and starts over from scratch - it never needs a manual reset.

## Troubleshooting

- **Serial log**: the Pico exposes a USB CDC serial port
  (`/dev/cu.usbmodem*` on macOS) with a full log of what it's doing - PID
  reads, Recovery/DFU detection, exploit timing, boot progress. Worth
  watching the first few times.
- **Recovery/DFU product IDs**: this project checks for Apple's standard
  `0x1281` (Recovery) and `0x1227` (DFU) product IDs. These are consistent
  across current Face ID iPhones, but if your serial log shows a different
  PID for "not in DFU", that's your device's actual Recovery Mode PID - open
  `main.c` and adjust `APPLE_PID_RECOVERY` to match.
- **Reliability**: usbliter8's own README notes the exploit is timing
  sensitive, and even unrelated code changes can affect it. If it's flaky,
  retry a few times before assuming something's wrong with the wiring.

## How it works, briefly

- `autoboot.c` stores your device's patched iBoot image in unused flash past
  the firmware itself, checked with a small magic/size/CRC32 header written by
  `make_boot_uf2.py`. On success it replays the same DFU_DNLOAD / CUSTOM_BOOT
  / DFU_ABORT sequence a computer-based `usbliter8ctl boot` command would send,
  straight from the Pico's own flash.
- `main.c`'s `wait_for_dfu()` loop, in addition to waiting for DFU, now
  recognizes the phone sitting in Recovery Mode. When it does, it runs the
  Volume-Down-and-Side hold sequence with LED cues, and sends a "reboot"
  command over a raw USB control transfer - the same one PC-based recovery
  tools send (`bmRequestType 0x40`, `bRequest 0`, ASCII `"reboot"`). Because
  the reboot itself is what triggers DFU when Volume Down is held (not the
  precise instant a human releases a button while watching the screen), this
  is far more reliable than a bare manual button combo done directly against
  the box.
