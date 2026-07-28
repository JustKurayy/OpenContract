add_executable(contract-runtime
    main.cpp)
target_link_libraries(contract-runtime PRIVATE
    OpenContract::platform-runtime)
contract_enable_strict_warnings(contract-runtime)
