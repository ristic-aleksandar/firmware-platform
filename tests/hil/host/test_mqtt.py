"""HIL MQTT self-test (single board, no network).

    pytest --port COM5 test_mqtt.py

Verifies topic-filter matching on-target and that the esp-mqtt client links and
allocates. A real broker round-trip needs Wi-Fi credentials + a broker and is
covered by the 23_mqtt demo, not by this automated suite.
"""


def test_mqtt_selftest(agent):
    agent.request("RUN mqtt_selftest", rb"@RSP RUN mqtt_selftest PASS")
