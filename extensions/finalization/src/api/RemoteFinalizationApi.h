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
#include "catapult/api/RemoteApi.h"
#include "catapult/model/RangeTypes.h"
#include "catapult/thread/Future.h"

namespace catapult {
	namespace crypto { struct StepIdentifier; }
	namespace ionet { class PacketIo; }
	namespace model { struct FinalizationMessage; }
}

namespace catapult { namespace api {

	/// Range of finalization messages.
	using FinalizationMessageRange = model::EntityRange<model::FinalizationMessage>;

	/// Api for retrieving finalization information from a remote node.
	class RemoteFinalizationApi : public RemoteApi {
	protected:
		/// Creates a remote api for the node with specified \a remoteIdentity.
		explicit RemoteFinalizationApi(const model::NodeIdentity& remoteIdentity) : RemoteApi(remoteIdentity)
		{}

	public:
		/// Gets all finalization messages from the remote starting at \a stepIdentifier excluding those with all hashes
		/// in \a knownShortHashes.
		virtual thread::future<FinalizationMessageRange> messages(
				const crypto::StepIdentifier& stepIdentifier,
				model::ShortHashRange&& knownShortHashes) const = 0;
	};

	/// Creates a finalization api for interacting with a remote node with the specified \a io and \a remoteIdentity.
	std::unique_ptr<RemoteFinalizationApi> CreateRemoteFinalizationApi(ionet::PacketIo& io, const model::NodeIdentity& remoteIdentity);
}}