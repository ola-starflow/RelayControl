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
8. Wait 60 ms for GreenPAK internal timers to settle.
9. If WDT auto-toggle was disabled before default-state entry, wait an additional 1050 ms.
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
3. After 30 ms, verify:
   - `REL_L` high
   - `PRE_CHARGE` low
   - `REL_H` low
4. After another 30 ms, verify:
   - `REL_L` high
   - `PRE_CHARGE` high
   - `REL_H` low
5. After another 1500 ms, verify:
   - `REL_L` high
   - `PRE_CHARGE` high
   - `REL_H` low
6. Set `REL_EN_H` high.
7. After 30 ms, verify:
   - `REL_L` high
   - `REL_H` high
   - `PRE_CHARGE` high
8. After another 30 ms, verify:
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
4. After another 20 ms, verify `BOOST_PWR` high, `REL_L` low, `REL_H` low.
5. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
6. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
7. After another 30 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
8. After another 10 ms, verify `BOOST_PWR` low, `REL_L` high, `REL_H` low.

### Test 2.2 — H only close sequence

Same as Test 2.1, but using `REL_EN_H` instead of `REL_EN_L`.

### Test 2.3 — L+H together close sequence

Same as Test 2.1, but setting both `REL_EN_L` and `REL_EN_H` high at the same time.

### Test 2.4 — L then H while L boost window is active

1. Apply default state and verify outputs.
2. Set `REL_EN_L` high.
3. After 100 us, verify `BOOST_PWR` high, `REL_L` low, `REL_H` low.
4. After another 40 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
5. Set `REL_EN_H` high.
6. After 100 us, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
7. After another 20 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
8. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
9. After another 40 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
10. After another 10 ms, verify `BOOST_PWR` low, `REL_L` high, `REL_H` high.

### Test 2.5 — L stable first, then H

1. Apply default state and verify outputs.
2. Set `REL_EN_L` high.
3. After 80 ms, verify `BOOST_PWR` low, `REL_L` high, `REL_H` low.
4. Set `REL_EN_H` high.
5. After 100 us, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
6. After another 20 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` low.
7. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
8. After another 10 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
9. After another 30 ms, verify `BOOST_PWR` high, `REL_L` high, `REL_H` high.
10. After another 10 ms, verify `BOOST_PWR` low, `REL_L` high, `REL_H` high.

### Test 2.6 — H stable first, then L

Same as Test 2.5, but using `REL_EN_H` first and `REL_EN_L` second.
