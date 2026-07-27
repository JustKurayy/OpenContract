add_executable(contract-launcher
    main.cpp)
target_link_libraries(contract-launcher PRIVATE OpenContract::launcher-support)
contract_enable_strict_warnings(contract-launcher)
