# EX-IOLC_proxy: Inductive LC Sensor Processing for Model Railroads

## Abstract

EX-IOLC_proxy is a software implementation based on PeteGSX's EX-IOExpander, designed to process inductive LC sensors for metal detection on model railways. It provides reliable detection of locomotives and wagons with a success rate exceeding 99.9%, using small coils and LC resonators monitored via an Arduino Nano. The system supports sequential sensor measurement, simple I2C interfacing, and flexible experimental configurations, offering a practical and noise-robust solution for modern model railway automation.

---

## Purpose

This document describes the **EX-IOLC_proxy** program, a modification of PeteGSX's **EX-IOExpander**, designed to process inductive LC (inductor-capacitor) sensors for detecting metal objects on model railways.

The program operates reliably with a detection rate exceeding **99.9%**. Feedback from users regarding practical applications and improvements is welcome.

**Repository:** [EX-IOLC_proxy on GitHub](https://github.com/mascheck55/EX-IOLC_proxy)

For the time being only SensorTester5 is ready for individual tests
---

## Technical Concept

### Core Principle of Operation

* Small 6 mm diameter coils placed between the rails detect metal (e.g., locomotives or wagons).
* Each coil forms an LC resonator, which is excited with a high-frequency impulse to induce oscillation.
* An analog comparator counts falling zero-crossings, which decrease when metal approaches the sensor.

The method was inspired by Albert Messmer's original design (archived [here](https://web.archive.org/web/20250808132851/http://coole-basteleien.de/naeherungssensor)).

### Electronics Architecture

* One GPIO pin drives a 2.5 V midpoint reference to the sensor's base connection.
* Another GPIO pin sends a single excitation pulse to the LC resonator.
* An analog comparator input (D6/AIN0) measures the oscillation frequency via zero-crossing detection.
* Measurement cycles run sequentially via interrupt service routine (ISR) at approximately 400 Hz per sensor.

### Signal Processing (Version 5+)

Signal processing has been significantly improved:

* **Ring Buffer Filtering:** 32-sample moving average provides robust filtering.
* **Symmetric Rounding:** Rounding rules are optimized so the averaged value increases and decreases symmetrically (counts range 0-50).
* **Auto-Calibration:** On startup, a user-selectable reference sensor measures the inactive state. All other sensors assume the same physical properties and are calibrated accordingly.

#### Initialization Example

```
bool success = mysensor.begin(uint8_t repeat, uint16_t hold, uint8_t threshold, uint8_t zero);
```

**Parameters:**

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `repeat` | 1 | 1-79 | Repetition filter (consecutive detections required) |
| `hold` | 200 | 0-799 | Signal hold time (in 1/400 second units) |
| `threshold` | 2 | 1-9 | Detection threshold (delta from baseline) |
| `zero` | 45 | 0-49 | Baseline count (inactive state). 0 = auto-calibrate |

These parameters can be tuned to reduce noise or adjust sensitivity for specific applications.

The parameters work together with a ring buffer that acts as a noise filter. For different model railway gauges, the values need to be adapted to match the speed and size of the trains.

At higher speeds, the signal detection should be more “peaky” (short and pronounced). To achieve this, the coil diameter can be increased to around 8–9 mm and the coil made flatter. This results in a stronger and more distinct signal, allowing (and requiring) a higher detection threshold.

**Recommended Settings** (for model railroad)

| Gauge | Buffer Size | Filter Time | Repeat | Threshold | Hold |
| N | 32 | 80ms | 3 | 2 | 100 |
| H0 | 16 | 40ms | 2 | 3 | 200 |

#### Auto-Calibration

For simplest operation with auto-calibration:

```
mysensor.begin(0, 0, 0, 0);
```

**Note:** The calibration sensor must be connected to A0 in this mode.
## Hardware Constraints

### ATmega328P Limitations

* **Single Analog Comparator:** Only one comparator available; sensors must be multiplexed sequentially.
* **Analog Input Pins:** A6 and A7 cannot be used as GPIO outputs. Pins D3 and D4 provide pulse generation for these channels.
* **Sampling Rate:** Each sensor updates at approximately 400 Hz; adding more sensors reduces the per-sensor update frequency proportionally.
* **CPU Performance:** Faster processors or optimized ISR implementations allow handling of more sensors.

### Pin Usage Summary

| Pin | Function | Notes |
|-----|----------|-------|
| D3  | Pulse output (A7 equivalent) | Output for channel 7 |
| D4  | Pulse output (A6 equivalent) | Output for channel 6 |
| D6  | Analog Comparator (AIN0) | Internal use, reserved |
| A0-A5 | LC sensor inputs | Port C (PORTC) |
| A6-A7 | LC sensor inputs | Port D (PORTD) via D3, D4 |
| A4, A5 | Optional I2C | Can be freed if not needed for sensors |

---

## Excitation Pulse ("Initial Stroke")

### Principle

Each sensor receives a short excitation pulse to initiate LC oscillation, similar to plucking a guitar string.

### Pulse Design

The revised implementation uses a simplified and optimized pulse sequence:

* **Total Duration:** 1.5 µs
  * 0.5 µs LOW: Discharge the LC circuit
  * 0.5 µs TRISTATE: Allow settling
  * 0.5 µs HIGH: Excite the oscillator
  
* **Benefits of Short Pulse:**
  * Eliminates CPU-specific timing dependencies
  * Reduces peak current draw
  * Simplifies electronics (series resistor removed)
  * Improves noise immunity

### Implementation

```
cli();                                      // Critical section start
*port &= ~bitmask;                         // Drive LOW
*ddr |= bitmask;                           // Output mode
_delay_us(0.5);

*ddr &= ~bitmask;                          // Tristate (release)
_delay_us(0.5);

*port |= bitmask;                          // Drive HIGH
*ddr |= bitmask;                           // Output mode
_delay_us(0.5);

*ddr &= ~bitmask;                          // Tristate (measure)
comparatorCount = 0;                       // Reset counter
sei();                                      // Critical section end
```

---

## I2C Interface (Proposed)

A simple I2C interface could significantly reduce code complexity:

* **Data Format:** 1 byte per I2C request (8 sensors, 1 bit per sensor)
* **Configuration:** I2C address and sensitivity tuning via register-based control
* **Benefits:** Eliminates ~90% of non-sensor-related code

**Status:** I2C implementation support offered by Chris (UKBloke).

---

## Component Specifications

### Electronic Components

| Component | Specification | Example Part Numbers |
|-----------|---|---|
| **Inductor** | Radial, 6-10 µH, ≤0.5 Ω resistance | WE-TI 7447462068 (6.8 µH) |
| **Capacitor** | Ceramic, 47 nF ±10% | C327C473K3G5TA-ND, CL21B473KBCNNNC |
| **MCU** | Arduino Nano V3/V4 | ATmega328P or ATmega328PB |
| **Wiring** | Twisted-pair, 0.6 mm | Distance: up to 1 m |

### Assembly Guidelines

* **Placement:** Position capacitor as close as possible to the inductor to minimize parasitic resistance.
* **Coil Quality:** Low internal resistance (≤0.5 Ω) is critical for reliable oscillation.
* **Wiring:** Use twisted-pair cables for the LC circuit; distances up to 1 meter are acceptable.
* **Flexibility:** Larger coils or alternative LC geometries can be used (e.g., under rail sleepers or in tunnel structures).

## Sensor Placement and Detection Performance

### Optimal Installation

* Mount sensors **vertically and centered** between the rails for uniform sensitivity.
* Typical detection distance: **~3 mm** (metal-dependent; varies with conductivity and shape).

### Detection Characteristics

| Object Type | Detectability | Notes |
|---|---|---|
| **Locomotives** | Reliable | High metal content; consistently detected |
| **Powered Wagons** | Reliable | Axles and metalwork provide detection |
| **Unpowered Wagons** | Variable | Requires more metal; may need proximity |

### Detection Factors

* Conductivity of approaching metal
* Distance to sensor (typically 3 mm optimal range)
* Shape and mass of metal object (washers, axles, etc.)
* Tuning of LC circuit Excitation Pulse
* Threshold typically TWO if ONE smaller changes will be detected, repeat your measurement to avoid misdetection.

### Scaling

For larger model scales, larger coil diameters can extend the detection range by increasing the magnetic field strength.

---

## Design Features and Advantages

### Key Strengths

* **Inductive Detection:** Functions like a metal detector; no capacitive sensing artifacts.
* **Unpowered Car Tracking:** Can detect static wagons without power supplies.
* **Short-Range Detection:** Minimizes crosstalk and interference from adjacent tracks.
* **Experimental Flexibility:** Supports custom sensor configurations and frequencies (100-300 kHz range).
* **Minimal Dependencies:** No external libraries; avoids compatibility issues.
* **Noise Immunity:** Optimized by removing series resistors, using shorter pulses, and tuning capacitor value beside the voltage divider.

### Optional I2C Support

Pins A4 and A5 can be freed for I2C communication, allowing sensor data to be shared with other devices without dedicated GPIO pins.

---

## Technical Reference Table

| Feature / Parameter | Value / Description |
|---|---|
| **Sensor Type** | Inductive LC resonator (coil + capacitor) |
| **Coil Diameter** | 6-10 mm (typical) |
| **Coil Resistance** | ≤ 0.5 Ω (recommended for stability) |
| **Capacitor Type** | Ceramic, 47 nF (±10% tolerance) |
| **MCU Platform** | Arduino Nano with ATmega328P or ATmega328PB |
| **Analog Inputs** | A0-A7 (8 channels total) |
| **Digital Pins Used** | D3, D4 (pulse output), D6 (comparator input) |
| **Optional I2C** | A4 (SDA), A5 (SCL) or D23, D24 (Nano V4) |
| **Reference Voltage** | 2.5 V midpoint on sensor base |
| **Pulse Duration** | 1.5 µs total (0.5 µs phases) |
| **Sampling Rate** | ~400 Hz per sensor |
| **Detection Threshold** | 40 zero-crossings (configurable) |
| **Repetition Filter** | 5 consecutive detections (tunable) |
| **Detection Distance** | ~3 mm typical (metal and geometry dependent) |
| **Detectable Objects** | Locomotives, wagons, and conductive bodies |
| **Wiring Distance** | Up to 1 m (twisted-pair preferred) |
| **LC Frequency Range** | 100-300 kHz (tunable via C and L values) |
| **Noise Mitigation** | Removed series resistor, optimized pulse timing, tuned capacitor placement |
| **External Libraries** | None (simplicity and compatibility focus) |

---

## Related Resources

* **Original Concept:** Albert Messmer's inductive sensor design ([archived](https://web.archive.org/web/20250808132851/http://coole-basteleien.de/naeherungssensor))
* **Test Repository:** [SensorTester5](https://github.com/mascheck55/SensorTester5)
* **Main Repository:** [EX-IOLC_proxy](https://github.com/mascheck55/EX-IOLC_proxy)

---

## Conclusion

EX-IOLC_proxy provides a practical, noise-robust solution for inductive metal detection on model railways. With proper component selection, sensor placement, and parameter tuning, detection rates exceeding 99.9% are achievable. The system's flexibility supports both standardized installations and experimental setups, making it suitable for various model railway layouts and automation requirements.

User feedback and practical insights are welcome for continued development and optimization.

