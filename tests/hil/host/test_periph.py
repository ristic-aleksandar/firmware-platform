"""HIL GPIO / analog / timer tests (single board, no wiring).

    pytest --port COM5 test_periph.py
"""


def test_periph_gpio(agent):
    agent.request("RUN periph_gpio", rb"@RSP RUN periph_gpio PASS")


def test_periph_ledc(agent):
    agent.request("RUN periph_ledc", rb"@RSP RUN periph_ledc PASS")


def test_periph_adc(agent):
    agent.request("RUN periph_adc", rb"@RSP RUN periph_adc PASS")


def test_periph_dac(agent):
    agent.request("RUN periph_dac", rb"@RSP RUN periph_dac PASS")


def test_periph_touch(agent):
    agent.request("RUN periph_touch", rb"@RSP RUN periph_touch PASS")


def test_periph_pcnt(agent):
    agent.request("RUN periph_pcnt", rb"@RSP RUN periph_pcnt PASS")


def test_periph_gptimer(agent):
    agent.request("RUN periph_gptimer", rb"@RSP RUN periph_gptimer PASS")
