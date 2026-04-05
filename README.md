# VitalSignLogger

**Echtzeit Vitalzeichenüberwachung auf dem STM32U575 mit FreeRTOS und On Device ML**

## Projektübersicht

VitalSignLogger ist eine eingebettete Firmware für das **NUCLEO U575ZI Q** (STM32U575ZIT6QU, Cortex M33, 160 MHz), die Herzfrequenz und Bewegungsklassifikation in Echtzeit verarbeitet. Die gesamte Verarbeitung findet vollständig auf dem Mikrocontroller statt, ohne Cloud Anbindung.

Das System fusioniert PPG und IMU Sensordaten, führt On Device Inferenz über ein quantisiertes neuronales Netz (STM32Cube.AI) durch und gibt die Messwerte über UART als CSV Stream aus. Ein SSD1306 OLED zeigt Herzfrequenz und Bewegungsklasse live an.

Dieses Projekt entstand als Portfolioprojekt im Rahmen meines Masterstudiums (Messtechnik und Sensorik, Hochschule Coburg) mit dem Ziel, praxisnahe Kenntnisse in eingebetteter Medizintechnik aufzubauen. Relevante Standards sind IEC 62304, ISO 14971 und IEC 60601 1.

## Systemstatus

| Teilsystem | Status | Anmerkung |
|---|---|---|
| MAX30102 PPG | In Betrieb | HR Erkennung bestätigt z. B. 167 bpm |
| MPU6050 IMU | In Betrieb | Accel/Gyro Auslese, kalibriert beim Boot |
| SSD1306 OLED | In Betrieb | Zeigt HR und Bewegungsklasse live |
| UART CSV Stream | In Betrieb | 100 Hz über COM Port bei 115200 Baud |
| ML Inferenz | In Betrieb | 3 Klassen REST / WALKING / ARM UP validiert |

## Hardware

### Stückliste

| Komponente | Beschreibung |
|---|---|
| NUCLEO U575ZI Q | Entwicklungsboard mit STM32U575, Cortex M33, 160 MHz, 2 MB Flash, 192 KB SRAM |
| MAX30102 | PPG Sensor mit IR und Rot Kanal, I2C, Adresse 0x57 |
| MPU 6050 | 6 Achsen IMU mit Beschleunigung und Gyroskop, I2C, Adresse 0x68 |
| SSD1306 | 128x64 OLED Display, I2C, Adresse 0x3C |

### Pin Belegung

Alle drei Sensoren teilen sich den **I2C1 Bus** auf den Arduino kompatiblen Steckverbindern CN7.

| STM32 Pin | CN7 Label | Funktion | Verbunden mit |
|---|---|---|---|
| PB8 | D15 SCL | I2C1 SCL | MAX30102 SCL und MPU6050 SCL und SSD1306 SCL |
| PB9 | D14 SDA | I2C1 SDA | MAX30102 SDA und MPU6050 SDA und SSD1306 SDA |
| PA9 | n.a. | USART1 TX | ST Link über USB zum COM Port bei 115200 Baud 8N1 |

PB8 und PB9 liegen auf dem CN7 Arduino Header. Die Morpho Steckverbinder PG13 und PG14 nicht verwenden, da diese die ursprüngliche CubeMX Zuweisung waren und umprogrammiert wurden.

## Architektur

### FreeRTOS Task Design

Das System läuft auf vier parallelen FreeRTOS Tasks.

| Task | Priorität | Stack | Aufgabe |
|---|---|---|---|
| SensorTask | ABOVE NORMAL | 512 Words | 100 Hz MAX30102 und MPU6050 Auslese, IIR Filter, Peakerkennung, Frame Enqueue |
| TxTask | NORMAL | 512 Words | CSV über UART1, SSD1306 Display Update alle 50 Frames |
| MLTask | BELOW NORMAL | 1024 Words | 1 Hz Feature Extraktion und neuronale Netz Inferenz |
| defaultTask | NORMAL | 128 Words | Idle |

### Datenpfad

```
SensorTask (100 Hz)
  osMutexAcquire(i2cMutexHandle, 5 ms)
      MAX30102_ReadFIFO()   PPG IR und Rot Rohzaehlwerte
      MPU6050_Read()        Accel in g, Gyro in deg/s, Temp in C
  osMutexRelease()
  Biquad HPF 0.5 Hz -> LPF 4 Hz -> Peakerkennung -> BPM
  accel_buf[write_idx][0..99]  Double Buffer, 100 Samples = 1 s
  alle 100 Samples: write_idx wechseln, osSemaphoreRelease(accelBufSemHandle)
  SensorFrame_t in sensorQueueHandle, 20 tief, Drop bei vollem Queue

MLTask (semaphorgesteuert, ca. 1 Hz)
  osSemaphoreAcquire(accelBufSemHandle)
  liest accel_buf[1 - write_idx]  Gegenpuffer, kein Mutex noetig
  19 Features extrahieren -> ai_network_inputs_get() -> ai_network_run()
  current_motion_class schreiben, volatile, Race bei 1 Hz akzeptiert

TxTask
  osMessageQueueGet(sensorQueueHandle)
  CSV: HR,bpm,ax,ay,az,gx,gy,gz,temp,motion,ts_ms nach UART1
  alle 50 Frames ca. 0.5 s: SSD1306 Framebuffer aufbauen -> SSD1306_Flush()
```

### I2C Nebenläufigkeit

I2C1 wird zwischen SensorTask (MAX30102 und MPU6050) und TxTask (SSD1306) geteilt und ist durch `i2cMutexHandle`, einen Priority Inheritance Mutex, geschützt.

SensorTask belegt den Bus mit einem Timeout von 5 ms und führt beide Sensorlesungen in einer Sperre durch. Bei laufendem Display Flush werden gecachte Werte verwendet. TxTask belegt mit osWaitForever für den ca. 103 ms langen Flush. Priority Inheritance hebt TxTask dabei auf ABOVE NORMAL an. Nach dem Flush wird hi2c1.ErrorCode geprüft. Bei einem NACK des Displays wird der Peripheral mit HAL I2C DeInit und Init wiederhergestellt.

### Schlüssel Quelldateien

| Datei | Inhalt |
|---|---|
| Core/Src/sensor_task.c | SensorTask und TxTask |
| Core/Src/ml_task.c | MLTask, ML_Init, ML_Classify, 19 Feature Extraktion |
| Core/Src/ssd1306.c | SSD1306 128x64 OLED Treiber |
| Core/Src/max30102.c | PPG Treiber I2C 0x57 |
| Core/Src/mpu6050.c | IMU Treiber I2C 0x68 inkl. I2C Fehlerwiederherstellung |
| Core/Src/biquad.c | Biquad IIR Filter HPF und LPF |
| Core/Src/peak_detect.c | PPG Peakerkennung und BPM Berechnung |
| Core/Inc/shared.h | SensorFrame_t, Puffer, Semaphor und Mutex Handles, Motion Konstanten |
| Core/Src/app_freertos.c | Task, Queue und Mutex Erstellung sowie Stack Overflow Hook |
| X-CUBE-AI/App/network.c | STM32Cube.AI Bewegungsklassifikator mit 3 Klassen |

## ML Inferenz

Das eingesetzte Modell ist ein 1D CNN, quantisiert auf INT8 und trainiert auf Beschleunigungsdaten. Es klassifiziert drei Bewegungsklassen: REST, WALKING und ARM UP. Die Inferenz läuft mit ca. 1 Hz auf einem 100 Sample Fenster bei 100 Hz Abtastrate.

Der Feature Vektor besteht aus 19 Floats: Mittelwert, Standardabweichung, Minimum und Maximum von ax, ay, az und dem Betrag g sowie die Perzentile 25 und 75 und der MAD von g. Das Deployment erfolgt über STM32Cube.AI via X CUBE AI Pack. I/O lebt im Aktivierungspuffer (AI_NETWORK_INPUTS_IN_ACTIVATIONS = 4).

### Validierte Ergebnisse auf echter Hardware

| Klasse | Testbedingung |
|---|---|
| REST | Board flach und still |
| WALKING | Board am Körper getragen während zügigem Gehen |
| ARM UP | Board ca. 90 Grad geneigt, ax annähernd 1g |

## Python Werkzeuge

Im Ordner `Python/` befinden sich zwei Analyse Skripte.

Das Live Dashboard (`vital_dashboard.py`) zeigt in Echtzeit einen HR Plot, Beschleunigungsplot, das Bewegungsklassen Label und loggt CSV Daten.

```bash
pip install pyserial matplotlib numpy pandas
python Python/vital_dashboard.py
```

Das Offline Analyse Skript (`analyse_log.py`) berechnet mittlere HR, maximale g Kraft und Bewegungsanteile in Prozent und speichert einen Plot.

```bash
python Python/analyse_log.py vital_log.csv
```

## Build Umgebung

| Parameter | Wert |
|---|---|
| IDE | STM32CubeIDE 1.19.0 |
| Toolchain | GNU Tools for STM32 GCC 13.3.rel1 |
| RTOS | FreeRTOS CMSIS RTOS v2 |
| ML Framework | STM32Cube.AI X CUBE AI |
| Takt | 160 MHz über MSI PLL M=3 N=10 R=1 |
| I2C Takt | ca. 100 kHz Standardmodus, Timing 0x30909DEC |
| UART | 115200 Baud 8N1 über ST Link Virtual COM Port |
| FreeRTOS Tick | 1000 Hz, Heap 32 KB |
| Linker Skript | STM32U575ZITXQ_FLASH.ld, 2 MB Flash, 192 KB SRAM |

Zum Bauen: Project > Build (Strg+B). Die `Debug/makefile` nicht manuell bearbeiten. Bei Änderungen an der Peripheriekonfiguration über CubeMX (.ioc) neu generieren.

## Bekannte Einschränkungen

Der SSD1306 wird beim Boot sporadisch nicht erkannt. Ursache ist instabile Verdrahtung. Der Code erholt sich automatisch und die Sensoren laufen weiter, auch wenn das Display ausfällt.

Die Herzfrequenzanzeige zeigt 0 ohne aufgelegten Finger. Das ist erwartetes Verhalten, da FINGER THRESH auf 3000 IR Zählwerte gesetzt ist.

Die MPU6050 Kalibrierung dauert ca. 1 Sekunde beim Boot. Das Board muss während des Kalibrierungsscreens flach liegen.

Die Walking Klassifikation benötigt 1 bis 2 Sekunden zügiges Gehen mit dem Board am Körper. Langsames Schlurfen löst die Klasse nicht aus.

## Entwicklungshinweise

Während der Entwicklung wurde **Claude Code** (Anthropic) als KI gestütztes Pair Programming Werkzeug eingesetzt, vergleichbar mit STM32CubeMX für die Peripherie Konfiguration oder einem Debugger für die Fehleranalyse. Sämtliche Systementscheidungen zu Architektur, Hardware Integration, FreeRTOS Task Design und ML Deployment wurden eigenständig getroffen und auf echter Hardware validiert.

## Autor

**Jai Vasanthan Panneer Selvam**
M.Sc. Analytical Instruments, Measurements and Sensor Technology, Hochschule Coburg
[LinkedIn](https://www.linkedin.com/in/jai-vasanthan-58846228a/?locale=de-DE) · [GitHub](https://github.com/jaivasanthanp)
