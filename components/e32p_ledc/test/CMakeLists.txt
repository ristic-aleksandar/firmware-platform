# Registers the e32p_ledc Unity tests as a component. A test-runner project
# pulls this in via TEST_COMPONENTS. WHOLE_ARCHIVE keeps the TEST_CASE
# self-registration constructors from being stripped by the linker.
idf_component_register(
    SRCS "test_e32p_ledc.c"
    INCLUDE_DIRS "."
    REQUIRES unity e32p_ledc
    WHOLE_ARCHIVE
)
