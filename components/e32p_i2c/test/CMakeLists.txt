# Registers the e32p_i2c Unity tests as a component (pulled in via
# TEST_COMPONENTS). WHOLE_ARCHIVE keeps the TEST_CASE constructors.
idf_component_register(
    SRCS "test_e32p_i2c.c"
    INCLUDE_DIRS "."
    REQUIRES unity e32p_i2c
    WHOLE_ARCHIVE
)
