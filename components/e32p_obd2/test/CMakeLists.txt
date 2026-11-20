# Registers the e32p_obd2 Unity tests as a component (pulled in via
# TEST_COMPONENTS). WHOLE_ARCHIVE keeps the TEST_CASE constructors.
idf_component_register(
    SRCS "test_e32p_obd2.c"
    INCLUDE_DIRS "."
    REQUIRES unity e32p_obd2 e32p_nvs
    WHOLE_ARCHIVE
)
