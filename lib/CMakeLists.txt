file (GLOB SOURCES
        Passes/*.cpp
        Passes/CallGraph/*.cpp
        Passes/AllocWrapperDetect/Heuristic/*.cpp
        Passes/AllocWrapperDetect/LLM/*.cpp
        Utils/*.cpp
        LLMQuery/*.cpp)

message("source ${SOURCES}")
link_directories(${LLVM_LIBRARY_DIRS})
add_library(WDLib SHARED ${SOURCES})

target_include_directories(WDLib PRIVATE "${CMAKE_SOURCE_DIR}/include")
if(DEFINED ZSTD_LIBRARIES)
    message("linking WDLib to: ${llvm_libs}, ${ZSTD_LIBRARIES}")
    target_link_libraries(WDLib ${llvm_libs} ${ZSTD_LIBRARIES} CURL::libcurl)
else()
    message("linking WDLib to: ${llvm_libs}")
    target_link_libraries(WDLib ${llvm_libs} CURL::libcurl)
endif()

install(TARGETS WDLib LIBRARY DESTINATION ${CMAKE_BINARY_DIR}/lib)