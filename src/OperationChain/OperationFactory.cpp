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

OperationFactory::OperationFactory()
{
    factories.emplace(
        OperationType::BUY_CRYPTO,
        [](const Config &config) -> operation
        {
            const auto preset = get<BaseConfig>(config);
            return [preset](OperationContext &context) -> OperationContext &
            {
                auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                OrderInfo orderInfo = service->buyCrypto(preset.outAsset, context.inAsset, context.quantity);

                OrderInfo completeOrderInfo = service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                context.orderId = completeOrderInfo.orderId;
                context.quantity =
                    completeOrderInfo.executedQty > 0 ? completeOrderInfo.executedQty : completeOrderInfo.origQty;

                context.previousInAsset = context.inAsset;
                context.inAsset = preset.outAsset;
                context.side = OrderOperation::BUY;

                return context;
            };
        });

    factories.emplace(
        OperationType::SELL_CRYPTO,
        [](const Config &config) -> operation
        {
            const auto preset = get<BaseConfig>(config);
            return [preset](OperationContext &context) -> OperationContext &
            {
                auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                OrderInfo orderInfo = service->sellCrypto(context.inAsset, preset.outAsset, context.quantity);

                OrderInfo completeOrderInfo = service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                context.orderId = completeOrderInfo.orderId;
                context.quantity =
                    completeOrderInfo.executedQty > 0 ? completeOrderInfo.executedQty : completeOrderInfo.origQty;

                context.previousInAsset = context.inAsset;
                context.inAsset = preset.outAsset;
                context.side = OrderOperation::SELL;

                return context;
            };
        });

    factories.emplace(OperationType::PLACE_ORDER,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<PlaceOrderConfig>(config);
                          return [preset](OperationContext &context) -> OperationContext &
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
                              request.clientOrderId = context.orderId;
                              request.category = OrderCategory::SPOT;
                              request.triggerPrice = preset.triggerPrice;
                              request.orderFilter = preset.orderFilter;
                              request.marketUnit = preset.marketUnit;
                              request.quantity = context.quantity;
                              request.price = preset.price;
                              OrderInfo orderInfo = service->placeOrder(request);

                              OrderInfo completeOrderInfo =
                                  service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                              context.orderId = completeOrderInfo.clientOrderId.empty()
                                                    ? completeOrderInfo.orderId
                                                    : completeOrderInfo.clientOrderId;
                              context.quantity = completeOrderInfo.executedQty > 0 ? completeOrderInfo.executedQty
                                                                                   : completeOrderInfo.origQty;

                              context.previousInAsset = context.inAsset;
                              context.inAsset = preset.outAsset;
                              context.side = preset.side;

                              return context;
                          };
                      });

    factories.emplace(OperationType::CANCEL_ORDER,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<BaseConfig>(config);
                          return [preset](OperationContext &context) -> OperationContext &
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                              OrderQuery request;

                              throwIf(!context.side.has_value(), "Side is missing or invalid for CANCEL_ORDER");

                              switch (context.side.value())
                              {
                              case OrderOperation::BUY:
                                  request.symbol = context.inAsset + context.previousInAsset;
                                  break;
                              case OrderOperation::SELL:
                                  request.symbol = context.previousInAsset + context.inAsset;
                                  break;
                              }

                              request.orderId = context.orderId;
                              service->cancelOrder(request);

                              context.side = std::nullopt;

                              return context;
                          };
                      });

    factories.emplace(OperationType::PLACE_OCO,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<PlaceOcoConfig>(config);
                          return [preset](OperationContext &context) -> OperationContext &
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

                              context.orderId = filledOrder.orderId;
                              context.quantity =
                                  filledOrder.executedQty > 0 ? filledOrder.executedQty : filledOrder.origQty;
                              context.previousInAsset = context.inAsset;
                              context.inAsset = preset.outAsset;
                              context.side = preset.side;

                              return context;
                          };
                      });

    factories.emplace(OperationType::CANCEL_OCO,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<BaseConfig>(config);
                          return [preset](OperationContext &context) -> OperationContext &
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                              OrderListQuery request;

                              throwIf(!context.side.has_value(), "Side is missing or invalid for CANCEL_OCO");

                              switch (context.side.value())
                              {
                              case OrderOperation::BUY:
                                  request.symbol = context.inAsset + context.previousInAsset;
                                  break;
                              case OrderOperation::SELL:
                                  request.symbol = context.previousInAsset + context.inAsset;
                                  break;
                              }

                              request.listClientOrderId = context.orderId;
                              service->cancelOco(request);

                              context.side = std::nullopt;

                              return context;
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
            return [preset](OperationContext &context) -> OperationContext &
            {
                string targetExchanger = context.exchangerType == ExchangerType::BYBIT ? "Bybit" : "Binance";
                string destinationExchanger = preset.destinationExchanger == ExchangerType::BYBIT ? "Bybit" : "Binance";
                cout << "Crypto currency " << context.inAsset << " in amount " << context.quantity << " had sent from "
                     << targetExchanger << " to the " << destinationExchanger << " by chain " << preset.chain
                     << " to the address " << preset.address << "." << endl;
                context.exchangerType = preset.destinationExchanger;

                context.side = std::nullopt;

                return context;
            };
        });
}

operation OperationFactory::create(const OperationType &type, const Config &config) const
{
    return factories.at(type)(config);
}
