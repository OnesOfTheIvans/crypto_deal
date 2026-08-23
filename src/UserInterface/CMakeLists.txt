add_library(UserInterfaceLib INTERFACE)

target_include_directories(UserInterfaceLib
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(GraphicalUserInterfaceLib
    graphical/CryptoDealWindow.cpp
    graphical/GraphicalUserInterface.cpp
    graphical/pages/AccountsPage.cpp
    graphical/pages/OperationChainsPage.cpp
    graphical/pages/OrdersPage.cpp
)

target_link_libraries(GraphicalUserInterfaceLib
    PUBLIC
        UserInterfaceLib
        Qt6::Widgets
    PRIVATE DealLib
)

qt_add_resources(GraphicalUserInterfaceLib graphical_styles
    PREFIX "/styles"
    BASE graphical/resources
    FILES graphical/resources/dark.qss
)
