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

using namespace std;
using namespace exception_handling;

namespace {
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
                          return [preset](OperationContext &context)
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                              OrderInfo orderInfo =
                                  service->buyCrypto(preset.outAsset, context.inAsset, context.quantity);

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
                          return [preset](OperationContext &context)
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                              OrderInfo orderInfo =
                                  service->sellCrypto(context.inAsset, preset.outAsset, context.quantity);

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
                          return [preset](OperationContext &context)
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
                          return [preset](OperationContext &context)
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

                              const OrderInfo filledOrder = service->waitUntilOcoOrderFilled(ocoInfo);

                              context.quantity = getReceivedQuantity(filledOrder, preset.side);
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
            return [preset](OperationContext &context)
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
