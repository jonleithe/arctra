function(arctra_set_project_warnings target_name)
    set(gcc_clang_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
    )

    set(msvc_warnings /W4 /permissive-)

    if(ARCTRA_WARNINGS_AS_ERRORS)
        list(APPEND gcc_clang_warnings -Werror)
        list(APPEND msvc_warnings /WX)
    endif()

    target_compile_options(
        "${target_name}"
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:${gcc_clang_warnings}>
            $<$<CXX_COMPILER_ID:MSVC>:${msvc_warnings}>
    )
endfunction()
