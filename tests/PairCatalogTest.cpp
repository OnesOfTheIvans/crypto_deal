#include "graphical/models/PairCatalog.hpp"
#include "TestDealService.hpp"
#include "graphical/CryptoDealWindow.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/models/BalanceCatalog.hpp"
#include "graphical/models/OrderPlacementModel.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"
#include "graphical/widgets/OrderEntryForm.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
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
        static char applicationName[] = "PairCatalogTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    QStringList getComboBoxItems(const QComboBox &comboBox)
    {
        QStringList items;
        for (int index = 0; index < comboBox.count(); ++index)
        {
            items.push_back(comboBox.itemText(index));
        }
        return items;
    }

    class CatalogDealService final : public TestDealService
    {
      public:
        CatalogDealService(ExchangerType exchangerType, function<vector<TradablePair>()> loadPairs)
            : TestDealService(exchangerType, move(loadPairs))
        {}

        size_t getRequestCount() const
        {
            return getTradablePairRequestCount();
        }
    };
}

TEST(PairCatalogTest, KeepsSuccessfulAndFailedExchangeResultsIndependentAndLoadsOnlyOnce)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    const vector<TradablePair> binancePairs = {
        {"BTCUSDT", "BTC", "USDT"},
        {"ETHUSDT", "ETH", "USDT"},
    };
    auto binanceService =
        make_shared<CatalogDealService>(ExchangerType::BINANCE, [binancePairs]() { return binancePairs; });
    auto bybitService = make_shared<CatalogDealService>(ExchangerType::BYBIT,
                                                        []() -> vector<TradablePair>
                                                        { throw runtime_error("Bybit catalog unavailable"); });

    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);
    EXPECT_EQ(pairCatalog.getPairs(ExchangerType::BINANCE), binancePairs);
    EXPECT_TRUE(pairCatalog.getPairs(ExchangerType::BYBIT).empty());
    EXPECT_EQ(pairCatalog.getLoadState(ExchangerType::BYBIT).getError(), QString("Bybit catalog unavailable"));

    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    QApplication::processEvents();

    EXPECT_EQ(binanceService->getRequestCount(), 1u);
    EXPECT_EQ(bybitService->getRequestCount(), 1u);
    EXPECT_EQ(pairCatalog.getPairs(ExchangerType::BINANCE), binancePairs);
    EXPECT_EQ(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(), UiTaskState::Status::FAILED);
}

TEST(PairCatalogTest, StartsBothExchangeLoadsWithoutWaitingForEitherOne)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    promise<void> releasePromise;
    const shared_future<void> release = releasePromise.get_future().share();
    atomic<int> startedRequestCount = 0;
    auto binanceService = make_shared<CatalogDealService>(ExchangerType::BINANCE,
                                                          [&startedRequestCount, release]()
                                                          {
                                                              ++startedRequestCount;
                                                              release.wait();
                                                              return vector<TradablePair>{};
                                                          });
    auto bybitService = make_shared<CatalogDealService>(ExchangerType::BYBIT,
                                                        [&startedRequestCount, release]()
                                                        {
                                                            ++startedRequestCount;
                                                            release.wait();
                                                            return vector<TradablePair>{{"ETHUSDT", "ETH", "USDT"}};
                                                        });

    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    QTRY_COMPARE_WITH_TIMEOUT(startedRequestCount.load(), 2, 1000);
    EXPECT_EQ(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(), UiTaskState::Status::LOADING);
    EXPECT_EQ(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(), UiTaskState::Status::LOADING);
    EXPECT_EQ(taskExecutor.getActiveTaskCount(), 2u);

    releasePromise.set_value();

    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_TRUE(pairCatalog.getPairs(ExchangerType::BINANCE).empty());
    EXPECT_EQ(pairCatalog.getPairs(ExchangerType::BYBIT), (vector<TradablePair>{{"ETHUSDT", "ETH", "USDT"}}));
}

TEST(PairCatalogTest, DeliversIndependentCatalogStatusToOrdersPage)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    promise<void> releasePromise;
    const shared_future<void> release = releasePromise.get_future().share();
    atomic<bool> binanceRequestStarted = false;
    auto binanceService = make_shared<CatalogDealService>(ExchangerType::BINANCE,
                                                          [&binanceRequestStarted, release]()
                                                          {
                                                              binanceRequestStarted = true;
                                                              release.wait_for(2s);
                                                              return vector<TradablePair>{
                                                                  {"BTCUSDT", "BTC", "USDT"},
                                                                  {"ETHUSDT", "ETH", "USDT"},
                                                              };
                                                          });
    auto bybitService =
        make_shared<CatalogDealService>(ExchangerType::BYBIT,
                                        []() -> vector<TradablePair> { throw runtime_error("Bybit status failure"); });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderPlacementModel orderPlacementModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel);
    auto *binanceStatus = window.findChild<QLabel *>("binancePairCatalogStatus");
    auto *bybitStatus = window.findChild<QLabel *>("bybitPairCatalogStatus");

    ASSERT_NE(binanceStatus, nullptr);
    ASSERT_NE(bybitStatus, nullptr);
    EXPECT_EQ(binanceStatus->text(), QString("Binance: Waiting for startup load"));
    EXPECT_EQ(bybitStatus->text(), QString("Bybit: Waiting for startup load"));

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    EXPECT_EQ(binanceStatus->text(), QString("Binance: Loading tradable pairs..."));
    EXPECT_EQ(bybitStatus->text(), QString("Bybit: Loading tradable pairs..."));
    QTRY_VERIFY_WITH_TIMEOUT(binanceRequestStarted.load(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(bybitStatus->text(), QString("Bybit: Unable to load pairs: Bybit status failure"), 1000);

    releasePromise.set_value();

    QTRY_COMPARE_WITH_TIMEOUT(binanceStatus->text(), QString("Binance: 2 tradable pairs available"), 1000);
}

TEST(PairCatalogTest, FiltersPairSelectorsAndKeepsExchangeChoiceStableDuringIndependentLoads)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    promise<void> releaseBinancePromise;
    const shared_future<void> releaseBinance = releaseBinancePromise.get_future().share();
    atomic<bool> binanceRequestStarted = false;
    auto binanceService = make_shared<CatalogDealService>(ExchangerType::BINANCE,
                                                          [&binanceRequestStarted, releaseBinance]()
                                                          {
                                                              binanceRequestStarted = true;
                                                              releaseBinance.wait_for(2s);
                                                              return vector<TradablePair>{
                                                                  {"BTCUSDC", "BTC", "USDC"},
                                                                  {"BTCUSDT", "BTC", "USDT"},
                                                                  {"BTCUSDT_ALT", "BTC", "USDT"},
                                                                  {"ETHBTC", "ETH", "BTC"},
                                                                  {"ETHUSDT", "ETH", "USDT"},
                                                              };
                                                          });
    auto bybitService = make_shared<CatalogDealService>(ExchangerType::BYBIT,
                                                        []()
                                                        {
                                                            return vector<TradablePair>{
                                                                {"SOLUSDC", "SOL", "USDC"},
                                                                {"SOLUSDT", "SOL", "USDT"},
                                                            };
                                                        });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderPlacementModel orderPlacementModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel);
    auto *entryFormWidget = window.findChild<QWidget *>("orderEntryForm");
    auto *exchangeSelector = window.findChild<QComboBox *>("orderExchangeSelector");
    auto *categoryField = window.findChild<QLineEdit *>("orderCategoryField");
    auto *pairControls = window.findChild<QWidget *>("orderPairDependentControls");
    auto *baseAssetSelector = window.findChild<QComboBox *>("orderBaseAssetSelector");
    auto *quoteAssetSelector = window.findChild<QComboBox *>("orderQuoteAssetSelector");
    auto *selectedCatalogStatus = window.findChild<QLabel *>("selectedPairCatalogStatus");

    ASSERT_NE(entryFormWidget, nullptr);
    auto &entryForm = static_cast<OrderEntryForm &>(*entryFormWidget);
    ASSERT_NE(exchangeSelector, nullptr);
    ASSERT_NE(categoryField, nullptr);
    ASSERT_NE(pairControls, nullptr);
    ASSERT_NE(baseAssetSelector, nullptr);
    ASSERT_NE(quoteAssetSelector, nullptr);
    ASSERT_NE(selectedCatalogStatus, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    EXPECT_EQ(exchangeSelector->currentText(), QString("Binance"));
    EXPECT_TRUE(exchangeSelector->isEnabled());
    EXPECT_EQ(categoryField->text(), QString("SPOT"));
    EXPECT_TRUE(categoryField->isReadOnly());
    EXPECT_FALSE(pairControls->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(binanceRequestStarted.load(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);

    EXPECT_EQ(exchangeSelector->currentText(), QString("Binance"));
    EXPECT_FALSE(pairControls->isEnabled());

    exchangeSelector->setCurrentText("Bybit");
    EXPECT_TRUE(pairControls->isEnabled());
    EXPECT_EQ(getComboBoxItems(*baseAssetSelector), QStringList({"SOL"}));
    EXPECT_EQ(getComboBoxItems(*quoteAssetSelector), QStringList({"USDC", "USDT"}));
    EXPECT_EQ(selectedCatalogStatus->text(), QString("Bybit pair selection is ready (2 pairs)."));

    releaseBinancePromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(exchangeSelector->currentText(), QString("Bybit"));

    exchangeSelector->setCurrentText("Binance");
    EXPECT_EQ(getComboBoxItems(*baseAssetSelector), QStringList({"BTC", "ETH"}));
    EXPECT_EQ(baseAssetSelector->currentText(), QString("BTC"));
    EXPECT_EQ(getComboBoxItems(*quoteAssetSelector), QStringList({"USDC", "USDT"}));

    quoteAssetSelector->setCurrentText("USDT");
    ASSERT_TRUE(entryForm.getSelectedPair().has_value());
    EXPECT_EQ(entryForm.getSelectedPair().value().symbol, string("BTCUSDT"));

    baseAssetSelector->setCurrentText("ETH");
    EXPECT_EQ(getComboBoxItems(*quoteAssetSelector), QStringList({"BTC", "USDT"}));
    EXPECT_EQ(quoteAssetSelector->currentText(), QString("USDT"));
    ASSERT_TRUE(entryForm.getSelectedPair().has_value());
    EXPECT_EQ(entryForm.getSelectedPair().value().symbol, string("ETHUSDT"));
}

TEST(PairCatalogTest, DisablesPairFormForEmptyAndFailedSelectedCatalogs)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService =
        make_shared<CatalogDealService>(ExchangerType::BINANCE, []() { return vector<TradablePair>{}; });
    auto bybitService =
        make_shared<CatalogDealService>(ExchangerType::BYBIT,
                                        []() -> vector<TradablePair> { throw runtime_error("Bybit pair failure"); });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderPlacementModel orderPlacementModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel);
    auto *exchangeSelector = window.findChild<QComboBox *>("orderExchangeSelector");
    auto *pairControls = window.findChild<QWidget *>("orderPairDependentControls");
    auto *selectedCatalogStatus = window.findChild<QLabel *>("selectedPairCatalogStatus");

    ASSERT_NE(exchangeSelector, nullptr);
    ASSERT_NE(pairControls, nullptr);
    ASSERT_NE(selectedCatalogStatus, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);
    EXPECT_FALSE(pairControls->isEnabled());
    EXPECT_EQ(selectedCatalogStatus->text(), QString("Binance has no tradable SPOT pairs available."));

    exchangeSelector->setCurrentText("Bybit");
    EXPECT_FALSE(pairControls->isEnabled());
    EXPECT_EQ(selectedCatalogStatus->text(), QString("Bybit pairs are unavailable: Bybit pair failure"));
}

TEST(PairCatalogTest, ShowsOnlyFieldsForTheSelectedPlacementOperation)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<CatalogDealService>(ExchangerType::BINANCE,
                                                          []()
                                                          {
                                                              return vector<TradablePair>{
                                                                  {"BTCUSDT", "BTC", "USDT"},
                                                              };
                                                          });
    auto bybitService = make_shared<CatalogDealService>(ExchangerType::BYBIT, []() { return vector<TradablePair>{}; });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderPlacementModel orderPlacementModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel);
    auto *entryFormWidget = window.findChild<QWidget *>("orderEntryForm");
    auto *operationSelector = window.findChild<QComboBox *>("orderOperationSelector");
    auto *amountLabel = window.findChild<QLabel *>("orderAmountLabel");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");
    auto *operationFormStack = window.findChild<QStackedWidget *>("orderOperationFormStack");
    auto *placeOrderTypeSelector = window.findChild<QComboBox *>("placeOrderTypeSelector");
    auto *placeOrderLimitFields = window.findChild<QWidget *>("placeOrderLimitFields");
    auto *placeOrderPriceLabel = window.findChild<QLabel *>("placeOrderPriceLabel");
    auto *placeOrderTimeInForce = window.findChild<QLineEdit *>("placeOrderTimeInForceField");
    auto *useOcoStopLimit = window.findChild<QCheckBox *>("placeOcoUseStopLimit");
    auto *ocoStopLimitFields = window.findChild<QWidget *>("placeOcoStopLimitFields");
    auto *ocoLimitPriceLabel = window.findChild<QLabel *>("placeOcoLimitPriceLabel");
    auto *ocoStopTimeInForce = window.findChild<QLineEdit *>("placeOcoStopLimitTimeInForceField");

    ASSERT_NE(entryFormWidget, nullptr);
    auto &entryForm = static_cast<OrderEntryForm &>(*entryFormWidget);
    ASSERT_NE(operationSelector, nullptr);
    ASSERT_NE(amountLabel, nullptr);
    ASSERT_NE(amountInput, nullptr);
    ASSERT_NE(operationFormStack, nullptr);
    ASSERT_NE(placeOrderTypeSelector, nullptr);
    ASSERT_NE(placeOrderLimitFields, nullptr);
    ASSERT_NE(placeOrderPriceLabel, nullptr);
    ASSERT_NE(placeOrderTimeInForce, nullptr);
    ASSERT_NE(useOcoStopLimit, nullptr);
    ASSERT_NE(ocoStopLimitFields, nullptr);
    ASSERT_NE(ocoLimitPriceLabel, nullptr);
    ASSERT_NE(ocoStopTimeInForce, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(getComboBoxItems(*operationSelector),
              QStringList({"Buy crypto", "Sell crypto", "Custom order", "Place OCO"}));
    EXPECT_EQ(operationFormStack->currentWidget()->objectName(), QString("buyCryptoFields"));
    EXPECT_EQ(entryForm.getSelectedOperation(), OperationType::BUY_CRYPTO);
    EXPECT_EQ(amountLabel->text(), QString("Amount (BTC)"));
    EXPECT_EQ(amountInput->validator(), nullptr);
    amountInput->setText("12..3");
    EXPECT_EQ(amountInput->text(), QString("12..3"));

    operationSelector->setCurrentText("Sell crypto");
    EXPECT_EQ(operationFormStack->currentWidget()->objectName(), QString("sellCryptoFields"));
    EXPECT_EQ(entryForm.getSelectedOperation(), OperationType::SELL_CRYPTO);

    operationSelector->setCurrentText("Custom order");
    EXPECT_EQ(operationFormStack->currentWidget()->objectName(), QString("placeOrderFields"));
    EXPECT_EQ(entryForm.getSelectedOperation(), OperationType::PLACE_ORDER);
    EXPECT_FALSE(placeOrderLimitFields->isVisible());

    placeOrderTypeSelector->setCurrentText("Limit");
    EXPECT_TRUE(placeOrderLimitFields->isVisible());
    EXPECT_EQ(placeOrderPriceLabel->text(), QString("Price (USDT per BTC)"));
    EXPECT_EQ(placeOrderTimeInForce->text(), QString("GTC"));
    EXPECT_TRUE(placeOrderTimeInForce->isReadOnly());

    operationSelector->setCurrentText("Place OCO");
    EXPECT_EQ(operationFormStack->currentWidget()->objectName(), QString("placeOcoFields"));
    EXPECT_EQ(entryForm.getSelectedOperation(), OperationType::PLACE_OCO);
    EXPECT_EQ(ocoLimitPriceLabel->text(), QString("Limit price (USDT per BTC)"));
    EXPECT_FALSE(ocoStopLimitFields->isVisible());

    useOcoStopLimit->setChecked(true);
    EXPECT_TRUE(ocoStopLimitFields->isVisible());
    EXPECT_EQ(ocoStopTimeInForce->text(), QString("GTC"));
    EXPECT_TRUE(ocoStopTimeInForce->isReadOnly());
}
