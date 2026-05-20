#include "OperationFactory.hpp"
#include "DealService.hpp"
#include "common/OrderListQuery.hpp"
#include "common/OrderQuery.hpp"
#include "common/PlaceOcoRequest.hpp"
#include "common/PlaceOrderRequest.hpp"
#include "common/exception_handling.hpp"
#include "type_aliasing.hpp"

// DEBUG
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std;
using namespace exception_handling;

OperationFactory::OperationFactory()
{
    factories.emplace(OperationType::BUY_CRYPTO,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<BaseConfig>(config);
                          return [preset](OperationContext &context) -> OperationContext &
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                              OrderInfo orderInfo =
                                  service->buyCrypto(preset.outAsset, context.inAsset, context.quantity);

                              service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                              OrderQuery orderQuery;
                              orderQuery.symbol = orderInfo.symbol;
                              orderQuery.orderId = orderInfo.orderId;
                              OrderInfo completeOrderInfo = service->getOrder(orderQuery);

                              context.orderId = completeOrderInfo.orderId;
                              context.quantity = completeOrderInfo.executedQty > 0 ? completeOrderInfo.executedQty
                                                                                   : completeOrderInfo.origQty;

                              context.previousInAsset = context.inAsset;
                              context.inAsset = preset.outAsset;
                              context.side = OrderOperation::BUY;

                              return context;
                          };
                      });

    factories.emplace(OperationType::SELL_CRYPTO,
                      [](const Config &config) -> operation
                      {
                          const auto preset = get<BaseConfig>(config);
                          return [preset](OperationContext &context) -> OperationContext &
                          {
                              auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                              OrderInfo orderInfo =
                                  service->sellCrypto(context.inAsset, preset.outAsset, context.quantity);

                              service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                              OrderQuery orderQuery;
                              orderQuery.symbol = orderInfo.symbol;
                              orderQuery.orderId = orderInfo.orderId;
                              OrderInfo completeOrderInfo = service->getOrder(orderQuery);

                              context.orderId = completeOrderInfo.orderId;
                              context.quantity = completeOrderInfo.executedQty > 0 ? completeOrderInfo.executedQty
                                                                                   : completeOrderInfo.origQty;

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
                              throwIf(!preset.side.has_value(), "Side is required for PLACE_ORDER");
                              throwIf(!preset.type.has_value(), "Type is required for PLACE_ORDER");

                              PlaceOrderRequest request;

                              if (preset.side.value() == OrderOperation::BUY)
                              {
                                  request.symbol = preset.outAsset + context.inAsset;
                              }
                              else // SELL
                              {
                                  request.symbol = context.inAsset + preset.outAsset;
                              }

                              request.side = preset.side;
                              request.type = preset.type;
                              request.timeInForce = preset.timeInForce;
                              request.clientOrderId = context.orderId;
                              request.category = "spot";
                              request.triggerPrice = preset.triggerPrice;
                              request.orderFilter = preset.orderFilter;
                              request.marketUnit = preset.marketUnit;
                              request.quantity = context.quantity;
                              request.price = preset.price;
                              OrderInfo orderInfo = service->placeOrder(request);

                              service->waitUntilOrderFilled(orderInfo.symbol, orderInfo.orderId);

                              OrderQuery orderQuery;
                              orderQuery.symbol = orderInfo.symbol;
                              orderQuery.orderId = orderInfo.orderId;
                              OrderInfo completeOrderInfo = service->getOrder(orderQuery);

                              context.orderId = completeOrderInfo.clientOrderId.empty()
                                                    ? completeOrderInfo.orderId
                                                    : completeOrderInfo.clientOrderId;
                              context.quantity = completeOrderInfo.executedQty > 0 ? completeOrderInfo.executedQty
                                                                                   : completeOrderInfo.origQty;

                              context.previousInAsset = context.inAsset;
                              context.inAsset = preset.outAsset;
                              context.side = preset.side.value();

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

                              if (context.side.value() == OrderOperation::BUY)
                              {
                                  request.symbol = context.inAsset + context.previousInAsset;
                              }
                              else if (context.side.value() == OrderOperation::SELL)
                              {
                                  request.symbol = context.previousInAsset + context.inAsset;
                              }

                              request.orderId = context.orderId;
                              service->cancelOrder(request);

                              context.side = std::nullopt;

                              return context;
                          };
                      });

    factories.emplace(
        OperationType::PLACE_OCO,
        [](const Config &config) -> operation
        {
            const auto preset = get<PlaceOcoConfig>(config);
            return [preset](OperationContext &context) -> OperationContext &
            {
                auto &service = context.exchangersPull.getExchanger(context.exchangerType);
                throwIf(!preset.side.has_value(), "Side is required for PLACE_OCO");

                PlaceOcoRequest request;

                if (preset.side.value() == OrderOperation::BUY)
                {
                    request.symbol = preset.outAsset + context.inAsset;
                }
                else // SELL
                {
                    request.symbol = context.inAsset + preset.outAsset;
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

                context.orderId = ocoInfo.listClientOrderId.empty() ? ocoInfo.orderListId : ocoInfo.listClientOrderId;
                context.previousInAsset = context.inAsset;
                context.inAsset = preset.outAsset;
                context.side = preset.side.value();

                // Polling loop to wait for execution
                cout << "Waiting for OCO execution (" << context.orderId.value_or("unknown") << ")..." << endl;
                int retries = 0;
                bool filled = false;
                while (true)
                {
                    if (retries > 60) // 30 seconds
                    {
                        throw runtime_error("Timeout waiting for OCO " + context.orderId.value_or("unknown") +
                                            " to fill");
                    }

                    for (const auto &order : ocoInfo.orders)
                    {
                        try
                        {
                            OrderQuery query;
                            query.symbol = request.symbol;
                            query.orderId = order.orderId;
                            OrderInfo currentInfo = service->getOrder(query);

                            if (currentInfo.status == "Filled")
                            {
                                cout << "OCO Order " << currentInfo.orderId << " filled!" << endl;
                                context.orderId = currentInfo.orderId;
                                context.quantity =
                                    currentInfo.executedQty > 0 ? currentInfo.executedQty : currentInfo.origQty;
                                filled = true;
                                break;
                            }
                            else if (currentInfo.status == "Cancelled" || currentInfo.status == "Rejected" ||
                                     currentInfo.status == "Deactivated" || currentInfo.status == "Expired")
                            {
                                // If one leg is cancelled/rejected, we might want to keep waiting for the other or
                                // fail? Usually OCO means if one cancels, other cancels. But if triggered? For now,
                                // let's focus on "Filled".
                            }
                        }
                        catch (const std::exception &e)
                        {
                            cerr << "Error checking OCO order status: " << e.what() << endl;
                        }
                    }

                    if (filled)
                    {
                        break;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    retries++;
                }

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

                              if (context.side.value() == OrderOperation::BUY)
                              {
                                  request.symbol = context.inAsset + context.previousInAsset;
                              }
                              else if (context.side.value() == OrderOperation::SELL)
                              {
                                  request.symbol = context.previousInAsset + context.inAsset;
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
