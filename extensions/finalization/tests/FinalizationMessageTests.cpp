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

#include "finalization/src/FinalizationMessage.h"
#include "finalization/src/FinalizationContext.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/crypto_voting/OtsTree.h"
#include "catapult/utils/MemoryUtils.h"
#include "tests/test/cache/AccountStateCacheTestUtils.h"
#include "tests/test/core/HashTestUtils.h"
#include "tests/test/core/VariableSizedEntityTestUtils.h"
#include "tests/test/core/mocks/MockMemoryStream.h"
#include "tests/test/nodeps/Alignment.h"
#include "tests/test/nodeps/KeyTestUtils.h"
#include "tests/test/nodeps/NumericTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace finalization {

#define TEST_CLASS FinalizationMessageTests

	namespace {
		// region test utils - message

		std::unique_ptr<FinalizationMessage> CreateMessage(uint32_t numHashes) {
			uint32_t messageSize = sizeof(FinalizationMessage) + numHashes * Hash256::Size;
			auto pMessage = utils::MakeUniqueWithSize<FinalizationMessage>(messageSize);
			pMessage->Size = messageSize;
			pMessage->HashesCount = numHashes;

			test::FillWithRandomData({
				reinterpret_cast<uint8_t*>(pMessage->HashesPtr()),
				numHashes * Hash256::Size
			});
			return pMessage;
		}

		// endregion
	}

	// region FinalizationMessage (size + alignment)

#define MESSAGE_FIELDS FIELD(HashesCount) FIELD(Signature) FIELD(StepIdentifier) FIELD(SortitionHashProof)

	TEST(TEST_CLASS, FinalizationMessageHasExpectedSize) {
		// Arrange:
		auto expectedSize = sizeof(model::TrailingVariableDataLayout<FinalizationMessage, Hash256>);

#define FIELD(X) expectedSize += sizeof(FinalizationMessage::X);
		MESSAGE_FIELDS
#undef FIELD

		// Assert:
		EXPECT_EQ(expectedSize, sizeof(FinalizationMessage));
		EXPECT_EQ(4 + 492u, sizeof(FinalizationMessage));
	}

	TEST(TEST_CLASS, FinalizationMessageHasProperAlignment) {
#define FIELD(X) EXPECT_ALIGNED(FinalizationMessage, X);
		MESSAGE_FIELDS
#undef FIELD

		EXPECT_EQ(0u, sizeof(FinalizationMessage) % 8);
	}

#undef MESSAGE_FIELDS

	// endregion

	// region FinalizationMessage (CalculateRealSize)

	TEST(TEST_CLASS, CanCalculateRealSizeWithReasonableValues) {
		// Arrange:
		FinalizationMessage message;
		message.Size = 0;
		message.HashesCount = 67;

		// Act:
		auto realSize = FinalizationMessage::CalculateRealSize(message);

		// Assert:
		EXPECT_EQ(sizeof(FinalizationMessage) + 67 * Hash256::Size, realSize);
	}

	TEST(TEST_CLASS, CalculateRealSizeDoesNotOverflowWithMaxValues) {
		// Arrange:
		FinalizationMessage message;
		message.Size = 0;
		test::SetMaxValue(message.HashesCount);

		// Act:
		auto realSize = FinalizationMessage::CalculateRealSize(message);

		// Assert:
		EXPECT_EQ(sizeof(FinalizationMessage) + message.HashesCount * Hash256::Size, realSize);
		EXPECT_GE(std::numeric_limits<uint64_t>::max(), realSize);
	}

	// endregion

	// region FinalizationMessage (data pointers)

	namespace {
		struct FinalizationMessageTraits {
			static constexpr auto GenerateEntityWithAttachments = CreateMessage;

			template<typename TEntity>
			static auto GetAttachmentPointer(TEntity& entity) {
				return entity.HashesPtr();
			}
		};
	}

	DEFINE_ATTACHMENT_POINTER_TESTS(TEST_CLASS, FinalizationMessageTraits) // HashesPtr

	// endregion

	namespace {
		// region test utils - FinalizationContext

		constexpr auto Harvesting_Mosaic_Id = MosaicId(9876);

		cache::AccountStateCacheTypes::Options CreateOptions() {
			auto options = test::CreateDefaultAccountStateCacheOptions(MosaicId(1111), Harvesting_Mosaic_Id);
			options.MinVoterBalance = Amount(2'000'000);
			return options;
		}

		FinalizationConfiguration CreateConfigurationWithSize(uint32_t size) {
			auto config = FinalizationConfiguration::Uninitialized();
			config.Size = size;
			return config;
		}

		struct AccountKeyPairContainer {
		public:
			AccountKeyPairContainer(crypto::KeyPair&& vrfKeyPair, crypto::KeyPair&& votingKeyPair)
					: VrfKeyPair(std::move(vrfKeyPair))
					, VotingKeyPair(std::move(votingKeyPair))
					, VrfPublicKey(VrfKeyPair.publicKey())
					, VotingPublicKey(VotingKeyPair.publicKey().copyTo<VotingKey>())
			{}

		public:
			crypto::KeyPair VrfKeyPair;
			crypto::KeyPair VotingKeyPair;

			Key VrfPublicKey;
			VotingKey VotingPublicKey;
		};

		std::vector<AccountKeyPairContainer> AddAccountsWithBalances(
				cache::AccountStateCache& cache,
				Height height,
				const std::vector<Amount>& balances) {
			std::vector<AccountKeyPairContainer> keyPairContainers;

			auto delta = cache.createDelta();
			for (auto balance : balances) {
				keyPairContainers.emplace_back(test::GenerateKeyPair(), test::GenerateKeyPair());

				auto address = test::GenerateRandomByteArray<Address>();
				delta->addAccount(address, height);
				auto& accountState = delta->find(address).get();
				accountState.SupplementalPublicKeys.vrf().set(keyPairContainers.back().VrfPublicKey);
				accountState.SupplementalPublicKeys.voting().add({
					keyPairContainers.back().VotingPublicKey,
					FinalizationPoint(1),
					FinalizationPoint(100)
				});
				accountState.Balances.credit(Harvesting_Mosaic_Id, balance);
			}

			delta->updateHighValueAccounts(height);
			cache.commit();

			return keyPairContainers;
		}

		enum class VoterType : uint32_t { Small, Large, Ineligible };

		constexpr auto Num_Expected_Large_Votes = 1'200u;
		constexpr auto Num_Expected_Large_Votes_Lower_Bound = Num_Expected_Large_Votes - Num_Expected_Large_Votes / 5;
		constexpr auto Num_Expected_Large_Votes_Upper_Bound = Num_Expected_Large_Votes + Num_Expected_Large_Votes / 5;

		template<typename TAction>
		void RunFinalizationContextTest(TAction action) {
			// Arrange: create context
			auto generationHash = test::GenerateRandomByteArray<GenerationHash>();
			auto config = CreateConfigurationWithSize(3'000);

			cache::AccountStateCache cache(cache::CacheConfiguration(), CreateOptions());
			auto keyPairContainers = AddAccountsWithBalances(cache, Height(123), {
				Amount(2'000'000), Amount(4'000'000'000'000), Amount(1'000'000), Amount(6'000'000'000'000)
			});

			FinalizationContext context(FinalizationPoint(50), Height(123), generationHash, config, *cache.createView());

			// Act + Assert:
			action(context, keyPairContainers);
		}

		// endregion
	}

	// region PrepareMessage

	namespace {
		template<typename TAction>
		void RunPrepareMessageTest(VoterType voterType, uint32_t numHashes, TAction action) {
			// Arrange:
			RunFinalizationContextTest([voterType, numHashes, action](const auto& context, const auto& keyPairContainers) {
				const auto& keyPairContainer = keyPairContainers[utils::to_underlying_type(voterType)];

				auto storage = mocks::MockSeekableMemoryStream();
				auto otsTree = crypto::OtsTree::Create(
						test::CopyKeyPair(keyPairContainer.VotingKeyPair),
						storage,
						FinalizationPoint(1),
						FinalizationPoint(20),
						{ 20, 20 });

				auto stepIdentifier = crypto::StepIdentifier{ 3, 4, 5 };
				auto hashes = test::GenerateRandomHashes(numHashes);

				// Act:
				auto pMessage = PrepareMessage(otsTree, keyPairContainer.VrfKeyPair, stepIdentifier, hashes, context);

				// Assert:
				action(pMessage, context, hashes);
			});
		}
	}

	TEST(TEST_CLASS, PrepareMessage_FailsWhenAccountIsNotVotingEligible) {
		// Arrange:
		RunPrepareMessageTest(VoterType::Ineligible, 3, [](const auto& pMessage, const auto&, const auto&) {
			// Assert:
			EXPECT_FALSE(!!pMessage);
		});
	}

	TEST(TEST_CLASS, PrepareMessage_FailsWhenVoterIsNotSelected) {
		// Arrange: sortition is probabilistic
		test::RunNonDeterministicTest("voter is not selected", []() {
			auto isTestSuccess = true;
			RunPrepareMessageTest(VoterType::Small, 3, [&isTestSuccess](const auto& pMessage, const auto&, const auto&) {
				if (pMessage) {
					isTestSuccess = false;
					return;
				}

				// Assert:
				EXPECT_FALSE(!!pMessage);
			});

			return isTestSuccess;
		});
	}

	namespace {
		model::HashRange ExtractHashes(const FinalizationMessage& message) {
			return model::HashRange::CopyFixed(reinterpret_cast<const uint8_t*>(message.HashesPtr()), message.HashesCount);
		}
	}

	TEST(TEST_CLASS, PrepareMessage_CanPrepareValidMessageWithoutHashes) {
		// Arrange:
		RunPrepareMessageTest(VoterType::Large, 0, [](const auto& pMessage, const auto& context, const auto& hashes) {
			// Assert:
			ASSERT_TRUE(!!pMessage);

			// - check a few fields
			EXPECT_EQ(sizeof(FinalizationMessage), pMessage->Size);
			EXPECT_EQ(0u, pMessage->HashesCount);

			EXPECT_EQ(crypto::StepIdentifier({ 3, 4, 5 }), pMessage->StepIdentifier);
			EXPECT_EQ(0u, model::FindFirstDifferenceIndex(hashes, ExtractHashes(*pMessage)));

			// - check that the message is valid and can be processed
			// - check that votes are within 1% of expected value
			auto processResultPair = ProcessMessage(*pMessage, context);
			EXPECT_EQ(ProcessMessageResult::Success, processResultPair.first);
			EXPECT_LT(Num_Expected_Large_Votes_Lower_Bound, processResultPair.second);
			EXPECT_GT(Num_Expected_Large_Votes_Upper_Bound, processResultPair.second);
		});
	}

	TEST(TEST_CLASS, PrepareMessage_CanPrepareValidMessageWithHashes) {
		// Arrange:
		RunPrepareMessageTest(VoterType::Large, 3, [](const auto& pMessage, const auto& context, const auto& hashes) {
			// Assert:
			ASSERT_TRUE(!!pMessage);

			// - check a few fields
			EXPECT_EQ(sizeof(FinalizationMessage) + 3 * Hash256::Size, pMessage->Size);
			EXPECT_EQ(3u, pMessage->HashesCount);

			EXPECT_EQ(crypto::StepIdentifier({ 3, 4, 5 }), pMessage->StepIdentifier);
			EXPECT_EQ(3u, model::FindFirstDifferenceIndex(hashes, ExtractHashes(*pMessage)));

			// - check that the message is valid and can be processed
			// - check that votes are within 1% of expected value
			auto processResultPair = ProcessMessage(*pMessage, context);
			EXPECT_EQ(ProcessMessageResult::Success, processResultPair.first);
			EXPECT_LT(Num_Expected_Large_Votes_Lower_Bound, processResultPair.second);
			EXPECT_GT(Num_Expected_Large_Votes_Upper_Bound, processResultPair.second);
		});
	}

	// endregion

	// region ProcessMessage

	namespace {
		void SetMessageSortitionHashProof(
				FinalizationMessage& message,
				const crypto::KeyPair& vrfKeyPair,
				const GenerationHash& generationHash) {
			std::vector<uint8_t> sortitionVrfInputBuffer(sizeof(crypto::StepIdentifier) + GenerationHash::Size);
			std::memcpy(&sortitionVrfInputBuffer[0], &generationHash, GenerationHash::Size);
			std::memcpy(&sortitionVrfInputBuffer[GenerationHash::Size], &message.StepIdentifier, sizeof(crypto::StepIdentifier));

			message.SortitionHashProof = crypto::GenerateVrfProof(sortitionVrfInputBuffer, vrfKeyPair);
		}

		void SignMessage(FinalizationMessage& message, const crypto::KeyPair& votingKeyPair) {
			auto storage = mocks::MockSeekableMemoryStream();
			auto otsTree = crypto::OtsTree::Create(
					test::CopyKeyPair(votingKeyPair),
					storage,
					FinalizationPoint(1),
					FinalizationPoint(20),
					{ 20, 20 });
			message.Signature = otsTree.sign(message.StepIdentifier, {
				reinterpret_cast<const uint8_t*>(&message) + FinalizationMessage::Header_Size,
				message.Size - FinalizationMessage::Header_Size
			});
		}

		template<typename TAction>
		void RunProcessMessageTest(VoterType voterType, uint32_t numHashes, TAction action) {
			// Arrange:
			RunFinalizationContextTest([voterType, numHashes, action](const auto& context, const auto& keyPairContainers) {
				const auto& keyPairContainer = keyPairContainers[utils::to_underlying_type(voterType)];

				// - create message
				auto pMessage = CreateMessage(numHashes);
				pMessage->StepIdentifier = { 3, 4, 5 };
				SetMessageSortitionHashProof(*pMessage, keyPairContainer.VrfKeyPair, context.generationHash());
				SignMessage(*pMessage, keyPairContainer.VotingKeyPair);

				// Act + Assert:
				action(context, keyPairContainer, *pMessage);
			});
		}
	}

	TEST(TEST_CLASS, ProcessMessage_FailsWhenSignatureIsInvalid) {
		// Arrange:
		RunProcessMessageTest(VoterType::Large, 3, [](const auto& context, const auto&, auto& message) {
			// - corrupt a hash
			test::FillWithRandomData(message.HashesPtr()[1]);

			// Act:
			auto processResultPair = ProcessMessage(message, context);

			// Assert:
			EXPECT_EQ(ProcessMessageResult::Failure_Message_Signature, processResultPair.first);
			EXPECT_EQ(0u, processResultPair.second);
		});
	}

	TEST(TEST_CLASS, ProcessMessage_FailsWhenAccountIsNotVotingEligible) {
		// Arrange:
		RunProcessMessageTest(VoterType::Ineligible, 3, [](const auto& context, const auto&, const auto& message) {
			// Act:
			auto processResultPair = ProcessMessage(message, context);

			// Assert:
			EXPECT_EQ(ProcessMessageResult::Failure_Voter, processResultPair.first);
			EXPECT_EQ(0u, processResultPair.second);
		});
	}

	TEST(TEST_CLASS, ProcessMessage_FailsWhenSortitionHashProofIsInvalid) {
		// Arrange:
		RunProcessMessageTest(VoterType::Large, 3, [](const auto& context, const auto& keyPairContainer, auto& message) {
			// - corrupt proof and resign
			test::FillWithRandomData(message.SortitionHashProof.Gamma);
			SignMessage(message, keyPairContainer.VotingKeyPair);

			// Act:
			auto processResultPair = ProcessMessage(message, context);

			// Assert:
			EXPECT_EQ(ProcessMessageResult::Failure_Sortition_Hash_Proof, processResultPair.first);
			EXPECT_EQ(0u, processResultPair.second);
		});
	}

	TEST(TEST_CLASS, ProcessMessage_FailsWhenVoterIsNotSelected) {
		// Arrange: sortition is probabilistic
		test::RunNonDeterministicTest("voter is not selected", []() {
			auto isTestSuccess = true;
			RunProcessMessageTest(VoterType::Small, 3, [&isTestSuccess](const auto& context, const auto&, const auto& message) {
				// Act:
				auto processResultPair = ProcessMessage(message, context);

				// - probabilistically, votes can be nonzero, but, if they're much higher than expected, fail the test
				if (0 < processResultPair.second && processResultPair.second < 10) {
					isTestSuccess = false;
					return;
				}

				// Assert:
				EXPECT_EQ(ProcessMessageResult::Failure_Selection, processResultPair.first);
				EXPECT_EQ(0u, processResultPair.second);
			});

			return isTestSuccess;
		});
	}

	TEST(TEST_CLASS, ProcessMessage_CanProcessValidMessageWithoutHashes) {
		// Arrange:
		RunProcessMessageTest(VoterType::Large, 0, [](const auto& context, const auto&, const auto& message) {
			// Act:
			auto processResultPair = ProcessMessage(message, context);

			// Assert: check that votes are within 1% of expected value
			EXPECT_EQ(ProcessMessageResult::Success, processResultPair.first);
			EXPECT_LT(Num_Expected_Large_Votes_Lower_Bound, processResultPair.second);
			EXPECT_GT(Num_Expected_Large_Votes_Upper_Bound, processResultPair.second);

			// Sanity:
			EXPECT_EQ(0u, message.HashesCount);
		});
	}

	TEST(TEST_CLASS, ProcessMessage_CanProcessValidMessageWithHashes) {
		// Arrange:
		RunProcessMessageTest(VoterType::Large, 3, [](const auto& context, const auto&, const auto& message) {
			// Act:
			auto processResultPair = ProcessMessage(message, context);

			// Assert: check that votes are within 1% of expected value
			EXPECT_EQ(ProcessMessageResult::Success, processResultPair.first);
			EXPECT_LT(Num_Expected_Large_Votes_Lower_Bound, processResultPair.second);
			EXPECT_GT(Num_Expected_Large_Votes_Upper_Bound, processResultPair.second);

			// Sanity:
			EXPECT_EQ(3u, message.HashesCount);
		});
	}

	// endregion
}}
