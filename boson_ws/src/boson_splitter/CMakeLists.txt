cmake_minimum_required(VERSION 3.8)
project(boson_splitter)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)

  set(ament_cmake_copyright_FOUND TRUE)

  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()

add_executable(boson_splitter_node src/boson_splitter_node.cpp)
ament_target_dependencies(boson_splitter_node rclcpp sensor_msgs std_msgs)

install(TARGETS
  boson_splitter_node
  DESTINATION lib/${PROJECT_NAME}
)

