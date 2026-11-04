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

# Ensure FTXUI properly treats Miscellaneous Symbols and Pictographs (0x1F000 - 0x1FFFF) as width 2 in modern terminals
set(FTXUI_STRING_CPP "${ftxui_SOURCE_DIR}/src/ftxui/screen/string.cpp")
if (EXISTS "${FTXUI_STRING_CPP}")
  file(READ "${FTXUI_STRING_CPP}" STRING_CPP_CONTENT)
  string(FIND "${STRING_CPP_CONTENT}" "0x1F000" HAS_EMOJI_FIX)
  if (HAS_EMOJI_FIX EQUAL -1)
    string(REPLACE
      "bool IsFullWidth(uint32_t ucs) {\n  if (ucs < 0x0300)  // Quick path: // NOLINT\n    return false;"
      "bool IsFullWidth(uint32_t ucs) {\n  if (ucs < 0x0300)  // Quick path: // NOLINT\n    return false;\n  if (ucs >= 0x1F000 && ucs <= 0x1FFFF)\n    return true;"
      STRING_CPP_CONTENT "${STRING_CPP_CONTENT}")
  endif()
  string(FIND "${STRING_CPP_CONTENT}" "0xFE00" HAS_VS_FIX)
  if (HAS_VS_FIX EQUAL -1)
    string(REPLACE
      "bool IsCombining(uint32_t ucs) {\n  return Bisearch(ucs, g_extend_characters);\n}"
      "bool IsCombining(uint32_t ucs) {\n  if (ucs >= 0xFE00 && ucs <= 0xFE0F) return true;\n  return Bisearch(ucs, g_extend_characters);\n}"
      STRING_CPP_CONTENT "${STRING_CPP_CONTENT}")
    string(REPLACE
      "if (IsCombining(codepoint)) {\n      if (!out.empty()) {\n        out.back() += append;\n      }\n      continue;\n    }"
      "if (IsCombining(codepoint)) {\n      if (!out.empty()) {\n        if (out.back().empty() && out.size() >= 2) {\n          out[out.size() - 2] += append;\n        } else {\n          out.back() += append;\n        }\n      }\n      continue;\n    }"
      STRING_CPP_CONTENT "${STRING_CPP_CONTENT}")
    file(WRITE "${FTXUI_STRING_CPP}" "${STRING_CPP_CONTENT}")
    message(STATUS "Applied variation selector fix to FTXUI string.cpp")
  endif()
endif()

# Ensure FTXUI Input component draws a crisp visible cursor block, scrollbar on multiline, and removes background shading
set(FTXUI_INPUT_CPP "${ftxui_SOURCE_DIR}/src/ftxui/component/input.cpp")
if (EXISTS "${FTXUI_INPUT_CPP}")
  file(READ "${FTXUI_INPUT_CPP}" INPUT_CPP_CONTENT)
  string(FIND "${INPUT_CPP_CONTENT}" "focusCursorBlock" HAS_CURSOR_FIX)
  if (HAS_CURSOR_FIX EQUAL -1)
    string(REPLACE
      "const auto focused =\n        (is_focused || hovered_) ? focusCursorBarBlinking : select;"
      "const bool is_focused = Focused();\n    auto focused = [is_focused](Element el) {\n      return is_focused ? (std::move(el) | inverted | focusCursorBlock) : select(std::move(el));\n    };"
      INPUT_CPP_CONTENT "${INPUT_CPP_CONTENT}")
    string(REPLACE
      "const auto focused =\n        is_focused ? (inverted | focusCursorBlock) : select;"
      "auto focused = [is_focused](Element el) {\n      return is_focused ? (std::move(el) | inverted | focusCursorBlock) : select(std::move(el));\n    };"
      INPUT_CPP_CONTENT "${INPUT_CPP_CONTENT}")
    string(REPLACE
      "auto element = vbox(std::move(elements)) | frame;"
      "auto element = multiline() ? (vbox(std::move(elements)) | vscroll_indicator | yframe)\n                               : (vbox(std::move(elements)) | frame);"
      INPUT_CPP_CONTENT "${INPUT_CPP_CONTENT}")
    string(REPLACE
      "    if (content->empty()) {\n      auto element = text(placeholder()) | xflex | frame;\n      if (is_focused) {\n        element |= focus;\n      }"
      "    if (content->empty()) {\n      auto element = is_focused ? hbox({text(\" \") | focused | reflect(cursor_box_), text(placeholder()) | xflex}) | frame\n                                : (text(placeholder()) | xflex | frame);"
      INPUT_CPP_CONTENT "${INPUT_CPP_CONTENT}")
    file(WRITE "${FTXUI_INPUT_CPP}" "${INPUT_CPP_CONTENT}")
    message(STATUS "Applied cursor block and multiline scrollbar fix to FTXUI input.cpp")
  endif()
endif()

set(FTXUI_COMP_OPT_CPP "${ftxui_SOURCE_DIR}/src/ftxui/component/component_options.cpp")
if (EXISTS "${FTXUI_COMP_OPT_CPP}")
  file(READ "${FTXUI_COMP_OPT_CPP}" COMP_OPT_CONTENT)
  string(FIND "${COMP_OPT_CONTENT}" "bgcolor(Color::GrayDark)" HAS_GRAY_BG)
  if (NOT HAS_GRAY_BG EQUAL -1)
    string(REPLACE
      "    if (state.focused) {\n      state.element |= inverted;\n    } else if (state.hovered) {\n      state.element |= bgcolor(Color::GrayDark);\n    }"
      "    // Clean background without gray hover/inverted flash"
      COMP_OPT_CONTENT "${COMP_OPT_CONTENT}")
    file(WRITE "${FTXUI_COMP_OPT_CPP}" "${COMP_OPT_CONTENT}")
    message(STATUS "Removed background shading from FTXUI component_options.cpp")
  endif()
endif()

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
