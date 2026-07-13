# Pergola Motor v2 — ESP32 + ESPHome Build Guide

Replaces the CC2530 + Arduino Nano stack with a single ESP32 DevKit
(WROOM-32) running ESPHome (`esphome/pergola-motor.yaml`). Home Assistant
talks to it natively over WiFi — no Zigbee, no UART bridge, no custom
converter.

## Parts

| Part | Notes |
| --- | --- |
| ESP32 DevKit (WROOM-32) | any 30/38-pin DevKit clone |
| DRV8825 on controller expansion board | reused from old build |
| NEMA17 stepper | reused |
| Inductive proximity sensor | 10–30 V supply, **NPN NO** output, reused |
| PSU 12 V / 4 A | new, single output |
| Mini-560 buck, **5 V** output | emo.cz `STEP DOWN 5V/5A` |
| 100 µF / 25 V electrolytic (optional) | expansion board already has one onboard; use as spare or at buck output |
| Wire, screw terminals | logic wires can be thin; 12 V runs ≥ 0.5 mm² |

## Wiring

### Overview

```
                      ┌──────────────────────────────────┐
 230 V ──► PSU 12V ───┼──► expansion board VMOT + GND    │  (onboard 100 µF
                      │         │                        │   35 V confirmed)
                      │         └──► DRV8825 ──► NEMA17  │
                      │                                  │
                      ├──► Mini-560 IN+ / IN−            │
                      │        OUT+ ──► ESP32 VIN (5V)   │
                      │        OUT− ──► ESP32 GND        │
                      │                                  │
                      └──► sensor brown (V+)             │
                           sensor blue  ──► GND          │
                           sensor black ──► ESP32 GPIO32 │
                      └──────────────────────────────────┘
 All grounds meet at the PSU output terminal (star ground).
 Run the expansion-board ground and the logic ground as separate wires.
```

### Pin map

| From | To | Wire |
| --- | --- | --- |
| ESP32 GPIO25 | expansion board STEP (CLK) | signal |
| ESP32 GPIO26 | expansion board DIR (CW) | signal |
| ESP32 GPIO27 | expansion board EN | signal |
| ESP32 3V3 | expansion board VCC / +5V pin (if present) | logic supply for onboard pull-ups — **3.3 V, never 5 V** |
| ESP32 GND | expansion board GND | signal ground |
| ESP32 VIN (5V pin) | Mini-560 OUT+ | power |
| ESP32 GND | Mini-560 OUT− | power |
| Mini-560 IN+ / IN− | PSU 12 V / GND | power |
| Expansion board VMOT / GND | PSU 12 V / GND | power, thick wires |
| Motor coils A+/A−/B+/B− | expansion board motor terminals | keep old pairing |
| Sensor brown | PSU 12 V | power |
| Sensor blue | PSU GND | power |
| Sensor black | ESP32 GPIO32 | signal, **direct — no divider** (NPN open collector) |

### Expansion board specifics

- **Onboard capacitor:** confirmed present on this board — the can marked
  `100 35V` (100 µF / 35 V) across VMOT/GND. No extra cap needed at the
  driver; it protects the DRV8825 from power-on voltage spikes. A spare
  100 µF across the Mini-560 output is optional cheap insurance for motor
  starts.
- **VCC / +5V logic pin:** if the board has one, feed it from the ESP32
  **3.3 V pin, not 5 V**. It typically only powers pull-ups; feeding it 5 V
  would put 5 V on the EN line and the ESP32 GPIO. The DRV8825 registers
  3.3 V logic fine.
- **Microstepping DIP switches (M0/M1/M2):** set to match the old build,
  otherwise all position/speed numbers shift. The old bare-module wiring
  left the MS pins floating = **full step** = all switches OFF. If travel
  or speed later seem off by exactly 2×/4×/…, this is why. Changing
  microstepping is fine — just recalibrate `Travel steps` afterwards.
- **Current limit (Vref):** the DRV8825 module carries its trimpot setting
  with it, so a reused driver is already set. Don't touch it. If you ever
  need to set it: Vref = motor rated current ÷ 2 (e.g. 1.5 A motor →
  0.75 V between the trimpot and GND, measured with only VMOT powered).

### Rules

1. **Never connect or disconnect motor wires while powered.** Breaking an
   energized coil kills stepper drivers instantly. Motor first, power last.
2. 12 V must never touch the ESP32 5 V pin, 3V3 pin, or any GPIO.
3. Electrolytic caps: stripe = negative = GND. Reversed on 12 V = bang.
4. ESP32 GPIOs are not 5 V tolerant. The sensor is safe only because its
   NPN output never sources voltage — it only pulls to GND.

## Step-by-step bring-up

Do it in this order; each step verifies the previous one.

### 1. Flash the bare ESP32 (USB, desk)

1. Install ESPHome (Home Assistant add-on, or `pip install esphome`).
2. Put WiFi credentials in `secrets.yaml` next to the config:
   `wifi_ssid: "..."` / `wifi_password: "..."`.
3. First flash over USB cable:
   `esphome run esphome/pergola-motor.yaml` and pick the serial port.
   (Later updates go over WiFi/OTA automatically.)
4. Check it comes online, HA discovers it (Settings → Devices → ESPHome).
   You should see: cover **Roof**, numbers **Speed** / **Travel steps**,
   button **Home**, sensor **Endstop**. It's fine that homing times out —
   nothing is wired yet.

### 2. Bench-test power (nothing else connected)

1. Wire PSU → Mini-560. Power on. **Measure Mini-560 output: 5.0 V**
   before connecting the ESP32.
2. Connect ESP32 VIN + GND. Power on. ESP32 boots, joins WiFi, shows
   online in HA. Let it run 10 minutes — regulator on the DevKit should be
   at most warm.

### 3. Wire the sensor

1. Power off. Sensor: brown → 12 V, blue → GND, black → GPIO32.
2. Power on. In HA watch **Endstop**: approach the sensor with metal —
   ON; remove — OFF. The sensor's own LED should match.

### 4. Wire the driver + motor

1. Power off. Control wires: GPIO25 → STEP, GPIO26 → DIR, GPIO27 → EN,
   GND → GND, and if the board has a VCC/+5V logic pin: ESP32 3V3 → VCC.
   Expansion board VMOT/GND → 12 V (onboard cap covers spike protection).
   Motor to the motor terminals — same coil pairing as the old build.
2. Check DIP switches (microstepping) and leave the Vref trimpot alone.
   Onboard 100 µF cap is already on the board — nothing to add here.
3. Power on. The boot homing script runs: motor should drive toward the
   closed endstop and stop there. If it drives **away** from the endstop,
   flip the DIR logic: swap one motor coil pair on the terminals (or
   invert `dir_pin` in the YAML) and retest.
4. Homing ends → position reads 0 → cover shows closed.

### 5. Calibrate

1. In HA set **Travel steps** to the known value from the old build
   (last known: 7000), or calibrate fresh: use the cover slider to open
   fully, count what fraction it actually travelled, scale, repeat.
2. Set **Speed** (old default: 1000 steps/s).
3. Test full cycle: open 100 %, close, stop mid-travel, position 50 %.
4. Press **Home** — motor returns to endstop, position re-zeros.

### 6. Install & clean up

1. Mount everything in the enclosure. Keep the 12 V motor wiring away
   from the ESP32 antenna area (the PCB end with the metal-can module).
2. Watch it for a few days — HA availability now reflects the device
   state within seconds (native API heartbeat), and the boot homing
   self-recovers position after any power cut.
3. Retire the Zigbee remains: remove the device from Zigbee2MQTT,
   delete the external converter entry (`soucek-motor.js`), repoint
   automations at the new `cover.roof` entity.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Motor hums, doesn't move | coil pairing wrong — identify pairs with multimeter (coil = ~few Ω) |
| Moves wrong direction at homing | swap one coil pair, or invert `dir_pin` |
| Steps lost / stalls | Vref too low, speed too high, or microstepping mismatch |
| Travel wrong by exact power of 2 | DIP switches ≠ old microstepping; recalibrate Travel steps |
| Endstop never triggers | sensor gap > rated range (typ. 4 mm for LJ12A3-4); NPN sensors need the target to be metal |
| ESP32 reboots when motor starts | shared-rail dip — verify Mini-560 output during motion, add 100 µF at its output |
| Driver very hot | Vref set too high, or EN logic inverted (driver always on — check coils are released when idle) |
