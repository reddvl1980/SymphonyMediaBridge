#pragma once

#include "memory/PacketPoolAllocator.h"
#include "transport/ice/IceSession.h"
#include <gmock/gmock.h>

using namespace transport;

namespace ice
{
class IceEndpoint;
}

namespace transport
{
class SocketAddress;
}

namespace fakenet
{

struct IceListenerMock : public ice::IceSession::IEvents
{
    MOCK_METHOD(void, onIceStateChanged, (ice::IceSession * session, ice::IceSession::State state), (override));
    MOCK_METHOD(void, onIceCompleted, (ice::IceSession * session), (override));

    /** Nomination may occur repeatedly as checks proceeds */
    MOCK_METHOD(void,
        onIceNominated,
        (ice::IceSession * session,
            ice::IceEndpoint* localEndpoint,
            const transport::SocketAddress& remotePort,
            uint64_t rtt),
        (override));
};

} // namespace fakenet
