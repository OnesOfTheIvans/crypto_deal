#include "graphical/CryptoDealWindow.hpp"
#include "graphical/models/PairCatalog.hpp"

#include <QApplication>
#include <QPushButton>
#include <QSize>
#include <QStackedWidget>
#include <QString>
#include <QtTest/QTest>
#include <gtest/gtest.h>

namespace {
    QApplication &getApplication()
    {
        auto *existingApplication = qobject_cast<QApplication *>(QApplication::instance());
        if (existingApplication != nullptr)
        {
            return *existingApplication;
        }

        static int argumentCount = 1;
        static char applicationName[] = "GraphicalUserInterfaceTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }
}

TEST(GraphicalUserInterfaceTest, ShowsOrdersPageByDefault)
{
    getApplication();
    PairCatalog pairCatalog;
    CryptoDealWindow window(pairCatalog);
    window.show();
    QApplication::processEvents();

    auto *pageStack = window.findChild<QStackedWidget *>("primaryPageStack");
    auto *ordersButton = window.findChild<QPushButton *>("ordersNavigationButton");

    ASSERT_NE(pageStack, nullptr);
    ASSERT_NE(ordersButton, nullptr);
    ASSERT_NE(pageStack->currentWidget(), nullptr);
    EXPECT_EQ(pageStack->count(), 3);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("ordersPage"));
    EXPECT_TRUE(ordersButton->isChecked());
}

TEST(GraphicalUserInterfaceTest, SwitchesPagesAndKeepsNavigationSelectionSynchronized)
{
    getApplication();
    PairCatalog pairCatalog;
    CryptoDealWindow window(pairCatalog);
    window.show();
    QApplication::processEvents();

    auto *pageStack = window.findChild<QStackedWidget *>("primaryPageStack");
    auto *ordersButton = window.findChild<QPushButton *>("ordersNavigationButton");
    auto *operationChainsButton = window.findChild<QPushButton *>("operationChainsNavigationButton");
    auto *accountsButton = window.findChild<QPushButton *>("accountsNavigationButton");

    ASSERT_NE(pageStack, nullptr);
    ASSERT_NE(ordersButton, nullptr);
    ASSERT_NE(operationChainsButton, nullptr);
    ASSERT_NE(accountsButton, nullptr);

    QTest::mouseClick(operationChainsButton, Qt::LeftButton);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("operationChainsPage"));
    EXPECT_TRUE(operationChainsButton->isChecked());
    EXPECT_FALSE(ordersButton->isChecked());
    EXPECT_FALSE(accountsButton->isChecked());

    QTest::mouseClick(accountsButton, Qt::LeftButton);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("accountsPage"));
    EXPECT_TRUE(accountsButton->isChecked());
    EXPECT_FALSE(ordersButton->isChecked());
    EXPECT_FALSE(operationChainsButton->isChecked());

    QTest::mouseClick(ordersButton, Qt::LeftButton);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("ordersPage"));
    EXPECT_TRUE(ordersButton->isChecked());
    EXPECT_FALSE(operationChainsButton->isChecked());
    EXPECT_FALSE(accountsButton->isChecked());
}

TEST(GraphicalUserInterfaceTest, UsesApprovedWindowDimensions)
{
    getApplication();
    PairCatalog pairCatalog;
    CryptoDealWindow window(pairCatalog);

    EXPECT_EQ(window.size(), QSize(1180, 760));
    EXPECT_EQ(window.minimumSize(), QSize(960, 640));
}
