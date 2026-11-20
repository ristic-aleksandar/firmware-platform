# Registers the e32p_isotp Unity tests as a component (pulled in via
# TEST_COMPONENTS). WHOLE_ARCHIVE keeps the TEST_CASE constructors.
idf_component_register(
    SRCS "test_e32p_isotp.c"
    INCLUDE_DIRS "."
    REQUIRES unity e32p_isotp
    WHOLE_ARCHIVE
)
