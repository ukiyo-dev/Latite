#pragma once
#include "mc/common/network/Packet.h"
#include "util/LMath.h"

namespace SDK {
	class MovePlayerPacket : public Packet {
	public:
		int64_t runtimeEntityId;
		Vec3 position;
		Vec3 rotation;
		int32_t mode;
		bool onGround;
		int64_t ridingRuntimeEntityId;
		int32_t teleportationCause;
		int32_t entityType;
		int64_t tick;
	};
}
