# Registers the e32p_uds Unity tests as a component (pulled in via
# TEST_COMPONENTS). WHOLE_ARCHIVE keeps the TEST_CASE constructors.
idf_component_register(
    SRCS "test_e32p_uds.c" "test_e32p_uds_isotp.c"
    INCLUDE_DIRS "."
    REQUIRES unity e32p_uds
    WHOLE_ARCHIVE
)
