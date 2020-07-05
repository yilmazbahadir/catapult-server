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
#include "ProofStorage.h"
#include "catapult/io/FixedSizeValueStorage.h"
#include "catapult/io/IndexFile.h"
#include <string>

namespace catapult { namespace io {

	/// File-based proof storage.
	class FileProofStorage final : public ProofStorage {
	public:
		/// Creates a file-based proof storage, where proofs will be stored inside \a dataDirectory.
		explicit FileProofStorage(const std::string& dataDirectory);

	public:
		FinalizationPoint finalizationPoint() const override;
		Height finalizedHeight() const override;
		model::HeightHashPairRange loadFinalizedHashesFrom(FinalizationPoint point, size_t maxHashes) const override;
		void saveProof(Height height, const chain::FinalizationProof& proof) override;
		std::shared_ptr<const model::PackedFinalizationProof> loadProof(FinalizationPoint point) const override;

	private:
		std::string m_dataDirectory;
		FinalizationPointHashFile m_hashFile;
		IndexFile m_indexFile;
		IndexFile m_heightIndexFile;
	};
}}