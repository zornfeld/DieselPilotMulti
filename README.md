Fork of https://github.com/PPTG/DieselPilot
THANX to PPTG for your excelent Work

### 📖 Beschreibung

**Diesel Pilot Multi** ist ein voll ausgestatteter ESP32-Controller für mehrere chinesische Dieselheizungen, die über 433 MHz RF kommunizieren.
Das Projekt ermöglicht die vollständige Steuerung mehrerer Heizungen über einen Webbrowser, MQTT sowie die Integration in Home Assistant.

⚠️ WICHTIG: Verwendung auf eigene Gefahr!



<img width="1296" height="336" alt="grafik" src="https://github.com/user-attachments/assets/02f6ef76-9f66-4416-a6dd-e640d1168c8f" />
<img width="1134" height="611" alt="grafik" src="https://github.com/user-attachments/assets/07008773-260f-4bcd-a896-4d42aa34d7aa" />


### 🔧 Kompatibilität:
PPTG hat zwei Controller getestet, bei denen 🔧 die obere linke Taste ist – einer mit roter Fernbedienung, der andere mit schwarzer. Beide funktionieren.

Es gibt außerdem eine Version des Controllers mit einem ☀️‑Symbol.

Unterstützung für diese Version kann hinzugefügt werden, sobald PPTG es schafft, ein solches Modell zu kaufen und die Datenrahmen des Funkprotokolls zu analysieren.

Derzeit unterstützt die Version nur Displays mit dem 🔧‑Symbol!

Weitere Informationen findest du auf der Wiki‑Seite von PPTG.

![edited](https://github.com/user-attachments/assets/3b78064b-d00a-4f14-a39b-06667b446803)


### ✨ Funktionen
🌐 Web‑GUI – elegantes Interface im Dark‑Theme
📟 OLED‑Display SH1106 – Echtzeit‑Status und IP‑Anzeige
📡 WiFi – AP‑Modus (Standard) + konfigurierbarer STA‑Modus
📨 MQTT – vollständige Integration in Home Assistant
🔗 Pairing – automatisch oder manuell
🎮 Steuerung – POWER, HOCH, RUNTER, MODUS
💾 NVS‑Speicher – Konfiguration bleibt nach einem Reset erhalten
☁︎ OTA‑Update – seit Version V1.2
  

Files:
-**DieselPilot.ino** – ursprünglicher Anwendungscode von PPTG

-**DieselPilotMulti.ino** – von mir modifizierter Anwendungscode für 3 Dieselheizungen sowie Übersetzung des Web‑Frontends ins Deutsche

-**HomeAssistantMQTT.txt** – MQTT‑Konfigurationsdatei für Home Assistant

-**tools** – Der Ordner „tools“ enthält nützliche Programme, mit denen du

die korrekte Verbindung des CC1101‑Moduls überprüfen,

die aktuelle Frequenz der Fernbedienung erkennen und

die erforderliche Frequenz feinabstimmen kannst.

### 🛠️ Benötigte Hardware

| Component | Model | Notes |
|-----------|-------|-------|
| Microcontroller | ESP32 |
| RF Transceiver | CC1101 | 433 MHz |
| Display | SH1106 | OLED 128x64, I2C |

**CC1101 Verdrahtung:**
```
ESP32    CC1101
-----    ------
GPIO4  - GDO2
GPIO18 - SCK
GPIO19 - MISO
GPIO23 - MOSI
GPIO5  - CSn
3.3V   - VCC
GND    - GND
```

**OLED Verdrahtung:**
```
ESP32    SH1106
-----    ------
GPIO21 - SDA
GPIO22 - SCL
3.3V   - VCC
GND    - GND
```

### 🚀 Installation

#### 1. Arduino Libraries
```
- U8g2 (by oliver)
- PubSubClient (by Nick O'Leary)
```

#### 2. Arduino IDE Configuration
```
Board: ESP32 Dev Module
Upload Speed: 115200
Flash Frequency: 80MHz
CPU Frequency: 240MHz
```

#### 3. Upload
1. Öffne `DieselPilotMulti.ino`
2. Upload to ESP32
3. Open Serial Monitor (115200 baud)


### 📱 Erster Start

1. ESP32 startet im **AP mode**
2. Mit Wlan verbinden: `DieselPilot-Multi` (password: `12345678`)
3. Browser: `http://192.168.4.1`
4. Wähle und paire eine Heizung seiner Wahl  (AUTO or MANUAL)
5. Benenne die Heizung (Standart is heater 1, heater 2...)
6. (Optional) Konfiguriere das Wlan 
7. (Ungetestet) Konfiguriere MQTT 

** Pairing mit der Heizung oder das Einrichten von WiFi und MQTT dauert nach dem Klick auf den Button eine Weile.
Warte, bis das Pop‑up‑Fenster erscheint und die Operation bestätigt.
Das liegt daran, dass diese Daten in den Speicher geschrieben werden müssen :)


### Automatic Pairing

1. Wähle die Heizung aus und Drücke **AUTO PAIRING** in GUI
2. ESP32 versucht für 60 sefunden zuzuhören.
3. **Drücke und halte die pairing Taste am Display** (circa ~5-10 Sekunden)
   - Die Heizung wechselt in den Entwicklungsmodus
   - Sendet STATUS-Frame mit Adresse
4. ESP32 erfasst die Adresse und speichert sie im NVS-Speicher
5. Fertig – Heizung gepairt!

- Video showing the pairing process: https://youtu.be/xmEbU_qbN60

### Manuelles Paring
Read: ForNerds.md


**Wnn keine Kommunikation mit der Heizung stattfindet**
- Überprüfe die Frequenz (433.937 MHz)
- Prüfe ob die Heizung im Pairing-Modus ist (Display zeigt HFA...)
- Prüfe ob die Dieselheizung überhaupt OLED Fernbedienungstauglich ist. 
- Prüfe die CC1101 Spannung (muss 3.3V !)

**OLED funktioniert nicht:**
- Check I2C address (default 0x3C)
- Verify SDA/SCL connections

### 📜 License

MIT License - use as you wish, at your own risk!

---

**CC1101 Debugging:

⚠️ WICHTIG: Jedes CC1101-Modul hat minimale Frequenzabweichungen!
5 verschiedene Module getestet – alle funktionieren.
Abweichungen: ±10–30 kHz von der Nennfrequenz 433,92 MHz.
Verwende SDR#, um die tatsächliche Sende‑(TX)‑Frequenz zu überprüfen.
Bei schwachem Empfang → Frequenzabstimmung im CC1101‑Code.

CC1101-Modul Kalibrierung:
cpp
// Bei Empfangsproblemen Frequenzabstimmung:
// Standard: 433,92 MHz (FREQ2=0x10, FREQ1=0xB1, FREQ0=0x3B)
// 
// Beispiel aus realem Test – Modul funktionierte optimal bei 433,937 MHz:
// Passe die FREQ-Register an die tatsächliche Frequenz deines Moduls an
// Nutze SDR#, um den Signal‑Mittelpunkt zu finden, dann CC1101 abstimmen
// Abweichungen ±10-30 kHz sind normal
```

**Empfohlene Tools:**

✅ rtl_433 – Paket‑Decoding
✅ SDR# / GQRX – Spektrum‑Visualisierung
✅ Inspectrum – IQ‑Aufnahme‑Analyse
✅ Universal Radio Hacker – Protokoll‑Reverse Engineering

---

## 🚀 Project Development - What's Next?

### 🔮 Planned Features

~~**0. Reading errors ❌**~~ ✅ 

- ~~Mapping error code to message~~ ✅
- ~~Forcing/scanning possible controller errors~~ ✅
- ~~Adding error field in GUI~~ ✅
- ~~Adding error field in MQTT~~ ✅

**0.a**
  -Code expansion – same hardware – control and monitoring of up to 3 diesel heaters✅
  
**0.b.**
  -3 OLED displays to monitor each heater individually and to avoid rotating the display on a single screen
  
**1. Fuel Level Sensor ⛽**
```
- Analog reading from fuel sensor
- Real-time level monitoring
- MQTT alerts when fuel < 20%
- Estimated runtime until depletion
- HA integration (fuel level sensor)
```

**2. Fake Heater Simulator 🎭**
```
- Heater simulator for testing remotes
- Responds like real heater
- Testing reverse engineering
- No need for actual device
- Coming soon to repo!
```

**3. Support for controller version ☀️**
```
- Driver version detection
- Pairing mode adjustment
- Data frame mapping
```

### 🤝 How to Help Development?

1. **Testing** - try with different heater models
2. **Bug reports** - report issues on GitHub Issues
3. **Pull requests** - share your improvements
4. **Documentation** - help translate to other languages
5. **Hardware** - test with different CC1101 modules

---

### 🙏 Acknowledgments

- **[merbanan/rtl_433](https://github.com/merbanan/rtl_433)** - THE tool for RF protocol reverse engineering! Without this project, protocol analysis would be impossible. Huge thanks for rtl_433! 📡
- **[DieselHeaterRF](https://github.com/jakkik/DieselHeaterRF)** - inspiration for parts of the protocol and CC1101 library - this is where it all started.
- **[PPTG]
- **RTL-SDR community** - for accessible and affordable SDR tools (DVB-T dongles)
- **SDR#** - for excellent RF spectrum visualization software
- **Home Assistant Community** - for motivation to create MQTT integration

**Tools used in the project:**
- rtl_433 (merbanan) - RF transmission decoding
- SDR# / GQRX - spectrum analysis
- DVB-T R820T2 dongle - cheap SDR receiver
- Arduino IDE, 
<img width="1134" height="611" alt="grafik" src="https://github.com/user-attachments/assets/f450bf01-1319-46e3-8ac2-94ada528104a" />


<img width="372" height="628" alt="grafik" src="https://github.com/user-attachments/assets/12b542b8-15aa-4f58-91e9-b567e355bb44" />


