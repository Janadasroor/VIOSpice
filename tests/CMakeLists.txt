# Root test aggregation entrypoint.
if(VIOSPICE_ENABLE_TESTS)
    vioraeda_add_test(test_design_rule
        SOURCES test_design_rule.cpp test_design_rule.h
        LINK_LIBS VioCore VioSymbols VioSimulator FluxScript FluxScriptManager VioUI
    )
    vioraeda_add_test(test_serialization_and_cloning
        SOURCES test_serialization_and_cloning.cpp test_serialization_and_cloning.h
        LINK_LIBS VioUI VioVioraIDE VioSchematic VioraPCBCore VioFootprints VioCore VioSymbols VioSimulator FluxScriptManager FluxScript
    )
endif()

# CLI-based regression tests (no Python needed)
if(VIOSPICE_ENABLE_TESTS AND BUILD_VIOSPICE_CLI)
    add_executable(test_sim_golden
        regression/test_sim_golden.cpp
    )
    target_compile_definitions(test_sim_golden PRIVATE VIO_CMD_PATH=\"$<TARGET_FILE:viora>\")
    target_link_libraries(test_sim_golden PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    add_test(NAME regression.sim_golden COMMAND test_sim_golden ${CMAKE_SOURCE_DIR}/tests/circuits)
    set_tests_properties(regression.sim_golden PROPERTIES LABELS "fast;regression;simulator")
endif()

# Python-dependent test suites
if(VIOSPICE_ENABLE_TESTS AND VIOSPICE_ENABLE_PYTHON)
    add_subdirectory(unit/ux)
    add_subdirectory(integration)
    add_subdirectory(regression)
endif()
# add_subdirectory(benchmarks)
