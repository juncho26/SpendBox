# Build guide

How to make your own SpendBox. No soldering is needed.

## 1. Parts

| Part | Notes |
|---|---|
| Waveshare **ESP32-S3 1.54" e-Paper AIoT board, version V2** | check the board is the V2 revision; other revisions may use different pins |
| LiPo battery 3.7 V, e.g. **652540 (800 mAh)** | one cell with a built-in protection circuit. The plug must match the board's battery connector **with the same polarity**: compare the + and − marks on the board with the battery wires before plugging in. Many cheap packs are wired the opposite way round and need their wires swapped by someone who knows how |
| USB-C data cable | a charge-only cable cannot flash the board |
| Case (optional) | see [`hardware/case`](../hardware/case) |

You can try everything first **without the battery**, powered from USB. In that case the battery icon shows full and the low-battery protection is not active.

## 2. Flash the ready-made firmware (easiest)

You need desktop **Chrome or Edge** (Firefox and Safari cannot talk to USB devices).

1. Download `prebuilt/spendbox-merged.bin` from this repository.
2. Put the board into **download mode**: unplug the cable, hold the **BOOT** button, plug the cable in while holding it, release.
3. Open <https://espressif.github.io/esptool-js/>.
4. Set the baud rate to 921600, click **Connect**, and choose the entry named "USB JTAG/serial debug unit" (or "ESP32-S3").
5. Set **Flash Address to `0x0`**, choose `spendbox-merged.bin`, click **Program**.
6. When it reaches 100%, unplug and replug the cable **without** holding BOOT. The screen refreshes and shows today's date and a large 0.

If no port appears, repeat step 2 (the board must be in download mode), and try another cable or USB port.

### First start

- The clock is set from the firmware's build date. Set the real date and time from your phone (Upcoming tab of the website).
- Default settings until you run the setup: balance 100,000, payday on the 25th, step 100, categories FOOD, HOBBY and SCHOOL.
- Hold both buttons for 3 seconds to start sync mode, then follow the [user guide](user-guide.md).

## 3. Build the firmware yourself

Do this when you want to change the code.

### Install ESP-IDF 5.4.x (once)

**Windows:** download the *ESP-IDF offline installer v5.4.x* from docs.espressif.com, run it with the defaults and use the "ESP-IDF 5.4 PowerShell" it creates.

**macOS**
```
xcode-select --install
brew install cmake ninja dfu-util python3
mkdir -p ~/esp && cd ~/esp
git clone -b v5.4.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
```

**Ubuntu / Debian**
```
sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
mkdir -p ~/esp && cd ~/esp
git clone -b v5.4.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
sudo usermod -a -G dialout $USER     # log out and back in once
```

### Build and flash

macOS/Linux: in each new terminal first run `. ~/esp/esp-idf/export.sh`.

```
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash          # PORT: COM5 on Windows, /dev/cu.usbmodem* on macOS, /dev/ttyACM0 on Linux
```

The first build takes a few minutes. To make a single file like the prebuilt one:

```
cd build && esptool.py --chip esp32s3 merge_bin -o spendbox-merged.bin @flash_args
```

### If it will not connect

Unplug, hold **BOOT**, plug in, release, and flash again. The chip's built-in loader lives in ROM, so a bad program can never lock you out.

### Watching the log

The normal firmware sleeps between presses, which turns the USB port off, so no log appears. For development set `#define BRINGUP 1` in `firmware/main/main.c`: the board then stays awake for 2 minutes after a press and `idf.py monitor` works. Set it back to 0 for normal use.

## 4. Check it works

1. After flashing, hold both buttons for 3 seconds: the sync screen with a QR code appears.
2. Join the SPENDBOX Wi-Fi and open http://192.168.4.1.
3. Finish the 5 setup steps, press Done. Hold both buttons to leave sync mode if it has not closed.
4. Press up a few times, hold both buttons, pick a category, hold both again. The total goes up and the purchase appears in the website's Ledger.

## Troubleshooting

| Problem | Try |
|---|---|
| The screen stays blank | use a data cable; reflash; make sure both USB and flash address `0x0` are right |
| Screen looks smeared after small changes | in `firmware/main/hal_epd.c` set `#define EPD_PARTIAL_OK 0` (every update becomes a full flash, slower) |
| Phone cannot join SPENDBOX | scan the QR straight on; turn mobile data off; "forget" the network and scan again; start sync mode again |
| Footer says CHARGE FIRST | battery under about 3.2 V: charge it |
| Footer says SET CLOCK | set the clock from the Upcoming tab |
