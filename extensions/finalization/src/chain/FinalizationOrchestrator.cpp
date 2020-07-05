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

#include "FinalizationOrchestrator.h"
#include "finalization/src/model/FinalizationMessage.h"
#include "catapult/crypto_voting/OtsTree.h"

namespace catapult { namespace chain {

	namespace {
		bool ShouldAdvancePoint() {
			// TODO: implement
			return true;
		}

		HeightHashesPair CreateEmptyHeightHashesPair() {
			HeightHashesPair heightHashesPair;
			heightHashesPair.Height = Height(0);
			heightHashesPair.Hashes = model::HashRange::PrepareFixed(1);
			*heightHashesPair.Hashes.begin() = Hash256();
			return heightHashesPair;
		}
	}

	FinalizationOrchestrator::FinalizationOrchestrator(
			const finalization::FinalizationConfiguration& config,
			const supplier<HeightHashesPair>& heightHashesPairSupplier,
			const consumer<const HeightHashesPair&>& messageSink)
			: m_config(config)
			, m_heightHashesPairSupplier(heightHashesPairSupplier)
			, m_messageSink(messageSink)
	{}

	namespace {
		auto CreateFinalizationMessageCommonBlockAggregator(
				finalization::FinalizationConfiguration& config,
				const HeightHashesPair& heightHashesPair) {
			auto hashes = std::vector<Hash256>(heightHashesPair.Hashes.cbegin(), heightHashesPair.Hashes.cend());
			return chain::CreateFinalizationMessageCommonBlockAggregator(config, hashes, heightHashesPair.Height);
		}
	}

	SingleStepAggregatorFactory FinalizationOrchestrator::createSingleStepAggregatorFactory() {
		return [this](const auto& stepIdentifier) {
			CATAPULT_LOG(debug) << "creating single step aggregator for: " << stepIdentifier;

			switch (stepIdentifier.SubRound) {
			case 0:
				return CreateFinalizationMessageMaximumVotesAggregator(m_config);
			case 1:
				return CreateFinalizationMessageCommonBlockAggregator(m_config, m_heightHashesPairSupplier());
			default:
				return CreateFinalizationMessageCountVotesAggregator(m_config);
			}
		};
	}

	ConsensusSink FinalizationOrchestrator::createConsensusSink(const ConsensusSink& pointConsensusSink) {
		return [/*this,*/ pointConsensusSink](const auto& stepIdentifier, const auto& heightHashPair, const auto& proof) {
			// TODO: do something

			if (ShouldAdvancePoint())
				pointConsensusSink(stepIdentifier, heightHashPair, proof);
		};
	}

	void FinalizationOrchestrator::advance(Timestamp time) {
		m_stepStartTime = time;


		m_messageSink(CreateEmptyHeightHashesPair());
	}
}}
