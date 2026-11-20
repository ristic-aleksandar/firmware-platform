"""HIL bus tests: uart, i2c, spi, i2s, ws2812.

Single-board tests run with just --port. The UART two-board echo test needs a
companion + a cross-wire (see WIRING.md) and is skipped without --companion-port.

    pytest --port COM5 test_bus.py
    pytest --port COM5 --companion-port COM6 test_bus.py   # includes uart_xfer
"""
import pytest


def test_bus_uart_loop(agent):
    agent.request("RUN bus_uart_loop", rb"@RSP RUN bus_uart_loop PASS")


def test_bus_i2c_bus(agent):
    agent.request("RUN bus_i2c_bus", rb"@RSP RUN bus_i2c_bus PASS")


def test_bus_spi_loop(agent):
    agent.request("RUN bus_spi_loop", rb"@RSP RUN bus_spi_loop PASS")


def test_bus_i2s_tx(agent):
    agent.request("RUN bus_i2s_tx", rb"@RSP RUN bus_i2s_tx PASS")


def test_bus_ws2812(agent):
    agent.request("RUN bus_ws2812", rb"@RSP RUN bus_ws2812 PASS")


@pytest.mark.two_board
def test_bus_uart_xfer(agent, companion):
    # DUT writes on UART1; the companion echoes it back over the cross-wire.
    agent.request("RUN bus_uart_xfer", rb"@RSP RUN bus_uart_xfer PASS", timeout=6)


# --- SPI / I2C two-board: blocked on the deferred slave-mode drivers ---

@pytest.mark.two_board
@pytest.mark.needs_slave
def test_bus_spi_xfer(agent, companion):
    pytest.skip("needs e32p_spi_slave companion driver (deferred)")


@pytest.mark.two_board
@pytest.mark.needs_slave
def test_bus_i2c_xfer(agent, companion):
    pytest.skip("needs e32p_i2c_slave companion driver (deferred)")
