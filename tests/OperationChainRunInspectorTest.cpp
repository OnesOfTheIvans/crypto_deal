#include "graphical/widgets/OperationChainRunInspector.hpp"

#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QString>
#include <QTableWidget>
#include <QToolButton>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
        static char applicationName[] = "OperationChainRunInspectorTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    OperationStepSnapshot createStep(size_t index, OperationType type, Config config, OperationStepStatus status)
    {
        OperationStepSnapshot step;
        step.index = index;
        step.type = type;
        step.config = move(config);
        step.status = status;
        return step;
    }

    OperationChainRunSnapshot createDetailedRun()
    {
        OperationChainRunSnapshot run;
        run.runId = 7;
        run.updateSequence = 18;
        run.chainSnapshot.definitionName = "Detailed chain";
        run.chainSnapshot.status = OperationChainStatus::RUNNING;
        run.chainSnapshot.initialContext = {ExchangerType::BINANCE, "USDT", Decimal{100}};
        run.chainSnapshot.currentContext = {ExchangerType::BYBIT, "ETH", Decimal{2}};
        run.chainSnapshot.currentStepIndex = 2;

        run.chainSnapshot.steps.push_back(
            createStep(0, OperationType::BUY_CRYPTO, BaseConfig{"BTC"}, OperationStepStatus::PENDING));
        PlaceOrderConfig marketOrderConfig;
        marketOrderConfig.outAsset = "ETH";
        marketOrderConfig.side = OrderOperation::SELL;
        marketOrderConfig.type = OrderType::MARKET;
        marketOrderConfig.marketUnit = "quoteCoin";
        run.chainSnapshot.steps.push_back(
            createStep(1, OperationType::PLACE_ORDER, marketOrderConfig, OperationStepStatus::RUNNING));

        PlaceOrderConfig orderConfig;
        orderConfig.outAsset = "ETH";
        orderConfig.side = OrderOperation::BUY;
        orderConfig.type = OrderType::LIMIT;
        orderConfig.price = Decimal{2500};
        orderConfig.timeInForce = "GTC";
        orderConfig.triggerPrice = "2400.5";
        orderConfig.orderFilter = "Order";
        OperationStepSnapshot orderStep =
            createStep(2, OperationType::PLACE_ORDER, orderConfig, OperationStepStatus::AWAITING);
        orderStep.inputContext = OperationContextSnapshot{ExchangerType::BINANCE, "USDT", Decimal{5000}};
        orderStep.startedAt = OperationChainTimePoint{chrono::milliseconds{1'725'000'000'000}};
        orderStep.acceptedIdentifiers.orderId = "ordinary-42";
        run.chainSnapshot.steps.push_back(move(orderStep));

        PlaceOcoConfig ocoConfig;
        ocoConfig.outAsset = "USDT";
        ocoConfig.side = OrderOperation::SELL;
        ocoConfig.price = Decimal{3000};
        ocoConfig.stopPrice = Decimal{2200};
        ocoConfig.stopLimitPrice = Decimal{2190};
        ocoConfig.stopLimitTimeInForce = "GTC";
        OperationStepSnapshot ocoStep =
            createStep(3, OperationType::PLACE_OCO, ocoConfig, OperationStepStatus::SUCCEEDED);
        ocoStep.inputContext = OperationContextSnapshot{ExchangerType::BYBIT, "ETH", Decimal{2}};
        ocoStep.outputContext = OperationContextSnapshot{ExchangerType::BYBIT, "USDT", Decimal{6000}};
        ocoStep.startedAt = OperationChainTimePoint{chrono::milliseconds{1'725'000'010'000}};
        ocoStep.finishedAt = OperationChainTimePoint{chrono::milliseconds{1'725'000'020'000}};
        ocoStep.acceptedIdentifiers.ocoGroupId = "oco-group-9";
        ocoStep.acceptedIdentifiers.takeProfitOrderId = "take-profit-10";
        ocoStep.acceptedIdentifiers.stopLossOrderId = "stop-loss-11";
        run.chainSnapshot.steps.push_back(move(ocoStep));

        SendToConfig sendConfig;
        sendConfig.destinationExchanger = ExchangerType::BINANCE;
        sendConfig.chain = "TESTNET";
        sendConfig.address = "test-address";
        OperationStepSnapshot sendStep = createStep(4, OperationType::SEND_TO, sendConfig, OperationStepStatus::FAILED);
        sendStep.error = "Planned transfer failure";
        run.chainSnapshot.steps.push_back(move(sendStep));
        run.chainSnapshot.steps.push_back(
            createStep(5, OperationType::SELL_CRYPTO, BaseConfig{"SOL"}, OperationStepStatus::CANCELLED));
        return run;
    }

    QToolButton *getStepButton(OperationChainRunInspector &inspector, size_t stepIndex)
    {
        const QList<QToolButton *> buttons = inspector.findChildren<QToolButton *>("operationChainStepButton");
        for (QToolButton *button : buttons)
        {
            if (button->property("stepIndex").toULongLong() == stepIndex)
            {
                return button;
            }
        }
        return nullptr;
    }

    QString getDetailValue(const QTableWidget &table, const QString &field)
    {
        for (int row = 0; row < table.rowCount(); ++row)
        {
            if (table.item(row, 0) != nullptr && table.item(row, 0)->text() == field)
            {
                return table.item(row, 1) == nullptr ? QString{} : table.item(row, 1)->text();
            }
        }
        ADD_FAILURE() << "Missing detail field: " << field.toStdString();
        return {};
    }
}

TEST(OperationChainRunInspectorTest, ShowsAllStatesAndTypedStepDetails)
{
    getApplication();
    OperationChainRunInspector inspector;
    inspector.resize(720, 440);
    inspector.setRun(createDetailedRun());
    inspector.show();
    QApplication::processEvents();

    const vector<QString> stateKeys{"pending", "running", "awaiting", "succeeded", "failed", "cancelled"};
    for (size_t index = 0; index < stateKeys.size(); ++index)
    {
        QToolButton *button = getStepButton(inspector, index);
        ASSERT_NE(button, nullptr);
        EXPECT_EQ(button->property("operationChainStepState").toString(), stateKeys[index]);
        EXPECT_TRUE(button->text().contains(QString::number(static_cast<qulonglong>(index + 1))));
    }

    ASSERT_TRUE(inspector.getSelectedRunId().has_value());
    EXPECT_EQ(inspector.getSelectedRunId().value(), OperationChainRunId{7});
    ASSERT_TRUE(inspector.getSelectedStepIndex().has_value());
    EXPECT_EQ(inspector.getSelectedStepIndex().value(), size_t{2});
    EXPECT_TRUE(getStepButton(inspector, 2)->property("operationChainStepCurrent").toBool());
    EXPECT_TRUE(getStepButton(inspector, 2)->property("operationChainStepSelected").toBool());

    auto *details = inspector.findChild<QTableWidget *>("operationChainStepDetailsTable");
    ASSERT_NE(details, nullptr);
    EXPECT_EQ(getDetailValue(*details, "Operation"), QString("Custom order"));
    EXPECT_EQ(getDetailValue(*details, "State"), QString("Awaiting"));
    EXPECT_EQ(getDetailValue(*details, "Configured output asset"), QString("ETH"));
    EXPECT_EQ(getDetailValue(*details, "Side"), QString("Buy"));
    EXPECT_EQ(getDetailValue(*details, "Order type"), QString("Limit"));
    EXPECT_EQ(getDetailValue(*details, "Price"), QString("2500"));
    EXPECT_EQ(getDetailValue(*details, "Time in force"), QString("GTC"));
    EXPECT_EQ(getDetailValue(*details, "Trigger price"), QString("2400.5"));
    EXPECT_EQ(getDetailValue(*details, "Order filter"), QString("Order"));
    EXPECT_EQ(getDetailValue(*details, "Market unit"), QString("Not provided"));
    EXPECT_EQ(getDetailValue(*details, "Input exchange"), QString("Binance"));
    EXPECT_EQ(getDetailValue(*details, "Input quantity"), QString("5000"));
    EXPECT_EQ(getDetailValue(*details, "Result exchange"), QString("Not available"));
    EXPECT_EQ(getDetailValue(*details, "Order ID"), QString("ordinary-42"));
    EXPECT_EQ(getDetailValue(*details, "Finished"), QString("Not finished"));

    QToolButton *marketOrderButton = getStepButton(inspector, 1);
    ASSERT_NE(marketOrderButton, nullptr);
    QTest::mouseClick(marketOrderButton, Qt::LeftButton);
    EXPECT_EQ(getDetailValue(*details, "Order type"), QString("Market"));
    EXPECT_EQ(getDetailValue(*details, "Price"), QString("Not applicable"));
    EXPECT_EQ(getDetailValue(*details, "Time in force"), QString("Not provided"));
    EXPECT_EQ(getDetailValue(*details, "Market unit"), QString("quoteCoin"));

    QToolButton *ocoButton = getStepButton(inspector, 3);
    ASSERT_NE(ocoButton, nullptr);
    QTest::mouseClick(ocoButton, Qt::LeftButton);
    EXPECT_EQ(inspector.getSelectedStepIndex().value(), size_t{3});
    EXPECT_EQ(getDetailValue(*details, "Operation"), QString("Place OCO"));
    EXPECT_EQ(getDetailValue(*details, "Stop-limit price"), QString("2190"));
    EXPECT_EQ(getDetailValue(*details, "OCO group ID"), QString("oco-group-9"));
    EXPECT_EQ(getDetailValue(*details, "Take-profit order ID"), QString("take-profit-10"));
    EXPECT_EQ(getDetailValue(*details, "Stop-loss order ID"), QString("stop-loss-11"));
    EXPECT_EQ(getDetailValue(*details, "Result asset"), QString("USDT"));

    QToolButton *sendButton = getStepButton(inspector, 4);
    ASSERT_NE(sendButton, nullptr);
    QTest::mouseClick(sendButton, Qt::LeftButton);
    EXPECT_EQ(getDetailValue(*details, "Destination exchange"), QString("Binance"));
    EXPECT_EQ(getDetailValue(*details, "Transfer chain"), QString("TESTNET"));
    EXPECT_EQ(getDetailValue(*details, "Destination address"), QString("test-address"));
    EXPECT_EQ(getDetailValue(*details, "Error"), QString("Planned transfer failure"));

    QToolButton *pendingButton = getStepButton(inspector, 0);
    ASSERT_NE(pendingButton, nullptr);
    QTest::mouseClick(pendingButton, Qt::LeftButton);
    EXPECT_EQ(getDetailValue(*details, "Input asset"), QString("Not available"));
    EXPECT_EQ(getDetailValue(*details, "Started"), QString("Not started"));
    EXPECT_EQ(getDetailValue(*details, "Order ID"), QString("Not available"));
    EXPECT_EQ(getDetailValue(*details, "Error"), QString("None"));
}

TEST(OperationChainRunInspectorTest, PreservesExplicitStepSelectionAcrossLiveUpdates)
{
    getApplication();
    OperationChainRunInspector inspector;
    OperationChainRunSnapshot run = createDetailedRun();
    inspector.setRun(run);

    QToolButton *firstStep = getStepButton(inspector, 0);
    ASSERT_NE(firstStep, nullptr);
    QTest::mouseClick(firstStep, Qt::LeftButton);
    ASSERT_TRUE(inspector.getSelectedStepIndex().has_value());
    EXPECT_EQ(inspector.getSelectedStepIndex().value(), size_t{0});

    run.updateSequence = 19;
    run.chainSnapshot.currentStepIndex = 4;
    run.chainSnapshot.steps[2].status = OperationStepStatus::SUCCEEDED;
    run.chainSnapshot.steps[4].status = OperationStepStatus::RUNNING;
    inspector.setRun(run, "This selected run is pinned by the test.");

    EXPECT_EQ(inspector.getSelectedStepIndex().value(), size_t{0});
    EXPECT_TRUE(getStepButton(inspector, 0)->property("operationChainStepSelected").toBool());
    EXPECT_FALSE(getStepButton(inspector, 0)->property("operationChainStepCurrent").toBool());
    EXPECT_TRUE(getStepButton(inspector, 4)->property("operationChainStepCurrent").toBool());
    auto *notice = inspector.findChild<QLabel *>("operationChainRunInspectorNotice");
    ASSERT_NE(notice, nullptr);
    EXPECT_FALSE(notice->isHidden());
    EXPECT_EQ(notice->text(), QString("This selected run is pinned by the test."));

    run.runId = 8;
    run.chainSnapshot.currentStepIndex.reset();
    run.chainSnapshot.steps[4].status = OperationStepStatus::FAILED;
    inspector.setRun(run);
    EXPECT_EQ(inspector.getSelectedRunId().value(), OperationChainRunId{8});
    EXPECT_EQ(inspector.getSelectedStepIndex().value(), size_t{4});
}

TEST(OperationChainRunInspectorTest, KeepsLongProgressionHorizontallyScrollable)
{
    getApplication();
    OperationChainRunSnapshot run;
    run.runId = 12;
    run.chainSnapshot.definitionName = "Long chain";
    run.chainSnapshot.status = OperationChainStatus::PENDING;
    for (size_t index = 0; index < 24; ++index)
    {
        run.chainSnapshot.steps.push_back(
            createStep(index, OperationType::BUY_CRYPTO, BaseConfig{"BTC"}, OperationStepStatus::PENDING));
    }

    OperationChainRunInspector inspector;
    inspector.resize(360, 400);
    inspector.setRun(run);
    inspector.show();
    QApplication::processEvents();

    auto *progress = inspector.findChild<QScrollArea *>("operationChainStepProgressScroll");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(inspector.findChildren<QToolButton *>("operationChainStepButton").size(), 24);
    EXPECT_GT(progress->horizontalScrollBar()->maximum(), 0);
    EXPECT_EQ(progress->verticalScrollBar()->maximum(), 0);
}
