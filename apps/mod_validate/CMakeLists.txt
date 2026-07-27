add_executable(contract-mod-validate
    main.cpp)
target_link_libraries(contract-mod-validate PRIVATE OpenContract::mod-validator-tool)
contract_enable_strict_warnings(contract-mod-validate)
