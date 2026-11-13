# OpenModel fatigue analysis: the shared library OpenModel::Fatigue, its tests and the openmodel-fatigue tool.
#
# Two ways to build it:
#  - inside OpenModel (the root adds this directory when OPENMODEL_BUILD_FATIGUE is ON; it links the shared core
#    of the same build, so one copy of the core is loaded in a process);
#  - on its own, against an installed OpenModel package with OpenModel::Shared:
#        cmake -S fatigue -B build -DCMAKE_PREFIX_PATH=<OpenModel install prefix>
#    Standalone builds do not install anything; they build and test the library in the build tree.

if(TARGET OpenModel::Shared)
    set(OPENMODEL_FATIGUE_STANDALONE OFF)
else()
    set(OPENMODEL_FATIGUE_STANDALONE ON)
    cmake_minimum_required(VERSION 3.20)
    project(OpenModelFatigue VERSION 0.1.2 LANGUAGES CXX)
    include(CTest)
    option(OPENMODEL_BUILD_TESTS "Build the fatigue tests" ON)
    option(OPENMODEL_BUILD_CLI "Build the openmodel-fatigue tool" ON)
    find_package(OpenModel REQUIRED)
    if(NOT TARGET OpenModel::Shared)
        message(FATAL_ERROR "The installed OpenModel package has no OpenModel::Shared target; OpenModel::Fatigue needs the shared core.")
    endif()
endif()

set(OPENMODEL_FATIGUE_SOURCES
    src/FatigueSpecification.cpp
    src/Json.cpp
    src/CycleCounting.cpp
    src/FatigueAnalysis.cpp
    src/FatigueDamageMap.cpp
    src/FatigueDamageMapWebViewAdapter.cpp
    src/FatigueDamageMapWriter.cpp
    src/FatigueDamageModel.cpp
    src/FatigueMaterial.cpp
    src/FatigueReport.cpp
    src/FatigueReportWriter.cpp
    src/FatigueSamplePreparer.cpp
    src/FatigueSpectrumReport.cpp
    src/FatigueSpectrumReportWriter.cpp
    src/LocalStrainCorrection.cpp
    src/MeanStressCorrection.cpp
    src/MultiaxialCycleCounting.cpp
    src/OpenModelFemResultsSource.cpp
    src/StrainLifeAnalysis.cpp
    src/StrainLifeDamageModel.cpp
    src/StressHistory.cpp
    src/TensorNormalizer.cpp
    src/WangBrownDamageModel.cpp
    src/WangBrownRelativeStrain.cpp)

add_library(openmodel_fatigue SHARED ${OPENMODEL_FATIGUE_SOURCES})
# The alias exists only inside OpenModel: a standalone build beside an installed OpenModel package must not
# clash with the imported OpenModel::Fatigue of that package.
if(NOT OPENMODEL_FATIGUE_STANDALONE)
    add_library(OpenModel::Fatigue ALIAS openmodel_fatigue)
endif()
target_include_directories(openmodel_fatigue PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(openmodel_fatigue PUBLIC OpenModel::Shared)
target_compile_features(openmodel_fatigue PUBLIC cxx_std_17)
set_target_properties(openmodel_fatigue PROPERTIES
    OUTPUT_NAME OpenModelFatigue
    CXX_EXTENSIONS OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    EXPORT_NAME Fatigue)

if(OPENMODEL_BUILD_CLI)
    add_executable(openmodel-fatigue cli/main.cpp)
    target_link_libraries(openmodel-fatigue PRIVATE openmodel_fatigue)
    target_compile_features(openmodel-fatigue PRIVATE cxx_std_17)
    set_target_properties(openmodel-fatigue PROPERTIES CXX_EXTENSIONS OFF)
endif()

if(NOT OPENMODEL_FATIGUE_STANDALONE)
    install(TARGETS openmodel_fatigue
        EXPORT OpenModelTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY include/OpenModel
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

if(OPENMODEL_BUILD_TESTS)
    function(openmodel_fatigue_add_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE openmodel_fatigue)
        # Same as the core tests: keep assertions enabled in Release builds.
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES LABELS "fatigue")
    endfunction()
    openmodel_fatigue_add_test(fatigue_json_test tests/json_test.cpp)
    openmodel_fatigue_add_test(fatigue_specification_test tests/fatigue_specification_test.cpp)
    openmodel_fatigue_add_test(fatigue_result_source_test tests/openmodel_fem_results_source_test.cpp)
    openmodel_fatigue_add_test(fatigue_tensor_normalizer_test tests/tensor_normalizer_test.cpp)
    openmodel_fatigue_add_test(fatigue_sample_preparer_test tests/fatigue_sample_preparer_test.cpp)
    openmodel_fatigue_add_test(fatigue_stress_history_test tests/stress_history_test.cpp)
    openmodel_fatigue_add_test(fatigue_cycle_counting_test tests/cycle_counting_test.cpp)
    openmodel_fatigue_add_test(fatigue_multiaxial_cycle_contract_test tests/multiaxial_cycle_contract_test.cpp)
    openmodel_fatigue_add_test(fatigue_material_test tests/fatigue_material_test.cpp)
    openmodel_fatigue_add_test(fatigue_mean_stress_correction_test tests/mean_stress_correction_test.cpp)
    openmodel_fatigue_add_test(fatigue_damage_model_test tests/fatigue_damage_model_test.cpp)
    openmodel_fatigue_add_test(fatigue_strain_life_damage_model_test tests/strain_life_damage_model_test.cpp)
    openmodel_fatigue_add_test(fatigue_wang_brown_damage_model_test tests/wang_brown_damage_model_test.cpp)
    openmodel_fatigue_add_test(fatigue_wang_brown_relative_strain_test tests/wang_brown_relative_strain_test.cpp)
    openmodel_fatigue_add_test(fatigue_strain_life_analysis_test tests/strain_life_analysis_test.cpp)
    openmodel_fatigue_add_test(fatigue_local_strain_correction_test tests/local_strain_correction_test.cpp)
    openmodel_fatigue_add_test(fatigue_analysis_test tests/fatigue_analysis_test.cpp)
    openmodel_fatigue_add_test(fatigue_report_test tests/fatigue_report_test.cpp)
    openmodel_fatigue_add_test(fatigue_report_writer_test tests/fatigue_report_writer_test.cpp)
    openmodel_fatigue_add_test(fatigue_spectrum_report_test tests/fatigue_spectrum_report_test.cpp)
    openmodel_fatigue_add_test(fatigue_damage_map_test tests/fatigue_damage_map_test.cpp)
    openmodel_fatigue_add_test(fatigue_damage_map_writer_test tests/fatigue_damage_map_writer_test.cpp)
    openmodel_fatigue_add_test(fatigue_damage_map_webview_adapter_test tests/fatigue_damage_map_webview_adapter_test.cpp)
endif()

# Command-line checks. They read the solver-results fixture of the core tests (beam_fixture), so they exist only
# inside OpenModel where that fixture is generated.
if(OPENMODEL_BUILD_TESTS AND OPENMODEL_BUILD_CLI AND NOT OPENMODEL_FATIGUE_STANDALONE)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        set(_fatigue_beam_frd "${CMAKE_BINARY_DIR}/beam_fixture/beam.frd")
        set(_fatigue_data "${CMAKE_CURRENT_BINARY_DIR}/cli_data")
        file(MAKE_DIRECTORY "${_fatigue_data}")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/material.json" DESTINATION "${_fatigue_data}")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/bad_material.json" DESTINATION "${_fatigue_data}")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tests/data/spectrum.json.in" "${_fatigue_data}/spectrum.json" @ONLY)
        add_test(NAME fatigue_cli_info COMMAND openmodel-fatigue info)
        add_test(NAME fatigue_cli_analyze COMMAND openmodel-fatigue analyze
            --material "${_fatigue_data}/material.json" --field STRESS --position node:1
            --results "${_fatigue_beam_frd}" --unit MPa:1e6 --out "${_fatigue_data}/analyze")
        add_test(NAME fatigue_cli_spectrum COMMAND openmodel-fatigue spectrum
            --spectrum "${_fatigue_data}/spectrum.json" --material "${_fatigue_data}/material.json"
            --out "${_fatigue_data}/spectrum")
        add_test(NAME fatigue_cli_bad_material COMMAND openmodel-fatigue analyze
            --material "${_fatigue_data}/bad_material.json" --field STRESS --position node:1
            --results "${_fatigue_beam_frd}")
        set_tests_properties(fatigue_cli_bad_material PROPERTIES PASS_REGULAR_EXPRESSION "unknown kind 'sn'")
        set_tests_properties(fatigue_cli_analyze PROPERTIES PASS_REGULAR_EXPRESSION "STRESS, 1 position\\(s\\): success")
        set_tests_properties(fatigue_cli_spectrum PROPERTIES PASS_REGULAR_EXPRESSION "spectrum: success")
        set_tests_properties(fatigue_cli_analyze fatigue_cli_spectrum PROPERTIES FIXTURES_REQUIRED beam_fixture)
        set_tests_properties(fatigue_cli_info fatigue_cli_analyze fatigue_cli_spectrum fatigue_cli_bad_material
            PROPERTIES LABELS "fatigue")
    endif()
endif()
