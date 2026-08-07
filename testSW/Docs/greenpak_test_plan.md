# SLG47011 GreenPAK Test Plan

This document describes the automated tests implemented in the Nucleo-based SLG47011 test firmware.

The automated test runner is intended to validate GreenPAK behavior and timing by applying MCU GPIO and I2C stimulus, sampling GreenPAK output pins at defined checkpoints, and printing the result report only after the timed sequence has completed.

## Default state

Before each test section, the firmware returns the fixture and GreenPAK to a known default state.

Current default-state sequence:

1. Release GreenPAK output pins on the Nucleo side back to input mode.
2. Enable WDT auto-toggle on I2C OUT0.
3. Set I2C OUT1 RelayPWR command OFF.
4. Set PA4 / DAC1_OUT1 / Relay pwr voltage to 0 V.
5. Leave PA6 / DAC2_OUT1 untouched for now.
6. Pulse `SHUTDOWN_N` low and release it again before reset, so any closed relays are commanded open before reset.
7. Set MCU-controlled pins:
   - `SHUTDOWN_N` released
   - `REL_EN_H` low
   - `REL_EN_L` low
   - `RESET_N` high
8. Wait BOOST high-to-low window for GreenPAK internal timers to settle.
9. Always wait 2500 ms after enabling WDT auto-toggle before reset/default-state recovery, so the GreenPAK has time to see valid OUT0 toggling even after a previous WDT/shutdown latch.
10. Pulse reset:
    - `RESET_N` low for 100 us
    - `RESET_N` high
    - wait 100 us
11. Verify expected default GreenPAK outputs.

Expected default GreenPAK outputs:

| Signal | Expected |
|---|---:|
| `SHUTDOWN_N` | HIGH |
| `REL_H` | LOW |
| `REL_L` | LOW |
| `PRE_CHARGE` | LOW |
| `BOOST_PWR` | LOW |
| `REL_PWR_EN` | LOW |

## Test 1 — Verify PreChargeEn normal behavior

**Intention:** verify `PRE_CHARGE` works in a normal relay close sequence.

`BOOST_PWR` is intentionally ignored in this test, because boost behavior is verified by Test 2.

### Sequence

1. Apply default state and verify outputs.
2. Set `REL_EN_L` high.
3. After 1.2 s, verify:
   - `REL_L` high
   - `PRE_CHARGE` low
   - `REL_H` low
4. After another 1.2 s, verify:
   - `REL_L` high
   - `PRE_CHARGE` high
   - `REL_H` low
5. After another 1500 ms, verify:
   - `REL_L` high
   - `PRE_CHARGE` high
   - `REL_H` low
6. Set `REL_EN_H` high.
7. After 1.2 s, verify:
   - `REL_L` high
   - `REL_H` high
   - `PRE_CHARGE` high
8. After another 1.2 s, verify:
   - `REL_L` high
   - `REL_H` high
   - `PRE_CHARGE` low
9. After another 1500 ms, verify:
   - `REL_L` high
   - `REL_H` high
   - `PRE_CHARGE` low

## Test 2 — Verify Boost power normal behavior

**Intention:** verify `BOOST_PWR` asserts during normal relay close sequences and releases afterwards.

`PRE_CHARGE` is intentionally ignored in this test, because it is verified by Test 1.

Test 2 is split into sub-tests. When Test 2 is run directly, all checkpoint details are printed. When all tests are run with `a`, passing sub-tests only print a short pass line and failing sub-tests print their checkpoint details.

### Test 2.1 — L only close sequence

1. Apply default state and verify outputs.
2. Set `REL_EN_L` high.
3. After 100 us, verify `BOOST_PWR` high, `REL_L` low, `REL_H` low.
4. After another 1.2 s, verify `BOOST_PWR` high, `REL_L` low, `REL_H` low.
5. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
6. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
7. After another 1.2 s, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
8. After another 10 ms, verify `BOOST_PWR` low, `REL_L` high, `REL_H` low.

### Test 2.2 — H only close sequence

Same as Test 2.1, but using `REL_EN_H` instead of `REL_EN_L`.

### Test 2.3 — L+H together close sequence

Same as Test 2.1, but setting both `REL_EN_L` and `REL_EN_H` high at the same time.

### Test 2.4 — L then H while L boost window is active

1. Apply default state and verify outputs.
2. Set `REL_EN_L` high.
3. After 100 us, verify `BOOST_PWR` high, `REL_L` low, `REL_H` low.
4. After another 1.2 s, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
5. Set `REL_EN_H` high.
6. After 100 us, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
7. After another 1.2 s, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
8. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
9. After another 1.2 s, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
10. After another 10 ms, verify `BOOST_PWR` low, `REL_L` high, `REL_H` high.

### Test 2.5 — L stable first, then H

1. Apply default state and verify outputs.
2. Set `REL_EN_L` high.
3. After 80 ms, verify `BOOST_PWR` low, `REL_L` high, `REL_H` low.
4. Set `REL_EN_H` high.
5. After 100 us, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
6. After another 1.2 s, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
7. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
8. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
9. After another 1.2 s, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
10. After another 10 ms, verify `BOOST_PWR` low, `REL_L` high, `REL_H` high.

### Test 2.6 — H stable first, then L

Same as Test 2.5, but using `REL_EN_H` first and `REL_EN_L` second.

## Test 3 — Relay shutdown sequence

**Intention:** verify that the relay shutdown sequence opens the correct relay immediately, opens the other relay after the defined delay, and leaves `PRE_CHARGE` and `BOOST_PWR` low after the sequence has completed.

This test intentionally focuses on the relay-output shutdown sequence. Detailed verification of the `SHUTDOWN_N` latch and the exact reset-release requirements is left for a separate test.

Shutdown behavior under test:

- When shutdown starts and `CURRENT_DIRECTION` is low, `REL_L` shall open immediately and `REL_H` shall open after the shutdown delay.
- When shutdown starts and `CURRENT_DIRECTION` is high, `REL_H` shall open immediately and `REL_L` shall open after the shutdown delay.
- The current-direction state is latched when shutdown starts.
- Once the shutdown sequence has started, changing `REL_EN_L`, `REL_EN_H`, `SHUTDOWN_N`, `CURRENT_DIRECTION`, or pulsing `RESET_N` shall not change or interrupt the relay opening order.
- BOOST high-to-low window after the last relay has opened, both `PRE_CHARGE` and `BOOST_PWR` shall be low.

`PRE_CHARGE` and `BOOST_PWR` are ignored during the intermediate relay-opening checkpoints, and are only checked at the final auxiliary-output checkpoint.

### Test 3.1 — Both relays closed, current direction LOW

1. Apply default state and verify outputs.
2. Set `CURRENT_DIRECTION` low.
3. Close both relays and wait 100 ms.
4. Pull `SHUTDOWN_N` low.
5. After 100 us, verify `REL_L` low and `REL_H` high.
6. After another 1.2 s, verify `REL_L` low and `REL_H` high.
7. After another 10 ms, verify both relays low.
8. After another BOOST high-to-low window, verify both relays low, `PRE_CHARGE` low, and `BOOST_PWR` low.

### Test 3.2 — Both relays closed, current direction HIGH

Same as Test 3.1, but `CURRENT_DIRECTION` is high. `REL_H` shall open immediately and `REL_L` shall open after the delay.

### Test 3.3 — L relay only, current direction LOW

Verifies that a closed `REL_L` opens immediately when current direction is low.

### Test 3.4 — L relay only, current direction HIGH

Verifies that a closed `REL_L` opens after the delayed shutdown step when current direction is high.

### Test 3.5 — H relay only, current direction HIGH

Verifies that a closed `REL_H` opens immediately when current direction is high.

### Test 3.6 — H relay only, current direction LOW

Verifies that a closed `REL_H` opens after the delayed shutdown step when current direction is low.

### Test 3.7 — Both relays, current direction LOW, perturb after shutdown

After shutdown has started, the test releases `SHUTDOWN_N`, changes `CURRENT_DIRECTION`, toggles both relay-enable inputs low/high, and pulses `RESET_N`. The relay-opening order shall still follow the originally latched low current-direction state.

### Test 3.8 — Both relays, current direction HIGH, perturb after shutdown

Same as Test 3.7, but with high current direction latched at shutdown start.

## Test 4 — ADC and data-buffer behavior

**Intention:** verify that the ADC can be enabled/disabled through I2C OUT2, that DB0 measures relay-power voltage with reasonable accuracy, and that DB1 is updated only while `BOOST_PWR` is high.

ADC configuration assumptions used by the test:

- ADC Vref: 1.620 V internal reference.
- ADC resolution: 14 bit.
- Relay-power voltage divider: 100 kOhm / 10 kOhm, so relay voltage is 11 times the ADC input voltage.
- ADC sampling frequency: 2.5 ksps total, split across two channels, approximately 1.25 ksps per channel.
- Data buffers are moving averages. The automated test waits at least 1.2 s for normal settling, which is well above one 8-sample moving-average window at 1.25 ksps.

Voltage acceptance tolerance is +/- 0.5 V at the simulated relay-voltage level. The report prints the raw Data Buffer values, calculated ADC-pin voltage, calculated relay voltage, and approximate internal temperature.

### Test 4.1 — DB0 voltage accuracy

1. Apply analog default state and enable ADC through OUT2.
2. Set simulated relay voltage to 0 V and wait 1.2 s.
3. Read DB0, DB1, and DB2. Verify DB0 is within +/- 0.5 V of 0 V.
4. Set simulated relay voltage to 5 V and wait 1.2 s.
5. Read all values. Verify DB0 is within +/- 0.5 V of 5 V.
6. Set simulated relay voltage to 12 V and wait 1.2 s.
7. Read all values. Verify DB0 is within +/- 0.5 V of 12 V.
8. Set simulated relay voltage to 16 V and wait 1.2 s.
9. Read all values. Verify DB0 is within +/- 0.5 V of 16 V.

DB1 is printed but not checked in this sub-test. DB2 is checked only for a broad sanity range, not calibration accuracy.

### Test 4.2 — ADC enable/disable freezes and resumes DB0

1. Apply analog default state and enable ADC through OUT2.
2. Set simulated relay voltage to 4 V and wait 1.2 s.
3. Read all values and verify DB0 is within +/- 0.5 V of 4 V.
4. Disable ADC through OUT2.
5. Set simulated relay voltage to 14 V.
6. Wait 1.2 s. If ADC is truly stopped, DB0 shall remain at the old 4 V value.
7. Re-enable ADC through OUT2.
8. Wait 1.2 s.
9. Read all values and verify DB0 updates to within +/- 0.5 V of 14 V.

### Test 4.3 — DB1 only updates while BOOST is high

1. Apply analog default state and enable ADC through OUT2.
2. Command `REL_EN_L` high to start a BOOST window.
3. Set simulated relay voltage to 5 V.
4. Wait BOOST high-to-low window.
5. Set simulated relay voltage to 12 V.
6. Wait another 1.2 s and read all values.
7. Verify DB0 follows the current 12 V input, while DB1 still reports the 5 V value captured during the BOOST window.
8. Apply default state again and re-enable ADC.
9. Command `REL_EN_H` high to start a new BOOST window.
10. Set simulated relay voltage to 12 V.
11. Wait BOOST high-to-low window and read all values.
12. Verify DB1 now updates to approximately 12 V during the new BOOST window.


### Test 4 timing note

ADC/data-buffer voltage checks now wait 1.2 s after each DAC change to allow the GreenPAK moving-average buffers to settle. The DB1 BOOST-gate test no longer assumes a fixed BOOST duration; it waits for the actual BOOST_PWR pin to go high and then low before changing the DAC outside the capture window.

## Test 5 — I2C signal readback

Purpose:
Verify the eight GreenPAK signal-readback bits available over I2C register `0x0062`.

Signal mapping, from bit 0 at the bottom to bit 7 at the top:

| Readback bit | Signal |
|---:|---|
| IN0 / bit 0 | `Relay L` |
| IN1 / bit 1 | `Relay H` |
| IN2 / bit 2 | `Sdn latched` |
| IN3 / bit 3 | `Reset allowed` |
| IN4 / bit 4 | `Current direction` |
| IN5 / bit 5 | `Enable L` |
| IN6 / bit 6 | `Enable H` |
| IN7 / bit 7 | `WDT latched` |

Important behavior assumptions:

- `Reset allowed` is normally high.
- After shutdown is latched, `Reset allowed` goes low for approximately 50 ms, then returns high.
- WDT status does not directly affect `Reset allowed`.
- Stopping the OUT0 WDT toggle and waiting 2500 ms is used to latch WDT with margin.

### Test 5.1 — Default readback state

Initial state:

- Apply default state.
- Set `CURRENT_DIRECTION` low.

Expected:

- `Relay L` low
- `Relay H` low
- `Sdn latched` low
- `Reset allowed` high
- `Current direction` low
- `Enable L` low
- `Enable H` low
- `WDT latched` low

### Test 5.2 — MCU-driven input readback

Steps:

1. Apply default state.
2. Set `CURRENT_DIRECTION` low and verify default readback.
3. Set `CURRENT_DIRECTION` high and verify IN4 high.
4. Set `Enable L` high and verify IN5 high.
5. Set `Enable H` high and verify IN6 high.

Purpose:
Verify that the readback register correctly reports the MCU-driven input signals.

### Test 5.3 — Relay output readback

Steps:

1. Apply default state.
2. Close `Relay L`, wait 100 ms, verify IN0 high.
3. Apply default state.
4. Close `Relay H`, wait 100 ms, verify IN1 high.
5. Apply default state.
6. Close both relays, wait 100 ms, verify IN0 and IN1 high.

Purpose:
Verify that relay-output readback matches the relay output states.

### Test 5.4 — Shutdown latch and reset-allowed readback

Steps:

1. Apply default state.
2. Close both relays.
3. Pull `SHUTDOWN_N` low.
4. After 100 us, verify:
   - `Sdn latched` high
   - `Reset allowed` low
   - relay opening order follows current-direction low behavior
5. After 60 ms, verify:
   - `Sdn latched` high
   - `Reset allowed` high
   - both relays low

Purpose:
Verify shutdown latch readback and the 50 ms reset-allowed blocking period.

### Test 5.5 — WDT latched readback

Steps:

1. Apply default state.
2. Disable OUT0 WDT auto-toggle.
3. Wait 2500 ms.
4. Verify IN7 / `WDT latched` high.

Purpose:
Verify that the readback register reports the WDT latch when host WDT toggling stops.
