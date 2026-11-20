# Registers the e32p_pcnt Unity tests as a component (pulled in via
# TEST_COMPONENTS). Needs e32p_gpio to drive the loop-back pin. WHOLE_ARCHIVE
# keeps the TEST_CASE constructors.
idf_component_register(
    SRCS "test_e32p_pcnt.c"
    INCLUDE_DIRS "."
    REQUIRES unity e32p_pcnt e32p_gpio
    WHOLE_ARCHIVE
)
