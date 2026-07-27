add_executable(contract-inspect
    main.cpp)
target_link_libraries(contract-inspect PRIVATE OpenContract::format-inspector)
contract_enable_strict_warnings(contract-inspect)
