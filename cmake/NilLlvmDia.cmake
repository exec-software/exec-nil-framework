# Repair only a stale literal DIA import in a Windows LLVM binary package.
# The caller is responsible for invoking this on Windows, after find_package.
# Kept host-independent so synthetic configure tests need neither LLVM nor MSVC.
include_guard(GLOBAL)

function(nil_relocate_llvm_dia)
    # Capture before declaring FILEPATH: CMake otherwise converts an untyped
    # command-line relative path to an absolute path when assigning its type.
    set(_replacement "${NIL_LLVM_DIA_LIBRARY}")
    set(_missing "")
    set(_dia_count 0)
    set(_missing_count 0)
    if(TARGET LLVMDebugInfoPDB)
        get_target_property(_imported LLVMDebugInfoPDB IMPORTED)
        if(NOT _imported)
            message(FATAL_ERROR "NIL DIA repair requires imported LLVMDebugInfoPDB")
        endif()
        get_target_property(_links LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
        if(NOT "${_links}" STREQUAL "_links-NOTFOUND")
            foreach(_entry IN LISTS _links)
                string(TOLOWER "${_entry}" _lower_entry)
                if(_lower_entry MATCHES "diaguids[.]lib")
                    math(EXPR _dia_count "${_dia_count} + 1")
                    get_filename_component(_name "${_entry}" NAME)
                    string(TOLOWER "${_name}" _lower_name)
                    if(NOT IS_ABSOLUTE "${_entry}" OR
                       NOT "${_lower_name}" STREQUAL "diaguids.lib" OR
                       "${_entry}" MATCHES "\\$<")
                        message(FATAL_ERROR
                            "NIL DIA repair cannot interpret LLVM link entry: ${_entry}")
                    endif()
                    if(IS_DIRECTORY "${_entry}")
                        message(FATAL_ERROR "LLVM DIA link entry is a directory: ${_entry}")
                    endif()
                    if(NOT EXISTS "${_entry}")
                        math(EXPR _missing_count "${_missing_count} + 1")
                        set(_missing "${_entry}")
                    endif()
                endif()
            endforeach()
        endif()
    endif()

    if(_missing_count GREATER 0)
        if(NOT _missing_count EQUAL 1 OR NOT _dia_count EQUAL 1)
            message(FATAL_ERROR "NIL DIA repair refuses ambiguous multiple LLVM DIA link entries")
        endif()
        if("${_replacement}" STREQUAL "")
            message(FATAL_ERROR
                "LLVM imports missing DIA library: ${_missing}\n"
                "Configure -DNIL_LLVM_DIA_LIBRARY:FILEPATH=<absolute path to installed diaguids.lib>")
        endif()
        get_filename_component(_replacement_name "${_replacement}" NAME)
        string(TOLOWER "${_replacement_name}" _replacement_lower_name)
        if(NOT IS_ABSOLUTE "${_replacement}" OR
           NOT "${_replacement_lower_name}" STREQUAL "diaguids.lib" OR
           "${_replacement}" MATCHES "[;]" OR "${_replacement}" MATCHES "\\$<" OR
           NOT EXISTS "${_replacement}" OR IS_DIRECTORY "${_replacement}")
            message(FATAL_ERROR
                "NIL_LLVM_DIA_LIBRARY must be an existing absolute diaguids.lib file: ${_replacement}")
        endif()

        # Rewrite a complete literal list entry, not a substring or a generator
        # expression. Work on the original serialized property to preserve all
        # unrelated entries, ordering and escaped semicolons byte-for-byte.
        set(_wrapped ";${_links};")
        set(_needle ";${_missing};")
        string(FIND "${_wrapped}" "${_needle}" _offset)
        if(_offset LESS 0)
            message(FATAL_ERROR "NIL DIA repair cannot locate one literal LLVM DIA link entry")
        endif()
        string(LENGTH "${_needle}" _needle_length)
        math(EXPR _after "${_offset} + ${_needle_length}")
        string(SUBSTRING "${_wrapped}" ${_after} -1 _tail)
        string(FIND "${_tail}" "${_needle}" _duplicate)
        if(NOT _duplicate LESS 0)
            message(FATAL_ERROR "NIL DIA repair refuses ambiguous serialized LLVM DIA entries")
        endif()
        string(SUBSTRING "${_wrapped}" 0 ${_offset} _head)
        set(_repaired "${_head};${_replacement};${_tail}")
        string(LENGTH "${_repaired}" _repaired_length)
        math(EXPR _repaired_length "${_repaired_length} - 2")
        string(SUBSTRING "${_repaired}" 1 ${_repaired_length} _repaired)
        set_property(TARGET LLVMDebugInfoPDB PROPERTY INTERFACE_LINK_LIBRARIES "${_repaired}")
        message(STATUS "NIL LLVM DIA import relocated to: ${_replacement}")
    endif()

    # No FORCE: the selected installation survives future CMake regenerations.
    # Existing valid LLVM entries are never redirected to a different library.
    set(NIL_LLVM_DIA_LIBRARY "${_replacement}" CACHE FILEPATH
        "Installed diaguids.lib replacing a missing literal Windows LLVM DIA import")
endfunction()
