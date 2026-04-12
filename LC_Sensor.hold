// LC_Sensor.h - Inductive Proximity Sensor Library for Arduino Nano
/*
  LC_Sensor: Multi-channel inductive proximity detection
  
  Features:
  - Up to 8 configurable channels (A0-A7)
  - LC oscillator detection (~280 kHz)
  - Fast 8-bit ADC input
  - Ring-buffer based signal filtering (32 samples)
  - Configurable trigger & hold times
  - Calibration support
  
  Hardware:
  - Comparator: AIN0 (D6) vs AIN1 (D7)
  - Timer: Timer2 @ ~400 Hz sampling rate
  - Pull-ups: Globally disabled (LC interference)
  
  Copyright:
  - Original concept: A. Messmer 2025
  - Development: A. Mascheck 2026
  - License: GNU GPL V3
*/

#ifndef LC_SENSOR_H
#define LC_SENSOR_H
#include "Arduino.h"

// ============================================================================
// SYSTEM CONFIGURATION
// ============================================================================
#define MAX_SENSORS        8       // Total channels (A0-A7)
#define REF_CHANNEL        7       // Reference measurement channel
#define SIGNAL_HOLD        400     // Default hold time (1/400s units)

#define BUF_SIZE           32      // Ring buffer size (samples)
#define BUF_SHIFT          5       // log2(BUF_SIZE)
#define BUF_ROUNDING       16      // Rounding constant: (1 << (BUF_SHIFT-1))

// ============================================================================
// CHANNEL CONFIGURATION
// ============================================================================
#define C_UNUSED           0       // Channel not used (user available)
#define C_RESERVED         1       // Channel reserved (do not use)
#define C_LC_SENSOR        2       // LC inductive sensor
#define C_ANALOG_INPUT     3       // Fast ADC analog input (0-255)

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
struct ChannelConfig {
    volatile uint8_t *port;        // PORT register address
    volatile uint8_t *ddr;         // DDR register address
    uint8_t bitmask;               // Bit position on port
    uint8_t type;                  // Channel type (C_LC_SENSOR, etc.)
};

extern const ChannelConfig channels[] PROGMEM;

// ============================================================================
// SENSOR STATE (exported for monitoring)
// ============================================================================
extern volatile uint16_t signalLevel[MAX_SENSORS];     // Signal output per channel
extern volatile uint8_t  referenceLevel[MAX_SENSORS];  // Baseline per channel
extern volatile uint16_t holdTime;                     // Signal hold time
extern volatile uint8_t  triggerRepeat;                // Repetition filter count
extern volatile uint8_t  triggerThreshold;             // Detection threshold
extern volatile uint8_t  nrSensors;                    // Configured sensor count

// ============================================================================
// CALIBRATION STATE
// ============================================================================
enum SystemState {
    STATE_CALIBRATION,             // Calibration phase
    STATE_RUNNING                  // Normal operation
};

extern volatile SystemState systemState;

// ============================================================================
// LC_SENSOR CLASS
// ============================================================================
class LC_Sensor {
public:
    // Constructor
    LC_Sensor(void);
    
    // Initialization & Control
    bool begin(uint8_t repeat, uint16_t hold, uint8_t threshold, uint8_t zero,uint8_t refCh);
    bool end(void);
    uint8_t reCalibrate(uint8_t channel);
    
    // Sensor Reading
    uint16_t read(uint8_t channel);     // Get signal level (0-holdTime)
    bool activ(uint8_t channel);        // Quick: signal > 0 ?
    
    // Configuration & Status
    uint8_t pins(void);                 // Get number of active sensors
    int zero(uint8_t channel);          // Get baseline for channel
    bool isRunning(void);               // Is in STATE_RUNNING ?
    
    // Debug
    int Debug(void);                    // Get debug value
};

#endif // LC_SENSOR_H