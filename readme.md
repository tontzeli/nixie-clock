Nixie clock code © 2025 by Toni Mäkelä is licensed under CC BY-NC-SA 4.0. To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/ 


# Nixie Clock project
*Makela Research, 2025*  

A fully self-contained, real-time, DS3231-driven Nixie clock firmware for Arduino.  
Includes smooth digit transitions, cathode-cleaning cycles, automatic DST correction, and a midnight New Year’s animation.  

---

## 🕒 Overview

Firmware drives a 4-digit IN-16 Nixie tube clock using shift registers and an external DS3231 RTC module.  
All animations, display logic, and time calculations are handled in firmware — no external libraries are required beyond `Wire.h`.

The DS3231 stores and maintains **UTC time**, while the Arduino applies user-defined timezone and daylight saving rules dynamically before displaying local time.

---

## ⚙️ Hardware Supported

| Component | Purpose |
|------------|----------|
| **Arduino Nano / Uno** | Main controller |
| **DS3231 RTC module** | Real-time clock (keeps UTC) |
| **74HC595 shift registers (×4)** | Nixie digit drivers |
| **MPSA42 transistors** | High-voltage digit switching |
| **IN-16 Nixie tubes (×4)** | Display elements |
| **High-voltage supply (~170 V)** | Tube power source |

All driver and transistor control is handled directly through bit-level mapping to the shift register chain.

---

## 🧠 Functional Principle

1. **RTC Time Handling**
   - The DS3231 runs permanently in **UTC**.
   - At each loop iteration, the Arduino:
     - Reads the current hour, minute, and second from the RTC.
     - Reads the current date (day, month, year).
     - Calculates whether DST applies (see below).
     - Converts UTC → Local time using:
       ```
       local = UTC + TIMEZONE [+1 if DST active]
       ```

2. **Display Control**
   - The four Nixie tubes display time as `HH:MM`.
   - Tube control is done via 8 chained shift registers (one per anode/cathode pair).
   - Each digit change triggers a *slot-machine-style flicker animation* to minimize cathode poisoning and create a vintage feel.

3. **Automatic Daylight Saving Time (DST)**
   - Firmware follows **European Union rules**:
     - Starts: Last Sunday of March, **01:00 UTC**
     - Ends: Last Sunday of October, **01:00 UTC**
   - DST can be globally disabled or enabled with:
     ```cpp
     #define USE_DST 1   // or 0 to disable
     ```
   - The base timezone (e.g., Finland = +2 h) is defined via:
     ```cpp
     #define TIMEZONE 2
     ```
   - Negative offsets (e.g., UTC−8 for PST) are fully supported.

4. **Cathode Cleaning & Resync**
   - On startup: the clock performs a **resynchronization animation** (“slot roll to zeros”) followed by a **cathode cleaning sequence** cycling all digits.
   - Every hour at `HH:01`, a short cleaning routine runs to ensure long-term tube health.

5. **New Year’s Eve Animation**
   - At **23:59:41**, a 20-second countdown animation begins.
   - When the clock reaches `00:00:00`, it:
     - Displays the new year (e.g., `2026`) for 10 s.
     - Returns automatically to normal time display.
   - On non-New Year transitions, the routine simply blanks and resumes `00:00`.

---

## ✨ Features

- ✅ Accurate timekeeping using DS3231 (battery-backed)
- ✅ Real-time DST switching (EU rule)
- ✅ Fully configurable timezone (`+` or `−` UTC)
- ✅ Flicker-style digit transitions
- ✅ Hourly cathode cleaning
- ✅ Startup resynchronization
- ✅ Automatic New Year countdown & celebration animation
- ✅ Serial debug output showing:
