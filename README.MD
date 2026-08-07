# Wireless bike lock

A bike lock you open from your phone: no key, no combination. An ESP32-S3 hosts a small
web page over WiFi, and hitting Open fires a latching solenoid inside a padlock body. The
same page shows live accelerometer and GPS readings, so the lock can tell you it's being
moved and where it is.

This is built into an existing bike lock frame, so the project is only the two parts that
don't exist off the shelf: the control PCB and the padlock.

Mechanical design (enclosure + padlock body) lives in Onshape:
<https://cad.onshape.com/documents/6c2c18833c9d8444570c43c7/w/6a4bb112d0b0db7159f785b0/e/d206a90808809049c3ae2997?renderMode=0&uiState=6a67dae82bed96ba51b4ef59>

The PCB in its enclosure:
![PCB in enclosure](docs/image.png)

The padlock with the latching solenoid:
![padlock](docs/image-1.png)

## Board at a glance

| | |
|---|---|
| Dimensions | 50 × 50 mm |
| Stackup | 4 layer (F.Cu / In1.Cu / In2.Cu / B.Cu), 1.6 mm |
| MCU | ESP32-S3-WROOM-1 (WiFi + BLE) |
| Power in | USB-C 5 V, single-cell Li-ion via JST-PH |
| Rails | 3.3 V system (buck-boost), 12 V solenoid (boost) |
| Sensors | LIS2DH12 accelerometer (SPI), SIM68M GNSS + SMD antenna (UART) |
| EDA | KiCad 10.0 |
| Placements | 67 across 44 BOM lines |

## How it works

### Power tree

```
USB-C (J1, 5V) ──> U1 BQ24040 ──> Li-ion cell (J2)
                   charge mgmt         │
                                       ├──> U3 TPS63020 buck-boost ──> +3.3V  (MCU, GPS, accel)
                                       │                                 │
                                       └── R7 10k pullup -> U3 EN        └──> U4 TPS55340 boost ──> +12V (solenoid)
```

`U1` (BQ24040) takes 5 V from USB-C and charges the cell. `R1` (560 Ω on ISET) sets the
fast-charge current; `D1`/`D2` are its status outputs. `R2`/`R3` are the 5.1 k CC pulldowns
that make a USB-C source hand over 5 V, and `U2` (USBLC6-2SC6) clamps ESD on D+/D−.

The 3.3 V rail comes off `U3` (TPS63020), a *buck-boost* on purpose: a Li-ion cell sags from
4.2 V down past 3.3 V as it discharges, so a plain buck would brown out the ESP32 long
before the cell was empty. Output is set by `R9` (1 M) / `R10` (180 k) around the 0.5 V
reference → 3.28 V. Inductor is `L2` (2.2 µH).

For the solenoid, `U4` (TPS55340) boosts 3.3 V to 12 V through `L1` (10 µH) and `D3`, set by
`R12` (86.6 k) / `R13` (10 k) around the 1.229 V reference → ≈11.9 V. It's a separate rail so
the solenoid's inrush hits bulk caps (`C12`/`C14`/`C16`) and not the MCU supply.

### Solenoid drive

`CTRL_P` (GPIO40) → `R18` (220 Ω gate series) → `Q1` (AO3400A, low-side N-ch FET). `R17`
(10 k) pulls the gate down so the coil is guaranteed off through boot and reset. `D4` (S1M)
is the flyback diode across the coil. The coil itself lands on `TP1` (+12 V) and `TP2`
(Q1 drain), the two loop test points on the board.

There is no hold-current limiting in hardware. The firmware caps how long the coil can stay
energized instead (`MAX_OPEN_MS`, 5 s). If you drive `CTRL_P` from your own code, keep that
limit.

### GPS

`U6` (SIM68M) streams NMEA at 9600 baud on the primary UART. `U8` (GPS1003) is the SMD
antenna, fed through a pi matching network: shunt `C22`, series `R19` (0 Ω), shunt `L4`
(18 nH). The 0 Ω / 0402 placeholders are there so the match can be tuned after measuring.

## Pin map (U5, ESP32-S3-WROOM-1)


| Net | GPIO | Goes to |
|---|---|---|
| `CTRL_P` | 40 | Q1 gate (solenoid) via R18 |
| `LI_CS` | 10 | LIS2DH12 chip select |
| `SPI_MOSI` | 11 | LIS2DH12 SDI |
| `SPI_SCK` | 12 | LIS2DH12 SPC |
| `SPI_MISO` | 13 | LIS2DH12 SDO |
| `UART1_RX` | 17 | SIM68M TXD0, primary NMEA out |
| `UART1_TX` | 18 | SIM68M RXD0 |
| `UART0_RX` / `UART0_TX` | 6 / 7 | SIM68M secondary/debug UART |
| `BOOT` | 0 | SW3 tactile |
| `RST` | EN | SW2 tactile (R16 10 k pullup) |
| `TM` | — | Q2 → status LED D5; unused by the current firmware |

The accelerometer runs SPI mode 3 at 1 MHz; its INT1/INT2 pins are not connected, so motion
detection is by polling, not interrupt.

## Firmware

The sketch is `firmware/wireslesslock_controller/wireslesslock_controller.ino`. It joins your
WiFi, serves a page on port 80, and polls `/data` once a second for accelerometer + GPS
values. No cloud, no backend; the lock is only reachable on your local network.

| Route | Method | Does |
|---|---|---|
| `/` | GET | Control page |
| `/data` | GET | JSON: `locked`, `accel.x/y/z` (g), `gps.fix/sats/lat/lon` |
| `/open` | POST | Energize solenoid (auto-releases after 5 s) |
| `/close` | POST | De-energize |

### Flashing

1. Arduino IDE → Boards Manager → install the esp32 package.
2. Library Manager → install TinyGPSPlus (Mikal Hart). Everything else ships with the
   ESP32 core.
3. Set `WIFI_SSID` / `WIFI_PASS` at the top of the sketch.
4. Plug in over USB, pick the board and port under Tools, hit Upload.

If it sticks on `Connecting...`: hold BOOT (SW3), tap RESET (SW2), release BOOT. "Done
uploading" means it's already running, so open the serial monitor at 115200 to see the IP
address it got.

## Building the board

`gerbers.zip` is ready to send to a fab house as-is: 4 layer, 1.6 mm, 50 × 50 mm.

For assembly, `production/` holds what a PCBA service wants: `bom.csv`, `positions.csv`,
`designators.csv`, `netlist.ipc`. These are generated by the KiCad Fabrication Toolkit
plugin (settings in `fabrication-toolkit-options.json`); re-run the plugin rather than
editing the CSVs by hand.

Everything is LCSC/JLCPCB-orderable except the solenoid and the battery, which you source to
fit the padlock body.

## Bill of materials

67 placements, 44 lines. Machine-readable copy with purchase links: [BOM.csv](BOM.csv).
`production/bom.csv` is the plugin-generated original and stays the ground truth for
designators and values; this table is that file plus what each part is for.

| Designator | Qty | Value | Footprint | Function | LCSC |
|---|---|---|---|---|---|
| `C1, C2` | 2 | 1uf | 0603 | USB VBUS / charger input bulk decoupling (U1 BQ24040 IN, OUT) | [C15849](https://www.lcsc.com/search?q=C15849) |
| `C10` | 1 | 100nF | 0402 | ESP32-S3 (U5) supply decoupling | [C1525](https://www.lcsc.com/search?q=C1525) |
| `C11, C19, C21, C7` | 4 | 0.1uF | 0603 | General IC decoupling (U4 VIN/VCC, GPS, accel) | [C14663](https://www.lcsc.com/search?q=C14663) |
| `C12, C14, C16, C5` | 4 | 4.7uF | 1206 | 12V solenoid rail bulk output caps (U4 boost output) | [C29823](https://www.lcsc.com/search?q=C29823) |
| `C13, C15, C17` | 3 | 22uf | 0805 | 3.3V rail bulk output caps (U3 buck-boost output) | [C45783](https://www.lcsc.com/search?q=C45783) |
| `C18, C26, C27, C3, C8` | 5 | 100nF | 0603 | Local HF decoupling, 3.3V rail | [C14663](https://www.lcsc.com/search?q=C14663) |
| `C20` | 1 | 22uF | 1206 | 3.3V bulk reservoir for RF/GPS current bursts | [C12891](https://www.lcsc.com/search?q=C12891) |
| `C22, R19` | 2 | 0 | 0402 | GPS antenna pi-match: shunt cap + series 0R (U8 -> U6 GNSS_ANT) | [C1952](https://www.lcsc.com/search?q=C1952) |
| `C23` | 1 | 4.7uF | 0402 | 3.3V decoupling, SIM68M GPS module (U6) | [C23733](https://www.lcsc.com/search?q=C23733) |
| `C28, C4, C6` | 3 | 10uF | 0603 | Mid-value decoupling, battery and 3.3V rails | [C19702](https://www.lcsc.com/search?q=C19702) |
| `C9` | 1 | 0.047uF | 0603 | TPS55340 (U4) soft-start cap on SS pin | [C1622](https://www.lcsc.com/search?q=C1622) |
| `D1, D2, D5` | 3 | LED | 0603 | Status LEDs: D1/D2 = BQ24040 CHG/PGOOD, D5 = MCU-driven (via Q2) | [C2290](https://www.lcsc.com/search?q=C2290) |
| `D3` | 1 | D_Schottky | SMA_L4.3-W2.7-LS5.1-RD | Boost rectifier for U4 (SW node -> +12V) | [C8678](https://www.lcsc.com/search?q=C8678) |
| `D4` | 1 | S1M | SMA_L4.3-W2.7-LS5.1-RD | Flyback/freewheel diode across the solenoid coil | [C51898151](https://www.lcsc.com/search?q=C51898151) |
| `J1` | 1 | MC-311D | USB-C-SMD_MC-311D | USB-C receptacle: 5V charge input + native USB D+/D- | [C136423](https://www.lcsc.com/search?q=C136423) |
| `J2` | 1 | BATTERY | CONN-TH_S2B-PH-K-S-GW | JST-PH 2-pin Li-ion battery connector | [C157932](https://www.lcsc.com/search?q=C157932) |
| `L1` | 1 | 10uH | IND-SMD_L7.2-W6.6_GPSR07X0 | 10uH boost inductor for U4 (+3.3V -> +12V) | [C5189958](https://www.lcsc.com/search?q=C5189958) |
| `L2` | 1 | 2.2uH | L1008 | 2.2uH buck-boost inductor for U3 (TPS63020 L1/L2 pins) | [C86092](https://www.lcsc.com/search?q=C86092) |
| `L4` | 1 | 18nH | 0402 | 18nH shunt inductor, GPS antenna matching network | [C86060](https://www.lcsc.com/search?q=C86060) |
| `Q1` | 1 | AO3400A | SOT-23 | N-ch FET, low-side solenoid driver (gate = CTRL_P / GPIO40) | [C20917](https://www.lcsc.com/search?q=C20917) |
| `Q2` | 1 | DTC143Z | SOT-323_SC-70 | Digital NPN (built-in bias) driving status LED D5 from net TM | [C53066](https://www.lcsc.com/search?q=C53066) |
| `R1` | 1 | 560 | 0603 | BQ24040 ISET: sets fast-charge current | [C23204](https://www.lcsc.com/search?q=C23204) |
| `R10` | 1 | 180k | 0603 | U3 (TPS63020) feedback divider, bottom leg -> 3.3V out | [C22827](https://www.lcsc.com/search?q=C22827) |
| `R11` | 1 | 75k | 0603 | U4 (TPS55340) FREQ pin: switching frequency set | [C23242](https://www.lcsc.com/search?q=C23242) |
| `R12` | 1 | 86.6K | 0603 | U4 feedback divider, top leg from +12V | [C23121](https://www.lcsc.com/search?q=C23121) |
| `R13, R16, R7` | 3 | 10k | 0603 | R13 = U4 FB bottom leg (12V), R16 = RST pullup, R7 = U3 EN pullup | [C25804](https://www.lcsc.com/search?q=C25804) |
| `R17` | 1 | 10k | 0402 | Gate pulldown on CTRL_P (holds solenoid off during boot/reset) | [C60489](https://www.lcsc.com/search?q=C60489) |
| `R18` | 1 | 220 | 0402 | Gate series resistor for Q1 | [C25091](https://www.lcsc.com/search?q=C25091) |
| `R2, R3` | 2 | 5.1k | 0402 | USB-C CC1/CC2 5.1k pulldowns (advertise 5V sink) | [C25905](https://www.lcsc.com/search?q=C25905) |
| `R20` | 1 | 1k | 0402 | Series resistor for status LED D5 | [C11702](https://www.lcsc.com/search?q=C11702) |
| `R4, R5` | 2 | 1k | 0603 | Series resistors for charge-status LEDs D1/D2 | [C21190](https://www.lcsc.com/search?q=C21190) |
| `R8` | 1 | 2k | 0603 | U4 COMP network resistor (loop compensation) | [C22975](https://www.lcsc.com/search?q=C22975) |
| `R9` | 1 | 1M | 0603 | U3 feedback divider, top leg from 3.3V out | [C22935](https://www.lcsc.com/search?q=C22935) |
| `SW2, SW3` | 2 | TS-1088-AR02016 | SW-SMD_L3.9-W3.0-P4.45 | Tactile switches: SW2 = RESET (EN), SW3 = BOOT (IO0) | [C720477](https://www.lcsc.com/search?q=C720477) |
| `TP1` | 1 | VCC_SOL | TestPoint_Loop_D3.80mm_Drill2.8mm | Test point / solenoid output: +12V | — |
| `TP2` | 1 | GND_SOL | TestPoint_Loop_D3.80mm_Drill2.8mm | Test point / solenoid output: switched low side (Q1 drain) | — |
| `U1` | 1 | BQ24040DSQR | WSON-10_L2.0-W2.0-P0.40-BL-EP | Li-ion linear charge management IC (USB 5V -> battery) | [C81080](https://www.lcsc.com/search?q=C81080) |
| `U2` | 1 | USBLC6-2SC6 | SOT-23-6 | USB D+/D- ESD protection array | [C2827654](https://www.lcsc.com/search?q=C2827654) |
| `U3` | 1 | TPS63020DSJR | VSON-14_L4.0-W3.0-P0.50-BL-EP_TI_DSJ | Buck-boost converter: battery (2.5-5.5V) -> 3.3V system rail | [C15483](https://www.lcsc.com/search?q=C15483) |
| `U4` | 1 | TPS55340RTER | VQFN-16_L3.0-W3.0-P0.50-BL-EP1.7 | Boost converter: 3.3V -> 12V solenoid rail | [C169167](https://www.lcsc.com/search?q=C169167) |
| `U5` | 1 | ESP32-S3-WROOM-1 | ESP32-S3-WROOM-1 | Main MCU/radio: ESP32-S3, WiFi + BLE, hosts the control page | [C2913202](https://www.lcsc.com/search?q=C2913202) |
| `U6` | 1 | SIM68M | GPSM-SMD_18P-L10.1-W9.7-P1.10 | GNSS receiver module, NMEA out over UART at 9600 baud | [C2999762](https://www.lcsc.com/search?q=C2999762) |
| `U7` | 1 | LIS2DH12TR | LGA-12_L2.0-W2.0-P0.50-BL | 3-axis accelerometer (SPI) - tamper / motion detection | [C110926](https://www.lcsc.com/search?q=C110926) |
| `U8` | 1 | GPS1003 | ANT-SMD_L10.0-W3.0 | SMD GPS patch antenna, 1575.42 MHz | [C239243](https://www.lcsc.com/search?q=C239243) |

Not on the BOM (source to fit your padlock):

| Item | Notes |
|---|---|
| Latching solenoid | Driven from TP1/TP2 at 12 V, low-side switched |
| Li-ion cell | Single cell, JST-PH 2-pin to J2 |

## Repo layout

```
wireslesslock.kicad_pro      project file
wireslesslock.kicad_sch      root schematic (MCU, GPS, accel, switches)
power.kicad_sch              "Power" sub-sheet (charging, both converters, USB-C)
wireslesslock.kicad_pcb      4-layer board layout
easyeda2kicad.*              project-local library: parts converted from LCSC/EasyEDA
                             (BQ24040, TPS63020, TPS55340, GPS antenna, WSON/VSON pkgs)
BOM.csv                      annotated BOM (this README's table)
production/                  generated fab outputs (plugin output, not source)
gerbers.zip                  fab package, ready to order
firmware/                    ESP32-S3 Arduino sketch
3dfiles/                     STEP export + padlock mechanism snapshots from Onshape
docs/                        README images
archive/                     timestamped project backups
```

If you touch a symbol from `easyeda2kicad.kicad_sym`, keep its footprint in
`easyeda2kicad.pretty/` and its STEP/WRL in `easyeda2kicad.3dshapes/` in sync. They travel
together.

## Verifying changes

There's no build or test command here. Open the project in KiCad and run ERC on the
schematic and DRC on the board.

## License

[MIT](LICENSE).

Many thanks to the people that helped me make this.
