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
			, m_stage(Stage::Propose_Chain)
	{}

	uint64_t FinalizationOrchestrator::subRound() const {
		return utils::to_underlying_type(m_stage);
	}

	Timestamp FinalizationOrchestrator::subRoundStartTime() const {
		return m_stageStartTime;
	}

	namespace {
		auto CreateFinalizationMessageCommonBlockAggregator(
				finalization::FinalizationConfiguration& config,
				const HeightHashesPair& heightHashesPair) {
			auto hashes = std::vector<Hash256>(heightHashesPair.Hashes.cbegin(), heightHashesPair.Hashes.cend());
			return chain::CreateFinalizationMessageCommonBlockAggregator(config, hashes, heightHashesPair.Height);
		}
	}

	std::unique_ptr<SingleStepFinalizationMessageAggregator> FinalizationOrchestrator::createSingleStepAggregator(
			const crypto::StepIdentifier& stepIdentifier) {
		CATAPULT_LOG(debug) << "creating single step aggregator for: " << stepIdentifier;

		auto stage = static_cast<Stage>(stepIdentifier.SubRound);
		switch (stage) {
		case Stage::Propose_Chain:
			return CreateFinalizationMessageMaximumVotesAggregator(m_config);
		case Stage::Collect_Chain_Votes:
			return CreateFinalizationMessageCommonBlockAggregator(m_config, m_heightHashesPairSupplier());
		default:
			return CreateFinalizationMessageCountVotesAggregator(m_config);
		}
	}

	ConsensusSink FinalizationOrchestrator::createConsensusSink(const ConsensusSink& pointConsensusSink) {
		return [this, pointConsensusSink](const auto& stepIdentifier, const auto& heightHashPair, const auto& proof) {
			auto stage = static_cast<Stage>(stepIdentifier.SubRound);
			switch (stage) {
			case Stage::Propose_Chain:
				// save the last (best) proposal message, but don't increment the stage
				m_pLastProposeMessage = proof.front();
				return;

			case Stage::Collect_Chain_Votes:
				// TODO: sign and send message
				break;

			case Stage::Count_Best_Hash_Votes:
				// TODO: sign and send message
				break;

			case Stage::Binary_BA_Start:
				// TODO: sign and send message
				break;

			case Stage::Binary_BA_End:
				pointConsensusSink(stepIdentifier, heightHashPair, proof);
				break;
			}

			incrementStage();
		};
	}

	// TODO: not sure if this needs to be part of orchestrator or external?
	void FinalizationOrchestrator::propose() {
		m_messageSink(m_heightHashesPairSupplier());
	}

	void FinalizationOrchestrator::advance(Timestamp time) {
		if (m_stageStartTime == Timestamp()) {
			m_stageStartTime = time;
			return;
		}

		if (Stage::Propose_Chain == m_stage) {
			CATAPULT_LOG(warning) << "time " << time << " RHS " << m_stageStartTime + m_config.ProposeMessageStageDuration;
			CATAPULT_LOG(warning) << "lhs " << (time - m_stageStartTime) << " RHS " << (Timestamp(0) + m_config.ProposeMessageStageDuration);
			if (time - m_stageStartTime > Timestamp(0) + m_config.ProposeMessageStageDuration) {
				if (m_pLastProposeMessage) {
					// TODO: find first difference between m_heightHashesPairSupplier().Hashes and proof.front().Hashes
					//       and send an appropriate message
					m_pLastProposeMessage.reset();
				} else {
					m_messageSink(CreateEmptyHeightHashesPair());
				}
			}

			return;
		}

		if (time > m_stageStartTime + m_config.AggregationStageMaxDuration) {
			m_messageSink(CreateEmptyHeightHashesPair());
			incrementStage();
		}
	}

	void FinalizationOrchestrator::incrementStage() {
		m_stageStartTime = Timestamp();

		m_stage = Stage::Binary_BA_End == m_stage
				? Stage::Propose_Chain
				: static_cast<Stage>(utils::to_underlying_type(m_stage) + 1);
	}
}}
