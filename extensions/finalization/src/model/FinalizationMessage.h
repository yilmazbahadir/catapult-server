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
#include "catapult/crypto/Vrf.h"
#include "catapult/crypto_voting/OtsTypes.h"
#include "catapult/model/RangeTypes.h"
#include "catapult/model/TrailingVariableDataLayout.h"

namespace catapult {
	namespace crypto { class OtsTree; }
	namespace model { class FinalizationContext; }
}

namespace catapult { namespace model {

#pragma pack(push, 1)

	// region FinalizationMessage

	/// Finalization message.
	struct FinalizationMessage : public model::TrailingVariableDataLayout<FinalizationMessage, Hash256> {
	public:
		/// Size of the header that can be skipped when signing/verifying.
		static constexpr size_t Header_Size = sizeof(uint32_t) * 2 + sizeof(crypto::OtsTreeSignature);

	public:
		/// Number of hashes.
		uint32_t HashesCount;

		/// Message signature.
		crypto::OtsTreeSignature Signature;

		/// Step identifer.
		crypto::StepIdentifier StepIdentifier;

		/// Sortition hash proof.
		crypto::VrfProof SortitionHashProof;

	public:
		/// Gets a const pointer to the first hash contained in this message.
		const Hash256* HashesPtr() const {
			return HashesCount ? ToTypedPointer(PayloadStart(*this)) : nullptr;
		}

		/// Gets a pointer to the first hash contained in this message.
		Hash256* HashesPtr() {
			return HashesCount ? ToTypedPointer(PayloadStart(*this)) : nullptr;
		}

	public:
		/// Calculates the real size of \a message.
		static constexpr uint64_t CalculateRealSize(const FinalizationMessage& message) noexcept {
			return sizeof(FinalizationMessage) + message.HashesCount * Hash256::Size;
		}
	};

	// endregion

#pragma pack(pop)

	// region PrepareMessage

	/// Prepares a finalization message given \a otsTree, \a vrfKeyPair, \a stepIdentifier, \a hashes and \a context.
	/// \note If parameters don't yield a voting selection, \c nullptr will be returned.
	std::unique_ptr<FinalizationMessage> PrepareMessage(
			crypto::OtsTree& otsTree,
			const crypto::KeyPair& vrfKeyPair,
			const crypto::StepIdentifier& stepIdentifier,
			const model::HashRange& hashes,
			const FinalizationContext& context);

	// endregion

	// region ProcessMessage

#define PROCESS_MESSAGE_RESULT_LIST \
	/* Invalid message signature. */ \
	ENUM_VALUE(Failure_Message_Signature) \
	\
	/* Invalid voter. */ \
	ENUM_VALUE(Failure_Voter) \
	\
	/* Invalid sortition hash proof. */ \
	ENUM_VALUE(Failure_Sortition_Hash_Proof) \
	\
	/* Invalid selection. */ \
	ENUM_VALUE(Failure_Selection) \
	\
	/* Processing succeeded. */ \
	ENUM_VALUE(Success)

#define ENUM_VALUE(LABEL) LABEL,
	/// Process message results.
	enum class ProcessMessageResult {
		PROCESS_MESSAGE_RESULT_LIST
	};
#undef ENUM_VALUE

	/// Insertion operator for outputting \a value to \a out.
	std::ostream& operator<<(std::ostream& out, ProcessMessageResult value);

	/// Processes a finalization \a message using \a context.
	std::pair<ProcessMessageResult, size_t> ProcessMessage(const FinalizationMessage& message, const FinalizationContext& context);

	// endregion
}}
