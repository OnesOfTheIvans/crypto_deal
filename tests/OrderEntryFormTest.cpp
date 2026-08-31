#include "graphical/widgets/OrderEntryForm.hpp"
#include "TestDealService.hpp"
#include "graphical/CryptoDealWindow.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/BalanceCatalog.hpp"
#include "graphical/models/OrderSessionModel.hpp"
#include "graphical/models/PairCatalog.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QWidget>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace std::chrono_literals;

namespace {
    QApplication &getApplication()
    {
        auto *existingApplication = qobject_cast<QApplication *>(QApplication::instance());
        if (existingApplication != nullptr)
        {
            return *existingApplication;
        }

        static int argumentCount = 1;
        static char applicationName[] = "OrderEntryFormTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    SymbolInfo createBtcSymbolInfo(const string &symbol)
    {
        SymbolInfo symbolInfo;
        symbolInfo.symbol = symbol;
        symbolInfo.status = "TRADING";
        symbolInfo.baseAsset = "BTC";
        symbolInfo.quoteAsset = "USDT";
        symbolInfo.tickSize = DecimalConverter::parseDecimal("0.01");
        symbolInfo.stepSize = DecimalConverter::parseDecimal("0.001");
        symbolInfo.minQty = DecimalConverter::parseDecimal("0.001");
        symbolInfo.maxQty = DecimalConverter::parseDecimal("10");
        symbolInfo.minPrice = DecimalConverter::parseDecimal("0.01");
        symbolInfo.maxPrice = DecimalConverter::parseDecimal("1000000");
        symbolInfo.minNotional = DecimalConverter::parseDecimal("10");
        symbolInfo.maxNotional = DecimalConverter::parseDecimal("100000");
        symbolInfo.qtyPrecision = 3;
        symbolInfo.pricePrecision = 2;
        return symbolInfo;
    }

    void finishEditing(QLineEdit &input)
    {
        QTest::mouseClick(&input, Qt::LeftButton);
        QTest::keyClick(&input, Qt::Key_Tab);
        QApplication::processEvents();
    }
}

TEST(OrderEntryFormTest, ShowsLimitsAndGatesProceedWithExplicitQuantityAndPriceCorrections)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<TestDealService>(
        ExchangerType::BINANCE,
        []() { return vector<TradablePair>{{"BTCUSDT", "BTC", "USDT"}}; },
        [](const string &symbol) { return createBtcSymbolInfo(symbol); });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel);
    auto *entryFormWidget = window.findChild<QWidget *>("orderEntryForm");
    auto *symbolStatus = window.findChild<QLabel *>("selectedSymbolInfoStatus");
    auto *limits = window.findChild<QLabel *>("orderTradingLimits");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");
    auto *amountError = window.findChild<QLabel *>("orderAmountError");
    auto *amountCorrection = window.findChild<QWidget *>("orderAmountCorrection");
    auto *useAmountCorrection = window.findChild<QPushButton *>("orderAmountUseCorrection");
    auto *dismissAmountCorrection = window.findChild<QPushButton *>("orderAmountDismissCorrection");
    auto *operationSelector = window.findChild<QComboBox *>("orderOperationSelector");
    auto *orderTypeSelector = window.findChild<QComboBox *>("placeOrderTypeSelector");
    auto *priceInput = window.findChild<QLineEdit *>("placeOrderPriceInput");
    auto *priceError = window.findChild<QLabel *>("placeOrderPriceError");
    auto *priceCorrection = window.findChild<QWidget *>("placeOrderPriceCorrection");
    auto *usePriceCorrection = window.findChild<QPushButton *>("placeOrderPriceUseCorrection");
    auto *proceedButton = window.findChild<QPushButton *>("orderProceedButton");

    ASSERT_NE(entryFormWidget, nullptr);
    auto &entryForm = static_cast<OrderEntryForm &>(*entryFormWidget);
    ASSERT_NE(symbolStatus, nullptr);
    ASSERT_NE(limits, nullptr);
    ASSERT_NE(amountInput, nullptr);
    ASSERT_NE(amountError, nullptr);
    ASSERT_NE(amountCorrection, nullptr);
    ASSERT_NE(useAmountCorrection, nullptr);
    ASSERT_NE(dismissAmountCorrection, nullptr);
    ASSERT_NE(operationSelector, nullptr);
    ASSERT_NE(orderTypeSelector, nullptr);
    ASSERT_NE(priceInput, nullptr);
    ASSERT_NE(priceError, nullptr);
    ASSERT_NE(priceCorrection, nullptr);
    ASSERT_NE(usePriceCorrection, nullptr);
    ASSERT_NE(proceedButton, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    balanceCatalog.loadBalances();

    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(), QString("Trading rules are ready for BTCUSDT."), 1000);
    EXPECT_TRUE(limits->text().contains("Amount (BTC): step 0.001 · min 0.001 · max 10"));
    EXPECT_TRUE(limits->text().contains("Price (USDT): tick 0.01 · min 0.01 · max 1000000"));
    EXPECT_TRUE(limits->text().contains("Notional (USDT): min 10 · max 100000"));
    EXPECT_FALSE(proceedButton->isEnabled());
    EXPECT_FALSE(amountError->isVisible());
    EXPECT_FALSE(amountInput->property("validationError").toBool());

    QTest::mouseClick(amountInput, Qt::LeftButton);
    amountInput->setText("12..3");
    EXPECT_EQ(amountInput->text(), QString("12..3"));
    EXPECT_FALSE(amountError->isVisible());
    QTest::keyClick(amountInput, Qt::Key_Tab);
    QApplication::processEvents();
    EXPECT_TRUE(amountError->isVisible());
    EXPECT_EQ(amountError->text(), QString("Use fixed decimal notation, for example 0.25."));
    EXPECT_TRUE(amountInput->property("validationError").toBool());
    EXPECT_FALSE(proceedButton->isEnabled());

    amountInput->setText("0.0025");
    EXPECT_TRUE(amountCorrection->isVisible());
    EXPECT_EQ(useAmountCorrection->text(), QString("Use 0.003"));
    QTest::mouseClick(dismissAmountCorrection, Qt::LeftButton);
    EXPECT_EQ(amountInput->text(), QString("0.0025"));
    EXPECT_FALSE(amountCorrection->isVisible());
    EXPECT_FALSE(proceedButton->isEnabled());

    amountInput->setText("0.0026");
    EXPECT_TRUE(amountCorrection->isVisible());
    QTest::mouseClick(useAmountCorrection, Qt::LeftButton);
    EXPECT_EQ(amountInput->text(), QString("0.003"));
    EXPECT_TRUE(proceedButton->isEnabled());

    amountInput->setText("0.0030");
    EXPECT_EQ(amountError->text(), QString("Amount supports at most 3 decimal places."));
    EXPECT_FALSE(proceedButton->isEnabled());
    amountInput->setText("0.003");

    operationSelector->setCurrentText("Custom order");
    orderTypeSelector->setCurrentText("Limit");
    EXPECT_FALSE(proceedButton->isEnabled());
    EXPECT_FALSE(priceError->isVisible());
    EXPECT_FALSE(priceInput->property("validationError").toBool());

    finishEditing(*priceInput);
    EXPECT_TRUE(priceError->isVisible());
    EXPECT_EQ(priceError->text(), QString("Enter a value."));

    priceInput->setText("3333.335");
    EXPECT_TRUE(priceCorrection->isVisible());
    EXPECT_EQ(usePriceCorrection->text(), QString("Use 3333.34"));
    QTest::mouseClick(usePriceCorrection, Qt::LeftButton);
    EXPECT_EQ(priceInput->text(), QString("3333.34"));
    EXPECT_TRUE(proceedButton->isEnabled());

    const optional<BasicOrderDraft> limitDraft = entryForm.createBasicOrderDraft();
    ASSERT_TRUE(limitDraft.has_value());
    EXPECT_EQ(limitDraft->operation, OperationType::PLACE_ORDER);
    EXPECT_EQ(limitDraft->side, OrderOperation::BUY);
    EXPECT_EQ(limitDraft->type, OrderType::LIMIT);
    EXPECT_EQ(limitDraft->quantityText, "0.003");
    ASSERT_TRUE(limitDraft->price.has_value());
    EXPECT_EQ(limitDraft->price.value(), DecimalConverter::parseDecimal("3333.34"));
    ASSERT_TRUE(limitDraft->timeInForce.has_value());
    EXPECT_EQ(limitDraft->timeInForce.value(), "GTC");

    orderTypeSelector->setCurrentText("Market");
    priceInput->setText("invalid but inactive");
    EXPECT_TRUE(proceedButton->isEnabled());
}

TEST(OrderEntryFormTest, ValidatesOnlyActiveOcoFieldsAndExplicitNotionalLimits)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<TestDealService>(
        ExchangerType::BINANCE,
        []() { return vector<TradablePair>{{"BTCUSDT", "BTC", "USDT"}}; },
        [](const string &symbol) { return createBtcSymbolInfo(symbol); });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel);
    auto *symbolStatus = window.findChild<QLabel *>("selectedSymbolInfoStatus");
    auto *operationSelector = window.findChild<QComboBox *>("orderOperationSelector");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");
    auto *limitPriceInput = window.findChild<QLineEdit *>("placeOcoLimitPriceInput");
    auto *stopPriceInput = window.findChild<QLineEdit *>("placeOcoStopPriceInput");
    auto *useStopLimit = window.findChild<QCheckBox *>("placeOcoUseStopLimit");
    auto *stopLimitPriceInput = window.findChild<QLineEdit *>("placeOcoStopLimitPriceInput");
    auto *formError = window.findChild<QLabel *>("orderFormValidationError");
    auto *proceedButton = window.findChild<QPushButton *>("orderProceedButton");

    ASSERT_NE(symbolStatus, nullptr);
    ASSERT_NE(operationSelector, nullptr);
    ASSERT_NE(amountInput, nullptr);
    ASSERT_NE(limitPriceInput, nullptr);
    ASSERT_NE(stopPriceInput, nullptr);
    ASSERT_NE(useStopLimit, nullptr);
    ASSERT_NE(stopLimitPriceInput, nullptr);
    ASSERT_NE(formError, nullptr);
    ASSERT_NE(proceedButton, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(), QString("Trading rules are ready for BTCUSDT."), 1000);

    operationSelector->setCurrentText("Place OCO");
    amountInput->setText("0.002");
    limitPriceInput->setText("4000");
    stopPriceInput->setText("3900");
    EXPECT_FALSE(formError->isVisible());
    EXPECT_FALSE(proceedButton->isEnabled());

    finishEditing(*amountInput);
    EXPECT_FALSE(formError->isVisible());
    finishEditing(*limitPriceInput);
    EXPECT_EQ(formError->text(), QString("Limit leg: Notional 8 is below the minimum 10."));
    EXPECT_TRUE(formError->isVisible());
    EXPECT_FALSE(proceedButton->isEnabled());

    finishEditing(*stopPriceInput);

    limitPriceInput->setText("5000");
    EXPECT_EQ(formError->text(), QString("Stop leg: Notional 7.8 is below the minimum 10."));
    EXPECT_TRUE(formError->isVisible());
    EXPECT_FALSE(proceedButton->isEnabled());

    stopPriceInput->setText("5000");
    EXPECT_TRUE(proceedButton->isEnabled());

    useStopLimit->setChecked(true);
    EXPECT_FALSE(proceedButton->isEnabled());
    stopLimitPriceInput->setText("4000");
    EXPECT_FALSE(formError->isVisible());

    finishEditing(*stopLimitPriceInput);
    EXPECT_EQ(formError->text(), QString("Stop-limit leg: Notional 8 is below the minimum 10."));
    EXPECT_TRUE(formError->isVisible());
    EXPECT_FALSE(proceedButton->isEnabled());
    stopLimitPriceInput->setText("4900");
    EXPECT_FALSE(proceedButton->isEnabled());
    stopLimitPriceInput->setText("5000");
    EXPECT_TRUE(proceedButton->isEnabled());
}

TEST(OrderEntryFormTest, ShowsExactSymbolFailureAndRetriesWithoutChangingTheSelectedPair)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    atomic<int> attempts = 0;
    auto binanceService = make_shared<TestDealService>(
        ExchangerType::BINANCE,
        []() { return vector<TradablePair>{{"BTCUSDT", "BTC", "USDT"}}; },
        [&attempts](const string &symbol)
        {
            if (++attempts == 1)
            {
                throw runtime_error("temporary symbol failure");
            }
            return createBtcSymbolInfo(symbol);
        });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel);
    auto *symbolStatus = window.findChild<QLabel *>("selectedSymbolInfoStatus");
    auto *retryButton = window.findChild<QPushButton *>("retrySymbolInfoButton");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");

    ASSERT_NE(symbolStatus, nullptr);
    ASSERT_NE(retryButton, nullptr);
    ASSERT_NE(amountInput, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(),
                              QString("Trading rules are unavailable for BTCUSDT: temporary symbol failure"),
                              1000);
    EXPECT_TRUE(retryButton->isVisible());
    EXPECT_FALSE(amountInput->isEnabled());

    QTest::mouseClick(retryButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(), QString("Trading rules are ready for BTCUSDT."), 1000);
    EXPECT_FALSE(retryButton->isVisible());
    EXPECT_TRUE(amountInput->isEnabled());
    EXPECT_EQ(binanceService->getSymbolInfoRequestCount(), 2u);
}

TEST(OrderEntryFormTest, GatesPlacementUntilSelectedBalancesLoadAndRetriesAnExactFailure)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    promise<void> releaseBalancePromise;
    const shared_future<void> releaseBalance = releaseBalancePromise.get_future().share();
    atomic<int> balanceAttempts = 0;
    auto binanceService = make_shared<TestDealService>(
        ExchangerType::BINANCE,
        []() { return vector<TradablePair>{{"BTCUSDT", "BTC", "USDT"}}; },
        [](const string &symbol) { return createBtcSymbolInfo(symbol); },
        [&balanceAttempts, releaseBalance]()
        {
            if (++balanceAttempts == 1)
            {
                releaseBalance.wait_for(2s);
                throw runtime_error("temporary balance failure");
            }
            return BalanceCatalog::BalanceSnapshot{};
        });
    auto bybitService = make_shared<TestDealService>(
        ExchangerType::BYBIT,
        []() { return vector<TradablePair>{{"BTCUSDT", "BTC", "USDT"}}; },
        [](const string &symbol) { return createBtcSymbolInfo(symbol); });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel);
    auto *entryFormWidget = window.findChild<QWidget *>("orderEntryForm");
    auto *exchangeSelector = window.findChild<QComboBox *>("orderExchangeSelector");
    auto *symbolStatus = window.findChild<QLabel *>("selectedSymbolInfoStatus");
    auto *balanceStatus = window.findChild<QLabel *>("selectedBalanceStatus");
    auto *retryBalanceButton = window.findChild<QPushButton *>("retryBalanceButton");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");
    auto *proceedButton = window.findChild<QPushButton *>("orderProceedButton");

    ASSERT_NE(entryFormWidget, nullptr);
    auto &entryForm = static_cast<OrderEntryForm &>(*entryFormWidget);
    ASSERT_NE(exchangeSelector, nullptr);
    ASSERT_NE(symbolStatus, nullptr);
    ASSERT_NE(balanceStatus, nullptr);
    ASSERT_NE(retryBalanceButton, nullptr);
    ASSERT_NE(amountInput, nullptr);
    ASSERT_NE(proceedButton, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    balanceCatalog.loadBalances();

    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(), QString("Trading rules are ready for BTCUSDT."), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(balanceAttempts.load(), 1, 1000);
    EXPECT_EQ(balanceStatus->text(), QString("Loading Binance balances required for placement..."));
    amountInput->setText("0.010");
    EXPECT_FALSE(proceedButton->isEnabled());
    EXPECT_FALSE(entryForm.createBasicOrderDraft().has_value());

    exchangeSelector->setCurrentText("Bybit");
    QTRY_COMPARE_WITH_TIMEOUT(balanceStatus->text(), QString("Bybit balances are ready for placement."), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(proceedButton->isEnabled(), 1000);
    EXPECT_TRUE(entryForm.createBasicOrderDraft().has_value());

    exchangeSelector->setCurrentText("Binance");
    EXPECT_EQ(balanceStatus->text(), QString("Loading Binance balances required for placement..."));
    EXPECT_FALSE(proceedButton->isEnabled());

    releaseBalancePromise.set_value();

    QTRY_COMPARE_WITH_TIMEOUT(balanceStatus->text(),
                              QString("Binance balances are unavailable: temporary balance failure"),
                              1000);
    EXPECT_TRUE(retryBalanceButton->isVisible());
    EXPECT_FALSE(proceedButton->isEnabled());

    QTest::mouseClick(retryBalanceButton, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(balanceStatus->text(), QString("Binance balances are ready for placement."), 1000);
    EXPECT_FALSE(retryBalanceButton->isVisible());
    EXPECT_TRUE(proceedButton->isEnabled());
    EXPECT_TRUE(entryForm.createBasicOrderDraft().has_value());
    EXPECT_EQ(binanceService->getBalanceRequestCount(), 2u);
}
