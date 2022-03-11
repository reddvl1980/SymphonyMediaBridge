#pragma once

#include "concurrency/MpmcPublish.h"
#include "transport/PacketCounters.h"

namespace memory
{
class Packet;
} // namespace memory
namespace rtp
{
class ReportBlock;
class RtcpSenderReport;
struct RtcpHeader;
} // namespace rtp

namespace config
{
class Config;
}

namespace transport
{
struct ReportSummary
{
    bool empty() const { return packets == 0 && sequenceNumberSent == 0; }

    uint64_t getRtt() const { return (static_cast<uint64_t>(rttNtp) * utils::Time::sec) >> 16; }
    uint64_t packets = 0;
    uint64_t lostPackets = 0;
    double lossFraction = 0;
    uint32_t extendedSeqNoReceived = 0;
    uint32_t sequenceNumberSent = 0;
    uint32_t rttNtp = 0;
};

class RtpSenderState
{
public:
    explicit RtpSenderState(uint32_t rtpFrequency, const config::Config& config);

    // Transport interface
    void onRtpSent(uint64_t timestamp, memory::Packet& packet);
    void onReceiverBlockReceived(uint64_t timestamp, uint32_t wallClockNtp32, const rtp::ReportBlock& report);
    void onRtcpSent(uint64_t timestamp, const rtp::RtcpHeader* report);
    int64_t timeToSenderReport(uint64_t timestamp) const;

    uint64_t getLastSendTime() const { return _rtpSendTime; }
    uint32_t getSentSequenceNumber() const { return _sendCounters.sequenceNumber; }
    void fillInReport(rtp::RtcpSenderReport& report, uint64_t timestamp, uint64_t wallClockNtp) const;

    void setRtpFrequency(uint32_t rtpFrequency);

    // thread safe interface
    ReportSummary getSummary() const;
    PacketCounters getCounters() const;
    uint32_t getRttNtp() const;
    std::atomic_uint8_t payloadType;

    struct SendCounters
    {
        uint32_t packets = 0;
        uint32_t sequenceNumber = 0;
        uint64_t octets = 0;
        uint64_t timestamp = 0;
    };

private:
    uint32_t getRtpTimestamp(uint64_t timestamp) const;
    struct RemoteCounters
    {
        RemoteCounters()
            : cumulativeLoss(0),
              rttNtp(0),
              lossFraction(0),
              extendedSeqNoReceived(0),
              timestampNtp32(0),
              timestamp(0)
        {
        }

        uint32_t cumulativeLoss;
        uint32_t rttNtp;
        double lossFraction;
        uint32_t extendedSeqNoReceived;
        uint32_t timestampNtp32;
        uint64_t timestamp;
    };

    SendCounters _sendCounters;
    SendCounters _sendCounterSnapshot;

    struct
    {
        uint32_t rtp = 0;
        uint64_t local = 0;
    } _rtpTimestampCorrelation;

    std::atomic_uint64_t _rtpSendTime;
    uint64_t _senderReportSendTime;
    uint32_t _senderReportNtp;
    uint32_t _lossSinceSenderReport;

    const config::Config& _config;
    uint64_t _scheduledSenderReport;

    RemoteCounters _remoteReport;

    uint32_t _rtpFrequency;
    concurrency::MpmcPublish<PacketCounters, 4> _counters;
    concurrency::MpmcPublish<ReportSummary, 4> _summary;
    concurrency::MpmcPublish<SendCounters, 4> _sendReport;
};

} // namespace transport
