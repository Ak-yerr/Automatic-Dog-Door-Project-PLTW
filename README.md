cat > /mnt/user-data/outputs/dog-door/README.md << 'EOF'
# Automatic Dog Door

This is a project that me and three other engineering students built as our final capstone project for the Project Lead the Way (PLTW) program from November 2024 to May 2025. This project was presented to a board of industry experts who gave a 96% functionality rating on our prototype.

An Arduino-based automatic dog door that uses a PIR motion sensor to detect approach, an RC522 RFID reader to verify an authorized tag on the dog's collar, and a NEMA 17 stepper motor to open and close the door. Access events are logged with real timestamps to a local web server running on a connected PC, with a web UI for monitoring and manual override. The system runs fully standalone when no PC is connected.

---

## Table of Contents

- [Hardware](#hardware)
- [Wiring](#wiring)
- [Mechanism Development](#mechanism-development)
- [Calibration](#calibration)
- [Software](#software)
- [Setup](#setup)
- [Usage](#usage)
- [File Structure](#file-structure)

---

## Hardware

| Component | Purpose |
|---|---|
| Arduino UNO | Main controller |
| PIR Motion Sensor | Detects dog approaching the door |
| RC522 RFID Module | Reads authorized tag on dog's collar |
| NEMA 17 Bipolar Stepper Motor | Drives the door mechanism |
| A4988 Stepper Motor Driver Carrier | Controls stepper current and step pulses |
| LED — Red | Status: door closed / idle / denied |
| LED — Yellow | Status: door open |
| Momentary pushbutton | Physical manual override |
| 9V Battery | Dedicated motor power rail |
| Breadboard + jumper wires | Prototyping and connections |

**Additional components required:**

- 100µF electrolytic capacitor (across A4988 VMOT and GND — required, protects against back-EMF)
- 2× 220Ω resistors (LEDs)
- 9V battery connector / barrel jack

---

## Wiring

### Pin assignments

| Arduino Pin | Connected to |
|---|---|
| D2 | PIR OUT |
| D3 | A4988 STEP |
| D4 | A4988 DIR |
| D5 | LED Red (via 220Ω) |
| D6 | Manual override button (INPUT_PULLUP) |
| D7 | LCD EN *(unused in firmware)* |
| D8 | LCD RS *(unused in firmware)* |
| D9 | RC522 RST |
| D10 | RC522 SDA (SS) |
| D11 | RC522 MOSI (SPI) |
| D12 | RC522 MISO (SPI) |
| D13 | RC522 SCK (SPI) |
| A0 | LCD D5 *(unused in firmware)* |
| A1 | LCD D6 *(unused in firmware)* |
| A2 | LCD D7 *(unused in firmware)* |
| A3 | LED Yellow (via 220Ω) |

### Power

- **9V battery** → A4988 VMOT + (motor supply)
- **Arduino 5V** → breadboard logic rail (PIR, LEDs, A4988 VDD)
- **Arduino 3.3V** → RC522 VCC (the RC522 is a 3.3V device — do not connect to 5V)
- All grounds joined: 9V battery −, Arduino GND, A4988 GND (both motor and logic GND pins)

### A4988 notes

- Trim the current-limit potentiometer on the A4988 before first run. Set VREF = (motor rated current × 8 × Rsense). For a typical NEMA 17 rated at 1.5A with 0.1Ω sense resistors, target VREF ≈ 0.75V measured at the pot wiper against GND.
- MS1, MS2, MS3 tied to GND → full-step mode. Adjust for microstepping if finer resolution is needed (see [Calibration](#calibration)).
- The 100µF capacitor across VMOT and GND is not optional.

---

## Mechanism Development

Getting the physical door mechanism right took considerably longer than the electronics. Several approaches were prototyped and discarded before landing on the final design, including linear actuators, DC motors, and various types of gear trains and axles.

The final design uses a hinged axle system to keep our "door" in place with the frame. In order to move it between the "open" and "shut" states, we used a worm gear-conventional gear train system that has an automatic "lock" built into it, as the worm gear cannot be actuated in the opposite direction. In a commercial product, this system would be far smaller and hidden, but for demonstration purposes, we 3D printed clear-cut, large visual demo components. This is the mechanism that ended up working best and most simply for our design.

## Calibration

Calibration is the most important step after assembly. The firmware uses open-loop step counting, so incorrect calibration will cause the door to over-travel (motor stalls against the frame) or under-travel (door does not fully open).

### `DOOR_STEPS`

`DOOR_STEPS` in `dog_door.ino` defines the number of stepper steps for full door travel. This must be measured for your specific mechanism.

**Procedure:**

1. Set `DOOR_STEPS` to a deliberately low value (e.g. `50`) so the door cannot over-travel on first power-up.
2. Upload the sketch and trigger the door via the web UI override button.
3. Measure how far the door moved. The relationship is linear:

```
DOOR_STEPS_required = (measured steps) × (full travel distance) / (actual travel distance)
```

4. For full-step mode with a standard NEMA 17 (1.8°/step = 200 steps/revolution) and a 20mm pulley, one revolution moves the cord approximately 62.8mm. So for a 120mm door travel:

```
revolutions needed = 120mm / 62.8mm ≈ 1.91
DOOR_STEPS = 1.91 × 200 = 382
```

5. Increase `DOOR_STEPS` in increments of 20, re-uploading and testing each time, until the door reaches full open without stalling. Back off by 10 steps from the point of stall.

### Microstepping

Full-step mode (MS1/MS2/MS3 all LOW) gives maximum torque but coarser resolution and more vibration. If the door movement is jerky, connect MS1 to 5V for half-step mode (400 steps/revolution), or MS1+MS2 for quarter-step (800 steps/revolution), and double/quadruple `DOOR_STEPS` accordingly. Quarter-step is the recommended default for quiet operation.

### `STEPPER_SPEED` and `STEPPER_ACCEL`

`STEPPER_SPEED` (steps/sec) and `STEPPER_ACCEL` (steps/sec²) are set conservatively at 500 and 200. If the motor stalls at the start of motion, reduce `STEPPER_ACCEL`. If the door is too slow, increase `STEPPER_SPEED` in increments of 100, staying below the motor's resonance band (typically 800–1200 steps/sec in full-step mode).

### PIR sensitivity and timing

Most PIR modules have two trim potentiometers on the back — sensitivity (detection range) and hold time (how long the output stays HIGH after motion stops). Set the hold time to its minimum value (usually a turn fully counter-clockwise); the firmware manages its own timing via `PIR_RECHECK` and `DOOR_HOLD`. Sensitivity should be set so the PIR reliably triggers at the distance your dog approaches from, without false-triggering from movement elsewhere in the room.

### RFID read distance

The RC522's read range is approximately 3–5cm on a standard 13.56MHz ISO/IEC 14443 tag. Position the reader so the dog's collar tag passes within range naturally during approach — typically mounted at collar height on the door frame. Angling the reader 15–20° toward the expected approach direction extends the effective read window.

### A4988 current limit

Incorrect current limit is the most common cause of motor stalls and overheating. Measure VREF at the A4988 trimmer potentiometer pin relative to GND with the motor connected and the driver powered. Adjust until VREF matches your motor's rated current using the formula:

```
VREF = I_rated × 8 × R_sense
```

Check your A4988 carrier board's sense resistor value (Pololu carriers use 0.068Ω; generic clones commonly use 0.1Ω or 0.2Ω — measure with a multimeter if unsure).

---

## Software

### Architecture

The firmware runs a non-blocking state machine with four states:

```
IDLE → WAITING_FOR_RFID → DOOR_OPEN → CLOSING → IDLE
```

`stepper.run()` is called at the top of every `loop()` iteration so the motor always has CPU time regardless of which state is active. No `delay()` calls block the main loop; the only exception is `blinkRed()`, which manually pumps `stepper.run()` during blink intervals.

### PC-enhanced mode

On boot, the Arduino sends `PING` over serial and waits up to 2500ms for a `PONG` from `server.py`. If received, PC-enhanced mode is active for that session. If `server.py` is started after the Arduino has already booted, the Arduino accepts a late `PONG` at any point during operation via `checkSerial()` and upgrades to PC-enhanced mode without a reset.

### Serial protocol

| Direction | Message | Meaning |
|---|---|---|
| Arduino → PC | `PING` | Boot / restart handshake |
| Arduino → PC | `OPEN\|AA:BB:CC:DD` | Door opened, tag UID |
| Arduino → PC | `DENIED\|AA:BB:CC:DD` | Unauthorized tag scanned |
| Arduino → PC | `CLOSED` | Door returned to closed position |
| PC → Arduino | `PONG` | Handshake acknowledgement |
| PC → Arduino | `OVERRIDE` | Open door (web UI or manual trigger) |

### Libraries required

- [MFRC522](https://github.com/miguelbalboa/rfid) — RC522 RFID reader
- [AccelStepper](https://www.airspayce.com/mikem/arduino/AccelStepper/) — stepper control with acceleration

Install via Arduino IDE Library Manager or PlatformIO.

---

## Setup

### 1 — Get your dog's tag UID

Upload `uid_scan.ino`, open Serial Monitor at 9600 baud, and hold the RFID tag near the reader. Copy the "array format" line printed to Serial Monitor.

### 2 — Configure the firmware

Open `dog_door.ino` and replace the placeholder in `AUTHORIZED_TAGS`:

```cpp
const byte AUTHORIZED_TAGS[][4] = {
  { 0xAB, 0xCD, 0xEF, 0x12 },  // your dog's actual uid bytes here
};
```

To authorize multiple tags, add additional rows:

```cpp
const byte AUTHORIZED_TAGS[][4] = {
  { 0xAB, 0xCD, 0xEF, 0x12 },
  { 0x34, 0x56, 0x78, 0x9A },
};
```

### 3 — Calibrate DOOR_STEPS

See [Calibration](#calibration) above. Set `DOOR_STEPS` to the correct value for your mechanism before first full use.

### 4 — Upload

Upload `dog_door.ino` to the Arduino UNO.

### 5 — Run the server (optional)

```bash
pip install pyserial
python server.py
```

The server auto-detects the Arduino port. To specify manually:

```bash
python server.py COM3          # Windows
python server.py /dev/ttyUSB0  # Linux / Mac
```

Open `http://localhost:8080` in a browser.

The access log is saved to `dog_door_log.json` in the same directory as `server.py` and persists across server restarts.

---

## Usage

**Normal operation (standalone):**

1. Dog approaches → PIR triggers
2. Dog presents RFID tag to reader within 10 seconds
3. If authorized: yellow LED on, door opens, holds open for 5 seconds minimum
4. If PIR still active when hold time expires, door waits an additional 2 seconds before closing
5. Door closes, red LED on

**Denied tag:** red LED blinks 3 times, door stays closed, event logged if PC connected.

**Manual override:**
- Physical button on D6: momentary press opens the door immediately regardless of PIR/RFID state
- Web UI button at `http://localhost:8080`: same effect over serial

**LED states:**

| Red | Yellow | State |
|---|---|---|
| ON | OFF | Closed / idle |
| OFF | ON | Open |
| Blinking | OFF | Unauthorized tag |

---

## Notes

- The RC522 **must** be powered from the Arduino's 3.3V pin. Connecting it to 5V will damage the module.
- The 100µF capacitor across A4988 VMOT and GND is required. Without it, back-EMF from the motor when power is cut can destroy the driver instantly.
- `dog_door_log.json` is never automatically cleared. Delete or archive it manually if it grows large.
- The `DOOR_STEPS` constant must be recalibrated if the motor is remounted, the pulley diameter changes, or the microstepping mode is changed.
EOF
echo "done"
