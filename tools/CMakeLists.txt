
function(add_executable_with_props name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} WDLib)
    set_target_properties(${name}
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
            INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${LLVM_LIBRARY_DIRS};${CURL_LIB_DIR}"
            )
    install(TARGETS ${name}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            )
endfunction()

add_executable_with_props(sawd SAWD.cpp)
add_executable_with_props(lawd LAWD.cpp)
add_executable_with_props(dbgawd Debug.cpp)
add_executable_with_props(kmeld KMeld.cpp)