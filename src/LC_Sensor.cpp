// LC_Sensor.cpp - Implementation
/*
  LC_Sensor: Multi-channel inductive proximity detection library
  
  Architecture:
  - Timer2 ISR: Channel scheduling @ ~400 Hz
  - AC ISR: Zero-crossing counter @ 280 kHz
  - Signal processing: Ring-buffer FIR filter (32 samples = 80ms)
  - State machine: Calibration → Running
  
  Signal Flow:
  1. Timer2 ISR: Select channel, store previous, activate next
  2. AC ISR: Count zero-crossings (comparator triggers)
  3. SignalProcessor: Ring-buffer update, threshold detection
  4. Output: signalLevel[ch] holds until decay
  
  Copyright: A. Mascheck 2026, GNU GPL V3
*/

#include <Arduino.h>
#include "LC_Sensor.h"

// ============================================================================
// PLATFORM DETECTION
// ============================================================================
#if defined(ARDUINO_AVR_NANO) || defined(ARDUINO_AVR_UNO)
// PRR already defined
#elif defined(ARDUINO_AVR_ATmega328PB) || defined(__AVR_ATmega328PB__)
#define PRR PRR0
#else
#error "Unsupported board! Add ARDUINO_AVR_* check"
#endif

// ============================================================================
// RUNTIME CONFIGURATION (User Parameters)
// ============================================================================
volatile uint16_t holdTime = SIGNAL_HOLD;
volatile uint8_t triggerRepeat = 1;
volatile uint8_t triggerThreshold = 2;
volatile uint8_t referenceValue = 0;
volatile uint8_t referenceChannel = REF_CHANNEL;
// ============================================================================
// SENSOR STATE (Output)
// ============================================================================
volatile uint16_t signalLevel[MAX_SENSORS];    // Hold counter per channel
volatile uint8_t triggerCounter[MAX_SENSORS];  // Repetition filter per channel

// ============================================================================
// SIGNAL PROCESSING (Filters & Statistics)
// ============================================================================
volatile uint16_t movingSum[MAX_SENSORS];      // FIR sum (32 samples)
volatile uint8_t referenceLevel[MAX_SENSORS];  // Per-channel baseline

// ============================================================================
// SAMPLING BUFFER (Ring Buffer)
// ============================================================================
volatile uint8_t sampleBuffer[MAX_SENSORS][BUF_SIZE];
volatile uint8_t sampleIndex = 0;

// ============================================================================
// SCHEDULER & STATE
// ============================================================================
volatile uint8_t channelIndex = 0;
volatile uint8_t nrSensors = 0;
volatile uint8_t comparatorCount = 0;

// ============================================================================
// HARDWARE STATE FLAGS
// ============================================================================
volatile bool comparatorActive = false;
volatile bool adcActive = false;

// ============================================================================
// DEBUG OUTPUT
// ============================================================================
int debugValue = 0;

// ============================================================================
// CALIBRATION STATE
// ============================================================================
volatile SystemState systemState = STATE_CALIBRATION;
uint8_t calibrationSamples = 0;

// ============================================================================
// CHANNEL CONFIGURATION (from header)
// ============================================================================
const ChannelConfig channels[] PROGMEM = {
  { &PORTC, &DDRC, (1 << 0), C_LC_SENSOR },  // A0 = PC0
  { &PORTC, &DDRC, (1 << 1), C_LC_SENSOR },  // A1 = PC1
  { &PORTC, &DDRC, (1 << 2), C_LC_SENSOR },  // A2 = PC2
  { &PORTC, &DDRC, (1 << 3), C_LC_SENSOR },  // A3 = PC3
  { &PORTC, &DDRC, (1 << 4), C_LC_SENSOR },  // A4 = PC4
  { &PORTC, &DDRC, (1 << 5), C_LC_SENSOR },  // A5 = PC5
  { &PORTD, &DDRD, (1 << 4), C_LC_SENSOR },  // A6 = PD4
  { &PORTD, &DDRD, (1 << 3), C_LC_SENSOR },  // A7 = PD3
};

// ============================================================================
// PRIVATE FUNCTION DECLARATIONS
// ============================================================================
void ReadADC(uint8_t adc_ch);
void SetAnalogComparator(void);
void InitialStroke(uint8_t channel);
void SignalProcessor(uint8_t channel, uint8_t idx);
void StartTimer2_400Hz(void);

// ============================================================================
// PUBLIC CLASS METHODS
// ============================================================================

LC_Sensor::LC_Sensor(void) {
  // Constructor
}

// ============================================================================
bool LC_Sensor::begin(uint8_t repeat, uint16_t hold, uint8_t threshold, uint8_t zero, uint8_t refCh) {
  /*
     * Initialize sensor operation
     * 
     * Parameters:
     * - repeat: Repetition filter (1-79, default 1)
     * - hold: Hold time in 1/400s units (0-799, default 400)
     * - threshold: Detection threshold (1-9, default 2)
     * - zero: Reference level (0-49, default 0=auto-calibrate)
     */

  // Parameter validation & assignment
  if (threshold > 0 && threshold < 10)
    triggerThreshold = threshold;

  if (zero > 0 && zero < 50)
    referenceValue = zero;

  if (zero == 0) {
    systemState = STATE_CALIBRATION;
    calibrationSamples = 0;
    referenceValue = 0;
  }

  if (repeat > 0 && repeat < 80)
    triggerRepeat = repeat;

  if (hold > 0 && hold < 800)
    holdTime = hold;

  if (refCh < 8) referenceChannel = refCh;
  else referenceChannel = REF_CHANNEL;

  // Hardware initialization
  bitSet(MCUCR, PUD);    // Disable all pull-ups globally
  bitClear(PRR, PRADC);  // ADC power on

  // State initialization
  sampleIndex = channelIndex = 0;
  nrSensors = 0;

  for (uint8_t i = 0; i < MAX_SENSORS; i++) {
    triggerCounter[i] = 0;
    signalLevel[i] = 0;
    movingSum[i] = 0;

    // Clear sample buffer
    for (uint8_t j = 0; j < BUF_SIZE; j++)
      sampleBuffer[i][j] = 0;

    // Count active sensors
    uint8_t type = pgm_read_byte(&channels[i].type);
    if (type == C_LC_SENSOR || type == C_ANALOG_INPUT)
      nrSensors++;
  }

  StartTimer2_400Hz();
  sei();  // Enable interrupts
  return true;
}

// ============================================================================
bool LC_Sensor::end(void) {
  cli();
  PORTC = 0x00;
  DDRC = 0x00;
  bitClear(MCUCR, PUD);
  bitSet(PRR, PRADC);
  sei();
  return true;
}

// ============================================================================
// PUBLIC SENSOR METHODS
// ============================================================================

uint16_t LC_Sensor::read(uint8_t channel) {
  if (channel >= MAX_SENSORS)
    return 0;
  return signalLevel[channel];
}

// ============================================================================
int LC_Sensor::zero(uint8_t channel) {
  if (channel >= MAX_SENSORS)
    return 0;
  return referenceLevel[channel];
}

// ============================================================================
bool LC_Sensor::activ(uint8_t channel) {
  return (read(channel) > 0);
}

// ============================================================================
bool LC_Sensor::isRunning(void) {
  return (systemState == STATE_RUNNING);
}

// ============================================================================
int LC_Sensor::Debug(void) {
  return debugValue;
}
// ============================================================================
uint8_t LC_Sensor::reCalibrate(uint8_t channel) {

  systemState = STATE_CALIBRATION;
  calibrationSamples = 0;
  referenceValue = 0;
  referenceChannel=channel;
  while (!isRunning()) delay(40);
  return zero(channel);
}

// ============================================================================
uint8_t LC_Sensor::pins(void) {
  return nrSensors;
}

// ============================================================================
// PRIVATE IMPLEMENTATION
// ============================================================================

void StartTimer2_400Hz(void) {
  /*
     * Timer2 Setup for ~400 Hz sampling rate
     * 
     * Calculation:
     * - Clock: 16 MHz
     * - Mode: CTC (Clear Timer on Compare)
     * - Prescaler: 32
     * - OCR2A: 150
     * - Rate: 16,000,000 / (32 * 150) = 3,333 Hz
     * - Per channel (8 channels): 3,333 / 8 = ~417 Hz
     */
  bitClear(ASSR, AS2);                  // System clock (not RTC)
  TCCR2A = TCCR2B = TIMSK2 = 0x00;      // Reset
  bitSet(TCCR2A, WGM21);                // CTC mode
  TCCR2B |= (1 << CS21) | (1 << CS20);  // Prescaler 32
  OCR2A = 150;                          // Compare value
  TCNT2 = 0;                            // Counter reset
  bitSet(TIMSK2, OCIE2A);               // Enable compare interrupt
}

// ============================================================================
ISR(TIMER2_COMPA_vect, ISR_NOBLOCK) {
  /*
     * Timer2 Compare ISR - ~400 Hz (triggered 3,333 times/sec)
     * 
     * Cyclic channel switching: 0→1→2→...→7→0
     * 
     * Per cycle:
     * 1. Store previous channel (if AC active)
     * 2. Activate new channel (LC or ADC)
     * 3. Increment sample index (at channel 0 only)
     */
  uint8_t channel = (channelIndex++) % MAX_SENSORS;
  uint8_t sampleIdx = 0;

  if (channel == 0)
    sampleIdx = (sampleIndex++) & (BUF_SIZE - 1);

  uint8_t prevChannel = (channel + MAX_SENSORS - 1) % MAX_SENSORS;

  // Process previous channel (if comparator was active)
  if (comparatorActive)
    SignalProcessor(prevChannel, sampleIdx);

  // Activate current channel
  uint8_t type = pgm_read_byte(&channels[channel].type);

  if (type == C_LC_SENSOR) {
    InitialStroke(channel);
  } else if (type == C_ANALOG_INPUT && !comparatorActive) {
    ReadADC(channel);
  }
}

// ============================================================================
ISR(ANALOG_COMP_vect) {
  // Analog Comparator ISR - counts falling edges @ ~280 kHz
  comparatorCount++;
}

// ============================================================================
void ReadADC(uint8_t adc_ch) {
  /*
     * ADC Single-Shot Conversion
     * 
     * - Channel: adc_ch (0-7 → A0-A7)
     * - Result: 8-bit (ADCH, left-aligned)
     * - Timing: ~150 µs
     * - Output: signalLevel[adc_ch]
     */
  cli();
  adcActive = true;

  // ADC Setup
  bitClear(ADCSRB, ACME);  // Disable AC multiplexer
  ADCSRA = 0x00;           // Reset
  bitSet(ADCSRA, ADEN);    // Enable ADC
  bitSet(ADCSRA, ADPS2);   // Prescaler
  bitSet(ADCSRA, ADPS0);   // Prescaler = 32

  ADMUX = (adc_ch & 0x07);  // Select channel
  bitSet(ADMUX, REFS0);     // AVcc reference
  bitSet(ADMUX, ADLAR);     // Left-aligned (8-bit in ADCH)

  ADCSRB = 0x00;  // Free-running off
  DDRC = 0x00;    // All A-pins input
  PORTC = 0x00;   // No pull-ups

  bitSet(ADCSRA, ADSC);  // Start conversion
  sei();

  // Wait for completion
  while (bit_is_set(ADCSRA, ADSC))
    ;

  cli();
  bitClear(ADCSRA, ADEN);      // ADC off
  signalLevel[adc_ch] = ADCH;  // 8-bit result
  adcActive = false;
  sei();
}

// ============================================================================
void SetAnalogComparator(void) {
  /*
     * Analog Comparator Preparation
     * 
     * - AIN0 (D6): + input
     * - AIN1 (D7): - input
     * - Trigger: Falling edge
     * - Interrupt: Disabled (will be enabled in InitialStroke)
     */
  bitSet(DIDR1, AIN1D);  // Disable digital buffer AIN1
  bitSet(DIDR1, AIN0D);  // Disable digital buffer AIN0

  ACSR = 0x00;           // Reset
  bitSet(ACSR, ACIS1);   // Falling edge interrupt
  bitClear(ACSR, ACIE);  // Interrupt off
  bitSet(ACSR, ACI);     // Clear interrupt flag

  ADMUX = 0x00;            // Mux to A0
  bitClear(ADCSRA, ADEN);  // ADC off

  DDRC = 0x00;   // A0-A5 as input
  PORTC = 0x00;  // No pull-ups

  comparatorActive = false;
}

// ============================================================================
void InitialStroke(uint8_t channel) {
  /*
     * LC Sensor Measurement Initialization
     * 
     * Pulse sequence (timing optimized for 280 kHz):
     * 1. Pull DOWN: Discharge LC circuit
     * 2. PAUSE: Wait for oscillation to settle
     * 3. Pull UP: Excite LC circuit
     * 4. TRISTATE: Let LC oscillate freely
     * 
     * Output: Comparator counts falling edges until next cycle
     */
  SetAnalogComparator();
  comparatorActive = true;
  ADMUX = channel & 0x07;

  // Load port/ddr from PROGMEM
  volatile uint8_t *port = (volatile uint8_t *)pgm_read_ptr(&channels[channel].port);
  volatile uint8_t *ddr = (volatile uint8_t *)pgm_read_ptr(&channels[channel].ddr);
  uint8_t bitmask = pgm_read_byte(&channels[channel].bitmask);

  cli();  // Time-critical section

  // Pulse sequence (0.5 µs each)
  *port &= ~bitmask;  // Drive LOW
  *ddr |= bitmask;    // Output mode
  _delay_us(0.5);

  *ddr &= ~bitmask;  // Release (Tristate)
  _delay_us(0.5);

  *port |= bitmask;  // Drive HIGH
  *ddr |= bitmask;   // Output mode
  _delay_us(0.5);

  *ddr &= ~bitmask;     // Release (Tristate) → Measurement starts
  comparatorCount = 0;  // Reset counter

  sei();  // End time-critical section

  // Start comparator
  ADCSRB |= (1 << ACME);             // Enable AC multiplexer
  ACSR |= (1 << ACIE) | (1 << ACI);  // Enable interrupt + clear flag
}

// ============================================================================
void SignalProcessor(uint8_t channel, uint8_t idx) {
  /*
     * Signal Processing (runs per channel @ 400 Hz)
     * 
     * Processing steps:
     * 1. Stop comparator measurement
     * 2. Update ring buffer (32 samples = 80 ms history)
     * 3. Compute moving average (FIR filter)
     * 4. Execute calibration or trigger logic
     * 
     * Ring Buffer Benefits:
     * - Robust against single-sample drops
     * - Smooth edge detection
     * - 80 ms window for state changes
     */

  // Stop comparator
  bitClear(ADCSRB, ACME);
  bitClear(ACSR, ACIE);
  comparatorActive = false;

  // Signal decay (hold counter)
  if (signalLevel[channel])
    signalLevel[channel]--;

  // ========== Ring Buffer Update ==========
  uint8_t newValue = comparatorCount;
  uint8_t oldValue = sampleBuffer[channel][idx];

  sampleBuffer[channel][idx] = newValue;

  movingSum[channel] += newValue;
  movingSum[channel] -= oldValue;

  // Moving average with rounding (divide by 32)
  uint8_t averaged = (movingSum[channel] + BUF_ROUNDING) >> BUF_SHIFT;

  // ========== CALIBRATION STATE ==========
  // NOTE: Calibration logic intentionally unchanged
  // See specification in function header

  if (referenceValue != 0) {
    systemState = STATE_RUNNING;
  } else {
    systemState = STATE_CALIBRATION;
  }

  if (systemState == STATE_CALIBRATION) {
    if (channel == referenceChannel) {
      calibrationSamples++;
      if (calibrationSamples > 50) {
        systemState = STATE_RUNNING;
        referenceLevel[channel] = averaged;
        referenceValue = averaged;
      }
    }
    debugValue = channel;
    return;
  }

  // ========== NORMAL OPERATION ==========
  int8_t baseline = (referenceValue == 0) ? referenceLevel[channel] : referenceValue;
  int8_t delta = baseline - averaged;

  uint8_t count = triggerCounter[channel];

  // Trigger filter with repetition
  if (delta < triggerThreshold) {
    triggerCounter[channel] = 0;
  } else if (count < triggerRepeat) {
    triggerCounter[channel] = count + 1;
  }

  // Output signal on trigger
  if ((count == triggerRepeat) && (delta < 10))
    signalLevel[channel] = holdTime;
}

// ============================================================================