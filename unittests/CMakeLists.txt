find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE oxcore Qt6::Test)
add_test(NAME core COMMAND test_core)
