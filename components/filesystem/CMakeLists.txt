add_library(contract-filesystem
    src/ReadOnlyFilesystem.cpp)
add_library(OpenContract::filesystem ALIAS contract-filesystem)

target_include_directories(contract-filesystem PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(contract-filesystem PUBLIC OpenContract::core)
contract_enable_strict_warnings(contract-filesystem)
