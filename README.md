YUMO SMART CUBE
A DIY smart cube built on the ESP32-S3 with a round touch display, 6-axis IMU auto-rotation, live weather, photo gallery, and more — all packed into a cube you just set down and it knows what to show.
Built by YUMO BUILDS

---

🔧 Hardware

Waveshare ESP32-S3-Touch-LCD-1.46 (240MHz, 16MB Flash, 8MB PSRAM)
Round 412×412 display (SPD2010, QSPI)
QMI8658 6-axis IMU (accelerometer + gyro)
MicroSD card slot
Blue & Purple LED pairs (GPIO 12 & 13)
Physical power button + MOSFET power latch
Battery ADC (GPIO 8, 3:1 voltage divider)

---

🚀 Features

🔄 Auto-rotation — place it on any face, display snaps to the right content instantly via IMU gravity detection

🕐 Live clock — auto timezone via IP geolocation + NTP sync, no setup needed

🌤️ Weather station — live temp, humidity, wind, high/low, auto-detected location via OpenWeatherMap

🖼️ Photo gallery — JPEG photos from SD card, hardware-accelerated, auto-cycles every 4 seconds

😂 Joke fetcher — fresh random joke every 5 minutes from the internet

🎮 Tilt ball game — roll a ball with real gravity using live IMU readings

💡 LED animations — police flash on startup, breath pulse on load, heartwave ambient mode on face 6

😴 Auto sleep — blanks display + LEDs after 2.5 min, wakes on any movement ≥ 0.3g

🔋 Battery monitor — live voltage read from hardware ADC, displayed as percentage

⚡ 60FPS UI — LVGL 9 on a dedicated core, all background tasks pinned to Core 0

📶 WiFiManager — dark-themed captive portal on first boot, saves credentials forever

---

📺 Watch the Build
https://www.youtube.com/@yumobuilds

---

📄 Licence
Free to use for personal and hobby projects. Credit appreciated — tag @yumobuilds if you build one!
Copyright (c) 2026 Yumo Builds

This project, including all code, schematics, and design files for the 
YUMO SMART CUBE, is licensed under the Creative Commons 
Attribution-NonCommercial-ShareAlike 4.0 International Public License (CC BY-NC-SA 4.0).

You are free to:
- Share: Copy and redistribute the material in any medium or format.
- Adapt: Remix, transform, and build upon the material.

Under the following terms:
- Attribution: You must give appropriate credit to Yumo Builds, provide a link to the license, 
  and indicate if changes were made.
- NonCommercial: You may NOT use the material for commercial purposes or sell the software/design.
- ShareAlike: If you remix, transform, or build upon the material, you must distribute your 
  contributions under the same license as the original.

To view a copy of this license, visit: https://creativecommons.org/licenses/by-nc-sa/4.0/
