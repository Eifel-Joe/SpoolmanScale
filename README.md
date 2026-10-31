<div align="center">
<picture>
  <source media="(prefers-color-scheme: light)" srcset="images/logo_1.jpeg">
  <source media="(prefers-color-scheme: dark)" srcset="images/logo_2.jpeg">
  <img src="images/logo_2.jpeg" width="300">
</picture>
</div>

# SpoolmanScale (Eifel-Joe fork)

> [!NOTE]
> **This is a fork** of [Niko11111/SpoolmanScale](https://github.com/Niko11111/SpoolmanScale). It runs SpoolmanScale on the Waveshare ESP32-S3-Touch-LCD-7 and -7B next to the WT32-SC01 Plus, and adds a few things for BamBuddy with Spoolman, Klipper printers and Brother label printers. The project, its design and nearly all of its code are Niko's; the board layer comes from [wolfrage76](https://github.com/wolfrage76/Waveshare-Spoolman-Scale). Version numbers of this fork are its own; the original's 0.8.4 is a different firmware. If you have a WT32-SC01 Plus and need nothing from the list below, use the original.

<div align="center">
<img src="images/waveshare-spoolmanscale.jpg" width="480" alt="Waveshare ESP32-S3-Touch-LCD-7 running SpoolmanScale, with a spool on the scale">

<sub>Photo: wolfrage76</sub>
</div>

## What this fork adds

Compared with the original's current release, 0.8.3. Marked *since 0.8.5* is in this fork's latest stable release; marked *0.8.6 pre-release* needs **Pre-release** switched on at the scale (see [Download and updates](#download-and-updates)).

**Boards and updates**

- 🖥️ **Two more boards** – the Waveshare ESP32-S3-Touch-LCD-7 (800×480) and -7B (1024×600) next to the WT32-SC01 Plus, from one code base with an image per board. The scale refuses an image built for another board. Tested on hardware: the 7B. *since 0.8.5*
- 🔄 **Updates from this fork** – the daily check, "Check for updates" and the install at the scale or in the browser, stable or pre-release, every image checked against its published checksum, and a [web flasher](https://eifel-joe.github.io/SpoolmanScale/) with a choice of board. *since 0.8.5*

**Weighing**

- 📎 **Accessories on a spool** – parts such as an NFC holder that moves from spool to spool are subtracted from every filament weight. *since 0.8.5*
- 🧺 **Empty spools** (BamBuddy with Spoolman) – a spool's own empty weight, spool types from BamBuddy's empty spool catalog, suggested on the first weighing of an unused spool, new types named at the scale, and "weigh the full spool". *since 0.8.5* – Creating a spool from a Bambu tag asks for the spool type as well, also with BamBuddy's own inventory. *0.8.6 pre-release*
- ⚖️ **Raise the label** (BamBuddy with Spoolman) – as with BamBuddy's own inventory, a weighing above the label weight asks to raise the label instead of losing the difference. *since 0.8.5*

**Creating spools**

- 📋 **Copy with all fields** (BamBuddy with Spoolman) – a copy hangs on its template's Spoolman filament and takes over the profile, the profiles per printer model, the K profiles, the price per kg (which can be changed on the copy card), the storage location and the suppliers. *since 0.8.5*
- 🧵 **Enter filament** – a spool whose filament is in neither the inventory nor the filament database is created step by step, picked from the database and the inventory first and typed only where it is missing. *since 0.8.5* – A density the database lacks is found through the product name, or typed in at the scale. *0.8.6 pre-release*
- ➕ **Create spools without weighing and without a tag** – "Create spool" on the empty main screen, for a delivery: a copy, an entry of the database or "Enter filament", then the location and the label. Up to 10 identical spools in one go. "Into AMS" under "More info" for any spool on the screen (BamBuddy). *0.8.6 pre-release*
- 🔍 **Search and keyboards** – a magnifier in the spool type and colour lists, and keyboards that follow the scale's language: German QWERTZ with umlauts, French AZERTY with accents. *0.8.6 pre-release*

**Labels**

- 🏷️ **Brother QL over WiFi** – the QL-810W/820NWB with DK-11209 (62×29 mm), printed by the scale itself, no PC or app needed; "Label printer" chooses Off, Bluetooth or WiFi. *since 0.8.5*
- 🔠 **Larger label text** – the lines take the largest font that fits the label, and a switch adds the colour as an RGB line for the slicer. *0.8.6 pre-release*

**Printers**

- 🔌 **Klipper printers in the AMS view** (Spoolman, BamBuddy with Spoolman) – entered on the scale's web page with a connection test; Happy Hare, AFC and toolchangers are detected by themselves. A weighed spool goes into a gate, lane or tool through Moonraker, and a spool it displaces gets its old location back. *0.8.6 pre-release*
- 🖨️ **Choose the printer from a list** – the AMS view shows every printer as a button, with a search, up to 64 printers. *0.8.6 pre-release*

**Web interface**

- 💾 **Settings backup and a scale test page** – all settings as one file, loaded back exactly; raw value, weight and noise of the load cell live. *since 0.8.5*
- ⬇️ **Downloads inside BamBuddy** – the settings backup, screenshots and the log can be saved from the web interface embedded in BamBuddy as well. *0.8.6 pre-release*

Fixes, among them for the memory of the 7B, are in this fork's [release notes](https://github.com/Eifel-Joe/SpoolmanScale/releases).

### From the original's next version

Already in this fork, not yet in the original's 0.8.3 release: the original's work up to its 0.8.4-beta.45, Niko's, taken over with his commits. *0.8.6 pre-release*

- A new weight window with one large button that shows and sends the weight; automatic sending holds back a weight that rose
- A new "Update via GitHub" screen, one form for all number pads, a tap on the colour swatch shows the colour large, Info & Support as tiles
- Built, not tested on hardware here: a guided first setup, a second server set, the AMS view with Spoolman through an AMS source server, and Creality CFS tags (by Jensilein)

## Which firmware to use

| You have | Use |
|---|---|
| WT32-SC01 Plus | The [original](https://github.com/Niko11111/SpoolmanScale), unless you want something from the list above. This fork builds it but has not tested it on hardware |
| Waveshare ESP32-S3-Touch-LCD-7 (800×480) | This fork (`waveshare-s3-lcd7`, built, not tested on hardware here), or [wolfrage76's fork](https://github.com/wolfrage76/Waveshare-Spoolman-Scale), which also runs the 4.3B and the Guition JC3248W535C |
| Waveshare ESP32-S3-Touch-LCD-7B (1024×600) | This fork (`waveshare-s3-lcd7b`, tested on hardware) |

## Enclosures

Printable enclosures on MakerWorld, each under its own license there:

| Board | Enclosure |
|---|---|
| Waveshare ESP32-S3-Touch-LCD-7 and -7B | 👉 [7" Spoolman T-Scale](https://makerworld.com/en/models/3429397-7-spoolman-t-scale-waveshare-esp32-s3-touch-7) by wolfrage76 – one enclosure for both, the 7B has the same outline |
| WT32-SC01 Plus | 👉 [SpoolmanScale](https://makerworld.com/de/models/2713675-spoolmanscale#profileId-3005075) by FormFollowsF, the original's enclosure, or 👉 [Spoolman T-Scale](https://makerworld.com/en/models/3302257-spoolman-t-scale-filamant-scale-filaman-bambuddy#profileId-3747973) |

## Building a Waveshare scale

The parts are the WT32-SC01 Plus's (PN532, NAU7802 and load cell, see the hardware list in the original's README below), with a Waveshare board in its place. The wiring is not the WT32's:

- The PN532 and the NAU7802 go on the board's **UART2 connector**, and the **UART selection switch** next to it must be set to **UART2**.
- **Never connect the PN532 to the board's own I2C connector:** a chip on that bus answers on the same address and the reader would switch the backlight and the SD card off.
- Power and flashing through the connector labelled **USB**, not UART.

Pin table, flashing and everything else that is different: **[Waveshare 7 and 7B](https://eifel-joe.github.io/SpoolmanScale-Docs/build/waveshare/)** in this fork's documentation, or [Other Boards in BUILDING.md](BUILDING.md#other-boards). The wiring guides of the original's documentation describe the WT32-SC01 Plus only.

## Download and updates

**First flash:** open this fork's [Web Flasher](https://eifel-joe.github.io/SpoolmanScale/) in Chrome or Edge, pick your board, connect it over USB and click the button. It installs the latest stable release (0.8.5) and erases the settings.

The same files are on the [Releases page](https://github.com/Eifel-Joe/SpoolmanScale/releases):

| File | Board | Use |
|---|---|---|
| `SpoolmanScale-waveshare-s3-lcd7b_<version>_factory.bin` | Waveshare 7B | First flash with `esptool` at offset 0, see [BUILDING.md](BUILDING.md#waveshare-esp32-s3-touch-lcd-7b) |
| `SpoolmanScale-waveshare-s3-lcd7b_<version>.bin` | Waveshare 7B | Update through the scale's web page |
| `SpoolmanScale-waveshare-s3-lcd7_<version>_factory.bin` | Waveshare 7 | First flash with `esptool` at offset 0, see [BUILDING.md](BUILDING.md#waveshare-esp32-s3-touch-lcd-7) |
| `SpoolmanScale-waveshare-s3-lcd7_<version>.bin` | Waveshare 7 | Update through the scale's web page |
| `SpoolmanScale-wt32-sc01-plus_<version>_factory.bin` | WT32-SC01 Plus | First flash with `esptool` at offset 0 |
| `SpoolmanScale_<version>.bin` | WT32-SC01 Plus | Update through the scale's web page |

The files without a version in their name are what a scale downloads when it updates itself; each board takes its own.

**Updates** come from this fork, not from the original: **Settings → System → Firmware Update → Update via GitHub**, or the firmware page of the web interface. Switch on **Pre-release** there for the 0.8.6 pre-releases. A scale that runs the original (or another fork) moves over once by hand: web interface → Firmware → upload your board's file without `_factory`; the settings are kept.

[![Latest Release](https://img.shields.io/github/v/release/Eifel-Joe/SpoolmanScale?style=for-the-badge&color=28d49a)](https://github.com/Eifel-Joe/SpoolmanScale/releases/latest)

## Documentation

**[This fork's documentation](https://eifel-joe.github.io/SpoolmanScale-Docs/)**, in English and German, describes what this fork does differently: the Waveshare boards, updates, creating spools, empty spools, the Brother printer, Klipper printers and the web interface. Everything else is in the [original's documentation](https://niko11111.github.io/SpoolmanScale-Docs/), which covers the WT32-SC01 Plus and everything this fork has not changed.

<a href="https://eifel-joe.github.io/SpoolmanScale-Docs/">
  <img src="https://img.shields.io/badge/📖%20Documentation-This%20fork-28d49a?style=for-the-badge&logoColor=white" alt="Documentation of this fork"/>
</a>

## How it relates to the original

This fork builds on the original's 0.8.3 release and takes over the original's later work, each commit under its author's name; so far up to the original's 0.8.4-beta.45. What helps every scale is offered back to the original ([issue #58](https://github.com/Niko11111/SpoolmanScale/issues/58)). The manual, the enclosure, MakerWorld, the Discord, Ko-fi and the FilaMan plugin in the original's README below belong to the original project.

Problems with the Waveshare boards or with anything in the list above belong to this fork: [open an issue here](https://github.com/Eifel-Joe/SpoolmanScale/issues). Please do not report them to the original project.

## License

Licensed under the [GNU General Public License v3.0 or later](LICENSE), like the original. The enclosures are not part of it; they are published on MakerWorld under their own licenses.

## Credits

This fork is built on the work of others, taken over with their commits or as marked parts of them:

- **[@Niko11111](https://github.com/Niko11111)** – SpoolmanScale itself, the original this fork follows.
- **[@wolfrage76](https://github.com/wolfrage76)** – the Waveshare support from [Waveshare-Spoolman-Scale](https://github.com/wolfrage76/Waveshare-Spoolman-Scale): one firmware for several boards with the Waveshare 7 as the second, a touch that wakes the panel, and screenshots at the panel's size. Also the photo above and the 7" enclosure.
- **[@AreYouThereAndi2](https://github.com/AreYouThereAndi2)** – from the fork [AreYouThereAndi2/SpoolmanScale](https://github.com/AreYouThereAndi2/SpoolmanScale): the subtraction of accessories on a spool, and the settings backup and the scale test page, taken over in parts and adapted.
- **Jensilein** – the Creality CFS tags, from the original's pre-releases.

Credits for SpoolmanScale itself are in the original's README below.

---

*What follows is the original project's README.*

# SpoolmanScale

### *One Scale to rule them all.*

[![Discord](https://img.shields.io/badge/Discord-Join%20the%20community-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/K3KmKnrWhP)
[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/formfollowsfunction)

**SpoolmanScale** is an ESP32 filament scale with an NFC reader, a touchscreen and open-source firmware. Place a spool on it: the scale reads its tag, pulls the spool from your filament manager and lets you update the weight, log a drying date, set a location or archive it. No phone needed. It also writes NFC tags, creates spools right at the scale and prints labels, and it works with [Spoolman](https://github.com/Donkie/Spoolman), [FilaMan](https://github.com/Fire-Devils/filaman-system) and [BamBuddy](https://github.com/maziggy/BamBuddy), which you can switch between at any time.

Over 200 active users, inventories of more than 300 spools, and only 5-star ratings on MakerWorld.

Yes, another filament scale – but hear me out, this one might actually earn a spot next to your printer. 😄

> [!IMPORTANT]
> **SpoolmanScale is a front end, not a database.** It needs one of the three filament managers running on your network. That is where your spools live; the scale reads them, weighs them and writes the result back. No server yet? [SpoolmanScale Pro](https://niko11111.github.io/SpoolmanScale-Docs/pro/) turns a Raspberry Pi into one.

---

## New in 0.8.3

- 🧵 **Create spools on the scale** – a spool that is not in your inventory yet is created right at the scale, from a Bambu tag or with a blank NTAG, with its starting weight. Put an unknown spool on and a card suggests the next step
- 🔢 **Spool by its ID** – no tag on the spool? Weigh it by its ID. And when you archive a spool, its tag is freed for the next one
- 🏷️ **Label printing, extended** – a first label editor in the browser, more printers, labels from FilaMan, printing on the PC through FilaMan, and a QR code that leads where your server does
- 🪟 **The scale inside BamBuddy or FilaMan** – its web interface in your filament manager's navigation
- 🎨 **Themes** – light, Spoolman and FilaMan looks, your own accent colour
- 🔒 **HTTPS and proxies** – a backend address with https, servers behind a key or password

Everything else is in the [release notes](https://github.com/Niko11111/SpoolmanScale/releases/tag/v0.8.3).

> [!NOTE]
> **Skipped the USB update from 0.8.0?** 0.8.3 no longer fits the old memory layout, so your scale will not install it over the air. Open the [Web Flasher](https://niko11111.github.io/SpoolmanScale) in Chrome or Edge, connect the scale and choose **Update**: about 2 minutes, all settings are kept. After that, updates arrive over the air as usual.

---

## Download

Firmware is available via the [Web Flasher](https://niko11111.github.io/SpoolmanScale) or as a direct download from [Releases](https://github.com/Niko11111/SpoolmanScale/releases). Already have a scale? Update it right on the device: **Settings → System → Firmware Update → Update via GitHub**.

[![Latest Release](https://img.shields.io/github/v/release/Niko11111/SpoolmanScale?style=for-the-badge&color=28d49a)](https://github.com/Niko11111/SpoolmanScale/releases/latest)

<a href="https://niko11111.github.io/SpoolmanScale-Docs/">
  <img src="https://img.shields.io/badge/📖%20Documentation-Full%20Build%20Guide%2C%20Wiring%20%26%20Setup-28d49a?style=for-the-badge&logoColor=white" alt="Documentation"/>
</a>

Questions or trouble? Join the [Discord](https://discord.gg/K3KmKnrWhP), happy to help.

---
<div align="center">
<img src="images/SpoolmanScale_3.jpeg" width="300"> <img src="images/SpoolmanScale_4.jpeg" width="300">

<img src="images/SpoolmanScale_5.jpeg" width="200"> <img src="images/SpoolmanScale_6.jpeg" width="200"> <img src="images/SpoolmanScale_7.jpeg" width="200">

<img src="images/screen_main_spool.png" width="200"> <img src="images/screen_ams.png" width="200"> <img src="images/screen_create_from_tag.png" width="200">

<img src="images/screen_three_spools.png" width="200"> <img src="images/screen_vendor.png" width="200">

</div>

---

## Features

- 🏷️ **Bambu Lab NFC tags** – the scale reads the tag and uses its data to show the spool, to link it to a spool you already have, or to create a new filament and spool
- 🔗 **Link and write NFC tags** – any NTAG sticker or MIFARE card can be linked to a spool, from any maker, and the scale writes the spool data onto NTAG stickers, with every backend and without help from the server, in four formats: OpenSpool, FilaMan, Anycubic ACE, or erase. A spool can carry a tag on each side: after linking, the scale asks for the second one, and from then on the spool is found whichever tag lies on the reader, so a single NFC reader is enough. That works with Spoolman 0.27 or newer, FilaMan 1.3.1 or newer, and BamBuddy with Spoolman 0.27 as its inventory. BamBuddy's own inventory holds one tag per spool, for now. 😉 The scale tells you when tag and inventory drift apart, and when you archive a spool it frees its tag for the next one
- 🧵 **Create and copy spools** – a spool that is not in your inventory is created right at the scale, from a Bambu tag or a blank NTAG. Running low? Place the new spool on, tap Copy Spool, and the scale creates an identical entry, tags the chip and logs the weight in one step
- 🎯 **The right spool, found quickly** – linking narrows the list by material, subtype (HF, CF, Matte) and colour, and puts the spool whose remaining weight matches the one on the pad first. If tag and spool do not fit together, the scale shows both side by side and asks before it links. Snapmaker tags can be read as well
- 🔢 **Spool by ID** – weigh a spool that has no tag by typing its ID
- ⚖️ **Weighing, of course** – a NAU7802 load cell amplifier with a moving average filter, TARE and optional auto-tare. The scale shows what is on the pad against what your inventory says is left, writes the new weight back, and knows the weight of the empty spool per spool, filament or vendor
- 🔀 **Three backends** – [Spoolman](https://github.com/Donkie/Spoolman), [FilaMan](https://github.com/Fire-Devils/filaman-system) or [BamBuddy](https://github.com/maziggy/BamBuddy), switchable in the settings, with https and with servers behind an API key or a password. BamBuddy can keep its own inventory or run against a Spoolman server behind it; the scale works out which. Filament managers on the internet are on the way, see the roadmap
- 🌡️ **Drying reminder** – color-coded `last_dried` date showing whether a spool needs drying, with thresholds per material or set manually
- 📍 **Locations** – assign and view storage locations on the scale, with an optional popup when you take a spool off
- 🖨️ **AMS view** – with FilaMan and BamBuddy: every AMS unit with its bays, material, colour, remaining filament, humidity and temperature. Tap a bay for the spool behind it, and record a drying right there
- 🏷️ **Label printing** – print spool and filament labels on Bluetooth label printers (Phomemo M220 and M110S, NIIMBOT B1 and M2, more experimental), with a QR code that opens the spool in your backend. Lay the label out in a first label editor in the browser, or print it on the PC through FilaMan
- 🌐 **Web interface** – the scale's own pages, in all three languages, as usable on a phone as on a desktop. See the status, set up backend and printer, lay out labels, read and write tags, pick a theme, follow the log live and update the firmware with the release notes in front of you. Parts you would rather not expose can be switched off one by one. It can also sit inside BamBuddy or FilaMan
- ⚙️ **On-device setup** – language, Wi-Fi and server address step by step on the touchscreen. Wi-Fi can also come straight from the Web Flasher or from your phone, so there is no long password to type on a small screen
- 🎨 **Themes** – dark and light, in the look of Spoolman, FilaMan or BamBuddy, with your own accent colour
- 🌍 **DE / EN / FR language support** – chosen on first boot, switchable in settings, and it applies to the web interface as well
- ⚡ **Web Flasher** – unpack the ESP with its touchscreen, connect it to your PC by USB, open the [Web Flasher](https://niko11111.github.io/SpoolmanScale) in your browser, flash, done
- 🔄 **Firmware updates (OTA)** – the scale checks for a new version once a day, and you install it right on the device or from the browser. No PC, no cables. Or upload a firmware file yourself
- 🌙 **Power management** – display dimming and deep sleep, and the scale wakes when you touch it or put a spool on
- 🧾 **Works without a load cell, too** – switch off *Scale fitted* and the device becomes a pure tag terminal: read, link and write tags and set locations, without weights on the screen
- 🩺 **Easy debugging** – a part defective, or the hardware assembled wrongly? The scale recognises the most common faults, says so in plain words and tells you what to do. If that does not help, the log is within reach in the browser, live if you want, and so are screenshots of the display. Share them in our Discord and you get help as fast as possible

---

## Hardware

| Component | Model | Link |
|---|---|---|
| MCU + Display | WT32-SC01 Plus (ESP32-S3, 480×320, ST7796) | [AliExpress](https://de.aliexpress.com/item/1005006050379552.html) |
| Debug Board (not necessary) | ZXACC-ESPDB | [AliExpress](https://a.aliexpress.com/_Eu5Y0Ug) |
| NFC Reader | PN532 | [AliExpress](https://a.aliexpress.com/_ExScN8M) |
| Scale ADC | NAU7802 (Adafruit recommended) | [AliExpress](https://de.aliexpress.com/item/1005011685825986.html) |
| Load Cell | YZC-133 **2 kg** beam cell (5 kg works too) | [AliExpress](https://a.aliexpress.com/_EuhhVF2) |
| USB-C Panel Mount 90° | 30 cm, Left/Right Angled, full USB-C PD + data | [AliExpress](https://de.aliexpress.com/item/1005003488021890.html) |
| Connector Cables | STEMMA QT / JST cables | [AliExpress](https://de.aliexpress.com/item/1005011904682215.html) |
| Connector Cables (easier assembly) | Micro JST 1.0 SH 5-pin | [Amazon](https://amzn.eu/d/0aKJ4Va9) |

Prefer everything in one cart? A community member put together an AliExpress list with parts that work well too: 👉 [SpoolmanScale parts list on AliExpress](https://www.aliexpress.com/p/wish-manage/share.html?spm=a2g0o.cart.headerAcount.6.321738dayZTIa0&wishGroupId=800000022363334&smbPageCode=wishlist-amp&spreadId=E95BDFF0E1B4367408F1423D8C0ABF206981C084C10F93E3EE124B0AFA678D69)

The 3D printable enclosure is available on MakerWorld, under MakerWorld's own license:
👉 [makerworld.com/@FormFollowsF](https://makerworld.com/de/models/2713675-spoolmanscale#profileId-3005075)

<a href="https://makerworld.com/de/models/2713675-spoolmanscale#profileId-3005075">
  <img src="https://img.shields.io/badge/🖨%203D%20Files-Download%20on%20MakerWorld-1a8cff?style=for-the-badge" alt="MakerWorld"/>
</a>

📖 A detailed build guide, wiring diagrams and setup instructions are available in the **[SpoolmanScale Documentation](https://niko11111.github.io/SpoolmanScale-Docs/)**

---
## Getting Started

**1. Order parts & print the enclosure**
Order from the hardware list and print the enclosure while you wait for shipping.

**2. Flash the board first**
Before assembling, flash via the [Web Flasher](https://niko11111.github.io/SpoolmanScale). Verify it works before wiring.

**3. Wire & assemble**
Full wiring tables and assembly tips: **[SpoolmanScale Documentation](https://niko11111.github.io/SpoolmanScale-Docs/)**

**4. First start**
Boot it up and enter your Wi-Fi: on the touchscreen keyboard, from your phone through the scale's own access point, or already while flashing. A short walk-through sets the most important settings, and the calibration of the scale comes last. Done.

---

## Server Setup

On first boot the scale asks which filament manager you use and then only shows the steps that apply.

**No server yet?** [SpoolmanScale Pro](https://niko11111.github.io/SpoolmanScale-Docs/pro/) turns a Raspberry Pi into one for Spoolman, FilaMan or BamBuddy, with backups and updates from a web page.

*Not available ready-made yet. Looking into kits and pre-built scales in small batches. Interested? Tell us on [Discord](https://discord.gg/K3KmKnrWhP) or [Instagram](https://www.instagram.com/spoolmanscale/): the more people ask, the sooner it can happen. [More in the FAQ](https://niko11111.github.io/SpoolmanScale-Docs/help/faq/#can-i-buy-the-scale-ready-made).*

### <img src="https://raw.githubusercontent.com/Niko11111/SpoolmanScale/main/images/Logo_Spoolman.png" height="20" align="top"> Spoolman

**Spoolman 0.27 and newer:** tags are stored the native way. They belong to the spool instead of sitting in an extra field, a spool can carry several, and a scan at the scale can open that spool in your browser. New setups use this by default, and a scale that used `extra.tag` before moves over once by itself; the old fields stay filled.

**Still on an older Spoolman?** The [manual](https://niko11111.github.io/SpoolmanScale-Docs/use/backends/) explains how it works there. But do yourself a favour and update: 0.27 has really good features. 😉

**Recommended add-on: [OpenSpoolMan](https://github.com/drndos/openspoolman)**
OpenSpoolMan connects to your Bambu printer via MQTT and reads which filament is loaded in which AMS tray. It finds Bambu spools by the UUID in `extra.tag` and does not know Spoolman's native tags yet. With native tags, set **Also in extra field** to `extra.tag` in the scale's Spoolman settings and the scale writes the UUID there as well, so OpenSpoolMan recognizes your linked spools instantly.

We are working on something of our own for Spoolman users. Stay tuned…

### <img src="https://raw.githubusercontent.com/Niko11111/SpoolmanScale/main/images/filaman_logo.png" height="20" align="top"> FilaMan

No extra fields needed. Tags are written to FilaMan's native `rfid_uid` field, so a spool you link here is recognised by **FilaMan's own smartphone app** as well. Spools imported from Spoolman are recognised by their old tag and migrated on the first scan.

FilaMan needs an **API key** and a **device token**, both entered on the scale's built-in web page, which a button on the scale opens. An API key inherits the rights of the user who made it; for a limited key, create a user with a limited role. The permissions the scale needs are behind the ℹ️ button on that page.

FilaMan reaches furthest into the tag features. It can send the scale a write job, which you confirm on the device, and ask it to read a tag into its inventory. Lift a freshly weighed spool off the pad and the scale offers it to FilaMan for the next printer that loads a tray. The FilaMan tab in your browser jumps to the spool you put on the scale, just as Spoolman 0.27 does, and the label features use FilaMan's label designer, also for printing on the PC. FilaMan 1.5.0 is the current version, and the label features need 1.4.3 or newer.

**Recommended add-on: [Bambu Usage Tracker](https://github.com/Niko11111/FilaManBambuUsage)**
For anyone running Bambu spools: it counts filament as it is printed and books it against the right spool, so your inventory stays right without weighing after every job.

**Recommended add-on: [SpoolmanScale for FilaMan](https://github.com/Niko11111/filaman-spoolmanscale-plugin/releases/latest)**
Shows the scale's web interface inside FilaMan, with its own entry in the navigation. Needs FilaMan 1.2.45 or newer and firmware 0.8.3 or newer.

### <img src="https://raw.githubusercontent.com/Niko11111/SpoolmanScale/main/images/BamBuddy_logo.png" height="20" align="top"> BamBuddy

BamBuddy needs an **API key**, entered on the same page as the FilaMan credentials.

It can keep its inventory in its own database or use a Spoolman server behind it. The scale detects which of the two it is talking to and says so in the status bar, because a few things differ: with Spoolman behind it, tare values and the drying date are stored the Spoolman way.

Since BamBuddy's October daily builds, the drying date has a field of its own, and the scale uses it. An older BamBuddy keeps it in the spool's note.

### Other servers

Any server that implements the [integration specification](https://github.com/Niko11111/SpoolmanScale-Integration) works with the scale, also online services that do not run on your own network. A checker is included.

---

## Roadmap

### Planned

- 🏷️ **Tags of more makers** – reading them and creating spools from them: Creality CFS, TigerTag and Snapmaker. The Elegoo Canvas needs tags written in its own format, which is planned as well
- 🔌 **Active spool to Klipper** – the spool you weigh becomes the active spool on your printer, through Moonraker
- ⚖️ **More load cell options** – an HX711 next to the NAU7802, detected by the firmware itself
- 🏷️ **Label printing, the next steps** – more printers and label sizes, and the printer's status (paper, lid). I cannot buy every printer, so help from the community is very welcome
- 🌍 **Filament managers on the internet** – not only servers on your own network, but also filament managers that run online. The [integration specification](https://github.com/Niko11111/SpoolmanScale-Integration) is published; the first online service is still to come. Early development

### Community requests & ideas

- More ideas welcome – open an issue or join the [Discord](https://discord.gg/K3KmKnrWhP)!

---

## Support This Project

A lot of my free time has gone into building SpoolmanScale. If you enjoy using it, please help spread the word:

- ⭐ **Star this repo on GitHub** – it helps more people discover the project
- ⭐⭐⭐⭐⭐ **Rate 5 stars & boost on MakerWorld** – every like, rating and boost helps: [makerworld.com/@FormFollowsF](https://makerworld.com/de/models/2713675-spoolmanscale#profileId-3005075)
- ☕ **Support on Ko-fi** – even a single euro makes a difference: [ko-fi.com/formfollowsfunction](https://ko-fi.com/formfollowsfunction)
- 💬 **Join the Discord** – share your build, report issues, or just say hi: [discord.gg/K3KmKnrWhP](https://discord.gg/K3KmKnrWhP)
- 📸 **Follow on Instagram and show your build** – post your SpoolmanScale and tag [@spoolmanscale](https://www.instagram.com/spoolmanscale/)

Have a feature request? Post it in the [Discord](https://discord.gg/K3KmKnrWhP) or [open an issue](https://github.com/Niko11111/SpoolmanScale/issues) – I read every one of them and do my best to make it happen.

**Thank you for your support. It means a lot. 🙏**

---

## License

SpoolmanScale is licensed under the [GNU General Public License v3.0 or later](LICENSE). You may use, change and share it, also commercially; whoever distributes a changed version, for example on pre-flashed scales, must publish its source under the same license.

The license covers the firmware and everything else in this repository. The enclosure is not part of it: it is a MakerWorld Exclusive under the Standard Digital File License - Community Use (SDFL-C).

---

## Credits

**[@Simon-CR](https://github.com/Simon-CR)** contributed large parts of SpoolmanScale: the web interface rebuilt as separate pages with access gates, writing NFC tags from the browser, both halves of FilaMan's device tag protocol, the firmware page that checks GitHub for updates, and in 0.8.0 the live tag page with raw data and the Snapmaker tag decoding. Plenty of ideas for what comes next, too - thank you.

**[@nanostra](https://github.com/nanostra)** (Frédéric Dubus) translated the scale and the web interface into French.

**[@akira69](https://github.com/akira69)** built label printer support in his fork, which was the groundwork and proof of concept for label printing.

The codebase was refactored with major help from **[@DanielNagy](https://github.com/DanielNagy)**, which made the multi-backend architecture possible in the first place.

Thanks also to mc-red, dox, Vik, sticky, and everyone who tested and sent logs.

## Inspiration

- [SpoolEase](https://github.com/yanshay/SpoolEase) by yanshay

---

*Not affiliated with Spoolman, FilaMan or BamBuddy. Uses their REST APIs.*
