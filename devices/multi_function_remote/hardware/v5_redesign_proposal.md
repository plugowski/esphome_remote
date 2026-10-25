# ESP32 Remote v5 – electronics redesign proposal

Phase 1 deliverable: analysis of the existing EasyEDA project "ESP32 Remote v5"
(board "ESP32 Remote v4", PCB "OLED Mosfet version") and the proposed component
list and GPIO mapping for the ESP32-S3-MINI-1-N8 redesign. Nothing in EasyEDA
has been modified yet.

All coordinates below are millimetres from the bottom-left corner of the board
outline, X to the right, Y up (OLED at the top, controller at the bottom).

## 1. Existing design

### 1.1 Board

| Item | Value |
|---|---|
| Outline | old board 36.8 x 124.3 mm. v5 board is 36.8 x 133.3 mm: extended 9 mm at the bottom (2026-10-21), 1.85 mm corner radii, clip cutouts 3.9 mm deep and 10 mm long on both sides at 10.4 to 20.9 mm from the top edge and, mirrored, from the bottom edge |
| Controller | WEMOS LOLIN32 on two rows of 2.54 mm through-holes at X = 7.07 and X = 29.93, Y = 2.8 to 33.3 |
| Supply | LiPo on the LOLIN32's own charger, 3V3 from the LOLIN32 LDO |
| Battery sense | 4.7 k / 10 k divider BAT -> V_DIV -> GPIO32 (R1, R2, through-hole, slot pads at the bottom corners) |
| OLED power | GPIO15 drives Q1 (IRLML6344) which switches the OLED ground (I2C_GND), R4 10 k gate pull-down, R5 0 R |
| Mounting holes | three, 2.2 mm (M2). Top centre stays at (18.63, 113.70). The two bottom holes keep their X (3.01 and 33.78) and sit 2.64 mm above the new bottom edge, i.e. at Y = -6.36 in the old coordinate frame, clear of the new bottom cutouts. |

Defects found during the review, to be fixed in the new layout:

- 12 stray diagonal lines on the Board Outline layer reaching 100 mm outside the board.
- 6 unassigned fill regions on the top layer at the OLED header (23 DRC errors).
- GND pour too close to the BAT pads on both layers (6 DRC errors).
- Schematic and PCB netlists do not match (LOLIN32 exists only as loose pads on the PCB).
- Schematic parts with no role in the new design, to be removed: U2 WEMOS LOLIN32,
  U4 Seeed XIAO ESP32-C3 (legacy idea, confirmed obsolete), U5 HY2.0 4-pin
  connector, JST1, JUMP1, R1, R2, R3, R4, R5, Q1 (ground-side OLED switch).
- The battery divider is permanently connected: 4.2 V / 14.7 k = 285 uA, which is the
  largest standby consumer of the current design by a factor of 20.

### 1.2 Mechanically constrained items (to be preserved)

Buttons, all SH-6X6X5H-CJ 6 x 6 mm through-hole tactile, rotation 0, pin 3 = GND:

| Designator | Function (firmware) | Centre X | Centre Y | Old GPIO |
|---|---|---|---|---|
| POWER | Power / wake / OFF | 6.98 | 117.60 | 0 |
| SELECT | Left circle | 6.82 | 75.46 | 22 |
| ECO | Mid circle | 18.38 | 75.46 | 5 |
| AC_POWER | Right circle | 29.93 | 75.46 | 33 |
| MODE | Left rectangle | 9.61 | 63.81 | 14 |
| FAN | Right rectangle | 26.50 | 63.81 | 26 |
| UP_1 | Long button 1, left | 9.61 (was 6.95, aligned to MODE column 2026-10-21) | 51.96 | 23 |
| UP_2 | Long button 1, right | 26.50 (was 29.81, aligned to FAN column) | 51.96 | 17 |
| DOWN_1 | Long button 2, left | 9.61 (was 6.95) | 40.46 | 19 |
| DOWN_2 | Long button 2, right | 26.50 (was 29.81) | 40.46 | 18 |

Jumpers U1 (18.16, 50.26) and U3 (17.99, 42.71) tie the two halves of each long
button together (UP_1/UP_2 and DOWN_1/DOWN_2). Keep them.

OLED connections:

| Item | Position | Pins (left to right) |
|---|---|---|
| OLED header, 4 x 2.54 mm through-hole, 1.2 mm drill | Y = 120.8, X = 20.56 / 23.11 / 25.66 / 28.16 | 3V3, GND, SCL, SDA |
| H1, OLED pogo-pin contact pads, 4 oval SMD pads 1.8 x 4.0 mm on 2.54 mm pitch, top layer | Y = 81.86, X = 14.60 / 17.14 / 19.68 / 22.22 | SDA, SCL, VCC (3V3), GND (switched OLED ground today) |

H1 is the set of pogo-pin contact pads the OLED module presses onto. Their
positions, pitch, size, shape and left-to-right order (SDA, SCL, VCC, GND) stay
exactly as they are. They are not a schematic symbol: they are drawn directly on
the PCB as four oval pads with their nets assigned (OLED_SDA, OLED_SCL,
OLED_3V3, GND), in parallel with the J3 header. In the new design the GND pad becomes true ground and the
VCC pad carries the switched OLED_3V3 from the high-side switch, so the OLED
module itself needs no change. The 4-pin through-hole header at the top edge
stays as well, wired identically.

Silkscreen text on the current board: "Smart Remote v3.3", "Inspired by The Stock
Pot", "Design by Pawel Lugowski", "https://tech.lugowski.dev". Version text will
become v5.

### 1.3 Firmware expectations (from the ESPHome configs)

- Ten active-low buttons to GND with pull-ups, GPIO0 doubles as deep-sleep wake.
- I2C at 400 kHz to an SH1106 128x64 OLED at 0x3C.
- OLED power output pin, driven on at boot priority 700.
- Deep sleep with timer wake for periodic battery checks and GPIO wake from POWER.
- Wi-Fi variant (remote.yaml) and ESP-NOW variant (remote_espnow.yaml), both
  currently built for board esp32dev with the Arduino framework.

### 1.4 Decisions taken (2026-10-21)

- Assembled by JLCPCB, so the TPS62840 buck in WSON-8 is used. No LDO fallback.
- Battery: 603450 LiPo, 1200 mAh, two wires, protection BMS inside the pack, no
  thermistor. BQ24075 TS pin is strapped with 10 k to GND so charging is never
  blocked. Charge current 0.5 C = 600 mA: RISET = 890 / 0.6 = 1.5 k (about 590 mA).
  Input limit: USB500 mode (EN1 = 1, EN2 = 0) = 500 mA, the safe default for any
  USB port. RILIM 1.1 k is fitted for a later switch to ILIM mode (1.4 A) by
  moving the EN2 strap to 3V3 (audit correction 2026-10-21: the earlier text
  claimed ILIM mode; the schematic implements USB500).
- All components on the top side except the USB-C receptacle J1 and the battery
  connector J2, which sit on the bottom side (decided 2026-10-21). JLCPCB then
  assembles both sides, or the two connectors are hand-soldered. The battery,
  6 x 34 x 50 mm, lies on the back under the button area (roughly Y = 36 to 90)
  with J2 just above its top end.
- The ten through-hole switches become SMD 6 x 6 mm tactile switches of the same
  5.0 mm height as SH-6X6X5H-CJ, so the keypad travel and feel do not change.
  Centres stay exactly as listed in 1.2.
- USB-C receptacle at the bottom edge, centred at X = 18.4. Requested protrusion
  was 2 mm; the TYPE-C-31-M-12 footprint's front shell legs limit it to 1.3 mm
  before the through-hole pads reach the board edge, so 1.3 mm is used. A
  connector with its legs further back would be needed for a full 2 mm.
- Consequence for the module: with USB-C at the bottom and the OLED at the top,
  the MINI-1 antenna cannot face either short edge. The module is placed with its
  antenna end at the right board edge (around Y = 12 to 30), with a copper
  keep-out on both layers under the antenna and no battery or metal behind it.
  The XIAO (U4) and LOLIN32 (U2) are removed from the schematic.

## 2. Proposed architecture

```
USB-C ──ESD/fuse──► VBUS ──► BQ24075 charger + power path ──► SYS ──► TPS62840 3.3 V buck ──► 3V3
                                   │                                         │
                        JST 1.25 ──┴── BAT ──► MAX17048 fuel gauge (I2C)      ├──► ESP32-S3-MINI-1-N8
                                                                             └──► high-side switch ──► OLED_3V3
```

### 2.1 Component list

| Ref | Part | Package | Purpose | Notes |
|---|---|---|---|---|
| U1 | ESP32-S3-MINI-1-N8 | module, PCB antenna | MCU, Wi-Fi, ESP-NOW, native USB | antenna at the bottom board edge with copper keep-out |
| U2 | BQ24075RGTR (C15464) | QFN-16 3x3 | 1-cell LiPo charger with power path, 1.5 A sys path | ISET 1.5 k for 590 mA charge; EN1 = 3V3 / EN2 = GND selects the USB500 input limit (500 mA), so actual charge current is 500 mA minus system load; R4 ILIM 1.1 k is fitted but only used if EN2 is restrapped high for the 1.4 A ILIM mode; CE and SYSOFF to GND, TS 10 k to GND (pack has no thermistor), TMR 47 k for a 6.3 h safety timer |
| U3 | TPS62840DLCR (C2071859) | WSON-8 2x2 | 3.3 V buck, 600 mA, 60 nA Iq, ~94 % efficiency | VSET 267 k to GND selects 3.3 V (datasheet Table 1), MODE and STOP to GND, EN to SYS, L 2.2 uH SWPA3012S2R2MT (C36402), 10 uF in, 22 uF out |
| U4 | MAX17048G+T10 | TDFN-8 2x2 | Battery fuel gauge, I2C 0x36 | 0.1 uF, ALRT to GPIO with 100 k pull-up, replaces R1/R2 divider |
| U5 | USBLC6-2SC6 (C2827654, TECH PUBLIC listing) | SOT-23-6 | ESD on D+, D-, VBUS | stock substitute 2026-10-21, same pinout as the ST listing |
| J1 | USB-C receptacle 16-pin, USB 2.0 | SMD, through-hole shell | flashing, serial, charging | 5.1 k CC1 and CC2 to GND |
| J2 | Molex 53261-0271 PicoBlade 1.25 mm, right-angle SMD (C177225) | SMD, bottom side | battery | pin 1 = GND, pin 2 = BAT+ (swapped 2026-10-23 to match the pack lead), pins 3-4 GND. Replaced on 2026-10-23: the first pick, JST SH 1.0 mm (C160402), did not match the pack's 1.25 mm plug and shipped on the first 25-board batch; on those boards use the BAT+/BAT- solder pads or re-plug the pack |
| I2C_EXT (was J3) | 5-pin 2.54 mm header PZ254V-11-05P (C492404), no 3D model | through-hole, pin 1 at the original position, pin 5 added at X 30.7 | I2C expansion connector on the always-on bus: 3V3, GND, FSCL (GPIO16), FSDA (GPIO15), INT = EXP_INT on GPIO21 (RTC, wake-capable) | changed 2026-10-21: no longer on the OLED bus, so sensors such as a BMA400 keep running in deep sleep and can wake the remote through EXT1 together with the buttons. GPIO0 stays dedicated to the POWER button |
| F1 | BSMD1206-100-16V, 1.0 A hold / 2 A trip PTC (C883131) | 1206 | VBUS protection | changed from 0.5 A during the 2026-10-21 audit: with the charger's 500 mA input limit a 0.5 A hold fuse would sit at its hold current |
| D1 | PESD5V0S1BA (C2827694) | SOD-323 | VBUS ESD/surge clamp, 5 V standoff, bidirectional | replaced the SMBJ5.0A on 2026-10-21 because no SMB-size 5 V TVS was in JLCPCB stock; the USBLC6 also clamps VBUS |
| Q1 | AO3401A | SOT-23 | high-side P-MOSFET, OLED_3V3 | 100 k gate pull-up to 3V3, source to 3V3 |
| Q2 | 2N7002 | SOT-23 | level driver for Q1 gate | gate from GPIO via 1 k, 100 k pull-down so the OLED stays off during reset and sleep |
| R | 4.7 k x 2 OLED bus pull-ups from OLED_3V3; 10 k x 2 fuel-gauge bus pull-ups from 3V3 | 0603 | no leakage into the unpowered OLED | |
| SW1..SW10 | HYP 1TS005F-2500-5001 (C255812) | SMD 4-pad gull wing, pads on an 8.4 x 4.5 mm grid | buttons | 6 x 6 mm, 5.0 mm high, round top actuator, 2.5 N, 3D model. Wired on diagonal pads 1 and 4 so the switch is correct whichever pad pair is internally common; pads 2 and 3 mechanical only. Chosen 2026-10-21 for JLCPCB stock (30k) after KFC-A06-21ATH went out of stock. Unchanged centres |
| SW11 | 3x4 mm side tact or 2 pads | SMD | RESET (EN) | recovery, optional but recommended |
| LED1 | XINGLIGHT XL-1608SURC-04 red (C965798) | 0603, footprint LED0603-RD | CHG indicator, 1 k from VBUS to /CHG | draws nothing from the battery; same footprint and silkscreen as LED2; stock substitute 2026-10-21 |
| LED2 | Hongli KT-0603G green (C12624) | 0603, footprint LED0603-RD | PGOOD indicator, 1 k from VBUS | optional; stock substitute 2026-10-21 |
| PROG (was J4) | 4-pin 2.54 mm header PZ254V-11-04P (C2691448), DNP, no 3D model | through-hole, bottom-right corner | recovery over UART if USB-JTAG is ever bricked: pin 1 TXD0, 2 RXD0, 3 EN, 4 GND, labelled TX RX EN GND on the silkscreen (both v4 and v4.1) | renamed from J4 on 2026-10-21 |
| R | 10 k EN pull-up, 100 k x 2 (Q1, Q2), 1 k x 3, 5.1 k x 2, 1.8 k, 1.1 k, 10 k TS | 0603 | | |
| C | 1 uF on EN, 10 uF + 0.1 uF at module 3V3, 10 uF VBUS, 10 uF BAT, 10 uF + 22 uF buck, 0.1 uF MAX17048, 4.7 uF OLED_3V3 | 0603 / 0805 | | |
| L1 | 2.2 uH, 1 A, 100 mOhm or less | 2016 or 2520 | buck inductor | omitted with the LDO fallback |

Button pull-ups use the ESP32-S3 internal RTC pull-ups (about 45 k). External
100 k pull-ups can be fitted later as DNP footprints if the internal ones prove
too weak against the enclosure's leakage.

### 2.2 GPIO mapping (ESP32-S3-MINI-1-N8)

Rules applied: all buttons on RTC-capable GPIO0..21 so that EXT1 wake works
from any of them; strapping pins 3, 45, 46 avoided; GPIO19/20 reserved for USB;
GPIO26..37 not used (flash and reserved pins on the MINI-1).

| Signal | GPIO | Notes |
|---|---|---|
| POWER (SW) | 0 | strapping pin: holding POWER while resetting enters the ROM bootloader, which gives programming recovery for free |
| SELECT | 1 | RTC |
| ECO | 2 | RTC |
| AC_POWER | 4 | RTC |
| MODE | 5 | RTC |
| FAN | 6 | RTC |
| UP_1 | 7 | RTC |
| UP_2 | 10 | RTC |
| DOWN_1 | 11 | RTC |
| DOWN_2 | 12 | RTC |
| OLED SDA | 8 | OLED bus; 4.7 k pull-ups powered from the switched OLED_3V3 rail so the bus is dead when the OLED is off; driven low before deep sleep |
| OLED SCL | 9 | |
| FUEL SDA | 15 | second I2C bus for the MAX17048, 10 k pull-ups to 3V3, idle bus draws nothing; readable with the OLED off |
| FUEL SCL | 16 | |
| FUEL_ALRT | 13 | MAX17048 alert, RTC, can wake on low battery |
| EXP_INT | 21 | J3 pin 5, interrupt from an expansion sensor such as a BMA400, RTC, wake-capable, 100 k pull-up R23 |
| VBUS_SENSE | 17 | USB presence, VBUS / 2 through R21/R22, RTC-capable for wake on cable insertion |
| OLED_EN | 14 | high, enables the high-side switch; held low during deep sleep |
| USB D- | 19 | native USB-Serial-JTAG and USB-OTG |
| USB D+ | 20 | |
| U0TXD / U0RXD | 43 / 44 | test pads only |
| EN | - | 10 k pull-up, 1 uF to GND, RESET button |

Firmware changes implied: board esp32-s3-devkitc-1, framework esp-idf, logger on
USB_SERIAL_JTAG, deep_sleep wake from all ten buttons through EXT1 ANY_LOW,
MAX17048 sensor over I2C instead of the ADC divider.

### 2.3 Standby budget (deep sleep, OLED off, USB unplugged)

| Consumer | Typical |
|---|---|
| ESP32-S3-MINI-1 deep sleep | 7 uA |
| BQ24075 battery leakage, no input | 3 uA (6 uA max) |
| MAX17048 in hibernate | 3 uA |
| TPS62840 | 0.06 uA (XC6220 LDO: 8 uA) |
| Button pull-ups, buttons released | 0 |
| OLED through the high-side switch | 0 |
| Total | about 13 uA, versus about 300 uA today |

A 1000 mAh cell would last years in standby. Runtime is dominated by Wi-Fi awake time.

### 2.4 Schematic status (2026-10-21)

Drawn in EasyEDA Pro as board "ESP32 Remote v5", schematic "ESP32 Remote v5",
page "Main", in the same project as the old board. 56 parts, 48 nets, 214 wire
stubs. ERC passes in strict and normal mode with zero errors and zero warnings.
Unused module pins are marked no-connect. The exported PADS netlist was checked
line by line against the GPIO map in 2.2. The drawing is a placement grid with
named wire stubs rather than a hand-routed sheet; connectivity is by net name.

### 2.5 PCB status (2026-10-21)

Board "ESP32 Remote v5", PCB "ESP32 Remote v5" in the same EasyEDA project.
36.8 x 133.3 mm, two layers, 56 parts. Re-routed 2026-10-21 after the final
manual placement (reset button renamed RESET and moved to the bottom side next
to the LEDs, J3 shifted left) and again after the stock substitutions: 743
tracks, 87 vias, filled GND pours on both layers, every net connected, DRC
zero violations in strict mode. All BOM lines were in JLCPCB stock on
2026-10-21 (TPS62840 kept despite its price).
Constrained footprints (10 switches, pogo pads, J3 header, USB-C, module,
mounting holes) are locked. Solid GND pours on both layers, islands removed,
rebuilt and verified: final DRC zero violations, zero open connections, and the
exported copper Gerbers contain the filled regions (antenna corner excluded by
the keep-out).

Exports for this buck design (board v4.1) are in `hardware/v4_1_outputs/`:
`ESP32_Remote_v4_1_BOM.csv`, `ESP32_Remote_v4_1_CPL.csv` (pick-and-place, mm),
`ESP32_Remote_v4_1_CPL_jlc.csv` (J1 rotated for JLCPCB's preview) and
`ESP32_Remote_v4_1_Gerber.zip`. The LDO variant (board v4) is in
`hardware/v4_outputs/` with the same file set named `ESP32_Remote_v4_*`.

### 2.6 Design audit (2026-10-21)

Checked the exported netlist pin by pin against the GPIO map and the
datasheet pin functions of BQ24075, TPS62840, MAX17048, USBLC6-2SC6,
AO3401A/2N7002 and the USB-C receptacle. Result: all 45 nets have at least two
pins, every intended connection is present, no unintended ones. Findings and
actions:

- BQ24075 EN straps implement the 500 mA USB mode, not ILIM mode as earlier text
  said. Kept (safer for unknown USB ports); text corrected.
- F1 was a 0.5 A hold PTC, equal to the charger's input limit. Replaced by a
  1 A hold part (C883131), same 1206 footprint; PCB position unchanged.
- The OLED pogo pads had no schematic representation. Added a schematic-only
  connector symbol H1 (not in BOM, not transferred to PCB) showing SDA, SCL,
  3V3 and GND on the switched OLED nets.
- The schematic sheet was reorganised into eight framed, titled functional
  blocks with every net stub labelled; the netlist before and after is
  identical, ERC passes, and the PCB DRC still passes after re-import.
- Open points unchanged: EXT1 any-low wake in ESPHome, KFC-A06 pad pairing,
  USB-C protrusion vs enclosure, J3 sensor INT drive type, and the fact that a
  J3 module adds its own sleep current to the always-on 3V3 rail.

### 2.7 Pre-order verification against primary sources (2026-10-21)

- ESP32-S3 EXT1 wake: ESP-IDF documents ESP_EXT1_WAKEUP_ANY_LOW for the S3 on
  GPIO0-21, so all ten active-low buttons plus the GPIO21 interrupt can be in
  one wake mask. Resolves the earlier open point.
- BQ24075 (TI SLUS810N): pin map matches the symbol; CE is active-low and tied
  low (charging enabled); SYSOFF tied low (battery connected to OUT); EN2 = 0,
  EN1 = 1 is the USB500 mode (500 mA input limit); ISET 1.5 k gives
  890 / 1500 = 593 mA (allowed 590 to 8900 ohm); TMR 47 k is inside the 18 to
  72 k range and gives a 6.3 h fast-charge timer; TS 10 k to VSS is the
  datasheet's prescribed no-thermistor connection; ILIM is fitted (an open ILIM
  would disable charging); IBAT(PDWN) sleep current into BAT is 4.3 uA typical,
  6.5 uA max.
- TPS62840 (TI SLVSEC6D): VSET 267 k selects 3.3 V on the TPS62840; MODE and
  STOP low; EN to VIN; rated 750 mA, above the module's Wi-Fi TX peaks.
- MAX17048: CTG to ground, QSTRT to ground when unused, ALRT open-drain with
  pull-up, VDD to the cell; CELL is not internally connected on the MAX17048 so
  tying it to BAT is harmless.
- Netlist: 45 nets, none with a single pin; every pin of U1..U5, Q1, Q2 matched
  to its datasheet function.

Standby estimate (deep sleep, USB unplugged, OLED off): module ~7 uA, charger
4.3 uA, gauge in hibernate 3 uA, buck 0.06 uA, all pull-ups idle: about 15 uA.
Awake with Wi-Fi power save: 80 to 120 mA average; 1200 mAh gives roughly 10
hours of continuous awake time, which is years of normal remote use.

### 2.8 Pre-order fixes applied after independent review (2026-10-21)

1. OLED switch soft-start: R20 10 k between Q2 drain (net Q2D) and Q1 gate, C11
   10 nF gate-to-source on Q1; turn-on time about 100 us so C10 and the panel do
   not pull the 3V3 rail down when the display is enabled.
2. USB presence sense: R21/R22 100 k / 100 k divider from VBUS to net VBUS_SENSE
   on GPIO17 (U1 pin 21), 2.5 V when USB is present, 25 uA only while plugged in.
   Firmware can detect charging and wake on cable insertion (GPIO17 is RTC).
3. EXP_INT pull-up: R23 100 k to 3V3 so GPIO21 never floats in the wake mask
   when J3 is unpopulated.
4. BQ24075 EN1 strap moved from 3V3 to VBUS: removes up to 10 uA of EN1 input
   current from the sleep budget and selects USB500 the instant USB arrives,
   even with a dead battery.

Reviewer notes carried to firmware: disable the I2C driver's internal pull-ups
on GPIO8/9 and drive them low before deep sleep; tolerate NACK from the gauge
when no battery is fitted; FUEL_ALRT asserted costs 33 uA through R8 until
cleared. Standby estimate after the fixes: about 15 uA typical. Final board:
61 parts, 722 tracks, 72 vias, filled pours, DRC zero violations.

### 2.9 Project naming and the v4.1 LDO variant (2026-10-21)

Boards in the EasyEDA project (final naming, 2026-10-21): the original board
is "ESP32 Remote v3.2" (its PCB "ESP32 Remote v3.2 OLED Mosfet"); "ESP32
Remote v4" is the redesign with the Torex XC6220B331MR-G LDO as U3 (C86534,
SOT-25: VIN and CE to SYS, VSS to GND, VOUT to 3V3, NC open; C5 10 uF and C6
22 uF as its capacitors; no L1 or R19); "ESP32 Remote v4.1" is the identical
design with the TPS62840DLCR buck (U3, L1 2.2 uH, R19 267 k) described in the
rest of this document. v4.1 costs about 2.80 USD more per board, saves 8 uA in
standby (15 uA vs 23 uA) and gives roughly 10 to 15 percent more awake runtime.
Exports: `hardware/v4_outputs/` (LDO) and `hardware/v4_1_outputs/` (buck).

## 3. Items that need manual electrical verification

1. EXT1 ANY_LOW wake in ESPHome: the ESP-IDF call supports it on the S3, but the
   ESPHome deep_sleep component may only expose ALL_LOW and ANY_HIGH. If so, a
   small lambda calling esp_sleep_enable_ext1_wakeup is needed, or ten wake
   diodes into one wake line as a hardware fallback. The schematic will include
   DNP footprints for the diodes so the layout does not depend on the answer.
2. SMD switch height: confirm on the chosen part's drawing that the actuator top
   sits 5.0 mm above the PCB, the same as the SH-6X6X5H-CJ it replaces.
3. Antenna keep-out: the MINI-1 antenna sits at the right board edge with no
   copper under it on either layer; check that the enclosure has no metal there
   and that the battery on the back does not extend under it.
4. GPIO0 as POWER: a press during power-up enters the bootloader instead of
   booting. This matches the current design and is the intended recovery path.
5. USB-C at the bottom centre sits between the two bottom mounting holes, 2.6 mm
   from the edge: check the enclosure cut-out and screw-boss clearance against
   the receptacle shell (about 9 mm wide, 7.3 mm deep).

## 4. Next steps

1. Approve or amend the component list and GPIO map above.
2. Draw the new schematic in a new board inside the same project, run ERC.
3. Create the PCB from it, lock the constrained footprints at the coordinates in
   section 1.2, place and route the rest, run DRC.
4. Export BOM, pick-and-place and Gerber files.
