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

#include "FinalizationBootstrapperService.h"
#include "finalization/src/chain/MultiStepFinalizationMessageAggregator.h"
#include "finalization/src/io/ProofStorageCache.h"
#include "finalization/src/model/FinalizationContext.h"
#include "catapult/cache/CatapultCache.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/extensions/ConfigurationUtils.h"
#include "catapult/extensions/ServiceLocator.h"
#include "catapult/extensions/ServiceState.h"
#include "catapult/io/BlockStorageCache.h"
#include "catapult/subscribers/FinalizationSubscriber.h"

namespace catapult { namespace finalization {

	namespace {
		constexpr auto Hooks_Service_Name = "fin.hooks";
		constexpr auto Storage_Service_Name = "fin.proof.storage";
		constexpr auto Aggregator_Service_Name = "fin.aggregator.multistep";

		// region CreateMultiStepAggregator

		chain::SingleStepAggregatorFactory CreateSingleStepAggregatorFactory(const FinalizationConfiguration& config) {
			return [config](const auto& stepIdentifier) {
				CATAPULT_LOG(debug) << "creating single step aggregator for: " << stepIdentifier;
				// TODO: this will need to be step-dependent so we can process different types of messages
				return chain::CreateFinalizationMessageCountVotesAggregator(config);
			};
		}

		struct StorageContext {
			FinalizationPoint NextFinalizationPoint;
			Height LastFinalizedHeight;
			GenerationHash LastFinalizedGenerationHash;
		};

		StorageContext LoadStorageContext(const io::BlockStorageCache& storage, const io::ProofStorageCache& proofStorage) {
			auto proofStorageView = proofStorage.view();
			auto point = proofStorageView.finalizationPoint();
			auto height = proofStorageView.finalizedHeight();

			auto generationHash = storage.view().loadBlockElement(height)->GenerationHash;
			return { point + FinalizationPoint(1), height, generationHash };
		}

		chain::MessageProcessor CreateFinalizationMessageProcessor(
				const FinalizationConfiguration& config,
				const cache::AccountStateCache& accountStateCache,
				const io::BlockStorageCache& storage,
				const io::ProofStorageCache& proofStorage) {
			return [config, &accountStateCache, &storage, &proofStorage](const auto& message) {
				auto storageContext = LoadStorageContext(storage, proofStorage);
				auto finalizationContext = model::FinalizationContext(
						storageContext.NextFinalizationPoint,
						storageContext.LastFinalizedHeight,
						storageContext.LastFinalizedGenerationHash,
						config,
						*accountStateCache.createView());
				return ProcessMessage(message, finalizationContext);
			};
		}

		auto CreateMultiStepAggregator(
				const FinalizationConfiguration& config,
				extensions::ServiceState& state,
				io::ProofStorageCache& proofStorage) {
			auto messageProcessor = CreateFinalizationMessageProcessor(
					config,
					state.cache().sub<cache::AccountStateCache>(),
					state.storage(),
					proofStorage);

			auto& subscriber = state.finalizationSubscriber();
			return std::make_shared<chain::MultiStepFinalizationMessageAggregator>(
					config.MessageSynchronizationMaxResponseSize.bytes(),
					messageProcessor,
					CreateSingleStepAggregatorFactory(config),
					[&proofStorage, &subscriber](const auto& stepIdentifier, const auto& heightHashPair, const auto& proof) {
						CATAPULT_LOG(important) << "finalized consensus reached for " << stepIdentifier;

						// TODO: need to trigger subscriber and storage only when *final* consensus for a FP is reached
						auto height = heightHashPair.Height;
						proofStorage.modifier().saveProof(height, proof);
						subscriber.notifyFinalizedBlock(height, heightHashPair.Hash, FinalizationPoint(stepIdentifier.Point));
					});
		}

		// endregion

		// region FinalizationBootstrapperServiceRegistrar

		class FinalizationBootstrapperServiceRegistrar : public extensions::ServiceRegistrar {
		public:
			FinalizationBootstrapperServiceRegistrar(
					const FinalizationConfiguration& config,
					std::unique_ptr<io::ProofStorage>&& pProofStorage)
					: m_config(config)
					, m_pProofStorageCache(std::make_unique<io::ProofStorageCache>(std::move(pProofStorage)))
			{}

		public:
			extensions::ServiceRegistrarInfo info() const override {
				return { "FinalizationBootstrapper", extensions::ServiceRegistrarPhase::Initial };
			}

			void registerServiceCounters(extensions::ServiceLocator& locator) override {
				using MultiStepAggregator = chain::MultiStepFinalizationMessageAggregator;

				locator.registerServiceCounter<MultiStepAggregator>(Aggregator_Service_Name, "FIN ACT STEPS", [](const auto& aggregator) {
					return aggregator.view().size();
				});
				locator.registerServiceCounter<MultiStepAggregator>(Aggregator_Service_Name, "FIN POINT", [](const auto& aggregator) {
					return aggregator.view().minStepIdentifier().Point;
				});
				locator.registerServiceCounter<MultiStepAggregator>(Aggregator_Service_Name, "FIN ROUND", [](const auto& aggregator) {
					return aggregator.view().minStepIdentifier().Round;
				});
				locator.registerServiceCounter<MultiStepAggregator>(Aggregator_Service_Name, "FIN SUBROUND", [](const auto& aggregator) {
					return aggregator.view().minStepIdentifier().SubRound;
				});
			}

			void registerServices(extensions::ServiceLocator& locator, extensions::ServiceState& state) override {
				// register services
				locator.registerRootedService(Hooks_Service_Name, std::make_shared<FinalizationServerHooks>());

				locator.registerRootedService(Storage_Service_Name, m_pProofStorageCache);

				auto nextFinalizationPoint = m_pProofStorageCache->view().finalizationPoint() + FinalizationPoint(1);
				auto pMultiStepAggregator = CreateMultiStepAggregator(m_config, state, *m_pProofStorageCache);
				pMultiStepAggregator->modifier().setNextFinalizationPoint(nextFinalizationPoint);
				locator.registerRootedService(Aggregator_Service_Name, pMultiStepAggregator);
			}

		private:
			FinalizationConfiguration m_config;
			std::shared_ptr<io::ProofStorageCache> m_pProofStorageCache;
		};

		// endregion
	}

	DECLARE_SERVICE_REGISTRAR(FinalizationBootstrapper)(
			const FinalizationConfiguration& config,
			std::unique_ptr<io::ProofStorage>&& pProofStorage) {
		return std::make_unique<FinalizationBootstrapperServiceRegistrar>(config, std::move(pProofStorage));
	}

	chain::MultiStepFinalizationMessageAggregator& GetMultiStepFinalizationMessageAggregator(const extensions::ServiceLocator& locator) {
		return *locator.service<chain::MultiStepFinalizationMessageAggregator>(Aggregator_Service_Name);
	}

	FinalizationServerHooks& GetFinalizationServerHooks(const extensions::ServiceLocator& locator) {
		return *locator.service<FinalizationServerHooks>(Hooks_Service_Name);
	}
}}
