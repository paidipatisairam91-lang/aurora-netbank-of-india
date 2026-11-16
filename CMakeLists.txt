cmake_minimum_required(VERSION 3.16)
project(AuroraBank LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(aurora_bank
    src/main.cpp
    src/http_server.cpp
    src/bank_service.cpp
    src/fraud_engine.cpp
)
target_link_libraries(aurora_bank PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(aurora_bank PRIVATE ws2_32)
endif()

add_executable(bank_service_tests
    tests/bank_service_tests.cpp
    src/bank_service.cpp
    src/fraud_engine.cpp
)
target_link_libraries(bank_service_tests PRIVATE Threads::Threads)

add_executable(fraud_engine_tests
    tests/fraud_engine_tests.cpp
    src/fraud_engine.cpp
)

enable_testing()
add_test(NAME bank_service_tests COMMAND bank_service_tests)
add_test(NAME fraud_engine_tests COMMAND fraud_engine_tests)

set_tests_properties(bank_service_tests fraud_engine_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
