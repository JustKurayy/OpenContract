add_library(contract-diagnostics
    src/DiagnosticSink.cpp)
add_library(OpenContract::diagnostics ALIAS contract-diagnostics)

target_include_directories(contract-diagnostics PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
contract_enable_strict_warnings(contract-diagnostics)
