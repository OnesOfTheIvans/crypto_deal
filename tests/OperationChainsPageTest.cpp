#include "graphical/pages/OperationChainsPage.hpp"
#include "OperationChainRunManager.hpp"
#include "TestDealService.hpp"
#include "common/OrderWaitInterrupted.hpp"
#include "graphical/models/OperationChainRunModel.hpp"

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

using namespace std;

namespace {
    QApplication &getApplication()
    {
        auto *existingApplication = qobject_cast<QApplication *>(QApplication::instance());
        if (existingApplication != nullptr)
        {
            return *existingApplication;
        }

        static int argumentCount = 1;
        static char applicationName[] = "OperationChainsPageTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    OperationChainDefinition createBuyDefinition(string name, ExchangerType exchangerType = ExchangerType::BINANCE)
    {
        return OperationChainDefinition(move(name),
                                        exchangerType,
                                        "USDT",
                                        Decimal{1},
                                        {{OperationType::BUY_CRYPTO, BaseConfig{"BTC"}}});
    }

    class ControllableChainDealService final : public TestDealService
    {
      private:
        mutable mutex stateMutex;
        condition_variable_any stateChanged;
        bool orderWaitReleased = false;
        bool cancellationReleased = false;
        bool cancellationFails = false;
        atomic<size_t> nextOrderId{1};
        atomic<bool> orderWaitStarted{false};
        atomic<bool> cancellationStarted{false};

      public:
        explicit ControllableChainDealService(ExchangerType exchangerType) : TestDealService(exchangerType) {}

        OrderInfo buyCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity) override
        {
            OrderInfo order;
            order.symbol = baseAsset + quoteAsset;
            order.orderId = "chain-ui-order-" + to_string(nextOrderId.fetch_add(1));
            order.origQty = quantity;
            order.status = "NEW";
            return order;
        }

        OrderInfo waitUntilOrderFilled(const string &symbol, const string &orderId) override
        {
            return waitUntilOrderFilled(symbol, orderId, {});
        }

        OrderInfo waitUntilOrderFilled(const string &symbol, const string &orderId, stop_token stopToken) override
        {
            orderWaitStarted.store(true);
            stateChanged.notify_all();
            unique_lock<mutex> lock(stateMutex);
            const bool released = stateChanged.wait(lock, stopToken, [this]() { return orderWaitReleased; });
            if (!released)
            {
                throw OrderWaitInterrupted("Test chain wait was interrupted");
            }

            OrderInfo order;
            order.symbol = symbol;
            order.orderId = orderId;
            order.origQty = Decimal{1};
            order.executedQty = Decimal{1};
            order.status = "FILLED";
            return order;
        }

        OrderInfo cancelOrderAndWaitUntilTerminal(const OrderQuery &query) override
        {
            return cancelOrderAndWaitUntilTerminal(query, {});
        }

        OrderInfo cancelOrderAndWaitUntilTerminal(const OrderQuery &query, stop_token stopToken) override
        {
            cancellationStarted.store(true);
            stateChanged.notify_all();
            unique_lock<mutex> lock(stateMutex);
            const bool released = stateChanged.wait(lock, stopToken, [this]() { return cancellationReleased; });
            if (!released)
            {
                throw OrderWaitInterrupted("Test cancellation was interrupted");
            }
            if (cancellationFails)
            {
                throw runtime_error("Planned chain cancellation failure");
            }

            OrderInfo order;
            order.symbol = query.symbol;
            order.orderId = query.orderId.has_value() ? query.orderId.value() : "";
            order.origQty = Decimal{1};
            order.status = getExchangerType() == ExchangerType::BINANCE ? "CANCELED" : "Cancelled";
            return order;
        }

        bool hasOrderWaitStarted() const
        {
            return orderWaitStarted.load();
        }

        bool hasCancellationStarted() const
        {
            return cancellationStarted.load();
        }

        void allowCancellationToFinish(bool fails = false)
        {
            {
                lock_guard<mutex> lock(stateMutex);
                cancellationFails = fails;
                cancellationReleased = true;
            }
            stateChanged.notify_all();
        }

        void releaseAllWork()
        {
            {
                lock_guard<mutex> lock(stateMutex);
                orderWaitReleased = true;
                cancellationReleased = true;
            }
            stateChanged.notify_all();
        }
    };

    class WorkReleaseGuard
    {
      private:
        shared_ptr<ControllableChainDealService> service;

      public:
        explicit WorkReleaseGuard(shared_ptr<ControllableChainDealService> service) : service(move(service)) {}

        ~WorkReleaseGuard()
        {
            service->releaseAllWork();
        }
    };

    void clickConfirmationButton(QPushButton &trigger,
                                 const QString &dialogObjectName,
                                 const QString &buttonObjectName,
                                 const QString &defaultButtonObjectName)
    {
        bool handled = false;
        QTimer::singleShot(0,
                           [&handled, &dialogObjectName, &buttonObjectName, &defaultButtonObjectName]()
                           {
                               auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               EXPECT_NE(dialog, nullptr);
                               if (dialog == nullptr)
                               {
                                   return;
                               }

                               EXPECT_EQ(dialog->objectName(), dialogObjectName);
                               ASSERT_NE(dialog->defaultButton(), nullptr);
                               EXPECT_EQ(dialog->defaultButton()->objectName(), defaultButtonObjectName);
                               auto *button = dialog->findChild<QPushButton *>(buttonObjectName);
                               ASSERT_NE(button, nullptr);
                               handled = true;
                               QTest::mouseClick(button, Qt::LeftButton);
                           });
        QTest::mouseClick(&trigger, Qt::LeftButton);
        EXPECT_TRUE(handled);
    }

    void selectRunFilter(QListWidget &filterList, const QString &label)
    {
        auto *filterToggle = filterList.window()->findChild<QToolButton *>("operationChainRunFilterToggle");
        ASSERT_NE(filterToggle, nullptr);
        if (!filterToggle->isChecked())
        {
            QTest::mouseClick(filterToggle, Qt::LeftButton);
            QApplication::processEvents();
        }

        const QList<QListWidgetItem *> items = filterList.findItems(label, Qt::MatchExactly);
        ASSERT_EQ(items.size(), 1);
        const QRect itemRectangle = filterList.visualItemRect(items.front());
        ASSERT_TRUE(itemRectangle.isValid());
        QTest::mouseClick(filterList.viewport(), Qt::LeftButton, Qt::NoModifier, itemRectangle.center());
        QApplication::processEvents();
        EXPECT_FALSE(filterToggle->isChecked());
        EXPECT_TRUE(filterList.isHidden());
        EXPECT_EQ(filterToggle->text(), "Filter: " + label);
        EXPECT_EQ(filterToggle->arrowType(), Qt::RightArrow);
    }
}

TEST(OperationChainsPageTest, ShowsSeparateEmptyDefinitionAndSessionRunStates)
{
    getApplication();
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    OperationChainRunManager manager({}, binanceService, bybitService);
    OperationChainRunModel model(manager);
    OperationChainsPage page(model);
    page.resize(728, 640);
    page.show();
    QApplication::processEvents();

    auto *definitionsTable = page.findChild<QTableWidget *>("operationChainDefinitionsTable");
    auto *runsTable = page.findChild<QTableWidget *>("operationChainRunsTable");
    auto *definitionsEmptyState = page.findChild<QLabel *>("operationChainDefinitionsEmptyState");
    auto *runsEmptyState = page.findChild<QLabel *>("operationChainRunsEmptyState");
    auto *filterToggle = page.findChild<QToolButton *>("operationChainRunFilterToggle");
    auto *filterList = page.findChild<QListWidget *>("operationChainRunFilterList");

    ASSERT_NE(definitionsTable, nullptr);
    ASSERT_NE(runsTable, nullptr);
    ASSERT_NE(definitionsEmptyState, nullptr);
    ASSERT_NE(runsEmptyState, nullptr);
    ASSERT_NE(filterToggle, nullptr);
    ASSERT_NE(filterList, nullptr);
    ASSERT_EQ(filterList->count(), 6);
    ASSERT_NE(filterList->currentItem(), nullptr);
    EXPECT_EQ(filterList->currentItem()->text(), QString("Active"));
    EXPECT_EQ(filterList->item(0)->text(), QString("Active"));
    EXPECT_EQ(filterList->item(1)->text(), QString("Completed"));
    EXPECT_EQ(filterList->item(2)->text(), QString("Failed"));
    EXPECT_EQ(filterList->item(3)->text(), QString("Cancelled"));
    EXPECT_EQ(filterList->item(4)->text(), QString("Non-active"));
    EXPECT_EQ(filterList->item(5)->text(), QString("All"));
    EXPECT_FALSE(filterToggle->isChecked());
    EXPECT_EQ(filterToggle->text(), QString("Filter: Active"));
    EXPECT_EQ(filterToggle->arrowType(), Qt::RightArrow);
    EXPECT_TRUE(filterList->isHidden());

    QTest::mouseClick(filterToggle, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_TRUE(filterToggle->isChecked());
    EXPECT_EQ(filterToggle->arrowType(), Qt::DownArrow);
    EXPECT_FALSE(filterList->isHidden());
    EXPECT_LE(filterList->visualItemRect(filterList->item(5)).bottom(), filterList->viewport()->rect().bottom());

    QTest::mouseClick(filterToggle, Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_FALSE(filterToggle->isChecked());
    EXPECT_EQ(filterToggle->arrowType(), Qt::RightArrow);
    EXPECT_TRUE(filterList->isHidden());
    EXPECT_TRUE(definitionsTable->isHidden());
    EXPECT_TRUE(runsTable->isHidden());
    EXPECT_FALSE(definitionsEmptyState->isHidden());
    EXPECT_FALSE(runsEmptyState->isHidden());
    EXPECT_EQ(runsEmptyState->text(), QString("No active operation-chain runs."));

    selectRunFilter(*filterList, "All");
    EXPECT_EQ(runsEmptyState->text(), QString("No operation-chain runs have been started this session."));
}

TEST(OperationChainsPageTest, ShowsDefinitionContextAndCancelDefaultStartConfirmation)
{
    getApplication();
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Approved chain")}, binanceService, bybitService);
    OperationChainRunModel model(manager);
    OperationChainsPage page(model);
    page.show();
    QApplication::processEvents();

    auto *definitionsTable = page.findChild<QTableWidget *>("operationChainDefinitionsTable");
    auto *startButton = page.findChild<QPushButton *>("startOperationChainButton");
    ASSERT_NE(definitionsTable, nullptr);
    ASSERT_NE(startButton, nullptr);
    ASSERT_EQ(definitionsTable->rowCount(), 1);
    EXPECT_EQ(definitionsTable->item(0, 0)->text(), QString("Approved chain"));
    EXPECT_EQ(definitionsTable->item(0, 1)->text(), QString("Binance"));
    EXPECT_EQ(definitionsTable->item(0, 2)->text(), QString("USDT"));
    EXPECT_EQ(definitionsTable->item(0, 3)->text(), QString("1"));
    EXPECT_EQ(definitionsTable->item(0, 4)->text(), QString("1"));

    clickConfirmationButton(*startButton,
                            "startOperationChainConfirmationDialog",
                            "keepOperationChainIdleButton",
                            "keepOperationChainIdleButton");
    EXPECT_TRUE(model.getRuns().empty());
}

TEST(OperationChainsPageTest, StartsRepeatedRunsAndShowsMostRecentlyUpdatedHistory)
{
    getApplication();
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Repeatable chain")}, binanceService, bybitService);
    OperationChainRunModel model(manager);
    OperationChainsPage page(model);
    page.show();
    QApplication::processEvents();

    auto *startButton = page.findChild<QPushButton *>("startOperationChainButton");
    auto *runsTable = page.findChild<QTableWidget *>("operationChainRunsTable");
    auto *filterList = page.findChild<QListWidget *>("operationChainRunFilterList");
    ASSERT_NE(startButton, nullptr);
    ASSERT_NE(runsTable, nullptr);
    ASSERT_NE(filterList, nullptr);
    selectRunFilter(*filterList, "All");

    clickConfirmationButton(*startButton,
                            "startOperationChainConfirmationDialog",
                            "confirmOperationChainStartButton",
                            "keepOperationChainIdleButton");
    clickConfirmationButton(*startButton,
                            "startOperationChainConfirmationDialog",
                            "confirmOperationChainStartButton",
                            "keepOperationChainIdleButton");

    QTRY_COMPARE(model.getRuns().size(), size_t{2});
    QTRY_COMPARE(runsTable->rowCount(), 2);
    QTRY_COMPARE(runsTable->item(0, 1)->text(), QString("Completed"));
    QTRY_COMPARE(runsTable->item(1, 1)->text(), QString("Completed"));
    const vector<OperationChainRunSnapshot> runs = model.getRuns(OperationChainRunFilter::ALL);
    ASSERT_EQ(runs.size(), size_t{2});
    EXPECT_TRUE(runsTable->item(0, 0)->text().contains("Run #" + QString::number(runs[0].runId)));
    EXPECT_TRUE(runsTable->item(1, 0)->text().contains("Run #" + QString::number(runs[1].runId)));
    EXPECT_EQ(runsTable->cellWidget(0, 7), nullptr);
    EXPECT_EQ(runsTable->cellWidget(1, 7), nullptr);
    EXPECT_EQ(filterList->currentItem()->text(), QString("All"));
}

TEST(OperationChainsPageTest, ShowsStoppingStateAndConfirmedCancellationWithoutInventingTerminalState)
{
    getApplication();
    auto binanceService = make_shared<ControllableChainDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Cancellable chain")}, binanceService, bybitService);
    WorkReleaseGuard releaseGuard(binanceService);
    OperationChainRunModel model(manager);
    OperationChainsPage page(model);
    page.show();
    QApplication::processEvents();

    auto *startButton = page.findChild<QPushButton *>("startOperationChainButton");
    auto *runsTable = page.findChild<QTableWidget *>("operationChainRunsTable");
    ASSERT_NE(startButton, nullptr);
    ASSERT_NE(runsTable, nullptr);
    clickConfirmationButton(*startButton,
                            "startOperationChainConfirmationDialog",
                            "confirmOperationChainStartButton",
                            "keepOperationChainIdleButton");
    QTRY_VERIFY(binanceService->hasOrderWaitStarted());

    auto *cancelButton = qobject_cast<QPushButton *>(runsTable->cellWidget(0, 7));
    ASSERT_NE(cancelButton, nullptr);
    clickConfirmationButton(*cancelButton,
                            "cancelOperationChainRunConfirmationDialog",
                            "keepOperationChainRunningButton",
                            "keepOperationChainRunningButton");
    ASSERT_TRUE(model.getRun(1).has_value());
    EXPECT_FALSE(model.getRun(1)->cancellationRequested);

    cancelButton = qobject_cast<QPushButton *>(runsTable->cellWidget(0, 7));
    ASSERT_NE(cancelButton, nullptr);
    clickConfirmationButton(*cancelButton,
                            "cancelOperationChainRunConfirmationDialog",
                            "confirmOperationChainRunCancellationButton",
                            "keepOperationChainRunningButton");
    QTRY_VERIFY(binanceService->hasCancellationStarted());
    QTRY_VERIFY(model.getRun(1).has_value() && model.getRun(1)->cancellationRequested);
    QTRY_VERIFY(runsTable->item(0, 1)->text().contains("Cancellation requested"));
    cancelButton = qobject_cast<QPushButton *>(runsTable->cellWidget(0, 7));
    ASSERT_NE(cancelButton, nullptr);
    EXPECT_FALSE(cancelButton->isEnabled());
    EXPECT_EQ(cancelButton->text(), QString("Stopping..."));

    binanceService->allowCancellationToFinish();
    QTRY_COMPARE(model.getRun(1)->chainSnapshot.status, OperationChainStatus::CANCELLED);
    QTRY_COMPARE(runsTable->rowCount(), 0);

    auto *filterList = page.findChild<QListWidget *>("operationChainRunFilterList");
    ASSERT_NE(filterList, nullptr);
    selectRunFilter(*filterList, "Cancelled");
    QTRY_COMPARE(runsTable->rowCount(), 1);
    QTRY_COMPARE(runsTable->item(0, 1)->text(), QString("Cancelled"));
    EXPECT_EQ(runsTable->cellWidget(0, 7), nullptr);
}

TEST(OperationChainsPageTest, PreservesExactCancellationFailureAndReportsSynchronousActionErrors)
{
    getApplication();
    auto binanceService = make_shared<ControllableChainDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Failing cancellation")}, binanceService, bybitService);
    WorkReleaseGuard releaseGuard(binanceService);
    OperationChainRunModel model(manager);
    OperationChainsPage page(model);
    page.show();
    QApplication::processEvents();

    EXPECT_FALSE(model.startRun("Missing definition"));
    EXPECT_TRUE(model.getActionError().contains("Unknown operation-chain definition: Missing definition"));
    auto *actionError = page.findChild<QLabel *>("operationChainActionError");
    ASSERT_NE(actionError, nullptr);
    EXPECT_TRUE(actionError->text().contains("Unknown operation-chain definition: Missing definition"));

    auto *startButton = page.findChild<QPushButton *>("startOperationChainButton");
    ASSERT_NE(startButton, nullptr);
    clickConfirmationButton(*startButton,
                            "startOperationChainConfirmationDialog",
                            "confirmOperationChainStartButton",
                            "keepOperationChainIdleButton");
    QTRY_VERIFY(binanceService->hasOrderWaitStarted());
    EXPECT_TRUE(model.getActionError().isEmpty());

    auto *runsTable = page.findChild<QTableWidget *>("operationChainRunsTable");
    ASSERT_NE(runsTable, nullptr);
    auto *cancelButton = qobject_cast<QPushButton *>(runsTable->cellWidget(0, 7));
    ASSERT_NE(cancelButton, nullptr);
    clickConfirmationButton(*cancelButton,
                            "cancelOperationChainRunConfirmationDialog",
                            "confirmOperationChainRunCancellationButton",
                            "keepOperationChainRunningButton");
    QTRY_VERIFY(binanceService->hasCancellationStarted());
    binanceService->allowCancellationToFinish(true);

    QTRY_COMPARE(model.getRun(1)->chainSnapshot.status, OperationChainStatus::FAILED);
    EXPECT_EQ(model.getRun(1)->chainSnapshot.error, "Planned chain cancellation failure");
    QTRY_COMPARE(runsTable->rowCount(), 0);

    auto *filterList = page.findChild<QListWidget *>("operationChainRunFilterList");
    ASSERT_NE(filterList, nullptr);
    selectRunFilter(*filterList, "Failed");
    QTRY_COMPARE(runsTable->rowCount(), 1);
    QTRY_COMPARE(runsTable->item(0, 6)->text(), QString("Planned chain cancellation failure"));
    EXPECT_EQ(runsTable->cellWidget(0, 7), nullptr);
}
