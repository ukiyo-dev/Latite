#pragma once
#include "mc/common/network/Packet.h"

namespace SDK {
	class MoveActorDeltaPacket : public Packet {
	public:
		int64_t runtimeEntityId;
	};
}
