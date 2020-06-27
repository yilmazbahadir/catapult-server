/**
*** Copyright (c) 2016-present,
*** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#include "FinalizationMessageProcessingService.h"
#include "FinalizationBootstrapperService.h"
#include "finalization/src/FinalizationConfiguration.h"
#include "finalization/src/chain/MultiStepFinalizationMessageAggregator.h"
#include "finalization/src/ionet/FinalizationMessagePacketUtils.h"
#include "catapult/consumers/RecentHashCache.h"
#include "catapult/extensions/DispatcherUtils.h"
#include "catapult/extensions/ServiceState.h"
#include "catapult/extensions/ServiceUtils.h"
#include "catapult/thread/MultiServicePool.h"

namespace catapult { namespace finalization {

	namespace {
		using MessagesSink = consumer<const ionet::FinalizationMessages&>;

		constexpr auto Writers_Service_Name = "fin.writers";

		auto CreateNewMessagesSink(const extensions::ServiceLocator& locator) {
			return extensions::CreatePushEntitySink<MessagesSink>(locator, Writers_Service_Name);
		}

		class FinalizationMessageProcessingServiceRegistrar : public extensions::ServiceRegistrar {
		public:
			explicit FinalizationMessageProcessingServiceRegistrar(const FinalizationConfiguration& config) : m_config(config)
			{}

		public:
			extensions::ServiceRegistrarInfo info() const override {
				return { "FinalizationMessageProcessing", extensions::ServiceRegistrarPhase::Post_Extended_Range_Consumers };
			}

			void registerServiceCounters(extensions::ServiceLocator&) override {
				// no counters
			}

			void registerServices(extensions::ServiceLocator& locator, extensions::ServiceState& state) override {
				auto& multiStepAggregator = GetMultiStepFinalizationMessageAggregator(locator);
				auto& messagePool = *state.pool().pushIsolatedPool("messageProcessing");

				// register hooks
				auto pRecentHashCache = std::make_shared<consumers::SynchronizedRecentHashCache>(
						state.timeSupplier(),
						extensions::CreateHashCheckOptions(m_config.ShortLivedCacheMessageDuration, state.config().Node));

				auto messagesSink = CreateNewMessagesSink(locator);
				auto& hooks = GetFinalizationServerHooks(locator);
				hooks.setMessageRangeConsumer([&multiStepAggregator, &messagePool, pRecentHashCache, messagesSink](auto&& messages) {
					auto newMessages = ionet::FinalizationMessages();
					auto extractedMessages = model::FinalizationMessageRange::ExtractEntitiesFromRange(std::move(messages.Range));

					auto minStepIdentifier = multiStepAggregator.view().minStepIdentifier();
					for (const auto& pMessage : extractedMessages) {
						// ignore messages associated with a different finalization point
						if (minStepIdentifier.Point != pMessage->StepIdentifier.Point)
							continue;

						if (!pRecentHashCache->add(model::CalculateMessageHash(*pMessage)))
							continue;

						messagePool.ioContext().dispatch([&multiStepAggregator, pMessage]() {
							multiStepAggregator.modifier().add(pMessage);
						});
						newMessages.push_back(pMessage);
					}

					if (!newMessages.empty())
						messagesSink(newMessages);
				});
			}

		private:
			FinalizationConfiguration m_config;
		};
	}

	DECLARE_SERVICE_REGISTRAR(FinalizationMessageProcessing)(const FinalizationConfiguration& config) {
		return std::make_unique<FinalizationMessageProcessingServiceRegistrar>(config);
	}
}}