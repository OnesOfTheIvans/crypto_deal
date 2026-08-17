add_library(ApplicationLib
    ApplicationConfigurationUtil.cpp
)

target_include_directories(ApplicationLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
