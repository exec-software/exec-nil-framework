cmake_minimum_required(VERSION 3.20)

include(cmake/NilVersion.cmake)
project(NILFramework
    VERSION "${NIL_VERSION_NUMERIC}"
    DESCRIPTION "Shared compiler, runtime, and tools for Ny and Matrix"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CTest)

option(NIL_ENABLE_LLVM "Enable the optional LLVM native backend" ON)
option(NIL_BUILD_EXPERIMENTAL_TOOLS
       "Build experimental schema-extractor and agent tools"
       ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_subdirectory(nilcompiler)
add_subdirectory(nilinterpreter)
add_subdirectory(nillsp)
add_subdirectory(nildebugger)
add_subdirectory(nilassembler)
add_subdirectory(nillinker)

if(NIL_BUILD_EXPERIMENTAL_TOOLS)
    add_subdirectory(nilschema)
    add_subdirectory(nilagent)
endif()

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src/"
    DESTINATION "nilstd"
    FILES_MATCHING PATTERN "*.[nN][yY]" PATTERN "*.[mM][aA][tT][rR][iI][xX]" PATTERN "*.[mM][aA][tT]"
)

install(FILES LICENSE DESTINATION ".")

if(BUILD_TESTING)
    set(NY_SMOKE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/tests/hello.nil")
    set(NY_BINARY_SMOKE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/tests/hello-binary.nil")
    set(NY_FUNCTIONS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/tests/function-keywords.nil")
    set(NY_BINARY_FUNCTIONS_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/tests/function-keywords-binary.nil")
    set(NY_ARGUMENT_COUNT_INPUT
        "${CMAKE_CURRENT_BINARY_DIR}/tests/argument-count.nil")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/interpreter/argument-count.nil.in"
        "${NY_ARGUMENT_COUNT_INPUT}"
        COPYONLY
    )

    add_executable(ny_nil_loader_tests tests/nil_loader_tests.cpp)
    target_link_libraries(ny_nil_loader_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_nil_loader_tests PRIVATE cxx_std_17)

    add_test(NAME nil.loader COMMAND $<TARGET_FILE:ny_nil_loader_tests>)

    add_executable(ny_array_identity_vm_tests tests/vm_array_identity_tests.cpp)
    target_link_libraries(ny_array_identity_vm_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_array_identity_vm_tests PRIVATE cxx_std_17)
    add_test(NAME nil.array-identity COMMAND $<TARGET_FILE:ny_array_identity_vm_tests>)
    set_tests_properties(nil.array-identity PROPERTIES LABELS "nil;interpreter;rc" TIMEOUT 30)

    add_executable(ny_execution_metrics tests/tools/vm_execution_metrics.cpp)
    target_link_libraries(ny_execution_metrics PRIVATE nilcompiler_core)
    target_compile_features(ny_execution_metrics PRIVATE cxx_std_17)

    add_executable(ny_fault_vm_tests tests/fault_vm_tests.cpp)
    target_link_libraries(ny_fault_vm_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_fault_vm_tests PRIVATE cxx_std_17)
    add_test(NAME nil.fault-contract COMMAND $<TARGET_FILE:ny_fault_vm_tests>)

    add_executable(ny_class_vm_tests tests/class_vm_tests.cpp)
    target_link_libraries(ny_class_vm_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_class_vm_tests PRIVATE cxx_std_17)
    add_test(NAME nil.class-contract COMMAND $<TARGET_FILE:ny_class_vm_tests>)

    add_executable(ny_context_manifest_tests tests/context_manifest_tests.cpp)
    target_link_libraries(ny_context_manifest_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_context_manifest_tests PRIVATE cxx_std_17)
    add_test(NAME context.manifest COMMAND $<TARGET_FILE:ny_context_manifest_tests>)
    set_tests_properties(context.manifest PROPERTIES LABELS "tools;rc" TIMEOUT 30)

    add_executable(ny_parallel_runtime_tests tests/parallel_runtime_tests.cpp)
    target_link_libraries(ny_parallel_runtime_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_parallel_runtime_tests PRIVATE cxx_std_17)
    add_test(NAME concurrency.runtime COMMAND $<TARGET_FILE:ny_parallel_runtime_tests>)
    set_tests_properties(concurrency.runtime PROPERTIES LABELS "concurrency;rc" TIMEOUT 30)

    add_executable(ny_ownership_typecheck_tests tests/ownership_typecheck_tests.cpp)
    target_link_libraries(ny_ownership_typecheck_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_ownership_typecheck_tests PRIVATE cxx_std_17)
    add_test(NAME ownership.typecheck COMMAND $<TARGET_FILE:ny_ownership_typecheck_tests>)
    set_tests_properties(ownership.typecheck PROPERTIES LABELS "typecheck;ownership;rc" TIMEOUT 60)

    add_executable(ny_owned_contract_tests tests/owned_contract_tests.cpp)
    target_link_libraries(ny_owned_contract_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_owned_contract_tests PRIVATE cxx_std_17)
    add_test(NAME nil.owned-contract COMMAND $<TARGET_FILE:ny_owned_contract_tests>)
    set_tests_properties(nil.owned-contract PROPERTIES LABELS "nil;ownership;rc" TIMEOUT 30)

    find_package(Threads REQUIRED)
    add_executable(ny_database_runtime_tests tests/database_runtime_tests.cpp)
    target_link_libraries(ny_database_runtime_tests PRIVATE nilruntime)
    target_include_directories(ny_database_runtime_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/nilcompiler/src")
    target_compile_features(ny_database_runtime_tests PRIVATE cxx_std_17)
    add_test(NAME nil.database-runtime COMMAND $<TARGET_FILE:ny_database_runtime_tests>)
    set_tests_properties(nil.database-runtime PROPERTIES LABELS "database;runtime" SKIP_RETURN_CODE 77 TIMEOUT 60)

    add_executable(ny_native_owned_tests tests/native_owned_tests.cpp)
    target_link_libraries(ny_native_owned_tests PRIVATE nilruntime Threads::Threads)
    target_include_directories(ny_native_owned_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/nilcompiler/src")
    target_compile_features(ny_native_owned_tests PRIVATE cxx_std_17)
    add_test(NAME native.owned-runtime COMMAND $<TARGET_FILE:ny_native_owned_tests>)
    set_tests_properties(native.owned-runtime PROPERTIES LABELS "native;ownership;rc" TIMEOUT 30)

    add_executable(ny_parser_tests tests/parser_tests.cpp)
    target_link_libraries(ny_parser_tests PRIVATE nilcompiler_core)
    target_compile_features(ny_parser_tests PRIVATE cxx_std_17)

    add_test(NAME parser.inline-assembly COMMAND $<TARGET_FILE:ny_parser_tests>)

    add_executable(ny_inline_asm_contract_tests tests/inline_asm_contract_tests.cpp)
    target_include_directories(ny_inline_asm_contract_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/nilcompiler/src")
    target_compile_features(ny_inline_asm_contract_tests PRIVATE cxx_std_17)
    add_test(NAME inline-assembly.contract COMMAND $<TARGET_FILE:ny_inline_asm_contract_tests>)

    add_executable(ny_native_runtime_tests tests/native_runtime_tests.cpp)
    target_link_libraries(ny_native_runtime_tests PRIVATE nilruntime)
    target_include_directories(
        ny_native_runtime_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/nilcompiler/src"
    )
    target_compile_features(ny_native_runtime_tests PRIVATE cxx_std_17)

    add_test(NAME native.runtime-abi COMMAND $<TARGET_FILE:ny_native_runtime_tests>)

    # Exercise the distributed shared library as well as the static AOT runtime.
    add_executable(nil_shared_runtime_tests tests/native_runtime_tests.cpp)
    target_link_libraries(nil_shared_runtime_tests PRIVATE nilruntime_shared)
    target_include_directories(nil_shared_runtime_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/nilcompiler/src")
    target_compile_features(nil_shared_runtime_tests PRIVATE cxx_std_17)
    add_test(NAME native.shared-runtime-abi COMMAND $<TARGET_FILE:nil_shared_runtime_tests>)

    add_executable(ny_native_managed_gc_tests tests/native_managed_gc_tests.cpp)
    target_link_libraries(ny_native_managed_gc_tests PRIVATE nilruntime)
    target_include_directories(ny_native_managed_gc_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/nilcompiler/src")
    target_compile_features(ny_native_managed_gc_tests PRIVATE cxx_std_17)
    add_test(NAME native.managed-gc COMMAND $<TARGET_FILE:ny_native_managed_gc_tests>)

    add_executable(ny_native_fault_tests tests/native_fault_tests.cpp)
    target_link_libraries(ny_native_fault_tests PRIVATE nilruntime)
    target_include_directories(ny_native_fault_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/nilcompiler/src")
    target_compile_features(ny_native_fault_tests PRIVATE cxx_std_17)
    add_test(NAME native.fault-runtime COMMAND $<TARGET_FILE:ny_native_fault_tests>)

    add_test(
        NAME nilinterpreter.arguments
        COMMAND $<TARGET_FILE:nilinterpreter>
                --no-ai
                "${NY_ARGUMENT_COUNT_INPUT}"
                alpha
                --
                beta
    )
    set_tests_properties(
        nilinterpreter.arguments
        PROPERTIES PASS_REGULAR_EXPRESSION "^2"
    )

    add_test(
        NAME nilcompiler.smoke
        COMMAND $<TARGET_FILE:nilcompiler>
                --no-stdlib
                --no-bundle
                --quiet
                --output "${NY_SMOKE_OUTPUT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke/hello.ny"
    )
    set_tests_properties(nilcompiler.smoke PROPERTIES FIXTURES_SETUP ny_smoke)

    add_test(
        NAME nilinterpreter.smoke
        COMMAND $<TARGET_FILE:nilinterpreter> "${NY_SMOKE_OUTPUT}"
    )
    set_tests_properties(
        nilinterpreter.smoke
        PROPERTIES
            FIXTURES_REQUIRED ny_smoke
            PASS_REGULAR_EXPRESSION "Hello from Ny"
    )

    add_test(
        NAME nilcompiler.binary-smoke
        COMMAND $<TARGET_FILE:nilcompiler>
                --no-stdlib
                --no-bundle
                --emit-binary-nil
                --quiet
                --output "${NY_BINARY_SMOKE_OUTPUT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke/hello.ny"
    )
    set_tests_properties(
        nilcompiler.binary-smoke
        PROPERTIES FIXTURES_SETUP ny_binary_smoke
    )

    add_test(
        NAME nil.binary-magic
        COMMAND "${CMAKE_COMMAND}"
                -DINPUT_FILE=${NY_BINARY_SMOKE_OUTPUT}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyNilMagic.cmake"
    )
    set_tests_properties(
        nil.binary-magic
        PROPERTIES FIXTURES_REQUIRED ny_binary_smoke
    )

    add_test(
        NAME nilinterpreter.binary-smoke
        COMMAND $<TARGET_FILE:nilinterpreter> "${NY_BINARY_SMOKE_OUTPUT}"
    )
    set_tests_properties(
        nilinterpreter.binary-smoke
        PROPERTIES
            FIXTURES_REQUIRED ny_binary_smoke
            PASS_REGULAR_EXPRESSION "Hello from Ny"
    )

    set(NY_LIBRARY_TEST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/tests/nillib")
    set(NY_LIBRARY_TEST_ARTIFACT
        "${NY_LIBRARY_TEST_ROOT}/sample/library.nil")
    set(NY_LIBRARY_CONSUMER_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/tests/binary-library-consumer.nil")
    file(MAKE_DIRECTORY "${NY_LIBRARY_TEST_ROOT}/sample")

    add_test(
        NAME nilcompiler.binary-library
        COMMAND $<TARGET_FILE:nilcompiler>
                --no-stdlib
                --no-bundle
                --emit-binary-nil
                --emit-library
                --quiet
                --output "${NY_LIBRARY_TEST_ARTIFACT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/binary-library-module.ny"
    )
    set_tests_properties(
        nilcompiler.binary-library
        PROPERTIES FIXTURES_SETUP ny_binary_library
    )

    add_test(
        NAME nilcompiler.binary-library-import
        COMMAND $<TARGET_FILE:nilcompiler>
                --no-stdlib
                --sdk-path "${NY_LIBRARY_TEST_ROOT}"
                --no-bundle
                --emit-binary-nil
                --quiet
                --output "${NY_LIBRARY_CONSUMER_OUTPUT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/binary-library-consumer.ny"
    )
    set_tests_properties(
        nilcompiler.binary-library-import
        PROPERTIES
            FIXTURES_REQUIRED ny_binary_library
            FIXTURES_SETUP ny_binary_library_consumer
    )

    add_test(
        NAME nilinterpreter.binary-library-consumer
        COMMAND $<TARGET_FILE:nilinterpreter>
                --no-ai
                "${NY_LIBRARY_CONSUMER_OUTPUT}"
    )
    set_tests_properties(
        nilinterpreter.binary-library-consumer
        PROPERTIES
            FIXTURES_REQUIRED ny_binary_library_consumer
            PASS_REGULAR_EXPRESSION "^42"
    )

    # Keep module-owned aliases scoped and make split-module declarations
    # available independent of source/worklist order. The argument order is
    # intentional: nilcompiler consumes its worklist from the back, so the
    # consumer is checked before the struct declaration in the second pass.
    set(NY_MODULE_SCOPE_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/tests/module-scope.nil")
    add_test(
        NAME nilcompiler.module-scope
        COMMAND $<TARGET_FILE:nilcompiler>
                --no-stdlib
                --no-bundle
                --quiet
                --output "${NY_MODULE_SCOPE_OUTPUT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/module-scope-model.ny"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/module-scope-consumer.ny"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/module-scope-foreign-enum.ny"
    )
    set_tests_properties(
        nilcompiler.module-scope
        PROPERTIES FIXTURES_SETUP ny_module_scope
    )

    add_test(
        NAME nilinterpreter.module-scope
        COMMAND $<TARGET_FILE:nilinterpreter>
                --no-ai
                "${NY_MODULE_SCOPE_OUTPUT}"
    )
    set_tests_properties(
        nilinterpreter.module-scope
        PROPERTIES
            FIXTURES_REQUIRED ny_module_scope
            PASS_REGULAR_EXPRESSION "^42"
    )

    add_test(
        NAME nilcompiler.function-boundaries
        COMMAND $<TARGET_FILE:nilcompiler>
                --no-stdlib
                --no-bundle
                --quiet
                --output "${NY_FUNCTIONS_OUTPUT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/function-keywords.ny"
    )
    set_tests_properties(
        nilcompiler.function-boundaries
        PROPERTIES FIXTURES_SETUP ny_function_boundaries
    )

    add_test(
        NAME nilinterpreter.function-boundaries
        COMMAND $<TARGET_FILE:nilinterpreter> "${NY_FUNCTIONS_OUTPUT}"
    )
    set_tests_properties(
        nilinterpreter.function-boundaries
        PROPERTIES
            FIXTURES_REQUIRED ny_function_boundaries
            PASS_REGULAR_EXPRESSION "Both syntax tests should work!"
    )

    add_test(
        NAME nilcompiler.binary-function-boundaries
        COMMAND $<TARGET_FILE:nilcompiler>
                --no-stdlib
                --no-bundle
                --emit-binary-nil
                --quiet
                --output "${NY_BINARY_FUNCTIONS_OUTPUT}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/function-keywords.ny"
    )
    set_tests_properties(
        nilcompiler.binary-function-boundaries
        PROPERTIES FIXTURES_SETUP ny_binary_function_boundaries
    )

    add_test(
        NAME nilinterpreter.binary-function-boundaries
        COMMAND $<TARGET_FILE:nilinterpreter> "${NY_BINARY_FUNCTIONS_OUTPUT}"
    )
    set_tests_properties(
        nilinterpreter.binary-function-boundaries
        PROPERTIES
            FIXTURES_REQUIRED ny_binary_function_boundaries
            PASS_REGULAR_EXPRESSION "Both syntax tests should work!"
    )

    # The route matrix and production-application contracts are the bulk of
    # the executable language baseline. Do not silently configure a reduced
    # test suite when testing was explicitly enabled.
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(NY_IDENTITY_TEST_ARGUMENTS)
        foreach(tool IN ITEMS nilcompiler nilinterpreter nilassembler nillinker nillsp nildebugger nilschema nilagent)
            if(TARGET ${tool})
                list(APPEND NY_IDENTITY_TEST_ARGUMENTS --tool "${tool}=$<TARGET_FILE:${tool}>")
            endif()
        endforeach()
        add_test(
            NAME ny.release-identity
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/release_identity_tests.py"
                    --root "${CMAKE_CURRENT_SOURCE_DIR}"
                    --build-dir "${CMAKE_BINARY_DIR}"
                    ${NY_IDENTITY_TEST_ARGUMENTS}
        )
        set_tests_properties(ny.release-identity PROPERTIES LABELS "tools;release;rc" TIMEOUT 60)
        add_test(
            NAME ny.inline-assembly
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/inline_assembly.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --assembler $<TARGET_FILE:nilassembler>
                    --linker $<TARGET_FILE:nillinker>
                    --runtime $<TARGET_FILE:nilruntime>
                    --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-inline-assembly.ny"
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.inline-assembly PROPERTIES LABELS "routes;native")
        add_test(
            NAME ny.variable-scope
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/variable_scope.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
        )
        add_test(
            NAME ny.repository-policy
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/repository_policy.py"
                    --root "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(
            NAME ny.sdk-qualification
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_qualification_tests.py"
        )
        set_tests_properties(ny.sdk-qualification PROPERTIES LABELS "sdk;rc" TIMEOUT 30)
        add_test(
            NAME ny.release-artifacts
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/release_artifact_tests.py"
        )
        set_tests_properties(ny.release-artifacts PROPERTIES LABELS "sdk;release;rc" TIMEOUT 60)
        add_test(
            NAME ny.sdk-consumer-routes
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_consumer_routes_tests.py"
        )
        set_tests_properties(ny.sdk-consumer-routes PROPERTIES LABELS "sdk;rc" TIMEOUT 30)
        add_test(
            NAME ny.tools.pipeline-artifacts
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_toolchain_pipeline_tests.py"
        )
        set_tests_properties(ny.tools.pipeline-artifacts PROPERTIES LABELS "tools;rc" TIMEOUT 30)
        add_test(
            NAME ny.routes.artifact-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/route_artifact_tests.py"
        )
        set_tests_properties(ny.routes.artifact-contract PROPERTIES LABELS "tools;rc" TIMEOUT 30)
        add_test(
            NAME ny.sdk-capabilities
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_capabilities_tests.py"
        )
        set_tests_properties(ny.sdk-capabilities PROPERTIES LABELS "sdk;rc" TIMEOUT 30)
        set(NY_MATRIX_SDK_TEST_ARGUMENTS)
        if(NIL_ENABLE_LLVM)
            list(APPEND NY_MATRIX_SDK_TEST_ARGUMENTS --native)
        endif()
        add_test(
            NAME ny.sdk-matrix-sources
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_matrix_sources_tests.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --runtime $<TARGET_FILE:nilruntime>
                    ${NY_MATRIX_SDK_TEST_ARGUMENTS}
        )
        set_tests_properties(ny.sdk-matrix-sources PROPERTIES LABELS "sdk;matrix;routes;rc" TIMEOUT 180)
        add_test(
            NAME ny.collection-copy-bounds
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/collection_copy_bounds.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.collection-copy-bounds PROPERTIES LABELS "stdlib;collections;routes;rc" TIMEOUT 180)
        add_test(
            NAME ny.numeric-sort-complexity
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/numeric_sort_complexity.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --metrics $<TARGET_FILE:ny_execution_metrics>
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
        )
        set_tests_properties(ny.numeric-sort-complexity PROPERTIES LABELS "stdlib;sorting;interpreter;rc" TIMEOUT 120)
        add_test(
            NAME ny.native-managed-lifetime
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_managed_lifetime.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.native-managed-lifetime PROPERTIES LABELS "native;rc" SKIP_RETURN_CODE 77 TIMEOUT 150)
        add_test(NAME ny.ownership-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/ownership_contract.py"
                --compiler $<TARGET_FILE:nilcompiler>)
        set_tests_properties(ny.ownership-contract PROPERTIES LABELS "typecheck;ownership;rc" TIMEOUT 120)
        add_test(NAME ny.native-owned-lifetime
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_owned_lifetime.py"
                --compiler $<TARGET_FILE:nilcompiler> --llvm "${NIL_ENABLE_LLVM}")
        set_tests_properties(ny.native-owned-lifetime PROPERTIES LABELS "native;ownership;rc" SKIP_RETURN_CODE 77 TIMEOUT 240)
        # Matrix lexer conformance: the sample corpus must tokenise clean, and
        # each negative fixture must be rejected for the stated reason.
        add_executable(matrix_lex_dump tests/matrix/lex_dump.cpp)
        target_link_libraries(matrix_lex_dump PRIVATE nilcompiler_core)
        add_test(NAME matrix.lexer
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/matrix/run_lexer_tests.py"
                    --tool $<TARGET_FILE:matrix_lex_dump>
                    --root "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(matrix.lexer PROPERTIES LABELS "matrix;lexer" TIMEOUT 60)
        # Matrix execution conformance: each page lowers to NIL and runs on the
        # interpreter with exact expected output. Acceptance is covered by the
        # lexer test; this checks that the lowering means what the surface says.
        add_test(NAME matrix.execution
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/matrix/run_execution_tests.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --root "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(matrix.execution PROPERTIES LABELS "matrix;routes" TIMEOUT 120)
        # The whole sample corpus, not just the pages promoted into
        # tests/matrix/execution. The samples are what the design documents
        # point at, so a page that stops compiling is a documentation bug as
        # much as a compiler one.
        add_test(NAME matrix.samples
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/matrix/run_sample_corpus.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --root "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(matrix.samples PROPERTIES LABELS "matrix;routes" TIMEOUT 180)
        add_test(NAME nillsp.semantic-protocol
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/lsp_semantic_protocol.py"
                    --lsp $<TARGET_FILE:nillsp>)
        set_tests_properties(nillsp.semantic-protocol PROPERTIES LABELS "lsp;rc" TIMEOUT 30)
        add_test(NAME nillsp.matrix-protocol
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/lsp_matrix_protocol.py"
                    --lsp $<TARGET_FILE:nillsp>)
        set_tests_properties(nillsp.matrix-protocol PROPERTIES LABELS "lsp;matrix" TIMEOUT 30)
        add_test(NAME ny.class-execution
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/class_execution.py"
                    --compiler $<TARGET_FILE:nilcompiler>)
        set_tests_properties(ny.class-execution PROPERTIES LABELS "typecheck;rc" TIMEOUT 60)
        add_test(NAME ny.class-polymorphism
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/class_polymorphism.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>)
        set_tests_properties(ny.class-polymorphism PROPERTIES LABELS "typecheck;rc" TIMEOUT 120)
        add_test(NAME ny.reference-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/reference_contract.py"
                --compiler $<TARGET_FILE:nilcompiler>
                --interpreter $<TARGET_FILE:nilinterpreter>)
        set_tests_properties(ny.reference-contract PROPERTIES LABELS "typecheck;rc" TIMEOUT 120)
        add_test(NAME ny.tools.context-manifest
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/context_manifest.py"
                --compiler $<TARGET_FILE:nilcompiler>
                --assembler $<TARGET_FILE:nilassembler>
                --linker $<TARGET_FILE:nillinker>
                --runtime $<TARGET_FILE:nilruntime>
                --llvm "${NIL_ENABLE_LLVM}")
        set_tests_properties(ny.tools.context-manifest PROPERTIES LABELS "tools;rc" TIMEOUT 180)

        add_test(NAME ny.concurrency-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/concurrency_contract.py"
                --compiler $<TARGET_FILE:nilcompiler>
                --interpreter $<TARGET_FILE:nilinterpreter>
                --assembler $<TARGET_FILE:nilassembler>
                --linker $<TARGET_FILE:nillinker>
                --runtime $<TARGET_FILE:nilruntime>
                --llvm "${NIL_ENABLE_LLVM}")
        set_tests_properties(ny.concurrency-contract PROPERTIES LABELS "concurrency;rc" TIMEOUT 180)

        add_test(NAME ny.fault-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/fault_contract.py"
                --compiler $<TARGET_FILE:nilcompiler>)
        set_tests_properties(ny.fault-contract PROPERTIES LABELS "typecheck;rc" TIMEOUT 90)
        add_test(NAME nilcompiler.input-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/compiler_inputs.py"
                --compiler $<TARGET_FILE:nilcompiler>)
        set_tests_properties(nilcompiler.input-contract PROPERTIES LABELS "tools;rc" TIMEOUT 60)
        add_test(
            NAME ny.scalar-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/scalar_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.scalar-contract PROPERTIES LABELS "routes;rc" TIMEOUT 240)
        add_test(
            NAME ny.println-contract
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/println_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.println-contract PROPERTIES LABELS "routes;rc" TIMEOUT 120)
        add_test(
            NAME ny.assignment-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/assignment_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
        )
        set_tests_properties(ny.assignment-contract PROPERTIES LABELS "typecheck;rc" TIMEOUT 60)
        add_test(
            NAME ny.trait-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/trait_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
        )
        set_tests_properties(ny.trait-contract PROPERTIES LABELS "typecheck;rc" TIMEOUT 120)
        add_test(
            NAME ny.cast-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/cast_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.cast-contract PROPERTIES LABELS "routes;rc" TIMEOUT 240)
        add_test(
            NAME ny.class-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/class_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
        )
        set_tests_properties(ny.class-contract PROPERTIES LABELS "typecheck;rc" TIMEOUT 120)
        add_test(
            NAME ny.time-contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/time_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
        )
        set_tests_properties(ny.time-contract PROPERTIES LABELS "routes;rc" TIMEOUT 120)
        set_tests_properties(
            ny.repository-policy
            PROPERTIES LABELS "policy"
        )

        add_test(
            NAME ny.sdk-publish-concurrency
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/publish_sdk_concurrency.py"
                    --publisher "${CMAKE_CURRENT_SOURCE_DIR}/scripts/publish-sdk.py"
        )
        set_tests_properties(
            ny.sdk-publish-concurrency
            PROPERTIES
                LABELS "sdk;concurrency"
                SKIP_RETURN_CODE 77
                TIMEOUT 30
        )

        add_test(
            NAME ny.sdk-native-archive-validation
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/sdk_native_archive_validation.py"
                    --package-script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-sdk.py"
                    --validator-script "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-sdk.py"
                    --archive $<TARGET_FILE:nilruntime>
        )
        set_tests_properties(
            ny.sdk-native-archive-validation
            PROPERTIES
                LABELS "sdk;native;security"
                TIMEOUT 30
        )

        add_test(
            NAME ny.native-output-private-staging
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_output_staging.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/route_smoke.ny"
        )
        set_tests_properties(
            ny.native-output-private-staging
            PROPERTIES
                LABELS "native;aot;security"
                SKIP_RETURN_CODE 77
                TIMEOUT 180
        )

        if(TARGET nilassembler AND TARGET nillinker)
            add_test(
                NAME ny.global-contract
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/global_contract.py"
                        --compiler $<TARGET_FILE:nilcompiler>
                        --interpreter $<TARGET_FILE:nilinterpreter>
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
                        --llvm "${NIL_ENABLE_LLVM}"
            )
            set_tests_properties(ny.global-contract PROPERTIES LABELS "routes;rc" TIMEOUT 180)
            add_test(
                NAME ny.native-array-contract
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_array_contract.py"
                        --interpreter $<TARGET_FILE:nilinterpreter>
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
            )
            set_tests_properties(ny.native-array-contract PROPERTIES
                LABELS "routes;native;rc" SKIP_RETURN_CODE 77 TIMEOUT 180)
            add_test(
                NAME ny.tools.native-nil-validation
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_nil_validation.py"
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
            )
            set_tests_properties(ny.tools.native-nil-validation PROPERTIES
                LABELS "tools;native" SKIP_RETURN_CODE 77 TIMEOUT 120)
            add_test(
                NAME ny.tools.native-string-escape-parity
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_toolchain_pipeline.py"
                        --compiler $<TARGET_FILE:nilcompiler>
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
                        --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-string-escape-parity.ny"
                        --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-string-escape-parity.stdout"
                        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/tools"
            )
            set_tests_properties(ny.tools.native-string-escape-parity PROPERTIES
                LABELS "tools;native" SKIP_RETURN_CODE 77 TIMEOUT 240)
            add_test(
                NAME ny.tools.lexical-storage
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_toolchain_pipeline.py"
                        --compiler $<TARGET_FILE:nilcompiler>
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
                        --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/lexical-storage.ny"
                        --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/lexical-storage.stdout"
                        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/tools"
            )
            set_tests_properties(ny.tools.lexical-storage PROPERTIES
                LABELS "tools;native" SKIP_RETURN_CODE 77 TIMEOUT 240)
            add_test(
                NAME ny.tools.call-return-contract
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_toolchain_pipeline.py"
                        --compiler $<TARGET_FILE:nilcompiler>
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
                        --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/call-return-contract.ny"
                        --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/call-return-contract.stdout"
                        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/tools"
            )
            set_tests_properties(ny.tools.call-return-contract PROPERTIES
                LABELS "tools;native" SKIP_RETURN_CODE 77 TIMEOUT 240)
            add_test(NAME ny.tools.owned-buffers
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_toolchain_pipeline.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --assembler $<TARGET_FILE:nilassembler>
                    --linker $<TARGET_FILE:nillinker>
                    --runtime $<TARGET_FILE:nilruntime>
                    --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/owned-buffers.ny"
                    --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/owned-buffers.stdout"
                    --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/tools")
            set_tests_properties(ny.tools.owned-buffers PROPERTIES
                LABELS "tools;native;ownership;rc" SKIP_RETURN_CODE 77 TIMEOUT 240)
            foreach(ny_rc_fixture IN ITEMS parallel-map native-expression-joins user-function-dispatch scalar-cast-contract scalar-arithmetic-contract assignment-contract trait-contract trait-associated match-literal-contract cast-identity-contract native-time-contract native-nested-reference reference-handles scalar-array-parity native-global-contract stl-array-primitives native-scalar-fields module-global-contract native-managed-lifetime class-execution class-polymorphism fault-control-flow fault-nominal-identity fault-managed-lifetime native-struct-abi native-generic-enum-construction native-math-intrinsic-module)
                add_test(
                    NAME ny.tools.${ny_rc_fixture}
                    COMMAND "${Python3_EXECUTABLE}"
                            "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_toolchain_pipeline.py"
                            --compiler $<TARGET_FILE:nilcompiler>
                            --assembler $<TARGET_FILE:nilassembler>
                            --linker $<TARGET_FILE:nillinker>
                            --runtime $<TARGET_FILE:nilruntime>
                            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_rc_fixture}.ny"
                            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_rc_fixture}.stdout"
                            --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
                            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/tools"
                )
                set_tests_properties(ny.tools.${ny_rc_fixture} PROPERTIES
                    LABELS "tools;native;rc" SKIP_RETURN_CODE 77 TIMEOUT 240)
            endforeach()
            add_test(
                NAME ny.tools.native-fault-prefix
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_fault_pipeline.py"
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
                        --interpreter $<TARGET_FILE:nilinterpreter>
            )
            set_tests_properties(ny.tools.native-fault-prefix PROPERTIES
                LABELS "tools;native;rc" SKIP_RETURN_CODE 77 TIMEOUT 120)
            add_test(
                NAME ny.tools.native-class-contract
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_class_contract.py"
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
                        --interpreter $<TARGET_FILE:nilinterpreter>
            )
            set_tests_properties(ny.tools.native-class-contract PROPERTIES
                LABELS "tools;native;rc" SKIP_RETURN_CODE 77 TIMEOUT 180)
            add_test(
                NAME ny.tools.native-object-pipeline
                COMMAND "${Python3_EXECUTABLE}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_toolchain_pipeline.py"
                        --compiler $<TARGET_FILE:nilcompiler>
                        --assembler $<TARGET_FILE:nilassembler>
                        --linker $<TARGET_FILE:nillinker>
                        --runtime $<TARGET_FILE:nilruntime>
                        --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/route_smoke.ny"
                        --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/route_smoke.stdout"
                        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/tools"
            )
            set_tests_properties(
                ny.tools.native-object-pipeline
                PROPERTIES
                    LABELS "tools;native;aot;security"
                    SKIP_RETURN_CODE 77
                    TIMEOUT 240
            )
        endif()

        add_test(
            NAME ny.interpreter.byte-io
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/interpreter/byte_io_roundtrip.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/byte-io.ny"
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
        )
        set_tests_properties(
            ny.interpreter.byte-io
            PROPERTIES LABELS "interpreter;io"
        )

        add_test(
            NAME ny.interpreter.secure-byte-io
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/interpreter/secure_byte_io.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/secure-byte-io.ny"
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
        )
        set_tests_properties(
            ny.interpreter.secure-byte-io
            PROPERTIES LABELS "interpreter;io;security"
        )

        add_test(
            NAME ny.interpreter.generic_enum_metadata
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/interpreter/generic_enum_metadata.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic_enum_metadata.ny"
                    --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic_enum_metadata.stdout"
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
        )
        set_tests_properties(
            ny.interpreter.generic_enum_metadata
            PROPERTIES LABELS "codegen;interpreter;nil"
        )

        set(NY_ROUTE_HARNESS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/ny_route_matrix.py")

        function(add_ny_route_test test_name)
            add_test(
                NAME "${test_name}"
                COMMAND "${Python3_EXECUTABLE}" "${NY_ROUTE_HARNESS}"
                        --compiler $<TARGET_FILE:nilcompiler>
                        --interpreter $<TARGET_FILE:nilinterpreter>
                        --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/routes"
                        --report-json "${CMAKE_CURRENT_BINARY_DIR}/tests/route-reports/${test_name}.json"
                        ${ARGN}
            )
            set_tests_properties(
                "${test_name}"
                PROPERTIES
                    LABELS "routes"
                    SKIP_RETURN_CODE 77
            )
        endfunction()

        # Matrix stage 6: faults, ownership, parallelism, time and assembly on
        # the execution routes. The samples are the normative corpus, so the
        # route tests run the samples themselves rather than copies.
        foreach(matrix_stage6_sample IN ITEMS 08-faults 09-memory 10-concurrency-and-time 13-context-and-interop)
            add_ny_route_test(
                matrix.routes.${matrix_stage6_sample}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/matrix/samples/${matrix_stage6_sample}.matrix"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/matrix/routes/${matrix_stage6_sample}.stdout"
                --expected-exit 0
                --routes all
            )
        endforeach()
        # Inline assembly is a native-only operation. The interpreted routes
        # must REJECT a program that reaches it with a diagnostic naming the
        # route, never emulate it; the native routes execute the same page.
        add_ny_route_test(
            matrix.routes.inline-assembly-interpreted
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/matrix/routes/inline-assembly-called.matrix"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/matrix/routes/inline-assembly-interpreted.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
        )
        add_ny_route_test(
            matrix.routes.inline-assembly-native
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/matrix/routes/inline-assembly-called.matrix"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/matrix/routes/inline-assembly-native.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )

        add_ny_route_test(
            ny.routes.native-string-escape-parity
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-string-escape-parity.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-string-escape-parity.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.lexical-global-shadow
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/lexical-global-shadow.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/lexical-global-shadow.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.lexical-storage
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/lexical-storage.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/lexical-storage.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.lexical-aggregate-storage
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/lexical-aggregate-storage.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/lexical-aggregate-storage.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.call-return-contract
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/call-return-contract.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/call-return-contract.stdout"
            --expected-exit 0
            --routes all
        )
        foreach(ny_rc_fixture IN ITEMS native-expression-joins user-function-dispatch scalar-cast-contract scalar-arithmetic-contract scalar-array-handle-collision assignment-contract trait-contract trait-associated match-literal-contract cast-identity-contract native-time-contract native-nested-reference scalar-array-parity native-global-contract native-scalar-fields module-global-contract class-execution fault-control-flow fault-nominal-identity)
            add_ny_route_test(
                ny.routes.${ny_rc_fixture}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_rc_fixture}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_rc_fixture}.stdout"
                --expected-exit 0
                --routes all
            )
        endforeach()
        add_ny_route_test(
            ny.routes.stl-array-primitives
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/stl-array-primitives.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/stl-array-primitives.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        foreach(ny_checked_fixture IN ITEMS checked-hex-runtime checked-binary-runtime checked-varint-runtime checked-crc-runtime checked-array-runtime stable-index-ordering numeric-value-ordering text-value-ordering text-whitespace-escapes stdlib-native-primitives database-sqlite-runtime reflection-values standard-namespace-aliases)
            add_ny_route_test(
                ny.routes.${ny_checked_fixture}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_checked_fixture}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_checked_fixture}.stdout"
                --expected-exit 0
                --routes all
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            )
        endforeach()
        add_ny_route_test(
            ny.routes.reflection-values-matrix
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reflection-values-matrix.matrix"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reflection-values-matrix.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_test(
            NAME nil.analysis
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/analysis_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
        )
        set_tests_properties(nil.analysis PROPERTIES LABELS "analysis;typecheck" TIMEOUT 90)
        add_test(NAME nil.reflection-consumer
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/reflection_contract.py"
                --compiler $<TARGET_FILE:nilcompiler> --interpreter $<TARGET_FILE:nilinterpreter>
                --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src" --llvm "${NIL_ENABLE_LLVM}")
        set_tests_properties(nil.reflection-consumer PROPERTIES LABELS "analysis;reflection;routes" TIMEOUT 120)
        add_test(NAME nil.database-postgresql
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/database_postgresql_isolated.py"
                --compiler $<TARGET_FILE:nilcompiler> --interpreter $<TARGET_FILE:nilinterpreter>
                --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
                --provider-test $<TARGET_FILE:ny_database_runtime_tests>
                --report-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/database-postgresql")
        set_tests_properties(nil.database-postgresql PROPERTIES LABELS "database;routes" SKIP_RETURN_CODE 77 TIMEOUT 240)
        if(TARGET nilassembler AND NIL_ENABLE_LLVM)
            add_test(NAME nil.database-contract
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/database_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler> --assembler $<TARGET_FILE:nilassembler>
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src")
            set_tests_properties(nil.database-contract PROPERTIES LABELS "database;native" SKIP_RETURN_CODE 77 TIMEOUT 120)
        endif()

        add_ny_route_test(
            ny.routes.checked-array-matrix-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/checked-array-matrix-runtime.matrix"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/checked-array-matrix-runtime.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.stable-index-ordering-matrix
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/stable-index-ordering-matrix.matrix"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/stable-index-ordering-matrix.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.numeric-value-ordering-matrix
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/numeric-value-ordering-matrix.matrix"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/numeric-value-ordering-matrix.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.text-value-ordering-matrix
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-value-ordering-matrix.matrix"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-value-ordering-matrix.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.text-whitespace-escapes-matrix
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-whitespace-escapes-matrix.matrix"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-whitespace-escapes-matrix.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        foreach(ny_primitive_fixture IN ITEMS stdlib-native-primitives-shadow stdlib-native-primitives-lifetime)
            add_ny_route_test(
                ny.routes.${ny_primitive_fixture}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_primitive_fixture}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_primitive_fixture}.stdout"
                --expected-exit 0
                --routes all
            )
        endforeach()
        # Native text currently rejects embedded NUL at source compilation.
        # This fixture tests strict decoding of that input on the four routes
        # that can represent it; it is not a native text compatibility claim.
        add_ny_route_test(
            ny.routes.checked-hex-nul-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/checked-hex-nul-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/checked-hex-nul-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        foreach(ny_checked_reject IN ITEMS checked-hex-reject checked-hex-odd-reject stdlib-native-primitives-panic)
            set(ny_checked_stderr "${ny_checked_reject}.stderr")
            if(ny_checked_reject STREQUAL "checked-hex-odd-reject")
                set(ny_checked_stderr "core-convert-odd-hex-reject.stderr")
            endif()
            add_ny_route_test(
                ny.routes.${ny_checked_reject}-interpreter
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_checked_reject}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_checked_stderr}"
                --expected-exit 1
                --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            )
            add_ny_route_test(
                ny.routes.${ny_checked_reject}-native
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_checked_reject}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_checked_reject}.native.stderr"
                --expected-exit 1
                --routes llvm-jit,llvm-aot
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            )
        endforeach()
        add_ny_route_test(
            ny.routes.native-managed-lifetime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-managed-lifetime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-managed-lifetime.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.fault-managed-lifetime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/fault-managed-lifetime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/fault-managed-lifetime.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.class-polymorphism
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/class-polymorphism.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/class-polymorphism.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.class-polymorphism-unmanaged
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/class-polymorphism.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/class-polymorphism.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--unmanaged
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.reference-handles
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reference-handles.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reference-handles.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.parallel-map
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/parallel-map.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/parallel-map.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.owned-buffers
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/owned-buffers.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/owned-buffers.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.owned-buffers-unmanaged
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/owned-buffers.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/owned-buffers.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--unmanaged
        )
        add_ny_route_test(
            ny.routes.fault-uncaught
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/fault-uncaught.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/fault-uncaught.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/fault-uncaught.stderr"
            --expected-exit 1
            --routes all
        )
        foreach(ny_fault_fixture IN ITEMS fault-control-flow fault-nominal-identity fault-managed-lifetime)
            add_ny_route_test(
                ny.routes.${ny_fault_fixture}-unmanaged
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_fault_fixture}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_fault_fixture}.stdout"
                --expected-exit 0
                --routes llvm-jit,llvm-aot
                --compiler-arg=--unmanaged
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            )
        endforeach()
        add_ny_route_test(
            ny.routes.fault-global-initializer
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/fault-global-initializer.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/fault-global-initializer.stderr"
            --expected-exit 1
            --routes all
        )
        add_ny_route_test(
            ny.routes.fault-fatal-trap
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/fault-fatal-trap.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/runtime_error.stderr"
            --expected-exit 1
            --routes all
        )
        add_ny_route_test(
            ny.routes.stl-array-negative-size-interpreter
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/stl-array-negative-size.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/stl-array-negative-size.runtime.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.stl-array-negative-size-native
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/stl-array-negative-size.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/stl-array-negative-size.native.stderr"
            --expected-exit 1
            --routes llvm-jit,llvm-aot
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        foreach(ny_native_fixture IN ITEMS native-nested-reference scalar-array-parity)
            add_ny_route_test(
                ny.routes.${ny_native_fixture}-unmanaged
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_native_fixture}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_native_fixture}.stdout"
                --expected-exit 0
                --routes llvm-jit,llvm-aot
                --compiler-arg=--unmanaged
            )
        endforeach()
        foreach(ny_cast_reject IN ITEMS positive-overflow negative-overflow nan infinity)
            add_ny_route_test(
                ny.routes.scalar-cast-${ny_cast_reject}-interpreter
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/scalar-cast-${ny_cast_reject}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/scalar-cast-range.runtime.stderr"
                --expected-exit 1
                --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
            )
            add_ny_route_test(
                ny.routes.scalar-cast-${ny_cast_reject}-native
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/scalar-cast-${ny_cast_reject}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/scalar-cast-range.native.stderr"
                --expected-exit 1
                --routes llvm-jit,llvm-aot
            )
        endforeach()
        foreach(ny_cast_width IN ITEMS f32 u64)
            add_ny_route_test(
                ny.routes.scalar-cast-unsupported-${ny_cast_width}
                --compiler-arg=--no-typecheck
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/scalar-cast-unsupported-${ny_cast_width}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/scalar-cast-unsupported-${ny_cast_width}.runtime.stderr"
                --expected-exit 1
                --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
            )
        endforeach()
        add_ny_route_test(
            ny.routes.smoke
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/route_smoke.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/route_smoke.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.exit-code
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/return_seven.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/return_seven.stdout"
            --expected-exit 7
            --routes all
        )
        add_ny_route_test(
            ny.routes.globals
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/globals.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/globals.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.floating-comparisons
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/floating-comparisons.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/floating-comparisons.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.floating-array-values
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/floating-array-values.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/floating-array-values.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.integer-array-values
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/integer-array-values.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/integer-array-values.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.numeric-literals
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/numeric-literals.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/numeric-literals.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.generic-impl-method
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic-impl-method.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic-impl-method.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.impl-generic-bound-member
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/impl-generic-bound-member.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/impl-generic-bound-member.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.generic-multiple-trait-impls
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic-multiple-trait-impls.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic-multiple-trait-impls.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.method-named-main
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/method-named-main.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/method-named-main.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.intrinsic-wrapper-parameters
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/intrinsic-wrapper-parameters.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/intrinsic-wrapper-parameters.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-math-intrinsic-module
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-math-intrinsic-module.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-math-intrinsic-module.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.logical-bitwise
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/logical-bitwise.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/logical-bitwise.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native_text_concat
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-string-concat.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-string-concat.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native_text_content_comparison
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-text-comparison.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-text-comparison.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.text-comparison-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-comparison-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-comparison-runtime.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.native-typed-text-abi
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-text-abi.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-text-abi.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-typed-text-return
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-text-return.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-text-return.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-handle-abi
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-handle-abi.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-handle-abi.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-builtin-pointer-metadata
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-builtin-pointer-metadata.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-builtin-pointer-metadata.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.native-pointer-ownership-families
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-ownership-families.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-ownership-families.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.native-pointer-tombstone
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-tombstone.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-tombstone.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.native-rejects-reserved-runtime-symbol
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-reserved-runtime-symbol.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --xfail "llvm-jit=reachable Ny definitions cannot replace native runtime ABI symbols"
            --xfail "llvm-aot=reachable Ny definitions cannot replace native runtime ABI symbols"
            --xfail-stderr-contains "llvm-jit=reachable function name 'nyrt_alloc' is reserved by the native runtime ABI"
            --xfail-stderr-contains "llvm-aot=reachable function name 'nyrt_alloc' is reserved by the native runtime ABI"
        )
        add_ny_route_test(
            ny.routes.native-runtime-symbol-isolation
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-runtime-symbol-isolation.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-runtime-symbol-isolation.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.native-rejects-mul-handle
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-numeric-mul-handle.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM numeric multiplication requires scalar operands"
            --xfail "llvm-aot=LLVM numeric multiplication requires scalar operands"
            --xfail-stderr-contains "llvm-jit=MUL operands must be numeric scalars"
            --xfail-stderr-contains "llvm-aot=MUL operands must be numeric scalars"
        )
        add_ny_route_test(
            ny.routes.native-rejects-neg-handle
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-numeric-neg-handle.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM numeric negation requires a scalar operand"
            --xfail "llvm-aot=LLVM numeric negation requires a scalar operand"
            --xfail-stderr-contains "llvm-jit=NEG operand must be a numeric scalar"
            --xfail-stderr-contains "llvm-aot=NEG operand must be a numeric scalar"
        )
        add_ny_route_test(
            ny.routes.native-rejects-enum-multiply
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-numeric-enum-mul.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM enum metadata is not numeric"
            --xfail "llvm-aot=LLVM enum metadata is not numeric"
            --xfail-stderr-contains "llvm-jit=MUL operands must be numeric scalars"
            --xfail-stderr-contains "llvm-aot=MUL operands must be numeric scalars"
        )
        add_ny_route_test(
            ny.routes.native-rejects-enum-comparison
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-enum-comparison.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM enum comparison must match interpreter rejection"
            --xfail "llvm-aot=LLVM enum comparison must match interpreter rejection"
            --xfail-stderr-contains "llvm-jit=enum-valued comparisons are not implemented for CMP_EQ"
            --xfail-stderr-contains "llvm-aot=enum-valued comparisons are not implemented for CMP_EQ"
        )
        add_ny_route_test(
            ny.routes.native-rejects-free-text
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-builtin-free-text.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM free requires owned raw-pointer metadata"
            --xfail "llvm-aot=LLVM free requires owned raw-pointer metadata"
            --xfail-stderr-contains "llvm-jit=builtin 'free' requires exact raw-pointer metadata ptr<i32>, ptr<i64>, ptr<bool>, or ptr<u8>"
            --xfail-stderr-contains "llvm-aot=builtin 'free' requires exact raw-pointer metadata ptr<i32>, ptr<i64>, ptr<bool>, or ptr<u8>"
        )
        add_ny_route_test(
            ny.routes.native-rejects-free-array
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-builtin-free-array.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM free rejects managed array handles"
            --xfail "llvm-aot=LLVM free rejects managed array handles"
            --xfail-stderr-contains "llvm-jit=builtin 'free' requires exact raw-pointer metadata ptr<i32>, ptr<i64>, ptr<bool>, or ptr<u8>"
            --xfail-stderr-contains "llvm-aot=builtin 'free' requires exact raw-pointer metadata ptr<i32>, ptr<i64>, ptr<bool>, or ptr<u8>"
        )
        add_ny_route_test(
            ny.routes.native-rejects-read-i32-text
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-builtin-read-text.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM read_i32 requires ptr<i32> metadata"
            --xfail "llvm-aot=LLVM read_i32 requires ptr<i32> metadata"
            --xfail-stderr-contains "llvm-jit=builtin 'read_i32' requires exact raw-pointer metadata ptr<i32>"
            --xfail-stderr-contains "llvm-aot=builtin 'read_i32' requires exact raw-pointer metadata ptr<i32>"
        )
        add_ny_route_test(
            ny.routes.native-rejects-read-i32-pointer-kind
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-builtin-read-wrong-pointer.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM read_i32 rejects other raw-pointer element metadata"
            --xfail "llvm-aot=LLVM read_i32 rejects other raw-pointer element metadata"
            --xfail-stderr-contains "llvm-jit=builtin 'read_i32' requires exact raw-pointer metadata ptr<i32>"
            --xfail-stderr-contains "llvm-aot=builtin 'read_i32' requires exact raw-pointer metadata ptr<i32>"
        )
        add_ny_route_test(
            ny.routes.native-rejects-write-i32-text
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-builtin-write-text.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM write_i32 requires ptr<i32> metadata"
            --xfail "llvm-aot=LLVM write_i32 requires ptr<i32> metadata"
            --xfail-stderr-contains "llvm-jit=builtin 'write_i32' requires exact raw-pointer metadata ptr<i32>"
            --xfail-stderr-contains "llvm-aot=builtin 'write_i32' requires exact raw-pointer metadata ptr<i32>"
        )
        add_ny_route_test(
            ny.routes.native-rejects-write-i32-struct
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-builtin-write-struct.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM write_i32 rejects aggregate handles"
            --xfail "llvm-aot=LLVM write_i32 rejects aggregate handles"
            --xfail-stderr-contains "llvm-jit=builtin 'write_i32' requires exact raw-pointer metadata ptr<i32>"
            --xfail-stderr-contains "llvm-aot=builtin 'write_i32' requires exact raw-pointer metadata ptr<i32>"
        )
        add_ny_route_test(
            ny.routes.native-struct-abi
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-abi.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-abi.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-exhaustive-result-return
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-exhaustive-result-return.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-exhaustive-result-return.stdout"
            --expected-exit 0
            --routes all
            --with-stdlib
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        # Extension methods on builtin types. A method's symbol is built from
        # the impl target while the call site builds it from the receiver's
        # inferred type; module-qualifying a builtin target made the two
        # disagree and every such call failed with an unknown function.
        add_ny_route_test(
            ny.routes.primitive-extension
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/primitive-extension.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/primitive-extension.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
        )
        add_ny_route_test(
            ny.routes.nonvoid-fallthrough-interpreter
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/nonvoid-fallthrough.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/nonvoid-fallthrough-interpreter.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
        )
        add_ny_route_test(
            ny.routes.nonvoid-fallthrough-native
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/nonvoid-fallthrough.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/nonvoid-fallthrough-native.stderr"
            --expected-exit 1
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.native-rejects-struct-signature-mismatch
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-signature-mismatch.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM must reject mismatched struct signatures"
            --xfail "llvm-aot=LLVM must reject mismatched struct signatures"
            --xfail-stderr-contains "llvm-jit=passes 'left_record' to aggregate parameter"
            --xfail-stderr-contains "llvm-aot=passes 'left_record' to aggregate parameter"
        )
        add_ny_route_test(
            ny.routes.native-rejects-struct-field-mismatch
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-field-mismatch.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM must reject mismatched typed struct fields"
            --xfail "llvm-aot=LLVM must reject mismatched typed struct fields"
            --xfail-stderr-contains "llvm-jit=expects native handle type 'ptr<i64>', got 'ptr<i32>'"
            --xfail-stderr-contains "llvm-aot=expects native handle type 'ptr<i64>', got 'ptr<i32>'"
        )
        add_ny_route_test(
            ny.routes.native-struct-text-array-field
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-text-array-field.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-rejects-recursive-struct-field
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-recursive-field.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=cyclic struct layouts are not supported"
            --xfail "llvm-aot=cyclic struct layouts are not supported"
            --xfail-stderr-contains "llvm-jit=is a recursive aggregate"
            --xfail-stderr-contains "llvm-aot=is a recursive aggregate"
        )
        add_ny_route_test(
            ny.routes.rejects-incomplete-struct-literal
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-incomplete.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-incomplete.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.rejects-invalid-struct-literal-fields
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-struct-literal-fields.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-struct-literal-fields.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.native-rejects-incomplete-struct-literal
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-struct-incomplete.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM must reject native structs with uninitialized fields"
            --xfail "llvm-aot=LLVM must reject native structs with uninitialized fields"
            --xfail-stderr-contains "llvm-jit=leaves required field 'label' uninitialized"
            --xfail-stderr-contains "llvm-aot=leaves required field 'label' uninitialized"
        )
        add_ny_route_test(
            ny.routes.native-rejects-generic-enum-signature
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-generic-enum-signature.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=an integer cannot satisfy a concrete generic enum parameter"
            --xfail "llvm-aot=an integer cannot satisfy a concrete generic enum parameter"
            --xfail-stderr-contains "llvm-jit=passes 'i64' to enum parameter 'value' of type 'generic_mode<i32>'"
            --xfail-stderr-contains "llvm-aot=passes 'i64' to enum parameter 'value' of type 'generic_mode<i32>'"
        )
        add_ny_route_test(
            ny.routes.native_generic_enum_construction
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-generic-enum-construction.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-generic-enum-construction.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-rejects-unknown-nominal-signature
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unknown-nominal-signature.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=unknown nominal signatures cannot use the scalar word ABI"
            --xfail "llvm-aot=unknown nominal signatures cannot use the scalar word ABI"
            --xfail-stderr-contains "llvm-jit=has no exact native ABI metadata"
            --xfail-stderr-contains "llvm-aot=has no exact native ABI metadata"
        )
        add_ny_route_test(
            ny.routes.native-rejects-handle-signature-mismatch
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-handle-signature-mismatch.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM must reject mismatched opaque handle signatures"
            --xfail "llvm-aot=LLVM must reject mismatched opaque handle signatures"
            --xfail-stderr-contains "llvm-jit=passes 'ptr<i32>' to handle parameter"
            --xfail-stderr-contains "llvm-aot=passes 'ptr<i32>' to handle parameter"
        )
        add_ny_route_test(
            ny.routes.native-rejects-array-signature-mismatch
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-signature-mismatch.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM must reject mismatched array handle signatures"
            --xfail "llvm-aot=LLVM must reject mismatched array handle signatures"
            --xfail-stderr-contains "llvm-jit=passes 'array<i32>' to handle parameter"
            --xfail-stderr-contains "llvm-aot=passes 'array<i32>' to handle parameter"
        )
        add_ny_route_test(
            ny.routes.native-rejects-cast-laundering
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-cast-laundering.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=ordinary CAST cannot launder a typed handle through an integer"
            --xfail "llvm-aot=ordinary CAST cannot launder a typed handle through an integer"
            --xfail-stderr-contains "llvm-jit=ordinary CAST cannot convert native handle 'ptr<i32>' to scalar type 'i64'"
            --xfail-stderr-contains "llvm-aot=ordinary CAST cannot convert native handle 'ptr<i32>' to scalar type 'i64'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-integer-to-handle-cast
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-integer-to-handle-cast.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=ordinary CAST cannot manufacture a typed handle from an integer"
            --xfail "llvm-aot=ordinary CAST cannot manufacture a typed handle from an integer"
            --xfail-stderr-contains "llvm-jit=to native handle type 'ptr<i32>'"
            --xfail-stderr-contains "llvm-aot=to native handle type 'ptr<i32>'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-integer-enum-parameter
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-enum-integer-parameter.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=enum parameters require exact enum metadata"
            --xfail "llvm-aot=enum parameters require exact enum metadata"
            --xfail-stderr-contains "llvm-jit=passes 'i64' to enum parameter 'value' of type 'native_mode'"
            --xfail-stderr-contains "llvm-aot=passes 'i64' to enum parameter 'value' of type 'native_mode'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-enum-parameter-mismatch
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-enum-parameter-mismatch.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=distinct enum schemas cannot cross a call boundary"
            --xfail "llvm-aot=distinct enum schemas cannot cross a call boundary"
            --xfail-stderr-contains "llvm-jit=passes 'source_mode' to enum parameter 'value' of type 'target_mode'"
            --xfail-stderr-contains "llvm-aot=passes 'source_mode' to enum parameter 'value' of type 'target_mode'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-enum-return-mismatch
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-enum-return-mismatch.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=distinct enum schemas cannot cross a return boundary"
            --xfail "llvm-aot=distinct enum schemas cannot cross a return boundary"
            --xfail-stderr-contains "llvm-jit=returns 'source_mode' where its NIL signature requires enum type 'target_mode'"
            --xfail-stderr-contains "llvm-aot=returns 'source_mode' where its NIL signature requires enum type 'target_mode'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-integer-enum-return
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-enum-integer-return.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=enum returns require exact enum metadata"
            --xfail "llvm-aot=enum returns require exact enum metadata"
            --xfail-stderr-contains "llvm-jit=returns 'i64' where its NIL signature requires enum type 'native_mode'"
            --xfail-stderr-contains "llvm-aot=returns 'i64' where its NIL signature requires enum type 'native_mode'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-direct-array-literal-mismatch
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-direct-literal-mismatch.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=array literals retain their concrete element ABI"
            --xfail "llvm-aot=array literals retain their concrete element ABI"
            --xfail-stderr-contains "llvm-jit=passes 'array<i32>' to handle parameter 'values' of type 'array<i64>'"
            --xfail-stderr-contains "llvm-aot=passes 'array<i32>' to handle parameter 'values' of type 'array<i64>'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-untyped-array-literal
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-untyped-literal.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM cannot lower wildcard array element layouts"
            --xfail "llvm-aot=LLVM cannot lower wildcard array element layouts"
            --xfail-stderr-contains "llvm-jit=requires exact contextual element metadata"
            --xfail-stderr-contains "llvm-aot=requires exact contextual element metadata"
        )
        add_ny_route_test(
            ny.routes.native-rejects-uncontextual-empty-array
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-uncontextual-empty.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --xfail "llvm-jit=empty array literals require a concrete contextual element type"
            --xfail "llvm-aot=empty array literals require a concrete contextual element type"
            --xfail-stderr-contains "llvm-jit=requires exact contextual element metadata"
            --xfail-stderr-contains "llvm-aot=requires exact contextual element metadata"
        )
        add_ny_route_test(
            ny.routes.native-rejects-array-literal-stack-underflow
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-literal-stack-underflow.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=malformed array literals cannot consume a partial operand stack"
            --xfail "llvm-aot=malformed array literals cannot consume a partial operand stack"
            --xfail-stderr-contains "llvm-jit=the NIL operand stack underflowed while collecting them"
            --xfail-stderr-contains "llvm-aot=the NIL operand stack underflowed while collecting them"
        )
        foreach(ny_array_reject IN ITEMS signature-mismatch extra-parameter bool-index bool-size)
            if(ny_array_reject MATCHES "^bool-")
                set(ny_array_stderr "native-array-intrinsic-parameter.stderr")
            else()
                set(ny_array_stderr "native-array-intrinsic-signature.stderr")
            endif()
            add_ny_route_test(
                ny.routes.reject-array-intrinsic-${ny_array_reject}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-intrinsic-${ny_array_reject}.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${ny_array_stderr}"
                --expected-exit 1
                --routes compile-reject
                --compiler-arg=--no-typecheck
            )
        endforeach()
        add_ny_route_test(
            ny.routes.native-array-intrinsic-bounds
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-intrinsic-bounds.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-intrinsic-bounds.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.native-array-intrinsic-new
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-intrinsic-new.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-array-intrinsic-new.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.native-stdlib-array-wrappers
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-stdlib-array-wrappers.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-stdlib-array-wrappers.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.native-rejects-unknown-intrinsic
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-rejects-unknown-intrinsic.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --xfail "llvm-jit=unknown native intrinsics must fail closed"
            --xfail "llvm-aot=unknown native intrinsics must fail closed"
            --xfail-stderr-contains "llvm-jit=unsupported native annotation '@intrinsic(\"ny.tests.native.unknown\")'"
            --xfail-stderr-contains "llvm-aot=unsupported native annotation '@intrinsic(\"ny.tests.native.unknown\")'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-unknown-interop
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-rejects-unknown-interop.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --xfail "llvm-jit=unknown native interop libraries must fail closed"
            --xfail "llvm-aot=unknown native interop libraries must fail closed"
            --xfail-stderr-contains "llvm-jit=external interop library 'ny_tests_missing_native_library'"
            --xfail-stderr-contains "llvm-aot=external interop library 'ny_tests_missing_native_library'"
        )
        add_ny_route_test(
            ny.routes.native-rejects-multiple-dispatch-annotations
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-rejects-multiple-dispatch-annotations.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-rejects-multiple-dispatch-annotations.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-multiple-dispatch-math
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-multiple-dispatch-math.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-multiple-dispatch-math.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.native-rejects-embedded-nul-text
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-rejects-embedded-nul-text.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --xfail "llvm-jit=the native text ABI requires NUL-free values"
            --xfail "llvm-aot=the native text ABI requires NUL-free values"
            --xfail-stderr-contains "llvm-jit=contains an embedded NUL byte"
            --xfail-stderr-contains "llvm-aot=contains an embedded NUL byte"
        )
        add_ny_route_test(
            ny.routes.native-dead-embedded-nul-text
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-dead-embedded-nul-text.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-dead-embedded-nul-text.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-raw-pointer-layout
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-raw-pointer-layout.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-raw-pointer-layout.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-raw-pointer-layout-unmanaged
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-raw-pointer-layout.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-raw-pointer-layout.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--unmanaged
        )
        add_ny_route_test(
            ny.routes.native-pointer-arithmetic
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-arithmetic.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-arithmetic.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-pointer-arithmetic-unmanaged
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-arithmetic.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-arithmetic.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--unmanaged
        )
        add_ny_route_test(
            ny.routes.pointer-comparisons
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-comparisons.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-comparisons.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.pointer-comparisons-unmanaged
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-comparisons.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-comparisons.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--unmanaged
        )
        add_ny_route_test(
            ny.routes.native-rejects-pointer-comparison-mismatch
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-pointer-comparison-mismatch.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=raw-pointer comparisons require identical element metadata"
            --xfail "llvm-aot=raw-pointer comparisons require identical element metadata"
            --xfail-stderr-contains "llvm-jit=CMP_EQ raw-pointer operands must have identical exact"
            --xfail-stderr-contains "llvm-aot=CMP_EQ raw-pointer operands must have identical exact"
        )
        add_ny_route_test(
            ny.routes.pointer-one-past-identity
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-one-past-identity.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-one-past-identity.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.pointer-one-past-identity-unmanaged
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-one-past-identity.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-one-past-identity.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--unmanaged
        )
        add_ny_route_test(
            ny.routes.native_cli_array_primitives
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_array_primitives.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_array_primitives.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.native_cli_text_console
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_text_console.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_text_console.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_text_console.stderr"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.native_cli_numeric_conversions
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_numeric_conversions.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_numeric_conversions.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.native_cli_secure_byte_io
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_secure_byte_io.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native_cli_secure_byte_io.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.u8-canonicalization
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/u8-canonicalization.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/u8-canonicalization.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.u8-canonicalization-unmanaged
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/u8-canonicalization.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/u8-canonicalization.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--unmanaged
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.native-allocation-size-guard
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-allocation-size-guard.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-allocation-size-guard.stderr"
            --expected-exit 1
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.array-bounds-consistency
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/array-bounds-consistency.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/array-bounds-consistency.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.empty-array-contexts
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/empty-array-contexts.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/empty-array-contexts.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.variadic-parameters
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/variadic-parameters.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/variadic-parameters.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native_u8_handle_abi
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-byte-handle.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-unsupported-byte-handle.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.native-reachability-ignores-unused
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-reachability-unused.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-reachability-unused.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.native-rejects-reachable-duplicate-function
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-rejects-reachable-duplicate-function.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=reachable duplicate native function names are ambiguous"
            --xfail "llvm-aot=reachable duplicate native function names are ambiguous"
            --xfail-stderr-contains "llvm-jit=directly called function name 'duplicate_native_target' is ambiguous (2 NIL definitions)"
            --xfail-stderr-contains "llvm-aot=directly called function name 'duplicate_native_target' is ambiguous (2 NIL definitions)"
        )
        add_ny_route_test(
            ny.routes.native-rejects-duplicate-main
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-rejects-duplicate-main.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=duplicate native entry points are ambiguous"
            --xfail "llvm-aot=duplicate native entry points are ambiguous"
            --xfail-stderr-contains "llvm-jit=root function name 'main' is ambiguous (2 NIL definitions)"
            --xfail-stderr-contains "llvm-aot=root function name 'main' is ambiguous (2 NIL definitions)"
        )
        add_ny_route_test(
            ny.routes.native_reachable_u8_function
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-reachability-reachable-function.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-reachability-reachable-function.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.native_reachable_u8_struct
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-reachability-reachable-struct.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-reachability-reachable-struct.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
        )
        add_ny_route_test(
            ny.routes.typed-text-global
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-text-global.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-text-global.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.native-rejects-undefined-call
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-undefined-call.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-undefined-call.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=The frontend must reject a call statement with an unknown return contract"
            --xfail "llvm-aot=The frontend must reject a call statement with an unknown return contract"
            --xfail-stderr-contains "llvm-jit=cannot determine the return contract"
            --xfail-stderr-contains "llvm-aot=cannot determine the return contract"
        )
        add_ny_route_test(
            ny.routes.native-rejects-invalid-call-arity
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-invalid-call-arity.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-invalid-call-arity.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM must reject CALL arity that disagrees with the callee"
            --xfail "llvm-aot=LLVM must reject CALL arity that disagrees with the callee"
            --xfail-stderr-contains "llvm-jit=but the callee declares 2"
            --xfail-stderr-contains "llvm-aot=but the callee declares 2"
        )
        add_ny_route_test(
            ny.routes.native-rejects-main-parameters
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-main-parameters.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--no-typecheck
            --xfail "llvm-jit=LLVM must reject a parameterized process entry point"
            --xfail "llvm-aot=LLVM must reject a parameterized process entry point"
            --xfail-stderr-contains "llvm-jit=entry function 'main' must declare 0 parameters"
            --xfail-stderr-contains "llvm-aot=entry function 'main' must declare 0 parameters"
        )
        add_ny_route_test(
            ny.routes.global-assignment
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/global-assignment.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/global-assignment.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.module-local-call-resolution
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/module-local-call-resolution.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/module-local-call-resolution.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/tests/language/module-local-call-collision.ny
        )
        add_ny_route_test(
            ny.routes.internal-label-collision
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/internal-label-collision.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/internal-label-collision.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.range-continue
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/range-continue.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/range-continue.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.variant-spelling
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/variant-spelling.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/variant-spelling.stdout"
            --expected-exit 0
            --routes all
        )
        foreach(enum_frontend IN ITEMS ny matrix)
            if(enum_frontend STREQUAL "ny")
                set(enum_fixture qualified-enum-patterns)
            else()
                set(enum_fixture qualified-enum-patterns-matrix)
            endif()
            add_ny_route_test(
                ny.routes.${enum_fixture}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${enum_fixture}.${enum_frontend}"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${enum_fixture}.stdout"
                --expected-exit 0
                --routes all
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            )
            if(TARGET nilassembler AND TARGET nillinker)
                add_test(
                    NAME ny.tools.${enum_fixture}
                    COMMAND "${Python3_EXECUTABLE}"
                            "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_toolchain_pipeline.py"
                            --compiler $<TARGET_FILE:nilcompiler>
                            --assembler $<TARGET_FILE:nilassembler>
                            --linker $<TARGET_FILE:nillinker>
                            --runtime $<TARGET_FILE:nilruntime>
                            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${enum_fixture}.${enum_frontend}"
                            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${enum_fixture}.stdout"
                            --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
                            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/tools"
                )
                set_tests_properties(ny.tools.${enum_fixture} PROPERTIES
                    LABELS "tools;native;rc" SKIP_RETURN_CODE 77 TIMEOUT 240)
            endif()
        endforeach()
        add_test(
            NAME ny.qualified-enum-contract
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/qualified_enum_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.qualified-enum-contract PROPERTIES
            LABELS "typecheck;analysis;rc" TIMEOUT 120)
        foreach(numeric_fixture IN ITEMS scientific-literals scientific-literals-matrix
                                         integer-literal-widths integer-literal-widths-matrix)
            if(numeric_fixture MATCHES "-matrix$")
                set(numeric_frontend matrix)
            else()
                set(numeric_frontend ny)
            endif()
            add_ny_route_test(
                ny.routes.${numeric_fixture}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${numeric_fixture}.${numeric_frontend}"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${numeric_fixture}.stdout"
                --expected-exit 0
                --routes all
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            )
            if(TARGET nilassembler AND TARGET nillinker)
                add_test(
                    NAME ny.tools.${numeric_fixture}
                    COMMAND "${Python3_EXECUTABLE}"
                            "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/native_toolchain_pipeline.py"
                            --compiler $<TARGET_FILE:nilcompiler>
                            --assembler $<TARGET_FILE:nilassembler>
                            --linker $<TARGET_FILE:nillinker>
                            --runtime $<TARGET_FILE:nilruntime>
                            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${numeric_fixture}.${numeric_frontend}"
                            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${numeric_fixture}.stdout"
                            --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
                            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/tools"
                )
                set_tests_properties(ny.tools.${numeric_fixture} PROPERTIES
                    LABELS "tools;native;rc" SKIP_RETURN_CODE 77 TIMEOUT 240)
            endif()
        endforeach()
        add_test(
            NAME ny.scientific-literals-contract
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/scientific_literals_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.scientific-literals-contract PROPERTIES
            LABELS "typecheck;analysis;rc" TIMEOUT 120)
        add_test(
            NAME ny.integer-literals-contract
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/integer_literals_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.integer-literals-contract PROPERTIES
            LABELS "typecheck;analysis;rc" TIMEOUT 180)
        add_ny_route_test(
            ny.routes.canonical-option-result-methods
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/canonical-option-result-methods.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/canonical-option-result-methods.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.generic-static-inference
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic-static-inference.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic-static-inference.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.text-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.file-stream-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/file-stream-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/file-stream-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.io-path-file-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/io-path-file-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/io-path-file-runtime.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/io-path-file-runtime.stderr"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        foreach(nil_math_fixture IN ITEMS vector-norms vector-norms-matrix random-generators random-generators-matrix)
            if(nil_math_fixture MATCHES "-matrix$")
                set(nil_math_suffix matrix)
            else()
                set(nil_math_suffix ny)
            endif()
            add_ny_route_test(
                ny.routes.${nil_math_fixture}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${nil_math_fixture}.${nil_math_suffix}"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${nil_math_fixture}.stdout"
                --expected-exit 0
                --routes all
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            )
        endforeach()
        add_ny_route_test(
            ny.routes.math-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/math-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/math-runtime.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_test(
            NAME ny.random-range-contract
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/random_range_contract.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --stdlib "${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src"
                    --llvm "${NIL_ENABLE_LLVM}"
        )
        set_tests_properties(ny.random-range-contract PROPERTIES
            LABELS "stdlib;runtime;rc" TIMEOUT 180)
        add_ny_route_test(
            ny.routes.math-invalid-range-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/math-invalid-range-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/math-invalid-range-runtime.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.security-base64-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/security-base64-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/security-base64-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.reject-unbacked-io-security
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unbacked-io-security.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unbacked-io-security.stderr"
            --expected-exit 1
            --routes compile-reject
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.core-convert-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-convert-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-convert-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.core-convert-reject
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-convert-reject.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-convert-reject.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        function(add_ny_core_convert_reject_test test_name scenario stderr_name)
            add_ny_route_test(
                "${test_name}"
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-convert-edge-reject.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${stderr_name}"
                --expected-exit 1
                --routes text-interpreter,binary-interpreter
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
                --program-arg=${scenario}
            )
        endfunction()
        add_ny_core_convert_reject_test(
            ny.routes.core-convert-i64-overflow-reject
            i64-overflow
            core-convert-i64-overflow-reject.stderr
        )
        add_ny_core_convert_reject_test(
            ny.routes.core-convert-nonfinite-reject
            nonfinite-f64
            core-convert-nonfinite-reject.stderr
        )
        add_ny_core_convert_reject_test(
            ny.routes.core-convert-base-reject
            invalid-base
            core-convert-base-reject.stderr
        )
        add_ny_core_convert_reject_test(
            ny.routes.core-convert-scalar-reject
            invalid-scalar
            core-convert-scalar-reject.stderr
        )
        add_ny_core_convert_reject_test(
            ny.routes.core-convert-multiple-scalars-reject
            multiple-scalars
            core-convert-multiple-scalars-reject.stderr
        )
        add_ny_core_convert_reject_test(
            ny.routes.core-convert-odd-hex-reject
            odd-hex
            core-convert-odd-hex-reject.stderr
        )
        add_ny_core_convert_reject_test(
            ny.routes.core-convert-precision-reject
            precision
            core-convert-precision-reject.stderr
        )
        add_ny_route_test(
            ny.routes.core-behavior-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-behavior-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-behavior-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.generic-free-function-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic-free-function-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/generic-free-function-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/generic-sdk
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.core-assert-reject
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-assert-reject.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-assert-reject.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        function(add_ny_core_comparison_reject_test test_name scenario stderr_name)
            add_ny_route_test(
                "${test_name}"
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-aggregate-comparison-reject.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${stderr_name}"
                --expected-exit 1
                --routes text-interpreter,binary-interpreter
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
                --program-arg=${scenario}
            )
        endfunction()
        add_ny_core_comparison_reject_test(
            ny.routes.core-struct-equality-reject
            struct-equality
            core-struct-equality-reject.stderr
        )
        add_ny_core_comparison_reject_test(
            ny.routes.core-enum-equality-reject
            enum-equality
            core-enum-equality-reject.stderr
        )
        add_ny_core_comparison_reject_test(
            ny.routes.core-array-equality-reject
            array-equality
            core-array-equality-reject.stderr
        )
        add_ny_core_comparison_reject_test(
            ny.routes.core-aggregate-order-reject
            aggregate-order
            core-aggregate-order-reject.stderr
        )
        function(add_ny_core_algorithm_reject_test test_name scenario stderr_name)
            add_ny_route_test(
                "${test_name}"
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-algorithm-reject.ny"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${stderr_name}"
                --expected-exit 1
                --routes text-interpreter,binary-interpreter
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
                --program-arg=${scenario}
            )
        endfunction()
        add_ny_core_algorithm_reject_test(
            ny.routes.core-algorithm-reject
            factorial-negative
            core-algorithm-reject.stderr
        )
        add_ny_core_algorithm_reject_test(
            ny.routes.core-factorial-overflow-reject
            factorial-overflow
            core-factorial-overflow-reject.stderr
        )
        add_ny_core_algorithm_reject_test(
            ny.routes.core-clamp-reject
            clamp-range
            core-clamp-reject.stderr
        )
        add_ny_core_algorithm_reject_test(
            ny.routes.core-division-zero-reject
            division-zero
            core-division-zero-reject.stderr
        )
        add_ny_core_algorithm_reject_test(
            ny.routes.core-division-overflow-reject
            division-overflow
            core-division-overflow-reject.stderr
        )
        add_ny_core_algorithm_reject_test(
            ny.routes.core-negative-power-reject
            negative-power
            core-negative-power-reject.stderr
        )
        add_ny_core_algorithm_reject_test(
            ny.routes.core-power-overflow-reject
            power-overflow
            core-power-overflow-reject.stderr
        )
        add_ny_core_algorithm_reject_test(
            ny.routes.core-absolute-overflow-reject
            absolute-overflow
            core-absolute-overflow-reject.stderr
        )
        foreach(absolute_frontend IN ITEMS ny matrix)
            if(absolute_frontend STREQUAL "ny")
                set(absolute_fixture core-absolute-i64-overflow)
            else()
                set(absolute_fixture core-absolute-i64-overflow-matrix)
            endif()
            add_ny_route_test(
                ny.routes.${absolute_fixture}
                --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${absolute_fixture}.${absolute_frontend}"
                --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-absolute-i64-overflow.stderr"
                --expected-exit 1
                --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
                --compiler-arg=--sdk-path
                --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            )
            if(NIL_ENABLE_LLVM)
                add_ny_route_test(
                    ny.routes.native-${absolute_fixture}
                    --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/${absolute_fixture}.${absolute_frontend}"
                    --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
                    --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-absolute-i64-overflow.native.stderr"
                    --expected-exit 1
                    --routes llvm-jit,llvm-aot
                    --compiler-arg=--sdk-path
                    --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
                )
            endif()
        endforeach()
        add_ny_core_algorithm_reject_test(
            ny.routes.core-gcd-overflow-reject
            gcd-overflow
            core-gcd-overflow-reject.stderr
        )
        add_ny_route_test(
            ny.routes.core-option-unwrap-reject
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-option-unwrap-reject.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/core-option-unwrap-reject.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.system-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.system-arguments-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-arguments-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-arguments-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            --program-arg=alpha
            --program-arg=
            --program-arg=--looks-like-an-option
        )
        add_ny_route_test(
            ny.routes.native-system-arguments-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-system-arguments-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-system-arguments-runtime.stdout"
            --expected-exit 0
            --routes llvm-jit,llvm-aot
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            --program-arg=alpha
            --program-arg=
            --program-arg=--help
        )
        add_ny_route_test(
            ny.routes.compiler-rejects-program-arguments
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke/hello.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-program-arguments-route.stderr"
            --expected-exit 1
            --routes compile-reject
            --program-arg=--help
        )
        add_ny_route_test(
            ny.routes.native-aot-rejects-program-arguments
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke/hello.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/native-program-arguments-route.stderr"
            --expected-exit 1
            --routes compile-reject
            --compiler-arg=--native
            --program-arg=--help
        )
        add_ny_route_test(
            ny.routes.system-argument-reject
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-argument-reject.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-argument-reject.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.system-exit-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-exit-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-exit-runtime.stdout"
            --expected-exit 7
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.system-exit-code-reject
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-exit-code-reject.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/system-exit-code-reject.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.gc-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/gc-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/gc-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            --interpreter-arg=--memory-mode=safe
        )
        add_ny_route_test(
            ny.routes.pointer-interior-gc
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-interior-gc.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/pointer-interior-gc.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
            --interpreter-arg=--memory-mode=safe
        )
        add_ny_route_test(
            ny.routes.gc-unsafe-reject
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/gc-unsafe-reject.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/gc-unsafe-reject.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.collections-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/collections-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/collections-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.db-query-builder-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/db-query-builder-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/db-query-builder-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --with-stdlib
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.http-values-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/http-values-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/http-values-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --with-stdlib
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.reject-unbacked-db-net
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unbacked-db-net.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unbacked-db-net.stderr"
            --expected-exit 1
            --routes compile-reject
            --with-stdlib
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.threading-sleep
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/threading-sleep.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/threading-sleep.stdout"
            --expected-exit 0
            --routes all
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.text-array-values
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-array-values.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/text-array-values.stdout"
            --expected-exit 0
            --routes all
        )
        add_ny_route_test(
            ny.routes.datetime-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/datetime-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/datetime-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --with-stdlib
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.diagnostics-logging
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/diagnostics-logging.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/diagnostics-logging.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --with-stdlib
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.diagnostics-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/diagnostics-runtime.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/diagnostics-runtime.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --with-stdlib
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.nydata-roundtrip
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/nydata-roundtrip.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/nydata-roundtrip.stdout"
            --expected-exit 0
            --routes text-interpreter,binary-interpreter
            --with-stdlib
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.runtime-error
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/runtime_error.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/runtime_error.stderr"
            --expected-exit 1
            --routes all
        )
        add_ny_route_test(
            ny.routes.reject-source
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/invalid_source.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/invalid_source.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-inline-assembly
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-inline-assembly.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-inline-assembly.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-unchecked-faults
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unchecked-faults.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unchecked-faults.stderr"
            --expected-exit 1
            --routes compile-reject
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.reject-error-propagation
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-error-propagation.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-error-propagation.stderr"
            --expected-exit 1
            --routes compile-reject
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.reject-compiler-intrinsics
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-compiler-intrinsics.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-compiler-intrinsics.stderr"
            --expected-exit 1
            --routes compile-reject
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.reject-unbacked-concurrency
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unbacked-concurrency.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unbacked-concurrency.stderr"
            --expected-exit 1
            --routes compile-reject
            --compiler-arg=--sdk-path
            --compiler-arg=${CMAKE_CURRENT_SOURCE_DIR}/nilstd/src
        )
        add_ny_route_test(
            ny.routes.reject-structured-match-pattern
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-structured-match-pattern.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-structured-match-pattern.stderr"
            --expected-exit 1
            --routes compile-reject
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.reject-unlowered-ast-fallbacks
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unlowered-ast-fallbacks.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unlowered-ast-fallbacks.stderr"
            --expected-exit 1
            --routes compile-reject
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.reject-default-argument
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-default-argument.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-default-argument.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-unbounded-generic-member
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unbounded-generic-member.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-unbounded-generic-member.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-generic-inference-conflict
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-generic-inference-conflict.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-generic-inference-conflict.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-pointer-arithmetic
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-pointer-arithmetic.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-pointer-arithmetic.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-pointer-assignment
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-pointer-assignment.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-pointer-assignment.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-too-few-arguments
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-too-few-arguments.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-too-few-arguments.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-too-many-arguments
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-too-many-arguments.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-too-many-arguments.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-main-parameters
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-main-parameters.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-main-parameters.stderr"
            --expected-exit 1
            --routes compile-reject
        )
        add_ny_route_test(
            ny.routes.reject-call-arity-at-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-too-few-arguments.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-too-few-arguments.runtime.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.reject-builtin-arity-at-runtime
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-string-len-missing-receiver.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/language/reject-string-len-missing-receiver.runtime.stderr"
            --expected-exit 1
            --routes text-interpreter,binary-interpreter,bundle-text,bundle-binary
            --compiler-arg=--no-typecheck
        )
        add_ny_route_test(
            ny.routes.reject-source-as-nil
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/route_smoke.ny"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/source_input.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-invalid-nil
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/invalid_text.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/invalid_text.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-unknown-opcode
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/unknown_opcode.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/unknown_opcode.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-missing-main
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/missing_main.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/missing_main.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-duplicate-label
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/duplicate_label.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/duplicate_label.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-fake-main-label
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/fake_main_label.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/missing_main.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-missing-operand
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/missing_operand.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/missing_operand.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-main-parameters-nil
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/main-parameters.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/main-parameters.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-integer-division-overflow
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/integer-division-overflow.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/integer-division-overflow.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )
        add_ny_route_test(
            ny.routes.reject-integer-modulo-overflow
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/integer-modulo-overflow.nil.in"
            --expected-stdout "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/empty.stdout"
            --expected-stderr "${CMAKE_CURRENT_SOURCE_DIR}/tests/routes/integer-modulo-overflow.stderr"
            --expected-exit 1
            --routes interpreter-reject
        )

        # Exercise the production applications through every backend that this
        # build actually contains. LLVM-disabled builds retain the four
        # interpreter/bundle routes instead of reporting false native failures.
        set(
            NY_APPLICATION_ROUTES
            "text-interpreter,binary-interpreter,bundle-text,bundle-binary"
        )
        get_target_property(
            NILCOMPILER_CORE_COMPILE_DEFINITIONS
            nilcompiler_core
            COMPILE_DEFINITIONS
        )
        if("NY_HAS_LLVM=1" IN_LIST NILCOMPILER_CORE_COMPILE_DEFINITIONS)
            set(NY_APPLICATION_ROUTES "all")
        endif()

        add_test(
            NAME ny.applications.matrix
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/applications/run_suite.py"
                    --compiler $<TARGET_FILE:nilcompiler>
                    --interpreter $<TARGET_FILE:nilinterpreter>
                    --routes "${NY_APPLICATION_ROUTES}"
                    --output-dir "${CMAKE_CURRENT_BINARY_DIR}/tests/applications"
        )
        set_tests_properties(
            ny.applications.matrix
            PROPERTIES
                LABELS "applications;routes"
                TIMEOUT 120
        )
    endif()
endif()
