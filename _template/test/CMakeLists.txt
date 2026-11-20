# TEMPLATE test registration. Registers the Unity test as a component so a
# test-runner app can pull it in via TEST_COMPONENTS. WHOLE_ARCHIVE keeps the
# TEST_CASE self-registration constructors from being stripped by the linker.
#
# After copying _template into components/e32p_<name>/, rename foo -> <name>.
# The test is built only when a test-runner project lists this component in
# TEST_COMPONENTS (that runner app arrives with the platform's example/test
# milestone; the module alone does not build its own tests).
idf_component_register(
    SRCS "test_e32p_foo.c"
    INCLUDE_DIRS "."
    REQUIRES unity e32p_foo
    WHOLE_ARCHIVE
)
