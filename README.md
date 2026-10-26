# esphome-thetford-ci

ESPHome-Komponente für **Thetford-Kompressorkühlschränke der Serie T2000** (z. B. T2152C) über den **CI-Bus** (LIN, 19 200 Bd). Der ESP32 arbeitet auf einem eigenen Bus-Strang als **alleiniger LIN-Master**. Ein vorhandenes CI-Bus-Bedienteil (z. B. Truma CP plus) bleibt davon getrennt.

Das Protokoll stammt aus dem Reverse-Engineering von [horchi/linux-homectld](https://github.com/horchi/linux-homectld/tree/master/thetford) und [skrebber/thetford](https://github.com/skrebber/thetford) (beide GPL-2.0).

> **Status: experimentell, noch nicht am Gerät getestet.** Schreiben ist standardmäßig gesperrt (`allow_write: false`).

![ESPHome validieren](https://github.com/bbruehmann/esphome-thetford-ci/actions/workflows/validate.yml/badge.svg)

## Inhalt

```
components/thetford_ci/          die Komponente (C++ + Python)
examples/tobbi-kuehlschrank.yaml Beispielkonfiguration (Knaus Van TI Plus „Tobbi“)
tests/ci.yaml                    Test-Konfiguration für GitHub Actions
```

## Einbinden

In der Gerätekonfiguration:

```yaml
external_components:
  - source: github://bbruehmann/esphome-thetford-ci@main
    components: [thetford_ci]
    refresh: 1d
```

Für ein festes Release nimmst du statt `@main` einen Tag, z. B. `@v0.1.0`. Die vollständige Konfiguration mit allen Entitäten steht in [`examples/tobbi-kuehlschrank.yaml`](examples/tobbi-kuehlschrank.yaml). Benötigte Secrets: `wifi_ssid`, `wifi_password`, `fallback_password`, `tobbi_kuehlschrank_api_key`.

## Hardware

| Teil | Hinweis |
|---|---|
| ESP32 (DevKit oder S3) | nur ESP32, die Komponente nutzt den IDF-UART-Treiber für den LIN-Break |
| LIN-Transceiver TJA1020/TJA1021 (Modul) oder Soldered LIN Board | **Master-Pull-up nötig**: 1 kΩ + Diode von LIN nach VBAT (bei vielen Modulen per Jumper „Master“) |
| 12→5-V-Wandler | eigene Sicherung 2 A, z. B. Reserveplatz 15 A im KNAUS-Verteiler mit kleinerem Einsatz |
| Stecker Kühlschrank | Molex 39012020 (CI-Bus-Buchse am T2152C) |

Verdrahtung (Pins wie in der YAML):

```
ESP32 GPIO17 (TX) ──► TXD  Transceiver
ESP32 GPIO16 (RX) ◄── RXD  Transceiver   (3,3-V-Pegel prüfen!)
ESP32 GPIO4       ──► SLP/EN Transceiver
ESP32 GND ─────────── GND Transceiver ── 12-V-Minus (gleiche Masse wie Kühlschrank)
12 V (abgesichert) ── VBAT Transceiver
LIN Transceiver ───── LIN-Pin am CI-Bus-Stecker des Kühlschranks
```

Laut Thetford-Installationshandbuch wird der **Massepin am CI-Bus-Stecker nicht angeschlossen**. Kühlschrank und Bus-Master müssen dieselbe Versorgungsmasse haben, hier also den Minus aus dem KNAUS-Verteiler. Welcher der beiden Pins LIN ist, misst du vorher: Im Ruhezustand liegt am LIN-Pin etwa die Bordspannung an (Slave-Pull-up), sobald der Kühlschrank versorgt ist.

## Inbetriebnahme in drei Schritten

1. **Nur lesen** (`allow_write: false`, Standard). Flashen und das Log ansehen. Erwartet wird:
   - `Checksummen-Typ erkannt: …`
   - `Kühlschrank antwortet`
   - `RX: 01.02.00.…` mit plausiblen Werten
   
   Wenn nichts ankommt: Verdrahtung, Master-Pull-up und SLP/EN prüfen, sonst `logger: level: VERBOSE` setzen.

2. **Werte abgleichen.** Am Kühlschrank-Display nacheinander Stufe Kühlteil, Stufe Gefrierfach, Nachtmodus und Aus/An ändern. Dabei `Status-Frame (roh)` und `Modus (Rohwert)` notieren. Damit klärst du:
   - ob die Stufen 0- oder 1-basiert gezählt werden. Bei Bedarf `min_value`/`max_value` der Numbers anpassen.
   - welcher Wert „Aus“ bedeutet (`0x08` oder `0x02`). Bei Bedarf `off_mode_value` anpassen.
   - ob Bit 3 wirklich der Nachtmodus ist.
   - was Byte 3 beim T2000 bedeutet. Im Beispielframe der Quelle steht dort 0x17 im Normalbetrieb. Wenn das so ist, meldet `Störung` dauerhaft „an“, und der Sensor sollte dann nicht für Alarme genutzt werden.

3. **Schreiben freigeben** (`allow_write: true`) – erst wenn Schritt 2 stimmt. Teste zuerst bei laufendem Kühlschrank den Nachtmodus, danach die Stufen. Jeder Befehl wird 3-mal gesendet (`write_repeat`). Ob er gewirkt hat, zeigt der folgende Status-Frame. Wenn nicht, springt der Schalter in HA von selbst zurück.

## Sicherheitsmechanismen

- Ohne `allow_write: true` wird nie ein Steuerframe gesendet.
- Geschrieben wird nur, wenn ein gültiger Status vorliegt und der Checksummen-Typ erkannt ist.
- Der Steuerframe übernimmt die zuletzt gelesenen Werte und ändert nur das angefragte Feld. Die Bytes 2–7 sind 0x00.
- Es wird nur bei einer Änderung geschrieben, nicht zyklisch. Ohne Befehle gelten die Einstellungen vom Bedienfeld.

## Entitäten

| Typ | Entität |
|---|---|
| Sensor | Versorgungsspannung, Stufe Kühlteil, Stufe Gefrierfach, Fehlercode, Modus (Rohwert) |
| Binary | CI-Bus verbunden, In Betrieb, Nachtmodus aktiv, Störung |
| Text | Betriebsmodus, Status, Status-Frame (roh) |
| Switch | Kühlschrank (an/aus), Nachtmodus |
| Number | Stufe Kühlteil / Gefrierfach einstellen |

Die **Innentemperatur liefert der CI-Bus nicht**. Dafür sind weiterhin die Zigbee-Thermometer gedacht.

## Protokoll (Kurzfassung)

- LIN 19 200 Bd. Break wird als 0x00 bei 9 600 Bd erzeugt (≈ 18 Bitzeiten).
- Status: Header mit ID **0x0C** (PID 0x4C), der Kühlschrank antwortet mit 8 Byte + Checksumme.
  - B0 Modus (Bits 0–2: 1 = an, 5 = 12 V …; Bit 3 = Nacht)
  - B1 Stufe (Low-Nibble Kühlteil, High-Nibble Gefrierfach)
  - B3 Code
  - B5 = Spannung × 10
- Steuerung: Frame mit ID **0x0B** (PID 0x8B), immer 8 Byte: B0 Modus, B1 Stufe.
- Checksumme classic oder enhanced wird aus der ersten gültigen Antwort erkannt (`checksum: auto`).

## Konfigurationsoptionen

| Option | Standard | Bedeutung |
|---|---|---|
| `allow_write` | `false` | Steuerframes erlauben |
| `checksum` | `auto` | `auto`, `classic` oder `enhanced` |
| `status_frame_id` | `0x0C` | LIN-ID des Status-Frames |
| `control_frame_id` | `0x0B` | LIN-ID des Steuerframes |
| `response_timeout` | `30ms` | Wartezeit auf die Antwort |
| `offline_after` | `3` | fehlende Antworten bis „getrennt“ |
| `on_mode_value` | `0x01` | Modusbyte für „an“ |
| `off_mode_value` | `0x08` | Modusbyte für „aus“ (alternativ `0x02`) |
| `write_repeat` | `3` | Wiederholungen je Steuerbefehl |
| `update_interval` | `5s` | Abfrageintervall |

## Lizenz

GPL-2.0, wie das zugrunde liegende Reverse-Engineering. Nutzung auf eigenes Risiko, ohne Gewähr. Eingriffe in die Bordelektrik nur mit entsprechender Sachkunde.

