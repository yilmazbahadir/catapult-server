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
		enum class Stage {
			Propose_Chain,
			Collect_Chain_Votes,
			Count_Best_Hash_Votes,

			// TODO: following stages are placeholders
			Binary_BA_Start,
			Binary_BA_End
		};

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

	// namespace {
	// 	size_t FindFirstDifferenceIndex(const EntityRange<TEntity>& lhs, const EntityRange<TEntity>& rhs) {
	// }

	ConsensusSink FinalizationOrchestrator::createConsensusSink(const ConsensusSink& pointConsensusSink) {
		return [this, pointConsensusSink](const auto& stepIdentifier, const auto& heightHashPair, const auto& proof) {
			auto stage = static_cast<Stage>(stepIdentifier.SubRound);
			switch (stage) {
			case Stage::Propose_Chain:
				m_pLastProposeMessage = proof.front();
				break;

			case Stage::Collect_Chain_Votes:
				// TODO: find first difference between m_heightHashesPairSupplier().Hashes and proof.front().Hashes
				break;

			case Stage::Count_Best_Hash_Votes:
				// m_messageSink(proof.
				break;

			case Stage::Binary_BA_Start:
				break;

			case Stage::Binary_BA_End:
				pointConsensusSink(stepIdentifier, heightHashPair, proof);
				break;
			}
		};
	}

	void FinalizationOrchestrator::advance(Timestamp time) {
		m_stageStartTime = time;

		m_messageSink(CreateEmptyHeightHashesPair());
	}
}}
