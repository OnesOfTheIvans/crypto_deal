add_library(UserInterfaceLib INTERFACE)

target_include_directories(UserInterfaceLib
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(GraphicalUserInterfaceLib
    graphical/GraphicalUserInterface.cpp
)

target_link_libraries(GraphicalUserInterfaceLib
    PUBLIC UserInterfaceLib
    PRIVATE DealLib Qt6::Widgets
)
