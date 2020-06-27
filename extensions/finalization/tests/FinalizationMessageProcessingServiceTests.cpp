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

#include "finalization/src/FinalizationMessageProcessingService.h"
#include "finalization/src/FinalizationConfiguration.h"
#include "finalization/src/chain/MultiStepFinalizationMessageAggregator.h"
#include "catapult/ionet/PacketPayloadFactory.h"
#include "catapult/utils/MemoryUtils.h"
#include "finalization/tests/test/FinalizationBootstrapperServiceTestUtils.h"
#include "tests/test/core/PacketPayloadTestUtils.h"
#include "tests/test/local/ServiceTestUtils.h"
#include "tests/test/net/mocks/MockPacketWriters.h"
#include "tests/TestHarness.h"

namespace catapult { namespace finalization {

#define TEST_CLASS FinalizationMessageProcessingServiceTests

	// region test context

	namespace {
		using AnnotatedFinalizationMessageRange = model::AnnotatedEntityRange<model::FinalizationMessage>;

		using VoterType = test::FinalizationBootstrapperServiceTestUtils::VoterType;

		struct FinalizationMessageProcessingServiceTraits {
			static auto CreateRegistrar() {
				auto config = FinalizationConfiguration::Uninitialized();
				config.ShortLivedCacheMessageDuration = utils::TimeSpan::FromMinutes(1);
				return CreateFinalizationMessageProcessingServiceRegistrar(config);
			}
		};

		class TestContext : public test::VoterSeededCacheDependentServiceLocatorTestContext<FinalizationMessageProcessingServiceTraits> {
		public:
			TestContext() : m_pWriters(std::make_shared<mocks::BroadcastAwareMockPacketWriters>()) {
				test::FinalizationBootstrapperServiceTestUtils::Register(locator(), testState().state());
				locator().registerService("fin.writers", m_pWriters);
			}

		public:
			size_t numBroadcastCalls() const {
				return m_pWriters->numBroadcastCalls();
			}

			const std::vector<ionet::PacketPayload>& broadcastedPayloads() const {
				return m_pWriters->broadcastedPayloads();
			}

		private:
			std::shared_ptr<mocks::BroadcastAwareMockPacketWriters> m_pWriters;
		};
	}

	// endregion

	// region basic

	ADD_SERVICE_REGISTRAR_INFO_TEST(FinalizationMessageProcessing, Post_Extended_Range_Consumers)

	TEST(TEST_CLASS, NoServicesOrCountersAreRegistered) {
		// Arrange:
		TestContext context;

		// Act:
		context.boot();

		// Assert: only dependency services are registered
		EXPECT_EQ(test::FinalizationBootstrapperServiceTestUtils::Num_Bootstrapper_Services + 1, context.locator().numServices());
		EXPECT_EQ(0u, context.locator().counters().size());
	}

	// endregion

	// region message processing

	namespace {
		using FinalizationMessages = std::vector<std::shared_ptr<const model::FinalizationMessage>>;

		ionet::PacketPayload CreateBroadcastPayload(const FinalizationMessages& messages) {
			return ionet::PacketPayloadFactory::FromEntities(ionet::PacketType::Push_Finalization_Messages, messages);
		}

		AnnotatedFinalizationMessageRange CreateMessageRange(const FinalizationMessages& messages) {
			std::vector<model::FinalizationMessageRange> messageRanges;
			for (const auto& pMessage : messages) {
				const auto* pMessageData = reinterpret_cast<const uint8_t*>(pMessage.get());
				messageRanges.push_back(model::FinalizationMessageRange::CopyVariable(pMessageData, pMessage->Size, { 0 }));
			}

			return AnnotatedFinalizationMessageRange(model::FinalizationMessageRange::MergeRanges(std::move(messageRanges)));
		}
	}

	TEST(TEST_CLASS, SingleNewMessageIsAddedToAggregatorAndForwarded) {
		// Arrange:
		TestContext context;
		context.boot();

		const auto& hooks = GetFinalizationServerHooks(context.locator());
		const auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());

		// - prepare message(s)
		const auto& hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage = context.createMessage(VoterType::Large1, { 1, 2, 3 }, hash);

		// Act:
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage }));

		// - wait for the aggregator and the broadcast
		WAIT_FOR_ONE_EXPR(aggregator.view().size());
		WAIT_FOR_ONE_EXPR(context.numBroadcastCalls());

		// Assert: check the aggregator
		EXPECT_EQ(1u, aggregator.view().size());

		// - check the packet(s)
		ASSERT_EQ(1u, context.numBroadcastCalls());
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage }), context.broadcastedPayloads()[0]);
	}

	TEST(TEST_CLASS, MultipleNewMessagesAreAddedToAggregatorAndForwarded) {
		// Arrange:
		TestContext context;
		context.boot();

		const auto& hooks = GetFinalizationServerHooks(context.locator());
		const auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());

		// - prepare message(s)
		const auto& hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = context.createMessage(VoterType::Large1, { 1, 2, 3 }, hash);
		auto pMessage2 = context.createMessage(VoterType::Large1, { 1, 2, 4 }, hash);
		auto pMessage3 = context.createMessage(VoterType::Large1, { 1, 2, 5 }, hash);
		auto pMessage4 = context.createMessage(VoterType::Large1, { 1, 2, 6 }, hash);
		auto pMessage5 = context.createMessage(VoterType::Large1, { 1, 2, 7 }, hash);

		// Act:
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage1, pMessage3, pMessage5 }));
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage2, pMessage4 }));

		// - wait for the aggregator and the broadcast
		WAIT_FOR_VALUE_EXPR(5u, aggregator.view().size());
		WAIT_FOR_VALUE_EXPR(2u, context.numBroadcastCalls());

		// Assert: check the aggregator
		EXPECT_EQ(5u, aggregator.view().size());

		// - check the packet(s)
		ASSERT_EQ(2u, context.numBroadcastCalls());
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage1, pMessage3, pMessage5 }), context.broadcastedPayloads()[0]);
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage2, pMessage4 }), context.broadcastedPayloads()[1]);
	}

	TEST(TEST_CLASS, PreviouslySeenMessageIsNotForwarded) {
		// Arrange:
		TestContext context;
		context.boot();

		const auto& hooks = GetFinalizationServerHooks(context.locator());
		const auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());

		// - prepare message(s)
		const auto& hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = context.createMessage(VoterType::Large1, { 1, 2, 3 }, hash);
		auto pMessage2 = context.createMessage(VoterType::Large1, { 1, 2, 4 }, hash);
		auto pMessage3 = context.createMessage(VoterType::Large1, { 1, 2, 5 }, hash);

		// - send first range
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage2 }));

		// - wait for the aggregator and the broadcast
		WAIT_FOR_ONE_EXPR(aggregator.view().size());
		WAIT_FOR_ONE_EXPR(context.numBroadcastCalls());

		// Act: send second range with duplicate
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage1, pMessage2, pMessage3 }));

		// - wait for the aggregator and the broadcast
		WAIT_FOR_VALUE_EXPR(3u, aggregator.view().size());
		WAIT_FOR_VALUE_EXPR(2u, context.numBroadcastCalls());

		// Assert: check the aggregator
		EXPECT_EQ(3u, aggregator.view().size());

		// - check the packet(s)
		ASSERT_EQ(2u, context.numBroadcastCalls());
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage2 }), context.broadcastedPayloads()[0]);
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage1, pMessage3 }), context.broadcastedPayloads()[1]);
	}

	TEST(TEST_CLASS, NoPayloadIsBroadcastWhenAllMessagesArePreviouslySeen) {
		// Arrange:
		TestContext context;
		context.boot();

		const auto& hooks = GetFinalizationServerHooks(context.locator());
		const auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());

		// - prepare message(s)
		const auto& hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = context.createMessage(VoterType::Large1, { 1, 2, 3 }, hash);
		auto pMessage2 = context.createMessage(VoterType::Large1, { 1, 2, 4 }, hash);
		auto pMessage3 = context.createMessage(VoterType::Large1, { 1, 2, 5 }, hash);

		// - send first range
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage1, pMessage2, pMessage3 }));

		// - wait for the aggregator and the broadcast
		WAIT_FOR_VALUE_EXPR(3u, aggregator.view().size());
		WAIT_FOR_ONE_EXPR(context.numBroadcastCalls());

		// Act: send second range with duplicate
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage2 }));

		// - allow some time for processing
		test::Pause();

		// Assert: check the aggregator
		EXPECT_EQ(3u, aggregator.view().size());

		// - check the packet(s)
		ASSERT_EQ(1u, context.numBroadcastCalls());
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage1, pMessage2, pMessage3 }), context.broadcastedPayloads()[0]);
	}

	TEST(TEST_CLASS, MessagesWithDifferentFinalizationPointsAreIgnored) {
		// Arrange:
		TestContext context;
		context.boot();

		const auto& hooks = GetFinalizationServerHooks(context.locator());
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().setNextFinalizationPoint(FinalizationPoint(2));

		// - prepare message(s)
		const auto& hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = context.createMessage(VoterType::Large1, { 2, 2, 3 }, hash);
		auto pMessage2 = context.createMessage(VoterType::Large1, { 3, 2, 4 }, hash);
		auto pMessage3 = context.createMessage(VoterType::Large1, { 2, 2, 5 }, hash);
		auto pMessage4 = context.createMessage(VoterType::Large1, { 1, 2, 6 }, hash);

		// Act:
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage1, pMessage2, pMessage3, pMessage4 }));

		// - wait for the aggregator and the broadcast
		WAIT_FOR_VALUE_EXPR(2u, aggregator.view().size());
		WAIT_FOR_ONE_EXPR(context.numBroadcastCalls());

		// Assert: check the aggregator
		EXPECT_EQ(2u, aggregator.view().size());

		// - check the packet(s)
		ASSERT_EQ(1u, context.numBroadcastCalls());
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage1, pMessage3 }), context.broadcastedPayloads()[0]);
	}

	TEST(TEST_CLASS, MessageWithHigherFinalizationPointCanBeProcessedAfterLocalFinalizationPointIncreases) {
		// Arrange:
		TestContext context;
		context.boot();

		const auto& hooks = GetFinalizationServerHooks(context.locator());
		auto& aggregator = GetMultiStepFinalizationMessageAggregator(context.locator());
		aggregator.modifier().setNextFinalizationPoint(FinalizationPoint(2));

		// - prepare message(s)
		const auto& hash = test::GenerateRandomByteArray<Hash256>();
		auto pMessage1 = context.createMessage(VoterType::Large1, { 2, 2, 3 }, hash);
		auto pMessage2 = context.createMessage(VoterType::Large1, { 3, 2, 4 }, hash);
		auto pMessage3 = context.createMessage(VoterType::Large1, { 2, 2, 5 }, hash);
		auto pMessage4 = context.createMessage(VoterType::Large1, { 1, 2, 6 }, hash);

		// - send the range
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage1, pMessage2, pMessage3, pMessage4 }));

		// - wait for the aggregator and the broadcast
		WAIT_FOR_VALUE_EXPR(2u, aggregator.view().size());
		WAIT_FOR_ONE_EXPR(context.numBroadcastCalls());

		// - increase the finalization point and resend the same range
		aggregator.modifier().setNextFinalizationPoint(FinalizationPoint(3));
		hooks.messageRangeConsumer()(CreateMessageRange({ pMessage1, pMessage2, pMessage3, pMessage4 }));

		// - wait for the aggregator and the broadcast
		WAIT_FOR_ONE_EXPR(aggregator.view().size());
		WAIT_FOR_VALUE_EXPR(2u, context.numBroadcastCalls());

		// Assert: check the aggregator
		EXPECT_EQ(1u, aggregator.view().size());

		// - check the packet(s)
		ASSERT_EQ(2u, context.numBroadcastCalls());
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage1, pMessage3 }), context.broadcastedPayloads()[0]);
		test::AssertEqualPayload(CreateBroadcastPayload({ pMessage2 }), context.broadcastedPayloads()[1]);
	}

	// endregion
}}