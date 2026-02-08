#pragma once
#include "util/LMath.h"
#include "mc/Util.h"
#include <cstdint>

namespace SDK {
	enum struct HitType : int {
		BLOCK = 0,
		ENTITY = 1,
		AIR = 3
	};

	class HitResult {
	public:
		// The game HitResult object is larger than this SDK model.
		// We only expose fields we need via explicit offsets.
		struct WeakEntityRef {
			uint8_t pad0[0x10];
			uint32_t id; // EntityContext::id
			uint8_t pad1[0x4];
		};

		Vec3 start;
		Vec3 end;
		HitType hitType;
		int8_t face;
		BlockPos hitBlock;
		Vec3 hitPos;

		// Present when hitType == ENTITY.
		CLASS_FIELD(WeakEntityRef, entity, 0x38);
		// ...
		CLASS_FIELD(BlockPos, liquidBlock, 0x6C);
		CLASS_FIELD(Vec3, liquidPos, 0x6C);
	};
}
