#include "bridge/RtpMap.h"

namespace
{

using namespace bridge;

RtpMap _opus(RtpMap::Format::OPUS, 111, 48000);
RtpMap _vp8(RtpMap::Format::VP8, 100, 90000);

} // namespace

namespace bridge
{

const RtpMap& RtpMap::opus()
{
    return _opus;
}

const RtpMap& RtpMap::pcma()
{
    static RtpMap _pcma(RtpMap::Format::PCMA, 8, 8000);
    return _pcma;
}

const RtpMap& RtpMap::pcmu()
{
    static RtpMap _pcmu(RtpMap::Format::PCMU, 0, 8000);
    return _pcmu;
}

const RtpMap& RtpMap::vp8()
{
    return _vp8;
}

} // namespace bridge
