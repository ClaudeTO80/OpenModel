cmake_minimum_required(VERSION 3.20)
project(OpenModel VERSION 0.1.0.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

option(OPENMODEL_BUILD_TESTS "Build dependency-free core tests" ON)
option(OPENMODEL_BUILD_BENCHMARKS "Build optional synthetic mesh performance benchmarks" OFF)
option(OPENMODEL_BUILD_CLI "Build the OpenModel command-line application" ON)
option(OPENMODEL_BUILD_SHARED "Build the shared library in addition to the static library" ON)
option(OPENMODEL_TEST_WEBGL_VIEWER "Run the headless WebGL viewer smoke test (needs Node.js with Playwright; set NODE_PATH)" OFF)
option(OPENMODEL_BUILD_WEBUI "Build the optional WebUI desktop application" OFF)
set(OPENMODEL_WEBUI_ROOT "" CACHE PATH "Root directory of the WebUI SDK")
option(OPENMODEL_BUILD_GMSH_BACKEND "Build the optional Gmsh adapter and integration test" OFF)
set(GMSH_ROOT "" CACHE PATH "Root of a Gmsh SDK/precompiled distribution")
set(GMSH_INCLUDE_DIR "" CACHE PATH "Directory containing gmsh.h")
set(GMSH_LIBRARY_DEBUG "" CACHE FILEPATH "Gmsh debug import/static library (for example gmshd.lib)")
set(GMSH_LIBRARY_RELEASE "" CACHE FILEPATH "Gmsh release import/static library (for example gmsh.lib)")
set(GMSH_LIBRARY "" CACHE FILEPATH "Legacy/single-configuration Gmsh library path; used as a fallback")
set(GMSH_RUNTIME_LIBRARY_DEBUG "" CACHE FILEPATH "Gmsh debug runtime DLL on Windows, if applicable")
set(GMSH_RUNTIME_LIBRARY_RELEASE "" CACHE FILEPATH "Gmsh release runtime DLL on Windows, if applicable")
option(OPENMODEL_BUILD_OCC_BACKEND "Build the optional Open CASCADE CAD adapter and integration test" OFF)
option(OPENMODEL_BUILD_NETGEN_BACKEND "Build the optional Netgen meshing adapter" OFF)
option(OPENMODEL_BUILD_MFEM_BACKEND "Build the optional MFEM adapter and integration test" OFF)
option(OPENMODEL_BUILD_ASSIMP_BACKEND "Build the optional Assimp model importer and smoke test" OFF)
option(OPENMODEL_BUILD_TETGEN_BACKEND "Build the optional TetGen volume mesher" OFF)
option(OPENMODEL_NETGEN_HAS_OCC "Netgen was built with Open CASCADE support (OCCGEOMETRY)" OFF)
set(OPENMODEL_NETGEN_TEST_STEP "" CACHE FILEPATH "Optional STEP/IGES/BREP fixture for a real Netgen integration test")

set(OPENMODEL_CORE_SOURCES
    src/MeshData.cpp
    src/MeshBinaryIO.cpp
    src/MeshImporter.cpp
    src/AbaqusInpImporter.cpp
    src/CalculixInpImporter.cpp
    src/AbaqusInpWriter.cpp
    src/AnalysisSetup.cpp
    src/MeshSelection.cpp
    src/ProcessRunner.cpp
    src/CADModel.cpp
    src/SurfaceMesh.cpp
    src/MeshTopology.cpp
    src/MeshValidator.cpp
    src/SurfaceRefiner.cpp
    src/GltfWriter.cpp
    src/WebViewWriter.cpp
    src/FemResults.cpp
    src/ResultBinding.cpp
    src/fatigue/OpenModelFemResultsSource.cpp
    src/fatigue/TensorNormalizer.cpp
    src/fatigue/FatigueSamplePreparer.cpp
    src/fatigue/StressHistory.cpp
    src/fatigue/CycleCounting.cpp
    src/fatigue/MultiaxialCycleCounting.cpp
    src/fatigue/FatigueMaterial.cpp
    src/fatigue/MeanStressCorrection.cpp
    src/fatigue/FatigueDamageModel.cpp
    src/fatigue/StrainLifeDamageModel.cpp
    src/fatigue/WangBrownDamageModel.cpp
    src/fatigue/StrainLifeAnalysis.cpp
    src/fatigue/LocalStrainCorrection.cpp
    src/fatigue/FatigueAnalysis.cpp
    src/fatigue/FatigueReport.cpp
    src/fatigue/FatigueReportWriter.cpp
    src/fatigue/FatigueSpectrumReport.cpp
    src/fatigue/FatigueSpectrumReportWriter.cpp
    src/fatigue/FatigueDamageMap.cpp
    src/fatigue/FatigueDamageMapWriter.cpp
    src/fatigue/FatigueDamageMapWebViewAdapter.cpp)

function(openmodel_configure_core_target target export_name)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME ${export_name})
endfunction()

find_package(Threads REQUIRED)
add_library(openmodel_core STATIC ${OPENMODEL_CORE_SOURCES})
target_link_libraries(openmodel_core PRIVATE Threads::Threads)
add_library(OpenModel::Core ALIAS openmodel_core)
set_target_properties(openmodel_core PROPERTIES OUTPUT_NAME OpenModelCore)
openmodel_configure_core_target(openmodel_core Core)

if(OPENMODEL_BUILD_SHARED)
    add_library(openmodel_shared SHARED ${OPENMODEL_CORE_SOURCES})
    add_library(OpenModel::Shared ALIAS openmodel_shared)
    target_link_libraries(openmodel_shared PRIVATE Threads::Threads)
    set_target_properties(openmodel_shared PROPERTIES
        OUTPUT_NAME OpenModel
        WINDOWS_EXPORT_ALL_SYMBOLS ON)
    openmodel_configure_core_target(openmodel_shared Shared)
endif()

if(OPENMODEL_BUILD_CLI)
    add_executable(openmodel-cli src/cli/main.cpp)
    target_link_libraries(openmodel-cli PRIVATE OpenModel::Core)
    target_compile_features(openmodel-cli PRIVATE cxx_std_17)
    target_compile_definitions(openmodel-cli PRIVATE OPENMODEL_VERSION="${PROJECT_VERSION}")
    set_target_properties(openmodel-cli PROPERTIES OUTPUT_NAME openmodel)
endif()

if(OPENMODEL_BUILD_WEBUI)
    find_path(OPENMODEL_WEBUI_INCLUDE_DIR webui.h
        HINTS "${OPENMODEL_WEBUI_ROOT}" "${OPENMODEL_WEBUI_ROOT}/include"
        PATH_SUFFIXES include)
    find_library(OPENMODEL_WEBUI_LIBRARY
        NAMES webui-2 webui
        HINTS "${OPENMODEL_WEBUI_ROOT}" "${OPENMODEL_WEBUI_ROOT}/lib" "${OPENMODEL_WEBUI_ROOT}/lib64"
        PATH_SUFFIXES lib lib64)
    if(NOT OPENMODEL_WEBUI_INCLUDE_DIR OR NOT OPENMODEL_WEBUI_LIBRARY)
        message(FATAL_ERROR "OPENMODEL_BUILD_WEBUI=ON requires WebUI SDK (webui.h and library). Set OPENMODEL_WEBUI_ROOT.")
    endif()
    # Embed the GUI page and the viewer page as byte arrays (portable: no string
    # literal length limits). Regenerated when the HTML sources change.
    function(openmodel_embed_file input output symbol)
        file(READ "${input}" _hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
        string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1
" _bytes "${_bytes}")
        file(WRITE "${output}"
            "static const unsigned char ${symbol}[] = {
${_bytes}0x00};
"
            "static const unsigned long ${symbol}_size = sizeof(${symbol}) - 1;
")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    endfunction()
    set(_openmodel_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    openmodel_embed_file("${CMAKE_CURRENT_SOURCE_DIR}/src/webui/gui.html" "${_openmodel_gen_dir}/gui_html.inc" gui_html)
    openmodel_embed_file("${CMAKE_CURRENT_SOURCE_DIR}/examples/webgl-fem-viewer/index.html" "${_openmodel_gen_dir}/viewer_html.inc" viewer_html)
    add_executable(openmodel-webui src/webui/main.cpp)
    target_include_directories(openmodel-webui PRIVATE "${OPENMODEL_WEBUI_INCLUDE_DIR}" "${_openmodel_gen_dir}")
    target_compile_definitions(openmodel-webui PRIVATE OPENMODEL_VERSION="${PROJECT_VERSION}")
    target_link_libraries(openmodel-webui PRIVATE OpenModel::Core "${OPENMODEL_WEBUI_LIBRARY}")
    target_compile_features(openmodel-webui PRIVATE cxx_std_17)
    set_target_properties(openmodel-webui PROPERTIES OUTPUT_NAME openmodel-webui)
endif()

if(OPENMODEL_BUILD_BENCHMARKS)
    add_executable(openmodel-benchmark benchmarks/openmodel_benchmark.cpp)
    target_link_libraries(openmodel-benchmark PRIVATE OpenModel::Core)
    target_compile_features(openmodel-benchmark PRIVATE cxx_std_17)
    add_executable(openmodel-io-benchmark benchmarks/openmodel_io_benchmark.cpp)
    target_link_libraries(openmodel-io-benchmark PRIVATE OpenModel::Core)
    target_compile_features(openmodel-io-benchmark PRIVATE cxx_std_17)
    if(WIN32)
        target_link_libraries(openmodel-benchmark PRIVATE Psapi)
    endif()
endif()

if(OPENMODEL_BUILD_TESTS)
    if(OPENMODEL_BUILD_CLI)
        add_test(NAME openmodel_cli_info COMMAND openmodel-cli info)
        add_test(NAME openmodel_cli_validate_demo COMMAND openmodel-cli validate-demo)
    endif()
    function(openmodel_add_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE OpenModel::Core)
        # The test suite uses assert in several legacy tests. Keep assertions
        # enabled even when the project is configured as Release.
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${target} COMMAND ${target})
    endfunction()
    openmodel_add_test(core_smoke_test tests/core_smoke_test.cpp)
    openmodel_add_test(mesh_binary_io_test tests/mesh_binary_io_test.cpp)
    openmodel_add_test(mesh_importer_test tests/mesh_importer_test.cpp)
    openmodel_add_test(mesh_validation_test tests/mesh_validation_test.cpp)
    openmodel_add_test(abaqus_writer_test tests/abaqus_writer_test.cpp)
    openmodel_add_test(physical_groups_test tests/physical_groups_test.cpp)
    openmodel_add_test(mesh_topology_test tests/mesh_topology_test.cpp)
    openmodel_add_test(surface_mesher_test tests/surface_mesher_test.cpp)
    openmodel_add_test(gltf_writer_test tests/gltf_writer_test.cpp)
    openmodel_add_test(surface_refiner_test tests/surface_refiner_test.cpp)
    openmodel_add_test(web_view_writer_test tests/web_view_writer_test.cpp)
    openmodel_add_test(fem_results_test tests/fem_results_test.cpp)
    openmodel_add_test(fatigue_result_source_test tests/fatigue/openmodel_fem_results_source_test.cpp)
    openmodel_add_test(fatigue_tensor_normalizer_test tests/fatigue/tensor_normalizer_test.cpp)
    openmodel_add_test(fatigue_sample_preparer_test tests/fatigue/fatigue_sample_preparer_test.cpp)
    openmodel_add_test(fatigue_stress_history_test tests/fatigue/stress_history_test.cpp)
    openmodel_add_test(fatigue_cycle_counting_test tests/fatigue/cycle_counting_test.cpp)
    openmodel_add_test(fatigue_multiaxial_cycle_contract_test tests/fatigue/multiaxial_cycle_contract_test.cpp)
    openmodel_add_test(fatigue_material_test tests/fatigue/fatigue_material_test.cpp)
    openmodel_add_test(fatigue_mean_stress_correction_test tests/fatigue/mean_stress_correction_test.cpp)
    openmodel_add_test(fatigue_damage_model_test tests/fatigue/fatigue_damage_model_test.cpp)
    openmodel_add_test(fatigue_strain_life_damage_model_test tests/fatigue/strain_life_damage_model_test.cpp)
    openmodel_add_test(fatigue_wang_brown_damage_model_test tests/fatigue/wang_brown_damage_model_test.cpp)
    openmodel_add_test(fatigue_strain_life_analysis_test tests/fatigue/strain_life_analysis_test.cpp)
    openmodel_add_test(fatigue_local_strain_correction_test tests/fatigue/local_strain_correction_test.cpp)
    openmodel_add_test(fatigue_analysis_test tests/fatigue/fatigue_analysis_test.cpp)
    openmodel_add_test(fatigue_report_test tests/fatigue/fatigue_report_test.cpp)
    openmodel_add_test(fatigue_report_writer_test tests/fatigue/fatigue_report_writer_test.cpp)
    openmodel_add_test(fatigue_spectrum_report_test tests/fatigue/fatigue_spectrum_report_test.cpp)
    openmodel_add_test(fatigue_damage_map_test tests/fatigue/fatigue_damage_map_test.cpp)
    openmodel_add_test(fatigue_damage_map_writer_test tests/fatigue/fatigue_damage_map_writer_test.cpp)
    openmodel_add_test(fatigue_damage_map_webview_adapter_test tests/fatigue/fatigue_damage_map_webview_adapter_test.cpp)
    openmodel_add_test(analysis_setup_test tests/analysis_setup_test.cpp)
    openmodel_add_test(mesh_selection_test tests/mesh_selection_test.cpp)
    set(_openmodel_core_test_names core_smoke_test mesh_validation_test abaqus_writer_test physical_groups_test
        mesh_topology_test surface_mesher_test gltf_writer_test surface_refiner_test
        web_view_writer_test mesh_binary_io_test mesh_importer_test fem_results_test fatigue_result_source_test fatigue_tensor_normalizer_test fatigue_sample_preparer_test fatigue_stress_history_test fatigue_cycle_counting_test fatigue_multiaxial_cycle_contract_test fatigue_material_test fatigue_mean_stress_correction_test fatigue_damage_model_test fatigue_strain_life_damage_model_test fatigue_wang_brown_damage_model_test fatigue_analysis_test fatigue_report_test fatigue_report_writer_test fatigue_spectrum_report_test fatigue_damage_map_test fatigue_damage_map_writer_test fatigue_damage_map_webview_adapter_test analysis_setup_test mesh_selection_test)
    if(OPENMODEL_BUILD_CLI)
        list(APPEND _openmodel_core_test_names openmodel_cli_info openmodel_cli_validate_demo)
    endif()
    set_tests_properties(${_openmodel_core_test_names} PROPERTIES LABELS "core")
endif()

# Solver-results fixture (cantilever beam: mesh, CalculiX-style .frd, .csv) written by a Python script, and CLI checks on it.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(OPENMODEL_BUILD_TESTS AND OPENMODEL_BUILD_CLI AND Python3_FOUND)
    set(_beam_dir "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture")
    add_test(NAME fem_results_fixture
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/make_beam_results.py" "${_beam_dir}")
    add_test(NAME fem_results_cli_frd
        COMMAND openmodel-cli export-web "${_beam_dir}/beam.inp" "${_beam_dir}/beam.mgwv" --results "${_beam_dir}/beam.frd")
    add_test(NAME fem_results_cli_csv
        COMMAND openmodel-cli export-web "${_beam_dir}/beam.inp" "${_beam_dir}/beam_csv.mgwv" --results "${_beam_dir}/beam.csv")
    add_test(NAME fem_results_cli_info COMMAND openmodel-cli results-info "${_beam_dir}/beam.frd")
    add_test(NAME fem_results_cli_modal
        COMMAND openmodel-cli export-web "${_beam_dir}/beam.inp" "${_beam_dir}/beam_modal.mgwv" --results "${_beam_dir}/beam_modal.frd")
    add_test(NAME fem_results_cli_modal_info COMMAND openmodel-cli results-info "${_beam_dir}/beam_modal.frd")
    add_test(NAME fem_results_cli_dat_merge
        COMMAND openmodel-cli export-web "${_beam_dir}/beam.inp" "${_beam_dir}/beam_merged.mgwv"
                --results "${_beam_dir}/beam.frd" --results "${_beam_dir}/beam.dat")
    add_test(NAME fem_results_cli_dat_eigen COMMAND openmodel-cli results-info "${_beam_dir}/beam_modal.dat")
    add_test(NAME fem_results_cli_elements
        COMMAND openmodel-cli export-web "${_beam_dir}/beam.inp" "${_beam_dir}/beam_elem.mgwv" --results "${_beam_dir}/beam_elem.csv")
    add_test(NAME fem_results_cli_wrong_mesh
        COMMAND openmodel-cli export-web "${_beam_dir}/beam.inp" "${_beam_dir}/rejected.mgwv" --results "${_beam_dir}/beam_other.frd")
    set_tests_properties(fem_results_fixture PROPERTIES FIXTURES_SETUP beam_fixture LABELS "core;results")
    set_tests_properties(fem_results_cli_frd fem_results_cli_csv fem_results_cli_info PROPERTIES
        FIXTURES_REQUIRED beam_fixture LABELS "core;results")
    set_tests_properties(fem_results_cli_modal PROPERTIES FIXTURES_REQUIRED beam_fixture LABELS "core;results"
        PASS_REGULAR_EXPRESSION "Modes: 1=8355")
    set_tests_properties(fem_results_cli_modal_info PROPERTIES FIXTURES_REQUIRED beam_fixture LABELS "core;results"
        PASS_REGULAR_EXPRESSION "5 vibration mode")
    set_tests_properties(fem_results_cli_dat_merge PROPERTIES FIXTURES_REQUIRED beam_fixture LABELS "core;results"
        PASS_REGULAR_EXPRESSION "element fields")
    set_tests_properties(fem_results_cli_dat_eigen PROPERTIES FIXTURES_REQUIRED beam_fixture LABELS "core;results"
        PASS_REGULAR_EXPRESSION "mode 1: f = 8355")
    set_tests_properties(fem_results_cli_elements PROPERTIES FIXTURES_REQUIRED beam_fixture LABELS "core;results"
        PASS_REGULAR_EXPRESSION "13 element fields")
    # Results of another mesh must be refused with a message (the command exits with an error).
    set_tests_properties(fem_results_cli_wrong_mesh PROPERTIES
        FIXTURES_REQUIRED beam_fixture LABELS "core;results" PASS_REGULAR_EXPRESSION "different mesh")

    # Solver launch: a stand-in for ccx copies the fixture results (plumbing test, not a physics test).
    set(_fake_ccx "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/fake_ccx.py")
    set(_support "${CMAKE_CURRENT_SOURCE_DIR}/tests/support")
    set(_solve_env "${CMAKE_COMMAND}" -E env "FAKE_CCX_FIXTURES=${_beam_dir}")
    add_test(NAME solver_cli_write_input
        COMMAND openmodel-cli write-input "${_beam_dir}/beam.inp" "${_support}/beam_analysis.txt" "${_beam_dir}/solve_wi/job.inp")
    add_test(NAME solver_cli_bad_script
        COMMAND openmodel-cli write-input "${_beam_dir}/beam.inp" "${_support}/bad_analysis.txt" "${_beam_dir}/solve_bad/job.inp")
    add_test(NAME solver_cli_static
        COMMAND ${_solve_env} $<TARGET_FILE:openmodel-cli> solve "${_beam_dir}/beam.inp" "${_support}/beam_analysis.txt"
                "${_beam_dir}/solve_static" --solver "${_fake_ccx}" --web "${_beam_dir}/solve_static/out.mgwv")
    add_test(NAME solver_cli_modal
        COMMAND ${_solve_env} $<TARGET_FILE:openmodel-cli> solve "${_beam_dir}/beam.inp" "${_support}/beam_analysis_modal.txt"
                "${_beam_dir}/solve_modal" --solver "${_fake_ccx}" --web "${_beam_dir}/solve_modal/out.mgwv")
    add_test(NAME solver_cli_failure
        COMMAND ${_solve_env} FAKE_CCX_FAIL=1 $<TARGET_FILE:openmodel-cli> solve "${_beam_dir}/beam.inp" "${_support}/beam_analysis.txt"
                "${_beam_dir}/solve_fail" --solver "${_fake_ccx}")
    add_test(NAME solver_cli_missing
        COMMAND openmodel-cli solve "${_beam_dir}/beam.inp" "${_support}/beam_analysis.txt" "${_beam_dir}/solve_missing" --solver /nonexistent/ccx)
    set_tests_properties(solver_cli_write_input solver_cli_bad_script solver_cli_static solver_cli_modal solver_cli_failure solver_cli_missing
        PROPERTIES FIXTURES_REQUIRED beam_fixture LABELS "core;solver")
    set_tests_properties(solver_cli_write_input PROPERTIES PASS_REGULAR_EXPRESSION "force on 9 nodes")
    set_tests_properties(solver_cli_bad_script PROPERTIES PASS_REGULAR_EXPRESSION "line 2.*line 3")
    set_tests_properties(solver_cli_static PROPERTIES PASS_REGULAR_EXPRESSION "Results: .*job.frd .*job.dat")
    set_tests_properties(solver_cli_modal PROPERTIES PASS_REGULAR_EXPRESSION "frequency analysis")
    set_tests_properties(solver_cli_failure PROPERTIES PASS_REGULAR_EXPRESSION "nonpositive jacobian")
    set_tests_properties(solver_cli_missing PROPERTIES PASS_REGULAR_EXPRESSION "Cannot start")
endif()

if(OPENMODEL_BUILD_TESTS AND OPENMODEL_TEST_WEBGL_VIEWER AND OPENMODEL_BUILD_CLI)
    find_program(OPENMODEL_NODE_EXECUTABLE node REQUIRED)
    set(_viewer_data "${CMAKE_CURRENT_BINARY_DIR}/viewer_smoke.mgwv")
    add_test(NAME webgl_viewer_data COMMAND openmodel-cli demo-web "${_viewer_data}" 8)
    add_test(NAME webgl_viewer_smoke
        COMMAND "${OPENMODEL_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/webgl/viewer_smoke.js"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/webgl-fem-viewer/index.html" "${_viewer_data}")
    set_tests_properties(webgl_viewer_data PROPERTIES FIXTURES_SETUP viewer_data LABELS "webgl")
    if(Python3_FOUND)
        # The same viewer with solver results (fields + deformed shape) and with a file that has none.
        add_test(NAME webgl_results_smoke
            COMMAND "${OPENMODEL_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/webgl/viewer_results_smoke.js"
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/webgl-fem-viewer/index.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam.mgwv" "${_viewer_data}")
        set_tests_properties(webgl_results_smoke PROPERTIES
            FIXTURES_REQUIRED "viewer_data;beam_fixture" LABELS "webgl;results" TIMEOUT 180)
        add_test(NAME webgl_pick_data
            COMMAND openmodel-cli export-web "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam.inp"
                    "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam_pick.mgwv" --results "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam.frd")
        add_test(NAME webgl_pick_smoke
            COMMAND "${OPENMODEL_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/webgl/viewer_pick_smoke.js"
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/webgl-fem-viewer/index.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam_pick.mgwv" "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam.inp")
        set_tests_properties(webgl_pick_data PROPERTIES FIXTURES_REQUIRED beam_fixture FIXTURES_SETUP pick_data LABELS "webgl;pick")
        set_tests_properties(webgl_pick_smoke PROPERTIES FIXTURES_REQUIRED "beam_fixture;pick_data" LABELS "webgl;pick" TIMEOUT 240)
        add_test(NAME webgl_modal_smoke
            COMMAND "${OPENMODEL_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/webgl/viewer_modal_smoke.js"
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/webgl-fem-viewer/index.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam_modal.mgwv"
                    "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam_elem.mgwv"
                    "${CMAKE_CURRENT_BINARY_DIR}/beam_fixture/beam.mgwv")
        set_tests_properties(webgl_modal_smoke PROPERTIES
            FIXTURES_REQUIRED "beam_fixture" LABELS "webgl;results" TIMEOUT 180
            DEPENDS "fem_results_cli_modal;fem_results_cli_elements;fem_results_cli_frd")
    endif()
    set_tests_properties(webgl_viewer_smoke PROPERTIES FIXTURES_REQUIRED viewer_data LABELS "webgl" TIMEOUT 180)
endif()

if(OPENMODEL_BUILD_ASSIMP_BACKEND)
    find_package(assimp CONFIG REQUIRED)
    add_library(openmodel_assimp STATIC src/AssimpModel.cpp)
    target_link_libraries(openmodel_assimp PUBLIC OpenModel::Core)
    if(TARGET assimp::assimp)
        target_link_libraries(openmodel_assimp PUBLIC assimp::assimp)
    elseif(TARGET assimp)
        target_link_libraries(openmodel_assimp PUBLIC assimp)
    else()
        message(FATAL_ERROR "Assimp package found, but no assimp::assimp or assimp target is available")
    endif()
    if(OPENMODEL_BUILD_TESTS)
        add_executable(assimp_model_test tests/assimp_model_test.cpp)
        target_link_libraries(assimp_model_test PRIVATE openmodel_assimp)
        add_test(NAME assimp_model_test COMMAND assimp_model_test)
        set_tests_properties(assimp_model_test PROPERTIES LABELS "integration;assimp")
    endif()
endif()

if(OPENMODEL_BUILD_GMSH_BACKEND)
    # Explicit SDK paths take precedence over package discovery. This is useful
    # with the official precompiled Windows SDK, which may not ship a CMake config.
    set(_openmodel_gmsh_target "")
    if(NOT GMSH_ROOT AND NOT GMSH_INCLUDE_DIR AND NOT GMSH_LIBRARY_DEBUG
       AND NOT GMSH_LIBRARY_RELEASE AND NOT GMSH_LIBRARY)
        find_package(Gmsh CONFIG QUIET)
        find_package(gmsh CONFIG QUIET)
        foreach(_candidate Gmsh::gmsh gmsh::gmsh gmsh)
            if(TARGET ${_candidate})
                set(_openmodel_gmsh_target ${_candidate})
                break()
            endif()
        endforeach()
    endif()

    if(NOT _openmodel_gmsh_target)
        # Accept both a conventional SDK root and the directory explicitly
        # supplied by the user. Keep GMSH_INCLUDE_DIR as a cache PATH so it is
        # visible/editable in CMake GUIs (including Visual Studio generators).
        if(NOT GMSH_INCLUDE_DIR)
            find_path(_OPENMODEL_GMSH_INCLUDE_DIR gmsh.h
                HINTS "${GMSH_ROOT}" "$ENV{GMSH_ROOT}"
                PATH_SUFFIXES include include/gmsh)
            if(_OPENMODEL_GMSH_INCLUDE_DIR)
                set(GMSH_INCLUDE_DIR "${_OPENMODEL_GMSH_INCLUDE_DIR}" CACHE PATH
                    "Directory containing gmsh.h" FORCE)
            endif()
        endif()

        # Preserve the old single GMSH_LIBRARY setting, but prefer per-config
        # paths when available. The official Windows SDK typically provides
        # gmsh.h, gmsh.lib and gmsh.dll, without an accompanying package config.
        if(GMSH_LIBRARY AND NOT GMSH_LIBRARY_RELEASE)
            set(GMSH_LIBRARY_RELEASE "${GMSH_LIBRARY}" CACHE FILEPATH
                "Gmsh release import/static library (for example gmsh.lib)" FORCE)
        endif()
        if(NOT GMSH_LIBRARY_RELEASE)
            find_library(_OPENMODEL_GMSH_LIBRARY_RELEASE NAMES gmsh libgmsh gmsh.lib
                HINTS "${GMSH_ROOT}" "$ENV{GMSH_ROOT}"
                PATH_SUFFIXES lib/Release Release lib/RelWithDebInfo lib lib64)
            if(_OPENMODEL_GMSH_LIBRARY_RELEASE)
                set(GMSH_LIBRARY_RELEASE "${_OPENMODEL_GMSH_LIBRARY_RELEASE}" CACHE FILEPATH
                    "Gmsh release import/static library (for example gmsh.lib)" FORCE)
            endif()
        endif()
        if(NOT GMSH_LIBRARY_DEBUG)
            find_library(_OPENMODEL_GMSH_LIBRARY_DEBUG NAMES gmshd libgmshd gmsh-debug gmsh_d gmsh
                HINTS "${GMSH_ROOT}" "$ENV{GMSH_ROOT}"
                PATH_SUFFIXES lib/Debug Debug lib lib64)
            if(_OPENMODEL_GMSH_LIBRARY_DEBUG)
                set(GMSH_LIBRARY_DEBUG "${_OPENMODEL_GMSH_LIBRARY_DEBUG}" CACHE FILEPATH
                    "Gmsh debug import/static library (for example gmshd.lib)" FORCE)
            endif()
        endif()

        if(NOT GMSH_INCLUDE_DIR OR (NOT GMSH_LIBRARY_RELEASE AND NOT GMSH_LIBRARY_DEBUG))
            message(FATAL_ERROR
                "OPENMODEL_BUILD_GMSH_BACKEND=ON, but Gmsh was not fully located. "
                "Set GMSH_ROOT, GMSH_INCLUDE_DIR (directory containing gmsh.h), "
                "and GMSH_LIBRARY_RELEASE/GMSH_LIBRARY_DEBUG. For a precompiled "
                "Windows SDK, point them to its include directory and matching .lib files.")
        endif()

        # If only one variant is available, use it for every configuration.
        if(NOT GMSH_LIBRARY_DEBUG)
            set(GMSH_LIBRARY_DEBUG "${GMSH_LIBRARY_RELEASE}")
        endif()
        if(NOT GMSH_LIBRARY_RELEASE)
            set(GMSH_LIBRARY_RELEASE "${GMSH_LIBRARY_DEBUG}")
        endif()

        add_library(openmodel_gmsh_external UNKNOWN IMPORTED GLOBAL)
        set_target_properties(openmodel_gmsh_external PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${GMSH_INCLUDE_DIR}"
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION_DEBUG "${GMSH_LIBRARY_DEBUG}"
            IMPORTED_LOCATION_RELEASE "${GMSH_LIBRARY_RELEASE}"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
            MAP_IMPORTED_CONFIG_MINSIZEREL Release)
        # The target is intentionally configuration-aware: Debug uses the
        # debug library when supplied; Release/RelWithDebInfo/MinSizeRel use release.
        set(_openmodel_gmsh_target openmodel_gmsh_external)

        if(WIN32)
            if(NOT GMSH_RUNTIME_LIBRARY_DEBUG)
                find_file(_OPENMODEL_GMSH_RUNTIME_LIBRARY_DEBUG NAMES gmsh.dll libgmsh.dll gmshd.dll
                    HINTS "${GMSH_ROOT}" "$ENV{GMSH_ROOT}"
                    PATH_SUFFIXES bin/Debug Debug bin)
                if(_OPENMODEL_GMSH_RUNTIME_LIBRARY_DEBUG)
                    set(GMSH_RUNTIME_LIBRARY_DEBUG "${_OPENMODEL_GMSH_RUNTIME_LIBRARY_DEBUG}" CACHE FILEPATH
                        "Gmsh debug runtime DLL on Windows, if applicable" FORCE)
                endif()
            endif()
            if(NOT GMSH_RUNTIME_LIBRARY_RELEASE)
                find_file(_OPENMODEL_GMSH_RUNTIME_LIBRARY_RELEASE NAMES gmsh.dll libgmsh.dll
                    HINTS "${GMSH_ROOT}" "$ENV{GMSH_ROOT}"
                    PATH_SUFFIXES bin/Release Release bin)
                if(_OPENMODEL_GMSH_RUNTIME_LIBRARY_RELEASE)
                    set(GMSH_RUNTIME_LIBRARY_RELEASE "${_OPENMODEL_GMSH_RUNTIME_LIBRARY_RELEASE}" CACHE FILEPATH
                        "Gmsh release runtime DLL on Windows, if applicable" FORCE)
                endif()
            endif()
            if(NOT GMSH_RUNTIME_LIBRARY_DEBUG)
                set(GMSH_RUNTIME_LIBRARY_DEBUG "${GMSH_RUNTIME_LIBRARY_RELEASE}")
            endif()
            if(NOT GMSH_RUNTIME_LIBRARY_RELEASE)
                set(GMSH_RUNTIME_LIBRARY_RELEASE "${GMSH_RUNTIME_LIBRARY_DEBUG}")
            endif()
        endif()
    endif()

    add_library(openmodel_gmsh STATIC src/GmshMesher.cpp)
    target_link_libraries(openmodel_gmsh PUBLIC OpenModel::Core ${_openmodel_gmsh_target})
    # Ensure the user-specified include path is present even when a package
    # config target was discovered and GMSH_INCLUDE_DIR was explicitly set.
    if(GMSH_INCLUDE_DIR)
        target_include_directories(openmodel_gmsh PUBLIC "${GMSH_INCLUDE_DIR}")
    endif()
    target_compile_definitions(openmodel_gmsh PUBLIC OPENMODEL_HAS_GMSH=1)

    if(OPENMODEL_BUILD_TESTS)
        add_executable(gmsh_integration_test tests/gmsh_integration_test.cpp)
        target_link_libraries(gmsh_integration_test PRIVATE openmodel_gmsh)
        if(MSVC)
            target_compile_options(gmsh_integration_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(gmsh_integration_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME gmsh_integration_test COMMAND gmsh_integration_test)
        set_tests_properties(gmsh_integration_test PROPERTIES LABELS "integration;gmsh" TIMEOUT 180)
    endif()

    # Windows DLLs are not copied automatically when linking to a .lib import
    # library. Copy the matching runtime DLL beside executables for easy launch.
    if(WIN32 AND GMSH_RUNTIME_LIBRARY_RELEASE)
        function(openmodel_copy_gmsh_runtime target_name)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<IF:$<CONFIG:Debug>,${GMSH_RUNTIME_LIBRARY_DEBUG},${GMSH_RUNTIME_LIBRARY_RELEASE}>"
                    "$<TARGET_FILE_DIR:${target_name}>"
                VERBATIM)
        endfunction()
        if(TARGET gmsh_integration_test)
            openmodel_copy_gmsh_runtime(gmsh_integration_test)
        endif()
        if(TARGET openmodel-cli)
            openmodel_copy_gmsh_runtime(openmodel-cli)
        endif()
        if(TARGET openmodel-webui)
            openmodel_copy_gmsh_runtime(openmodel-webui)
        endif()
    endif()
endif()

if(OPENMODEL_BUILD_OCC_BACKEND)
    find_package(OpenCASCADE REQUIRED)
    add_library(openmodel_occ STATIC src/OCCModel.cpp)
    target_link_libraries(openmodel_occ PUBLIC OpenModel::Core)
    if(DEFINED OpenCASCADE_INCLUDE_DIR)
        target_include_directories(openmodel_occ PUBLIC ${OpenCASCADE_INCLUDE_DIR})
    elseif(DEFINED OpenCASCADE_INCLUDE_DIRS)
        target_include_directories(openmodel_occ PUBLIC ${OpenCASCADE_INCLUDE_DIRS})
    endif()
    # Link only the toolkits that OCCModel actually uses. OpenCASCADE_LIBRARIES
    # may list every toolkit, including optional Draw/Test libraries that
    # distribution packages split into separate -dev packages.
    set(_openmodel_occ_toolkits TKernel TKMath TKBRep TKGeomBase TKG3d TKTopAlgo
        TKGeomAlgo TKMesh TKXSBase TKSTEP TKIGES TKSTEPBase TKSTEPAttr TKSTEP209 TKXCAF TKLCAF TKCDF TKXDESTEP TKXDEIGES TKBO)
    foreach(_occ_lib IN LISTS _openmodel_occ_toolkits)
        if(TARGET OpenCASCADE::${_occ_lib})
            target_link_libraries(openmodel_occ PUBLIC OpenCASCADE::${_occ_lib})
        elseif(TARGET ${_occ_lib})
            target_link_libraries(openmodel_occ PUBLIC ${_occ_lib})
        else()
            find_library(_openmodel_occ_${_occ_lib} NAMES ${_occ_lib}
                HINTS "${OpenCASCADE_LIBRARY_DIR}" "${OpenCASCADE_DIR}/../.." PATH_SUFFIXES lib lib64)
            if(NOT _openmodel_occ_${_occ_lib})
                message(FATAL_ERROR "Open CASCADE toolkit ${_occ_lib} not found")
            endif()
            target_link_libraries(openmodel_occ PUBLIC "${_openmodel_occ_${_occ_lib}}")
        endif()
    endforeach()
    target_compile_definitions(openmodel_occ PUBLIC OPENMODEL_HAS_OCC=1)
    if(OPENMODEL_BUILD_TESTS)
        add_executable(occ_integration_test tests/occ_integration_test.cpp)
        target_link_libraries(occ_integration_test PRIVATE openmodel_occ)
        add_test(NAME occ_integration_test COMMAND occ_integration_test)
        set_tests_properties(occ_integration_test PROPERTIES LABELS "integration;occ" TIMEOUT 180)
        add_executable(occ_structure_test tests/occ_structure_test.cpp)
        target_link_libraries(occ_structure_test PRIVATE openmodel_occ)
        add_executable(make_assembly_fixture tests/make_assembly_fixture.cpp)
        target_link_libraries(make_assembly_fixture PRIVATE openmodel_occ)
        add_test(NAME occ_structure_test COMMAND occ_structure_test)
        set_tests_properties(occ_structure_test PROPERTIES LABELS "integration;occ" TIMEOUT 180)
    endif()
endif()

if(OPENMODEL_BUILD_NETGEN_BACKEND)
    find_package(Netgen CONFIG QUIET)
    find_package(netgen CONFIG QUIET)
    set(_openmodel_netgen_target "")
    foreach(_candidate Netgen::nglib netgen::nglib nglib Netgen::netgen netgen)
        if(TARGET ${_candidate})
            set(_openmodel_netgen_target ${_candidate})
            break()
        endif()
    endforeach()
    if(NOT _openmodel_netgen_target)
        find_path(NETGEN_INCLUDE_DIR nglib.h HINTS ENV NETGEN_ROOT PATH_SUFFIXES include include/netgen netgen nglib)
        find_library(NETGEN_LIBRARY NAMES nglib netgen HINTS ENV NETGEN_ROOT PATH_SUFFIXES lib lib64)
        if(NOT NETGEN_INCLUDE_DIR OR NOT NETGEN_LIBRARY)
            message(FATAL_ERROR "OPENMODEL_BUILD_NETGEN_BACKEND=ON, but nglib.h or the Netgen library was not found. Install Netgen or set NETGEN_ROOT.")
        endif()
        add_library(openmodel_netgen_external UNKNOWN IMPORTED)
        set_target_properties(openmodel_netgen_external PROPERTIES IMPORTED_LOCATION "${NETGEN_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${NETGEN_INCLUDE_DIR}")
        set(_openmodel_netgen_target openmodel_netgen_external)
    endif()
    add_library(openmodel_netgen STATIC src/NetgenMesher.cpp)
    target_link_libraries(openmodel_netgen PUBLIC OpenModel::Core ${_openmodel_netgen_target})
    if(OPENMODEL_NETGEN_HAS_OCC)
        target_compile_definitions(openmodel_netgen PUBLIC OPENMODEL_NETGEN_HAS_OCC=1)
    endif()
    if(OPENMODEL_BUILD_TESTS)
        add_executable(netgen_adapter_test tests/netgen_adapter_test.cpp)
        target_link_libraries(netgen_adapter_test PRIVATE openmodel_netgen)
        if(OPENMODEL_NETGEN_TEST_STEP)
            file(TO_CMAKE_PATH "${OPENMODEL_NETGEN_TEST_STEP}" _netgen_test_step_path)
            target_compile_definitions(netgen_adapter_test PRIVATE OPENMODEL_NETGEN_TEST_STEP="${_netgen_test_step_path}")
        endif()
        add_test(NAME netgen_adapter_test COMMAND netgen_adapter_test)
        if(OPENMODEL_NETGEN_TEST_STEP)
            set_tests_properties(netgen_adapter_test PROPERTIES LABELS "integration;netgen;real-cad-fixture" TIMEOUT 180)
        else()
            set_tests_properties(netgen_adapter_test PROPERTIES LABELS "backend-smoke;netgen" TIMEOUT 180)
        endif()
    endif()
endif()

if(OPENMODEL_BUILD_MFEM_BACKEND)
    find_package(MFEM CONFIG QUIET)
    set(_openmodel_mfem_target "")
    foreach(_candidate MFEM::mfem mfem)
        if(TARGET ${_candidate})
            set(_openmodel_mfem_target ${_candidate})
            break()
        endif()
    endforeach()
    if(NOT _openmodel_mfem_target)
        find_path(MFEM_INCLUDE_DIR mfem.hpp HINTS ENV MFEM_ROOT PATH_SUFFIXES include include/mfem)
        find_library(MFEM_LIBRARY NAMES mfem HINTS ENV MFEM_ROOT PATH_SUFFIXES lib lib64)
        if(NOT MFEM_INCLUDE_DIR OR NOT MFEM_LIBRARY)
            message(FATAL_ERROR "OPENMODEL_BUILD_MFEM_BACKEND=ON, but mfem.hpp or the MFEM library was not found. Install MFEM or set MFEM_ROOT.")
        endif()
        add_library(openmodel_mfem_external UNKNOWN IMPORTED)
        set_target_properties(openmodel_mfem_external PROPERTIES IMPORTED_LOCATION "${MFEM_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${MFEM_INCLUDE_DIR}")
        set(_openmodel_mfem_target openmodel_mfem_external)
    endif()
    add_library(openmodel_mfem STATIC src/MFEMMesh.cpp)
    target_link_libraries(openmodel_mfem PUBLIC OpenModel::Core ${_openmodel_mfem_target})
    target_compile_definitions(openmodel_mfem INTERFACE OPENMODEL_HAS_MFEM=1)
    if(OPENMODEL_BUILD_TESTS)
        add_executable(mfem_adapter_test tests/mfem_adapter_test.cpp)
        target_link_libraries(mfem_adapter_test PRIVATE openmodel_mfem)
        if(MSVC)
            target_compile_options(mfem_adapter_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(mfem_adapter_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME mfem_adapter_test COMMAND mfem_adapter_test)
        set_tests_properties(mfem_adapter_test PROPERTIES LABELS "integration;mfem" TIMEOUT 180)
    endif()
endif()

if(OPENMODEL_BUILD_TESTS AND OPENMODEL_BUILD_GMSH_BACKEND AND OPENMODEL_BUILD_OCC_BACKEND)
    add_executable(gmsh_assembly_test tests/gmsh_assembly_test.cpp)
    target_link_libraries(gmsh_assembly_test PRIVATE openmodel_gmsh openmodel_occ)
    add_test(NAME gmsh_assembly_test COMMAND gmsh_assembly_test)
    set_tests_properties(gmsh_assembly_test PROPERTIES LABELS "integration;gmsh;occ" TIMEOUT 180)
endif()

if(OPENMODEL_BUILD_TETGEN_BACKEND)
    find_path(TETGEN_INCLUDE_DIR tetgen.h HINTS ENV TETGEN_ROOT PATH_SUFFIXES include)
    find_library(TETGEN_LIBRARY NAMES tet tetgen HINTS ENV TETGEN_ROOT PATH_SUFFIXES lib lib64)
    if(NOT TETGEN_INCLUDE_DIR OR NOT TETGEN_LIBRARY)
        message(FATAL_ERROR "OPENMODEL_BUILD_TETGEN_BACKEND=ON, but tetgen.h or the TetGen library was not found. Set TETGEN_ROOT.")
    endif()
    add_library(openmodel_tetgen STATIC src/TetGenMesher.cpp)
    target_include_directories(openmodel_tetgen PUBLIC "${TETGEN_INCLUDE_DIR}")
    target_link_libraries(openmodel_tetgen PUBLIC OpenModel::Core "${TETGEN_LIBRARY}")
    target_compile_features(openmodel_tetgen PUBLIC cxx_std_17)
    if(OPENMODEL_BUILD_TESTS)
        add_executable(tetgen_integration_test tests/tetgen_integration_test.cpp)
        target_link_libraries(tetgen_integration_test PRIVATE openmodel_tetgen)
        if(MSVC)
            target_compile_options(tetgen_integration_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(tetgen_integration_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME tetgen_integration_test COMMAND tetgen_integration_test)
        set_tests_properties(tetgen_integration_test PROPERTIES LABELS "integration;tetgen" TIMEOUT 180)
    endif()
endif()

# Connect optional CAD importers to the GUI only when the corresponding backend
# was requested. The GUI remains buildable without external CAD dependencies.
if(OPENMODEL_BUILD_WEBUI)
    if(OPENMODEL_BUILD_ASSIMP_BACKEND)
        target_link_libraries(openmodel-webui PRIVATE openmodel_assimp)
        target_compile_definitions(openmodel-webui PRIVATE OPENMODEL_HAS_ASSIMP=1)
    endif()
    if(OPENMODEL_BUILD_OCC_BACKEND)
        target_link_libraries(openmodel-webui PRIVATE openmodel_occ)
        target_compile_definitions(openmodel-webui PRIVATE OPENMODEL_HAS_OCC=1)
    endif()
    if(OPENMODEL_BUILD_TETGEN_BACKEND)
        target_link_libraries(openmodel-webui PRIVATE openmodel_tetgen)
        target_compile_definitions(openmodel-webui PRIVATE OPENMODEL_HAS_TETGEN=1)
    endif()
    if(OPENMODEL_BUILD_NETGEN_BACKEND)
        target_link_libraries(openmodel-webui PRIVATE openmodel_netgen)
        target_compile_definitions(openmodel-webui PRIVATE OPENMODEL_HAS_NETGEN=1)
    endif()
endif()

if(OPENMODEL_BUILD_WEBUI AND OPENMODEL_BUILD_GMSH_BACKEND)
    target_link_libraries(openmodel-webui PRIVATE openmodel_gmsh)
    target_compile_definitions(openmodel-webui PRIVATE OPENMODEL_HAS_GMSH=1)
endif()

if(OPENMODEL_BUILD_CLI AND OPENMODEL_BUILD_GMSH_BACKEND)
    target_link_libraries(openmodel-cli PRIVATE openmodel_gmsh)
    target_compile_definitions(openmodel-cli PRIVATE OPENMODEL_HAS_GMSH=1)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(OPENMODEL_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/OpenModel")

set(OPENMODEL_INSTALL_TARGETS openmodel_core)
if(OPENMODEL_BUILD_SHARED)
    list(APPEND OPENMODEL_INSTALL_TARGETS openmodel_shared)
endif()
install(TARGETS ${OPENMODEL_INSTALL_TARGETS}
    EXPORT OpenModelTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(OPENMODEL_BUILD_CLI)
    install(TARGETS openmodel-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(OPENMODEL_BUILD_WEBUI)
    install(TARGETS openmodel-webui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY examples/webgl-fem-viewer
    DESTINATION ${CMAKE_INSTALL_DATADIR}/OpenModel/examples)
install(DIRECTORY include/OpenModel
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT OpenModelTargets
    FILE OpenModelTargets.cmake
    NAMESPACE OpenModel::
    DESTINATION ${OPENMODEL_INSTALL_CMAKEDIR})

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenModelConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenModelConfig.cmake"
    INSTALL_DESTINATION "${OPENMODEL_INSTALL_CMAKEDIR}"
    NO_SET_AND_CHECK_MACRO)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/OpenModelConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OpenModelConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenModelConfigVersion.cmake"
    DESTINATION "${OPENMODEL_INSTALL_CMAKEDIR}")

# Export a convenient build-tree package as well as the installed package.
export(EXPORT OpenModelTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/OpenModelTargets.cmake"
    NAMESPACE OpenModel::)

# Produce relocatable binary/source-install packages with CPack.
set(CPACK_PACKAGE_NAME "OpenModel")
set(CPACK_PACKAGE_VENDOR "OpenModel contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ mesh generation and CAD mesh utility library")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "OpenModel-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "OpenModel")
set(CPACK_GENERATOR "ZIP;TGZ")
include(CPack)
