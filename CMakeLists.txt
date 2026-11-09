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
set(OPENMODEL_GMSH_TEST_STEP "" CACHE FILEPATH "Optional real STEP file for the Gmsh/OCC end-face mapping regression test")
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
    src/OpenModelProject.cpp
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
    src/CodeAster.cpp
    src/CodeAsterResults.cpp
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
    src/fatigue/WangBrownRelativeStrain.cpp
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
target_compile_definitions(openmodel_core PRIVATE OPENMODEL_VERSION="${PROJECT_VERSION}")
add_library(OpenModel::Core ALIAS openmodel_core)
set_target_properties(openmodel_core PROPERTIES OUTPUT_NAME OpenModelCore)
openmodel_configure_core_target(openmodel_core Core)

if(OPENMODEL_BUILD_SHARED)
    add_library(openmodel_shared SHARED ${OPENMODEL_CORE_SOURCES})
    add_library(OpenModel::Shared ALIAS openmodel_shared)
    target_link_libraries(openmodel_shared PRIVATE Threads::Threads)
    target_compile_definitions(openmodel_shared PRIVATE OPENMODEL_VERSION="${PROJECT_VERSION}")
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
    if(WIN32)
        # webui.h pulls in <windows.h>, whose min/max macros break std::numeric_limits<T>::max().
        target_compile_definitions(openmodel-webui PRIVATE NOMINMAX)
    endif()
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
    openmodel_add_test(openmodel_project_test tests/openmodel_project_test.cpp)
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
    openmodel_add_test(fatigue_wang_brown_relative_strain_test tests/fatigue/wang_brown_relative_strain_test.cpp)
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
    openmodel_add_test(code_aster_test tests/code_aster_test.cpp)
    set(_openmodel_core_test_names core_smoke_test mesh_validation_test abaqus_writer_test physical_groups_test
        mesh_topology_test surface_mesher_test gltf_writer_test surface_refiner_test
        web_view_writer_test mesh_binary_io_test openmodel_project_test mesh_importer_test fem_results_test fatigue_result_source_test fatigue_tensor_normalizer_test fatigue_sample_preparer_test fatigue_stress_history_test fatigue_cycle_counting_test fatigue_multiaxial_cycle_contract_test fatigue_material_test fatigue_mean_stress_correction_test fatigue_damage_model_test fatigue_strain_life_damage_model_test fatigue_wang_brown_damage_model_test fatigue_analysis_test fatigue_report_test fatigue_report_writer_test fatigue_spectrum_report_test fatigue_damage_map_test fatigue_damage_map_writer_test fatigue_damage_map_webview_adapter_test analysis_setup_test mesh_selection_test code_aster_test)
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

    # code_aster: a stand-in for run_aster writes made-up result tables (plumbing test; real code_aster is not run).
    set(_fake_aster "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/fake_aster.py")
    add_test(NAME aster_cli_export
        COMMAND openmodel-cli export-aster "${_beam_dir}/beam.inp" "${_support}/beam_analysis.txt" "${_beam_dir}/aster_export" --job beam --solver MUMPS)
    add_test(NAME aster_cli_export_mail
        COMMAND openmodel-cli export-mail "${_beam_dir}/beam.inp" "${_beam_dir}/beam_export.mail")
    add_test(NAME aster_cli_import_mail
        COMMAND openmodel-cli export-web "${_beam_dir}/beam_export.mail" "${_beam_dir}/beam_from_mail.mgwv")
    add_test(NAME aster_cli_static
        COMMAND $<TARGET_FILE:openmodel-cli> solve-aster "${_beam_dir}/beam.inp" "${_support}/beam_analysis.txt"
                "${_beam_dir}/aster_static" --launcher "${_fake_aster}" --web "${_beam_dir}/aster_static/out.mgwv")
    add_test(NAME aster_cli_modal
        COMMAND $<TARGET_FILE:openmodel-cli> solve-aster "${_beam_dir}/beam.inp" "${_support}/beam_analysis_modal.txt"
                "${_beam_dir}/aster_modal" --launcher "${_fake_aster}" --web "${_beam_dir}/aster_modal/out.mgwv")
    add_test(NAME aster_cli_failure
        COMMAND ${CMAKE_COMMAND} -E env FAKE_ASTER_FAIL=1 $<TARGET_FILE:openmodel-cli> solve-aster "${_beam_dir}/beam.inp"
                "${_support}/beam_analysis.txt" "${_beam_dir}/aster_fail" --launcher "${_fake_aster}")
    add_test(NAME aster_cli_notables
        COMMAND ${CMAKE_COMMAND} -E env FAKE_ASTER_NOTAB=1 $<TARGET_FILE:openmodel-cli> solve-aster "${_beam_dir}/beam.inp"
                "${_support}/beam_analysis.txt" "${_beam_dir}/aster_notab" --launcher "${_fake_aster}")
    add_test(NAME aster_cli_missing
        COMMAND openmodel-cli solve-aster "${_beam_dir}/beam.inp" "${_support}/beam_analysis.txt" "${_beam_dir}/aster_missing" --launcher /nonexistent/run_aster)
    set_tests_properties(aster_cli_export aster_cli_export_mail aster_cli_import_mail aster_cli_static aster_cli_modal aster_cli_failure
        aster_cli_notables aster_cli_missing PROPERTIES FIXTURES_REQUIRED beam_fixture LABELS "core;solver;aster")
    set_tests_properties(aster_cli_export PROPERTIES PASS_REGULAR_EXPRESSION "static analysis of .* force on 9 nodes")
    set_tests_properties(aster_cli_import_mail PROPERTIES DEPENDS aster_cli_export_mail)
    set_tests_properties(aster_cli_static PROPERTIES PASS_REGULAR_EXPRESSION "Results: .*job_disp.tab .*job_stress.tab .*job_strain.tab")
    set_tests_properties(aster_cli_modal PROPERTIES PASS_REGULAR_EXPRESSION "Results: .*job_modes.tab .*job_modes_disp.tab")
    set_tests_properties(aster_cli_failure PROPERTIES PASS_REGULAR_EXPRESSION "singular matrix")
    set_tests_properties(aster_cli_notables PROPERTIES PASS_REGULAR_EXPRESSION "wrote no result table")
    set_tests_properties(aster_cli_missing PROPERTIES PASS_REGULAR_EXPRESSION "Cannot start")
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
    if(TARGET assimp::assimp)
        set(_openmodel_assimp_dependency assimp::assimp)
    elseif(TARGET assimp)
        set(_openmodel_assimp_dependency assimp)
    else()
        message(FATAL_ERROR "The Assimp package did not export an assimp target.")
    endif()

    include(CheckCXXSourceCompiles)
    set(_openmodel_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
    set(CMAKE_REQUIRED_LIBRARIES "${_openmodel_assimp_dependency}")
    # The check only needs the headers, so do not link: building a static library avoids failures that have
    # nothing to do with the Assimp version (import library with a single configuration, missing runtime
    # dependencies of a static Assimp, Debug/Release mismatch with multi-config generators).
    set(_openmodel_saved_try_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
    set(_openmodel_saved_try_config "${CMAKE_TRY_COMPILE_CONFIGURATION}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    # Assimp 6 headers need C++17, while a try-compile uses the compiler default (C++14 with MSVC): the
    # project's own targets request cxx_std_17, so the check must too. CMAKE_CXX_STANDARD is honoured by
    # try_compile (policy CMP0067, NEW from cmake_minimum_required 3.8).
    set(_openmodel_saved_cxx_standard "${CMAKE_CXX_STANDARD}")
    set(CMAKE_CXX_STANDARD 17)
    get_target_property(_openmodel_assimp_configs ${_openmodel_assimp_dependency} IMPORTED_CONFIGURATIONS)
    if(_openmodel_assimp_configs AND NOT "DEBUG" IN_LIST _openmodel_assimp_configs)
        set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
    endif()
    unset(OPENMODEL_ASSIMP_HAS_PBR_TEXTURE_API CACHE)
    check_cxx_source_compiles("
        #include <assimp/material.h>
        int main() {
            aiTextureType baseColor = aiTextureType_BASE_COLOR;
            aiTextureType emissive = aiTextureType_EMISSION_COLOR;
            aiTextureType metalness = aiTextureType_METALNESS;
            aiTextureType roughness = aiTextureType_DIFFUSE_ROUGHNESS;
            (void)baseColor; (void)emissive; (void)metalness; (void)roughness;
            return 0;
        }"
        OPENMODEL_ASSIMP_HAS_PBR_TEXTURE_API)
    set(CMAKE_REQUIRED_LIBRARIES "${_openmodel_saved_required_libraries}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_openmodel_saved_try_type}")
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${_openmodel_saved_try_config}")
    if(_openmodel_saved_cxx_standard)
        set(CMAKE_CXX_STANDARD "${_openmodel_saved_cxx_standard}")
    else()
        unset(CMAKE_CXX_STANDARD)
    endif()
    unset(_openmodel_saved_cxx_standard)
    unset(_openmodel_saved_required_libraries)
    unset(_openmodel_saved_try_type)
    unset(_openmodel_saved_try_config)
    unset(_openmodel_assimp_configs)
    if(NOT OPENMODEL_ASSIMP_HAS_PBR_TEXTURE_API)
        message(FATAL_ERROR
            "The Assimp PBR texture check failed. Assimp 5.0 or newer is required (aiTextureType_BASE_COLOR, "
            "aiTextureType_EMISSION_COLOR, aiTextureType_METALNESS and aiTextureType_DIFFUSE_ROUGHNESS), but the "
            "test program may also have failed for another reason, for example the include directory of the "
            "assimp target. The compiler output is in CMakeFiles/CMakeConfigureLog.yaml (CMakeError.log on "
            "CMake < 3.26) of the build directory.")
    endif()

    add_library(openmodel_assimp STATIC
        src/AssimpModel.cpp
        src/AssimpSurfaceMesher.cpp
        src/AssimpSurfacePatchAnalyzer.cpp
        src/AssimpPatchGroupBuilder.cpp
        src/AssimpSurfaceGeometryFitter.cpp)
    add_library(OpenModel::Assimp ALIAS openmodel_assimp)
    set_target_properties(openmodel_assimp PROPERTIES EXPORT_NAME Assimp)
    target_link_libraries(openmodel_assimp PUBLIC OpenModel::Core)
    target_link_libraries(openmodel_assimp PUBLIC ${_openmodel_assimp_dependency})
    unset(_openmodel_assimp_dependency)
    if(OPENMODEL_BUILD_TESTS)
        add_executable(assimp_model_test tests/assimp_model_test.cpp)
        target_link_libraries(assimp_model_test PRIVATE openmodel_assimp)
        add_test(NAME assimp_model_test COMMAND assimp_model_test)
        add_executable(assimp_surface_mesher_test tests/assimp_surface_mesher_test.cpp)
        target_link_libraries(assimp_surface_mesher_test PRIVATE openmodel_assimp)
        add_test(NAME assimp_surface_mesher_test COMMAND assimp_surface_mesher_test)
        set_tests_properties(assimp_model_test assimp_surface_mesher_test
            PROPERTIES LABELS "integration;assimp")
    endif()
endif()

if(OPENMODEL_BUILD_GMSH_BACKEND)
    # Explicit SDK paths take precedence over package discovery. This is useful
    # with the official precompiled Windows SDK, which may not ship a CMake config.
    set(_openmodel_gmsh_target "")
    if(NOT GMSH_ROOT AND NOT GMSH_ROOT_DEBUG AND NOT GMSH_ROOT_RELEASE AND NOT GMSH_INCLUDE_DIR
       AND NOT GMSH_LIBRARY_DEBUG AND NOT GMSH_LIBRARY_RELEASE AND NOT GMSH_LIBRARY)
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
        # Gmsh does not install a CMake package, so it is located from an install prefix:
        #   GMSH_ROOT          prefix used for every configuration (include/, lib/, bin/)
        #   GMSH_ROOT_DEBUG    optional prefix of a separate Debug build/install
        #   GMSH_ROOT_RELEASE  optional prefix of a separate Release build/install
        # GMSH_INCLUDE_DIR, GMSH_LIBRARY_DEBUG/RELEASE and GMSH_RUNTIME_LIBRARY_DEBUG/RELEASE override the
        # search. Nothing found by the search is written back to the cache, so changing a root and
        # re-configuring always searches again.
        set(GMSH_ROOT "" CACHE PATH "Gmsh install prefix (contains include/gmsh.h, lib/, bin/)")
        set(GMSH_ROOT_DEBUG "" CACHE PATH "Gmsh install prefix used for Debug (defaults to GMSH_ROOT)")
        set(GMSH_ROOT_RELEASE "" CACHE PATH "Gmsh install prefix used for Release (defaults to GMSH_ROOT)")
        option(OPENMODEL_GMSH_LINK_CHECK "Try to link a tiny program against the located Gmsh at configure time" ON)

        if(GMSH_LIBRARY AND NOT GMSH_LIBRARY_RELEASE)
            set(GMSH_LIBRARY_RELEASE "${GMSH_LIBRARY}" CACHE FILEPATH "Gmsh release import/static library" FORCE)
        endif()

        # Import libraries are tried before static ones: Gmsh's own MSVC build produces gmsh.dll.lib
        # (import library of the DLL) next to gmsh.lib (static library).
        set(_gmsh_names_RELEASE gmsh.dll gmsh libgmsh)
        set(_gmsh_names_DEBUG gmshd.dll gmshd libgmshd gmsh_d gmsh.dll gmsh libgmsh)
        set(_gmsh_libdirs_RELEASE lib/Release Release lib/RelWithDebInfo lib lib64 bin)
        set(_gmsh_libdirs_DEBUG lib/Debug Debug lib lib64 bin)
        if(CMAKE_LIBRARY_ARCHITECTURE) # Debian-style multiarch prefixes, e.g. /usr/lib/x86_64-linux-gnu
            list(APPEND _gmsh_libdirs_RELEASE lib/${CMAKE_LIBRARY_ARCHITECTURE})
            list(APPEND _gmsh_libdirs_DEBUG lib/${CMAKE_LIBRARY_ARCHITECTURE})
        endif()
        set(_gmsh_bindirs_RELEASE bin/Release Release bin lib/Release lib)
        set(_gmsh_bindirs_DEBUG bin/Debug Debug bin lib/Debug lib)

        foreach(_cfg DEBUG RELEASE)
            set(_gmsh_root_${_cfg} "${GMSH_ROOT_${_cfg}}")
            if(NOT _gmsh_root_${_cfg})
                set(_gmsh_root_${_cfg} "${GMSH_ROOT}")
            endif()
            if(NOT _gmsh_root_${_cfg} AND DEFINED ENV{GMSH_ROOT})
                set(_gmsh_root_${_cfg} "$ENV{GMSH_ROOT}")
            endif()
            set(_gmsh_search_${_cfg} "")
            if(_gmsh_root_${_cfg})
                set(_gmsh_search_${_cfg} NO_DEFAULT_PATH) # never mix in a Gmsh found elsewhere on the system
            endif()

            set(_gmsh_lib_${_cfg} "${GMSH_LIBRARY_${_cfg}}")
            if(NOT _gmsh_lib_${_cfg})
                unset(_OPENMODEL_GMSH_LIBRARY_${_cfg} CACHE)
                find_library(_OPENMODEL_GMSH_LIBRARY_${_cfg} NAMES ${_gmsh_names_${_cfg}}
                    HINTS "${_gmsh_root_${_cfg}}" PATH_SUFFIXES ${_gmsh_libdirs_${_cfg}} ${_gmsh_search_${_cfg}})
                set(_gmsh_lib_${_cfg} "${_OPENMODEL_GMSH_LIBRARY_${_cfg}}")
            endif()

            set(_gmsh_dll_${_cfg} "${GMSH_RUNTIME_LIBRARY_${_cfg}}")
            if(WIN32 AND NOT _gmsh_dll_${_cfg} AND _gmsh_root_${_cfg})
                # The DLL carries the version in its name (for example gmsh-4.15.dll).
                foreach(_dir ${_gmsh_bindirs_${_cfg}})
                    file(GLOB _gmsh_dll_candidates "${_gmsh_root_${_cfg}}/${_dir}/gmsh*.dll")
                    if(_gmsh_dll_candidates)
                        list(GET _gmsh_dll_candidates 0 _gmsh_dll_${_cfg})
                        break()
                    endif()
                endforeach()
            endif()
        endforeach()

        if(NOT GMSH_INCLUDE_DIR)
            set(_gmsh_inc_roots "${_gmsh_root_RELEASE}" "${_gmsh_root_DEBUG}")
            list(REMOVE_DUPLICATES _gmsh_inc_roots)
            unset(_OPENMODEL_GMSH_INCLUDE_DIR CACHE)
            find_path(_OPENMODEL_GMSH_INCLUDE_DIR gmsh.h
                HINTS ${_gmsh_inc_roots} PATH_SUFFIXES include include/gmsh ${_gmsh_search_RELEASE})
            set(_gmsh_include "${_OPENMODEL_GMSH_INCLUDE_DIR}")
        else()
            set(_gmsh_include "${GMSH_INCLUDE_DIR}")
        endif()

        foreach(_v _gmsh_include _gmsh_lib_RELEASE _gmsh_lib_DEBUG)
            if(NOT ${_v})
                set(${_v} "")  # drop the "...-NOTFOUND" marker so messages stay readable
            endif()
        endforeach()
        if(NOT _gmsh_include OR (NOT _gmsh_lib_RELEASE AND NOT _gmsh_lib_DEBUG))
            message(FATAL_ERROR
                "OPENMODEL_BUILD_GMSH_BACKEND=ON, but Gmsh was not fully located "
                "(include: '${_gmsh_include}', Release lib: '${_gmsh_lib_RELEASE}', Debug lib: '${_gmsh_lib_DEBUG}'). "
                "Set GMSH_ROOT to the Gmsh install prefix (containing include/gmsh.h and lib/), or "
                "GMSH_ROOT_DEBUG / GMSH_ROOT_RELEASE for separate builds, or give GMSH_INCLUDE_DIR and "
                "GMSH_LIBRARY_DEBUG/GMSH_LIBRARY_RELEASE explicitly.")
        endif()

        # If only one variant is available, use it for every configuration.
        if(NOT _gmsh_lib_DEBUG)
            set(_gmsh_lib_DEBUG "${_gmsh_lib_RELEASE}")
            set(_gmsh_dll_DEBUG "${_gmsh_dll_RELEASE}")
        endif()
        if(NOT _gmsh_lib_RELEASE)
            set(_gmsh_lib_RELEASE "${_gmsh_lib_DEBUG}")
            set(_gmsh_dll_RELEASE "${_gmsh_dll_DEBUG}")
        endif()
        if(NOT _gmsh_dll_DEBUG)
            set(_gmsh_dll_DEBUG "${_gmsh_dll_RELEASE}")
        endif()
        if(NOT _gmsh_dll_RELEASE)
            set(_gmsh_dll_RELEASE "${_gmsh_dll_DEBUG}")
        endif()
        message(STATUS "Gmsh include: ${_gmsh_include}")
        message(STATUS "Gmsh Release library: ${_gmsh_lib_RELEASE}")
        message(STATUS "Gmsh Debug library:   ${_gmsh_lib_DEBUG}")
        if(WIN32)
            message(STATUS "Gmsh Release DLL: ${_gmsh_dll_RELEASE}")
            message(STATUS "Gmsh Debug DLL:   ${_gmsh_dll_DEBUG}")
        endif()

        # A static Gmsh (gmsh.lib containing GamePad.obj, ...) needs the Windows system libraries it uses:
        # joyGetPosEx/joyGetDevCapsA are in winmm, sockets in ws2_32/wsock32, process queries in psapi.
        # They are part of the Windows SDK, so adding them is harmless for an import library (DLL build).
        # OPENMODEL_GMSH_EXTRA_LIBRARIES appends further libraries (other dependencies of a static Gmsh).
        set(_openmodel_gmsh_extra_libs ${OPENMODEL_GMSH_EXTRA_LIBRARIES})
        if(WIN32)
            list(APPEND _openmodel_gmsh_extra_libs winmm ws2_32 wsock32 psapi ole32)
        endif()

        # Catch a Gmsh build that cannot be used with this compiler before the long build starts, for
        # example a MinGW build of the C++ API with MSVC (unresolved gmsh:: symbols at link time). A static
        # Gmsh needs its own dependencies too, so a failure here is a warning, not an error.
        if(OPENMODEL_GMSH_LINK_CHECK AND NOT CMAKE_CROSSCOMPILING)
            file(WRITE "${CMAKE_BINARY_DIR}/gmsh_link_check.cpp"
                "#include <gmsh.h>\nint main() { gmsh::initialize(); gmsh::model::add(\"check\"); gmsh::finalize(); return 0; }\n")
            foreach(_cfg RELEASE DEBUG)
                if(_gmsh_lib_${_cfg})
                    if(_cfg STREQUAL "DEBUG")
                        set(CMAKE_TRY_COMPILE_CONFIGURATION Debug)
                    else()
                        set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
                    endif()
                    try_compile(_gmsh_link_ok
                        "${CMAKE_BINARY_DIR}/gmsh_link_check_${_cfg}"
                        "${CMAKE_BINARY_DIR}/gmsh_link_check.cpp"
                        CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${_gmsh_include}"
                        LINK_LIBRARIES "${_gmsh_lib_${_cfg}}" ${_openmodel_gmsh_extra_libs}
                        CXX_STANDARD 17
                        OUTPUT_VARIABLE _gmsh_link_log)
                    if(NOT _gmsh_link_ok)
                        message(WARNING
                            "A test program using the Gmsh C++ API did not link against ${_gmsh_lib_${_cfg}}. "
                            "With MSVC this typically means the library was built by another compiler (for "
                            "example the official MinGW SDK, whose C++ symbols are not MSVC-decorated), or it is "
                            "a static library whose dependencies are not on the link line. Configure with "
                            "-DOPENMODEL_GMSH_LINK_CHECK=OFF to skip this check. Details: "
                            "${CMAKE_BINARY_DIR}/gmsh_link_check_${_cfg}")
                        file(WRITE "${CMAKE_BINARY_DIR}/gmsh_link_check_${_cfg}.log" "${_gmsh_link_log}")
                    endif()
                endif()
            endforeach()
        endif()

        add_library(openmodel_gmsh_external UNKNOWN IMPORTED GLOBAL)
        set_target_properties(openmodel_gmsh_external PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_gmsh_include}"
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION_DEBUG "${_gmsh_lib_DEBUG}"
            IMPORTED_LOCATION_RELEASE "${_gmsh_lib_RELEASE}"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
            MAP_IMPORTED_CONFIG_MINSIZEREL Release)
        if(_openmodel_gmsh_extra_libs)
            set_property(TARGET openmodel_gmsh_external PROPERTY INTERFACE_LINK_LIBRARIES "${_openmodel_gmsh_extra_libs}")
        endif()
        # The target is configuration-aware: Debug uses the debug library, Release/RelWithDebInfo/MinSizeRel
        # use the release one (a single variant is used for every configuration).
        set(_openmodel_gmsh_target openmodel_gmsh_external)
        set(GMSH_INCLUDE_DIR "${_gmsh_include}")
        set(GMSH_RUNTIME_LIBRARY_DEBUG "${_gmsh_dll_DEBUG}")
        set(GMSH_RUNTIME_LIBRARY_RELEASE "${_gmsh_dll_RELEASE}")
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
    # Open CASCADE ships a CMake package (OpenCASCADEConfig.cmake). Any of these selects the installation:
    #   -DOpenCASCADE_DIR=<folder containing OpenCASCADEConfig.cmake>   (standard CMake variable)
    #   -DOPENCASCADE_ROOT=<install prefix>   (or the OPENCASCADE_ROOT / CASROOT environment variable)
    # The prefix is searched with the layouts of the official Windows package (<prefix>/cmake), of vcpkg and of
    # Linux distributions (<prefix>/lib/cmake/opencascade). It is also what Gmsh calls CASROOT: use the same
    # installation for both so that a single Open CASCADE runtime is loaded.
    set(OPENCASCADE_ROOT "" CACHE PATH "Open CASCADE install prefix (contains cmake/ or lib/cmake/opencascade)")
    set(_openmodel_occ_root "${OPENCASCADE_ROOT}")
    if(NOT _openmodel_occ_root AND DEFINED ENV{OPENCASCADE_ROOT})
        set(_openmodel_occ_root "$ENV{OPENCASCADE_ROOT}")
    endif()
    if(NOT _openmodel_occ_root AND DEFINED ENV{CASROOT})
        set(_openmodel_occ_root "$ENV{CASROOT}")
    endif()
    if(_openmodel_occ_root AND NOT OpenCASCADE_DIR)
        find_package(OpenCASCADE CONFIG QUIET
            HINTS "${_openmodel_occ_root}" "${_openmodel_occ_root}/cmake"
                  "${_openmodel_occ_root}/lib/cmake/opencascade" "${_openmodel_occ_root}/lib64/cmake/opencascade"
                  "${_openmodel_occ_root}/share/opencascade" "${_openmodel_occ_root}/share/cmake/opencascade"
            NO_DEFAULT_PATH)
        if(NOT OpenCASCADE_FOUND)
            message(FATAL_ERROR
                "OPENMODEL_BUILD_OCC_BACKEND=ON, but no OpenCASCADEConfig.cmake was found under "
                "OPENCASCADE_ROOT='${_openmodel_occ_root}'. Point OpenCASCADE_DIR to the folder that contains it "
                "(for the official Windows package: <prefix>/cmake).")
        endif()
    else()
        find_package(OpenCASCADE REQUIRED)
    endif()
    message(STATUS "Open CASCADE ${OpenCASCADE_VERSION}: ${OpenCASCADE_DIR}")
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
    # TKShHealing provides ShapeAnalysis_Surface (cached surface projectors of OCCModel::facesContainingPoint)
    # and TKPrim the BRepPrim* builders used by the tests. Windows import libraries do not pull in the
    # toolkits they depend on, so every toolkit that is used directly must be listed.
    set(_openmodel_occ_toolkits TKernel TKMath TKG2d TKBRep TKGeomBase TKG3d TKTopAlgo
        TKGeomAlgo TKMesh TKShHealing TKPrim TKXSBase TKXCAF TKLCAF TKCDF TKBO)
    # The package may or may not define imported targets, so probe for the legacy toolkit both ways.
    set(_openmodel_occ_legacy_exchange FALSE)
    if(TARGET OpenCASCADE::TKXDESTEP OR TARGET TKXDESTEP)
        set(_openmodel_occ_legacy_exchange TRUE)
    else()
        unset(_openmodel_occ_probe_xdestep CACHE)
        find_library(_openmodel_occ_probe_xdestep NAMES TKXDESTEP
            HINTS "${OpenCASCADE_LIBRARY_DIR}" "${OpenCASCADE_DIR}/../.." PATH_SUFFIXES lib lib64)
        if(_openmodel_occ_probe_xdestep)
            set(_openmodel_occ_legacy_exchange TRUE)
        endif()
    endif()
    if(_openmodel_occ_legacy_exchange)
        # Open CASCADE up to 7.8: STEP and IGES are split over several toolkits. Release 7.8 also ships the
        # new TKDE* toolkits, but the STEPCAF/IGESCAF readers stay in these ones.
        list(APPEND _openmodel_occ_toolkits TKSTEP TKIGES TKSTEPBase TKSTEPAttr TKSTEP209 TKXDESTEP TKXDEIGES)
    else()
        # Releases with the merged data-exchange toolkits: the STEP and IGES readers, XDE/CAF ones included,
        # live in TKDESTEP and TKDEIGES (TKSTEP, TKXDESTEP, ... no longer exist).
        list(APPEND _openmodel_occ_toolkits TKDE TKDESTEP TKDEIGES)
    endif()
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

if(OPENMODEL_BUILD_TESTS AND OPENMODEL_BUILD_GMSH_BACKEND AND OPENMODEL_BUILD_OCC_BACKEND
   AND OPENMODEL_GMSH_TEST_STEP)
    if(NOT EXISTS "${OPENMODEL_GMSH_TEST_STEP}")
        message(FATAL_ERROR "OPENMODEL_GMSH_TEST_STEP does not exist: ${OPENMODEL_GMSH_TEST_STEP}")
    endif()
    add_executable(gmsh_step_face_mapping_test tests/gmsh_step_face_mapping_test.cpp)
    target_link_libraries(gmsh_step_face_mapping_test PRIVATE openmodel_gmsh openmodel_occ)
    add_test(NAME gmsh_step_face_mapping_test
        COMMAND gmsh_step_face_mapping_test "${OPENMODEL_GMSH_TEST_STEP}")
    set_tests_properties(gmsh_step_face_mapping_test PROPERTIES
        LABELS "integration;gmsh;occ;cad-face-mapping" TIMEOUT 300)
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
if(OPENMODEL_BUILD_ASSIMP_BACKEND)
    list(APPEND OPENMODEL_INSTALL_TARGETS openmodel_assimp)
endif()
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
