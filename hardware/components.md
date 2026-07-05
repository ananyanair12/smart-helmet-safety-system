# Hardware — Bill of Materials & Wiring

## Bill of materials

| Component | Spec | Purpose |
|---|---|---|
| Arduino Nano | 5V, 16MHz, 32KB flash | Central processing unit |
| MQ-3 alcohol sensor | 0.05–10 mg/L range, 5V | Breath alcohol concentration |
| MPU6050 | 3-axis gyro + accelerometer, I2C, 16-bit | Head orientation / tilt tracking |
| Buzzer | 5V, 85dB | Internal audible alert |
| Red LED | 620–625nm | External alcohol alert |
| Yellow LED | 585–590nm | External drowsiness alert |
| NPN transistor | e.g. 2N2222 | Buzzer drive circuit |
| Resistors | 4.7kΩ ×2 (I2C pull-ups), current-limiting ×2 (LEDs) | Signal conditioning |
| Li-ion battery | 3.7V, 2000mAh | Power supply |
| Voltage regulator | 5V output | Stable system voltage |

## Pinout

| Signal | Arduino Nano Pin |
|---|---|
| MQ-3 analog output | A0 |
| MPU6050 SDA | A4 |
| MPU6050 SCL | A5 |
| Buzzer (via NPN transistor) | D9 |
| Red LED (alcohol) | D11 |
| Yellow LED (drowsiness) | D12 |

## Wiring notes

- **I2C lines** (SDA/SCL) need 4.7kΩ pull-up resistors to 5V for reliable MPU6050 communication.
- **Buzzer** is switched through an NPN transistor rather than driven directly from the GPIO pin, to handle current draw safely.
- **LEDs** each need a current-limiting resistor sized for the LED's forward voltage (~220Ω is a safe default for standard 5mm LEDs at 5V).
- **MQ-3** requires a ~20s heater warm-up before readings stabilize — this is handled in firmware (`ALCOHOL_WARMUP_MS`), but budget for it in physical testing too.

## Physical placement (helmet integration)

- **MQ-3**: front of helmet near the chin guard, with a small air channel directing breath toward the sensor and a protective mesh to prevent moisture damage.
- **MPU6050**: centrally located in the helmet's inner padding, secured against movement relative to the helmet shell.
- **Buzzer**: embedded in helmet padding near the ear.
- **LEDs**: mounted on the rear exterior of the helmet for maximum visibility to following traffic.
- **Arduino + battery**: housed in a shock-resistant enclosure (IP54-rated in the tested prototype) at the rear of the helmet, balancing weight distribution.

See the full project report in [`../docs/`](../docs/) for circuit design rationale, durability test results, and environmental testing data.
