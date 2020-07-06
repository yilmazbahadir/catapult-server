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

#pragma once
#include "FinalizationAggregatorTypes.h"
#include "finalization/src/FinalizationConfiguration.h"

namespace catapult { namespace chain {

	/// Height hashes pair
	struct HeightHashesPair {
		/// Height.
		catapult::Height Height;

		/// Hashes starting at height.
		model::HashRange Hashes;
	};

	/// Orchestrates finalization process.
	class FinalizationOrchestrator {
	public:
		/// Creates an orchestrator around \a config and \a heightHashesPairSupplier and \a messageSink.
		FinalizationOrchestrator(
				const finalization::FinalizationConfiguration& config,
				const supplier<HeightHashesPair>& heightHashesPairSupplier,
				const consumer<const HeightHashesPair&>& messageSink);

	public:
		/// Creates a single step aggregator factory.
		SingleStepAggregatorFactory createSingleStepAggregatorFactory();

		/// Creates a finalization consensus sink that delegates to \a pointConsensusSink when consensus is reached
		/// on a finalization point.
		ConsensusSink createConsensusSink(const ConsensusSink& pointConsensusSink);

	public:
		/// Runs the orchestrator given the current \a time.
		void advance(Timestamp time);

	private:
		// void handle(CollectChainVotes )
		// 	Propose_Chain,
		// 	Collect_Chain_Votes,
		// 	Count_Best_Hash_Votes,

		// 	// TODO: following stages are placeholders
		// 	Binary_BA_Start,
		// 	Binary_BA_End

	private:
		finalization::FinalizationConfiguration m_config;
		supplier<HeightHashesPair> m_heightHashesPairSupplier;
		consumer<const HeightHashesPair&> m_messageSink;

		Timestamp m_stageStartTime;
		// std::shared_ptr<const model::FinalizationMessage> m_pLastProposeMessage;
	};
}}
