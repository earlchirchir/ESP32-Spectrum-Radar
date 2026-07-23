# 📶 ESP32 Spectrum Radar

A high-performance, real-time **2.4GHz Wi-Fi Spectrum Analyzer & Rolling Spectrogram Waterfall** engineered for the **ESP32 Cheap Yellow Display (ESP32-2432S022C / CYD)** using **FreeRTOS** and **LVGL v9**.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20ESP32-green.svg)
![GUI](https://img.shields.io/badge/GUI-LVGL%20v9-orange.svg)
![PlatformIO](https://img.shields.io/badge/IDE-PlatformIO-orange.svg)

---

## 🌟 Key Features

### 📊 1. Real-Time 2.4GHz Spectrum Analyzer (`SPECTRUM` Tab)
- **13-Channel Bar Density Graph**: Visualizes Access Point saturation across all 2.4GHz Wi-Fi channels (1–13).
- **Primary Channel Highlight**: Gold indicators for non-overlapping channels (**1**, **6**, and **11**).
- **Congestion Color Code**:
  - 🟢 **Clean**: 1–2 APs (Recommended channel for router configuration).
  - 🟡 **Moderate**: 3–5 APs.
  - 🔴 **Congested**: 6+ APs.
- **Live Diagnostics**: Displays the quietest recommended channel and the strongest SSID broadcasting on the peak channel.

### 📶 2. Interactive Network Inspector (`DEVICES` Tab)
- **Touch-Activated Inspector Modal**: Tapping any network on the list pops up a sleek dark glassmorphic inspector card displaying:
  - **BSSID (MAC Address)**
  - **Center Frequency & Channel** (e.g. `Ch 6 (2.437 GHz)`)
  - **Security Encryption**: `WPA2-PSK (AES)`, `WPA3-PSK`, `Open`, `Enterprise`.
  - **Signal Strength Bar Meter**: Animated percentage gauge (`-30 dBm` Excellent to `-100 dBm` Weak).
- **Smooth Scroll Engine**: Preserves list scroll Y-position during background scan updates.

### 🌊 3. Rolling Spectrogram Waterfall Heatmap (`WATERFALL` Tab)
- **13×8 Heatmap Matrix**: Displays a 24-second rolling history of Wi-Fi congestion across all 13 channels.
- **Dynamic Spectral Decay**: Rows shift downwards automatically as new scan frames arrive.

---

## 🏗️ Architecture & Multithreading

The application leverages **FreeRTOS multi-core task pinning** to maintain liquid-smooth 60 FPS UI responsiveness while performing active radio frequency scans:

```
                  ┌─────────────────────────────────────────┐
                  │            ESP32 Dual-Core              │
                  └─────────────────────────────────────────┘
                                       │
            ┌──────────────────────────┴──────────────────────────┐
            ▼                                                     ▼
    ┌───────────────┐                                     ┌───────────────┐
    │    CORE 0     │                                     │    CORE 1     │
    │ Network Scan  │                                     │    LVGL v9    │
    │  Task (3s)    │                                     │  Rendering    │
    └───────┬───────┘                                     └───────┬───────┘
            │                                                     │
            └───────────── Mutex Thread Synchronization ──────────┘
```

---

## 🛠️ Hardware Requirements

- **Development Board**: ESP32-2432S022C (Cheap Yellow Display / CYD 2.4" Capacitive Touch).
- **Display IC**: ST7789 240x320 SPI TFT.
- **Touch Controller**: CST816S Capacitive Touch I2C.

---

## 🚀 Getting Started with PlatformIO

1. Clone this repository:
   ```bash
   git clone https://github.com/YOUR_GITHUB_USERNAME/ESP32-Spectrum-Radar.git
   cd ESP32-Spectrum-Radar
   ```

2. Open the project in **VS Code with PlatformIO extension**.

3. Build and Upload to your ESP32 board:
   ```bash
   pio run --target upload
   ```

---

## 📄 License
Distributed under the **MIT License**. Free for open-source modification and educational use.
