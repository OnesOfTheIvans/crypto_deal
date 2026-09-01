#include "OperationFactory.hpp"
#include "DealService.hpp"
#include "common/domain/OrderListQuery.hpp"
#include "common/domain/OrderQuery.hpp"
#include "common/domain/PlaceOcoRequest.hpp"
#include "common/domain/PlaceOrderRequest.hpp"
#include "common/exception_handling.hpp"
#include "type_aliasing.hpp"

// DEBUG
#include <iostream>
#include <optional>
#include <string>
#include <utility>

using namespace std;
using namespace exception_handling;

namespace {
    optional<string> getIdentifierIfPresent(const string &identifier)
    {
        return identifier.empty() ? nullopt : optional<string>(identifier);
    }

    OperationAcceptedIdentifiers createAcceptedIdentifiers(const OrderInfo &orderInfo)
    {
        OperationAcceptedIdentifiers identifiers;
        identifiers.orderId = getIdentifierIfPresent(orderInfo.orderId);
        return identifiers;
    }

    OperationAcceptedIdentifiers createAcceptedIdentifiers(const OcoInfo &ocoInfo)
    {
        OperationAcceptedIdentifiers identifiers;
        identifiers.ocoGroupId = getIdentifierIfPresent(ocoInfo.orderListId);
        identifiers.takeProfitOrderId = getIdentifierIfPresent(ocoInfo.takeProfitOrder.orderId);
        identifiers.stopLossOrderId = getIdentifierIfPresent(ocoInfo.stopLossOrder.orderId);
        return identifiers;
    }

    void reportAwaiting(const OperationProgressHandler &progressHandler, OperationAcceptedIdentifiers identifiers)
    {
        if (progressHandler)
        {
            progressHandler(move(identifiers));
        }
    }

    Decimal getReceivedQuantity(const OrderInfo &orderInfo, OrderOperation side)
    {
        if (side == OrderOperation::SELL)
        {
            return orderInfo.cumQuoteQty;
        }

        return orderInfo.executedQty > 0 ? orderInfo.executedQty : orderInfo.origQty;
    }
}

OperationFactory::OperationFactory()
{
    factories.emplace(OperationType::BUY_CRYPTO,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<BaseConfig>(config);
                          return [preset](OperationContext &context, const OperationProgressHandler &progressHandler)
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                              OrderInfo orderInfo =
                                  service->buyCrypto(preset.outAsset, context.inAsset, context.quantity);
                              reportAwaiting(progressHandler, createAcceptedIdentifiers(orderInfo));

                              OrderInfo completeOrderInfo =
                                  service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                              context.quantity = getReceivedQuantity(completeOrderInfo, OrderOperation::BUY);

                              context.inAsset = preset.outAsset;
                          };
                      });

    factories.emplace(OperationType::SELL_CRYPTO,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<BaseConfig>(config);
                          return [preset](OperationContext &context, const OperationProgressHandler &progressHandler)
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                              OrderInfo orderInfo =
                                  service->sellCrypto(context.inAsset, preset.outAsset, context.quantity);
                              reportAwaiting(progressHandler, createAcceptedIdentifiers(orderInfo));

                              OrderInfo completeOrderInfo =
                                  service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                              context.quantity = getReceivedQuantity(completeOrderInfo, OrderOperation::SELL);

                              context.inAsset = preset.outAsset;
                          };
                      });

    factories.emplace(OperationType::PLACE_ORDER,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<PlaceOrderConfig>(config);
                          return [preset](OperationContext &context, const OperationProgressHandler &progressHandler)
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);

                              PlaceOrderRequest request;

                              switch (preset.side)
                              {
                              case OrderOperation::BUY:
                                  request.symbol = preset.outAsset + context.inAsset;
                                  break;
                              case OrderOperation::SELL:
                                  request.symbol = context.inAsset + preset.outAsset;
                                  break;
                              }

                              request.side = preset.side;
                              request.type = preset.type;
                              request.timeInForce = preset.timeInForce;
                              request.category = OrderCategory::SPOT;
                              request.triggerPrice = preset.triggerPrice;
                              request.orderFilter = preset.orderFilter;
                              request.marketUnit = preset.marketUnit;
                              request.quantity = context.quantity;
                              request.price = preset.price;
                              OrderInfo orderInfo = service->placeOrder(request);
                              reportAwaiting(progressHandler, createAcceptedIdentifiers(orderInfo));

                              OrderInfo completeOrderInfo =
                                  service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                              context.quantity = getReceivedQuantity(completeOrderInfo, preset.side);

                              context.inAsset = preset.outAsset;
                          };
                      });

    factories.emplace(OperationType::PLACE_OCO,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<PlaceOcoConfig>(config);
                          return [preset](OperationContext &context, const OperationProgressHandler &progressHandler)
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);

                              PlaceOcoRequest request;

                              switch (preset.side)
                              {
                              case OrderOperation::BUY:
                                  request.symbol = preset.outAsset + context.inAsset;
                                  break;
                              case OrderOperation::SELL:
                                  request.symbol = context.inAsset + preset.outAsset;
                                  break;
                              }

                              request.side = preset.side;
                              request.quantity = context.quantity;
                              request.price = preset.price;
                              request.stopPrice = preset.stopPrice;
                              request.stopLimitPrice = preset.stopLimitPrice;
                              request.stopLimitTimeInForce = preset.stopLimitTimeInForce;
                              request.listClientOrderId = preset.listClientOrderId;
                              request.limitClientOrderId = preset.limitClientOrderId;
                              request.stopClientOrderId = preset.stopClientOrderId;
                              OcoInfo ocoInfo = service->placeOco(request);
                              reportAwaiting(progressHandler, createAcceptedIdentifiers(ocoInfo));

                              const OcoWaitResult result = service->waitUntilOcoOrderFilled(ocoInfo);

                              context.quantity = getReceivedQuantity(result.filledOrder, preset.side);
                              context.inAsset = preset.outAsset;
                          };
                      });

    /*
     * This operation is a simulation of sending crypto to another exchanger.
     * Real sending is impossible with testnet accounts.
     */
    factories.emplace(
        OperationType::SEND_TO,
        [](const Config &config) -> operation
        {
            const auto preset = get<SendToConfig>(config);
            return [preset](OperationContext &context, const OperationProgressHandler &)
            {
                string targetExchanger = context.exchangerType == ExchangerType::BYBIT ? "Bybit" : "Binance";
                string destinationExchanger = preset.destinationExchanger == ExchangerType::BYBIT ? "Bybit" : "Binance";
                cout << "Crypto currency " << context.inAsset << " in amount " << context.quantity << " had sent from "
                     << targetExchanger << " to the " << destinationExchanger << " by chain " << preset.chain
                     << " to the address " << preset.address << "." << endl;
                context.exchangerType = preset.destinationExchanger;
            };
        });
}

operation OperationFactory::create(const OperationType &type, const Config &config) const
{
    return factories.at(type)(config);
}
