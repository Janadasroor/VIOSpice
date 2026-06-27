# Root test aggregation entrypoint.
option(VIOSPICE_ENABLE_TESTS "Enable tests" ON)

if(VIOSPICE_ENABLE_TESTS)
    vioraeda_add_test(test_design_rule
        SOURCES test_design_rule.cpp test_design_rule.h
        LINK_LIBS VioCore VioSymbols VioSimulator FluxScript FluxScriptManager VioUI
    )
endif()

# Python-dependent test suites
if(VIOSPICE_ENABLE_TESTS AND VIOSPICE_ENABLE_PYTHON)
    add_subdirectory(unit/ux)
    add_subdirectory(integration)
    add_subdirectory(regression)
endif()
# add_subdirectory(benchmarks)
