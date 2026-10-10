find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE oxcore miniz Qt6::Test)   # miniz: to compare .xlsx files
add_test(NAME core COMMAND test_core)
