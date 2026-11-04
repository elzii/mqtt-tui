cmake_minimum_required(VERSION 3.20)
project(mqtt_tui_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
  pkg_check_modules(MOSQUITTO libmosquitto)
endif()

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(mqtt-tui ${SOURCES})

target_include_directories(mqtt-tui PRIVATE src)
target_link_libraries(mqtt-tui PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  nlohmann_json::nlohmann_json
)

if (MOSQUITTO_FOUND)
  target_include_directories(mqtt-tui PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
  target_link_libraries(mqtt-tui PRIVATE ${MOSQUITTO_LIBRARIES})
else()
  find_path(MOSQUITTO_INCLUDE_DIR mosquitto.h PATHS /opt/homebrew/include /usr/local/include /usr/include)
  find_library(MOSQUITTO_LIB mosquitto PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
  if (MOSQUITTO_INCLUDE_DIR AND MOSQUITTO_LIB)
    target_include_directories(mqtt-tui PRIVATE ${MOSQUITTO_INCLUDE_DIR})
    target_link_libraries(mqtt-tui PRIVATE ${MOSQUITTO_LIB})
  endif()
endif()
