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

#include "finalization/src/chain/FinalizationOrchestrator.h"
#include "finalization/tests/test/FinalizationMessageTestUtils.h"
// #include "tests/test/other/EntitiesSynchronizerTestUtils.h"
#include "tests/test/core/HashTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace chain {

#define TEST_CLASS FinalizationOrchestratorTests

	namespace {
		constexpr auto Max_Sub_Round = 4;

		// region test context

		class MockHeightHashesPairSupplier {
		public:
			size_t size() const {
				return m_pairs.size();
			}

		public:
			void push(HeightHashesPair&& pair) {
				m_pairs.push_back(std::move(pair));
			}

			HeightHashesPair pop() {
				auto pair = std::move(m_pairs.back());
				m_pairs.pop_back();
				return pair;
			}

		private:
			std::vector<HeightHashesPair> m_pairs;
		};

		class MockMessageSink {
		public:
			const auto& seeds() {
				return m_seeds;
			}

		public:
			void push(const HeightHashesPair& pair) {
				m_seeds.push_back({ pair.Height, model::HashRange::CopyRange(pair.Hashes) });
			}

		private:
			std::vector<HeightHashesPair> m_seeds;
		};

		class TestContext {
		public:
			TestContext()
					: m_orchestrator(
							CreateConfiguration(),
							[&supplier = m_heightHashesPairSupplier]() { return supplier.pop(); },
							[&sink = m_messageSink](const auto& pair) { sink.push(pair); })
			{}

		public:
			auto& heightHashesPairSupplier() {
				return m_heightHashesPairSupplier;
			}

			auto& messageSink() {
				return m_messageSink;
			}

			auto& orchestrator() {
				return m_orchestrator;
			}

		private:
			static finalization::FinalizationConfiguration CreateConfiguration() {
				auto config = finalization::FinalizationConfiguration::Uninitialized();
				config.Size = 3000;
				config.Threshold = 2000;
				config.MaxHashesPerPoint = 10;
				config.ProposeMessageStageDuration = utils::TimeSpan::FromMinutes(1);
				config.AggregationStageMaxDuration = utils::TimeSpan::FromMinutes(3);
				return config;
			}

		private:
			MockHeightHashesPairSupplier m_heightHashesPairSupplier;
			MockMessageSink m_messageSink;

			FinalizationOrchestrator m_orchestrator;
		};

		// endregion
	}

	// region constructor

	TEST(TEST_CLASS, CanCreateOrchestrator) {
		// Act:
		TestContext context;
		const auto& orchestrator = context.orchestrator();

		// Assert:
		EXPECT_EQ(0u, orchestrator.subRound());
		EXPECT_EQ(Timestamp(), orchestrator.subRoundStartTime());

		// Sanity:
		EXPECT_EQ(0u, context.heightHashesPairSupplier().size());
		EXPECT_EQ(0u, context.messageSink().seeds().size());
	}

	// endregion

	// region createSingleStepAggregator

	namespace {
		enum class SingleStepAggregatorType { Maximum_Votes, Common_Blocks, Common_Votes, Unknown };

		std::unique_ptr<model::FinalizationMessage> CreateMessage(uint64_t subRound, Height height, const Hash256& hash) {
			auto pMeessage = test::CreateMessage(height, hash);
			pMeessage->StepIdentifier = { 1, 0, subRound };
			return pMeessage;
		}

		SingleStepAggregatorType DetectSingleStepAggregatorTypeForSubRound(uint64_t subRound) {
			// Arrange:
			TestContext context;

			// - set the heightHashesPairSupplier because it is required for Common_Blocks
			auto hashRange = test::GenerateRandomHashes(3);
			context.heightHashesPairSupplier().push({ Height(101), model::HashRange::CopyRange(hashRange) });

			// Act:
			auto pAggregator = context.orchestrator().createSingleStepAggregator({ 1, 0, subRound });

			// Assert:
			auto detectedAggregatorType = SingleStepAggregatorType::Unknown;
			auto pMessageRandomHash1 = CreateMessage(subRound, Height(102), test::GenerateRandomByteArray<Hash256>());
			auto pMessageRandomHash2 = CreateMessage(subRound, Height(102), test::GenerateRandomByteArray<Hash256>());
			auto pMessageMatchingHash = CreateMessage(subRound, Height(102), *(++hashRange.begin()));

			// - any message with any number of votes will trigger Maximum_Votes
			pAggregator->add(*pMessageRandomHash1, 1);
			if (pAggregator->hasConsensus())
				detectedAggregatorType = SingleStepAggregatorType::Maximum_Votes;

			// - any message with at least threshold votes will trigger Common_Votes
			pAggregator->add(*pMessageRandomHash2, 2001);
			if (SingleStepAggregatorType::Unknown == detectedAggregatorType && pAggregator->hasConsensus())
				detectedAggregatorType = SingleStepAggregatorType::Common_Votes;

			// - only a message with at least threshold votes AND a matching height/hash will trigger Common_Blocks
			pAggregator->add(*pMessageMatchingHash, 2001);
			if (SingleStepAggregatorType::Unknown == detectedAggregatorType && pAggregator->hasConsensus())
				detectedAggregatorType = SingleStepAggregatorType::Common_Blocks;

			// - only Common_Blocks requires a call to heightHashesPairSupplier
			auto description = "subRound " + std::to_string(subRound);
			EXPECT_EQ(
					SingleStepAggregatorType::Common_Blocks == detectedAggregatorType ? 0 : 1,
					context.heightHashesPairSupplier().size()) << description;

			// - no other orchestrator state was affected
			EXPECT_EQ(0u, context.orchestrator().subRound()) << description;
			EXPECT_EQ(Timestamp(), context.orchestrator().subRoundStartTime()) << description;

			EXPECT_EQ(0u, context.messageSink().seeds().size()) << description;

			return detectedAggregatorType;
		}
	}

	TEST(TEST_CLASS, CreateSingleStepAggregator_CreatesAppropriateAggregatorBasedOnSubRound) {
		EXPECT_EQ(SingleStepAggregatorType::Maximum_Votes, DetectSingleStepAggregatorTypeForSubRound(0));
		EXPECT_EQ(SingleStepAggregatorType::Common_Blocks, DetectSingleStepAggregatorTypeForSubRound(1));

		for (auto i = 2u; i <= Max_Sub_Round; ++i)
			EXPECT_EQ(SingleStepAggregatorType::Common_Votes, DetectSingleStepAggregatorTypeForSubRound(i)) << "subRound " << i;
	}

	// endregion

	// region createConsensusSink

	// endregion

	// region propose

	TEST(TEST_CLASS, Propose_PreparesProposalMessage) {
		// Arrange;
		TestContext context;

		// - set the heightHashesPairSupplier because it is required for propose
		auto hashRange = test::GenerateRandomHashes(3);
		context.heightHashesPairSupplier().push({ Height(101), model::HashRange::CopyRange(hashRange) });

		// Act:
		context.orchestrator().propose();

		// Assert: heightHashesPairSupplier was called and a message was created
		EXPECT_EQ(0u, context.heightHashesPairSupplier().size());

		const auto& messageSeeds = context.messageSink().seeds();
		ASSERT_EQ(1u, messageSeeds.size());
		EXPECT_EQ(Height(101), messageSeeds[0].Height);
		EXPECT_EQ(3u, model::FindFirstDifferenceIndex(hashRange, messageSeeds[0].Hashes));

		// - no other orchestrator state was affected
		EXPECT_EQ(0u, context.orchestrator().subRound());
		EXPECT_EQ(Timestamp(), context.orchestrator().subRoundStartTime());
	}

	// endregion

	// region advance

	// endregion
}}
