# ESP32-C3 Mini NTP Clock with TM1637 Display

This project implements a **network-synchronized digital clock** using
an **ESP32-C3 Mini** and a **TM1637 4-digit display**.\
The device connects to WiFi, syncs time via **NTP**, automatically
handles **timezone (Europe/Madrid)** including **DST**, and displays the
time in **HH:MM** format with a blinking colon.

Features include: - Automatic WiFi connection with status reporting\
- NTP time synchronization via `pool.ntp.org`\
- Automatic timezone setup for **CET/CEST (Europe/Madrid)**\
- Daily automatic NTP re-sync (03:00)\
- Automatic brightness adjustment (day/night)\
- Blinking colon every second\
- TM1637 4-digit display output\
- Automatic WiFi reconnection\
- Serial logging of date/time

## 📦 Hardware Requirements

-   **ESP32-C3 Mini**\
-   **TM1637 4-digit 7-segment display** (CLK + DIO)\
-   USB cable\
-   WiFi network

### Default Pinout

  TM1637 Pin   ESP32-C3 Pin
  ------------ --------------
  CLK          GPIO 3
  DIO          GPIO 4

## 🔧 How It Works

### 1. WiFi Connection

The ESP32 attempts to connect to the defined SSID and prints connection
status, IP address, and RSSI.

### 2. NTP Synchronization

On successful WiFi connection: - Time is fetched from **pool.ntp.org** -
The device waits up to 10 seconds for NTP sync - Timezone and DST rules
for **Europe/Madrid** are applied: `CET-1CEST,M3.5.0,M10.5.0/3`

### 3. Display Updates

-   Time is shown as **HHMM**\
-   The colon blinks every second\
-   Brightness:
    -   **7** between 08:00 and 20:00\
    -   **1** during nighttime

### 4. Daily Auto-Resync

Every day at **03:00:00**, the device re-syncs with the NTP server to
maintain accuracy.

### 5. WiFi Loss Handling

If WiFi drops: - The device attempts reconnection - When back online, it
re-syncs time automatically

## 🗂 Project Structure

This repository contains a single Arduino sketch implementing the full
functionality in one file.

## ⚙️ Configuration

Update your WiFi credentials in the code:

``` cpp
const char *ssid = "your-network";
const char *ssid_password = "your-password";
```

If needed, you can change the NTP server:

``` cpp
const char* ntpServer = "pool.ntp.org";
```

## ▶️ Usage

1.  Install required libraries:

    -   **WiFi.h** (ESP32 core)
    -   **time.h**
    -   **TM1637Display**
    -   **Arduino.h**

2.  Select your **ESP32-C3** board in the Arduino IDE or PlatformIO.

3.  Upload the sketch.

4.  Open the Serial Monitor at **115200 baud**.

You should see: - WiFi connection logs\
- Local time initialized\
- Clock updates every second

## 📸 Display Behavior

-   Shows hours and minutes in **24-hour format**.\
-   The colon `:` toggles ON/OFF every second.\
-   Automatically dims at night.

## 📅 Daily NTP Resync Logic

    At 03:00:00 every day:
    - Perform NTP resync
    - Apply timezone + DST rules again
    - Avoid multiple resync attempts within the same day

## 📝 License

This project is released under the **MIT License**.\
You are free to use, modify, and distribute it.
