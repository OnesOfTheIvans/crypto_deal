add_library(UserInterfaceLib INTERFACE)

target_include_directories(UserInterfaceLib
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(GraphicalUserInterfaceLib
    graphical/CryptoDealWindow.cpp
    graphical/GraphicalUserInterface.cpp
    graphical/async/AsyncTaskExecutor.cpp
    graphical/async/UiTaskState.cpp
    graphical/models/BalanceCatalog.cpp
    graphical/models/OrderSessionModel.cpp
    graphical/models/PairCatalog.cpp
    graphical/models/SymbolInfoCatalog.cpp
    graphical/pages/AccountsPage.cpp
    graphical/pages/OperationChainsPage.cpp
    graphical/pages/OrdersPage.cpp
    graphical/validation/OrderInputValidation.cpp
    graphical/widgets/DecimalInputField.cpp
    graphical/widgets/OrderConfirmationDialog.cpp
    graphical/widgets/OrderEntryForm.cpp
)

set_target_properties(GraphicalUserInterfaceLib PROPERTIES AUTOMOC ON)

target_link_libraries(GraphicalUserInterfaceLib
    PUBLIC
        DealLib
        OperationChainLib
        UserInterfaceLib
        Qt6::Widgets
)

qt_add_resources(GraphicalUserInterfaceLib graphical_styles
    PREFIX "/styles"
    BASE graphical/resources
    FILES graphical/resources/dark.qss
)
