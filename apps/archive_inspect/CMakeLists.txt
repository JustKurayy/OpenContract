add_executable(contract-archive-inspect
    main.cpp)
target_link_libraries(
    contract-archive-inspect
    PRIVATE OpenContract::format-inspector)
contract_enable_strict_warnings(contract-archive-inspect)
