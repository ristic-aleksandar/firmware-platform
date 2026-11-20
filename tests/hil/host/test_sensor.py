"""HIL device-driver tests — need the physical part wired.

These are opt-in (they don't run on a bare board):

    pytest --port COM5 --with-sensors test_sensor.py

Wiring:
  BMP280  -> I2C: SDA=GPIO21, SCL=GPIO22, addr 0x76, VCC=3V3
  TM1637  -> CLK=GPIO18, DIO=GPIO19, VCC=3V3
"""
import pytest


@pytest.mark.needs_sensor
def test_sensor_bmp280(agent, sensors):
    # Agent checks the reading is physically plausible before reporting PASS.
    agent.request("RUN sensor_bmp280", rb"@RSP RUN sensor_bmp280 PASS", timeout=8.0)


@pytest.mark.needs_sensor
def test_sensor_tm1637(agent, sensors):
    # Electrically write-only; the agent PASSes on clean writes, human confirms digits.
    agent.request("RUN sensor_tm1637", rb"@RSP RUN sensor_tm1637 PASS", timeout=8.0)
