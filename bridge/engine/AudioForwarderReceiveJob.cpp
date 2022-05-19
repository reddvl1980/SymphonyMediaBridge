#include "bridge/engine/AudioForwarderReceiveJob.h"
#include "bridge/engine/ActiveMediaList.h"
#include "bridge/engine/EngineMixer.h"
#include "codec/G711.h"
#include "codec/G711codec.h"
#include "codec/Opus.h"
#include "codec/OpusDecoder.h"
#include "codec/PcmResampler.h"
#include "codec/PcmUtils.h"
#include "logger/Logger.h"
#include "memory/Packet.h"
#include "memory/PacketPoolAllocator.h"
#include "rtp/RtpHeader.h"
#include "transport/RtcTransport.h"
#include "utils/CheckedCast.h"

namespace bridge
{

void AudioForwarderReceiveJob::onPacketDecoded(const int32_t decodedFrames, const uint8_t* decodedData)
{
    if (decodedFrames > 0)
    {
        auto pcmPacket = memory::makeUniquePacket(_audioPacketAllocator, *_packet);
        if (!pcmPacket)
        {
            return;
        }
        auto rtpHeader = rtp::RtpHeader::fromPacket(*pcmPacket);
        const auto decodedPayloadLength = decodedFrames * codec::Opus::channelsPerFrame * codec::Opus::bytesPerSample;
        memcpy(rtpHeader->getPayload(), decodedData, decodedPayloadLength);
        pcmPacket->setLength(rtpHeader->headerLength() + decodedPayloadLength);

        _engineMixer.onMixerAudioRtpPacketDecoded(_sender, std::move(pcmPacket));
        return;
    }

    logger::error("Unable to decode opus packet, error code %d", "OpusDecodeJob", decodedFrames);
}

void AudioForwarderReceiveJob::decodeOpus(const memory::Packet& opusPacket)
{
    if (!_ssrcContext._opusDecoder)
    {
        logger::debug("Creating new opus decoder for ssrc %u in mixer %s",
            "OpusDecodeJob",
            _ssrcContext._ssrc,
            _engineMixer.getLoggableId().c_str());
        _ssrcContext._opusDecoder.reset(new codec::OpusDecoder());
    }

    codec::OpusDecoder& decoder = *_ssrcContext._opusDecoder;

    if (!decoder.isInitialized())
    {
        return;
    }

    uint8_t decodedData[memory::AudioPacket::size];
    auto rtpPacket = rtp::RtpHeader::fromPacket(*_packet);
    if (!rtpPacket)
    {
        return;
    }

    const uint32_t headerLength = rtpPacket->headerLength();
    const uint32_t payloadLength = _packet->getLength() - headerLength;
    auto payloadStart = rtpPacket->getPayload();

    if (decoder.hasDecoded() && _extendedSequenceNumber != decoder.getExpectedSequenceNumber())
    {
        const int32_t lossCount = static_cast<int32_t>(_extendedSequenceNumber - decoder.getExpectedSequenceNumber());
        if (lossCount <= 0)
        {
            logger::debug("Old opus packet sequence %u expected %u, discarding",
                "OpusDecodeJob",
                _extendedSequenceNumber,
                decoder.getExpectedSequenceNumber());
            return;
        }

        logger::debug("Lost opus packet sequence %u expected %u, fec",
            "OpusDecodeJob",
            _extendedSequenceNumber,
            decoder.getExpectedSequenceNumber());

        const auto concealCount = std::min(5u, _extendedSequenceNumber - decoder.getExpectedSequenceNumber() - 1);
        for (uint32_t i = 0; i < concealCount; ++i)
        {
            const auto decodedFrames = decoder.conceal(decodedData);
            onPacketDecoded(decodedFrames, decodedData);
        }

        const auto decodedFrames = decoder.conceal(payloadStart, payloadLength, decodedData);
        onPacketDecoded(decodedFrames, decodedData);
    }

    const auto framesInPacketBuffer =
        memory::AudioPacket::size / codec::Opus::channelsPerFrame / codec::Opus::bytesPerSample;

    const auto decodedFrames =
        decoder.decode(_extendedSequenceNumber, payloadStart, payloadLength, decodedData, framesInPacketBuffer);
    onPacketDecoded(decodedFrames, decodedData);
}

void AudioForwarderReceiveJob::decodeG711(const memory::Packet& g711Packet)
{
    if (!_ssrcContext._resampler)
    {
        _ssrcContext._resampler =
            codec::createPcmResampler(codec::Opus::sampleRate / codec::Opus::packetsPerSecond, 8000, 48000);
    }
    if (!_ssrcContext._resampler)
    {
        return;
    }

    auto rtpHeader = rtp::RtpHeader::fromPacket(g711Packet);

    auto pcmPacket = memory::makeUniquePacket(_audioPacketAllocator, g711Packet);
    auto pcmHeader = rtp::RtpHeader::fromPacket(*pcmPacket);

    const auto sampleCount = g711Packet.getLength() - rtpHeader->headerLength();
    auto pcmPayload = reinterpret_cast<int16_t*>(pcmHeader->getPayload());
    if (rtpHeader->payloadType == codec::Pcma::payloadType)
    {
        codec::PcmaCodec::decode(rtpHeader->getPayload(), pcmPayload, sampleCount);

        auto producedSamples = _ssrcContext._resampler->resample(pcmPayload, sampleCount, pcmPayload);
        if (producedSamples < sampleCount)
        {
            return;
        }
        codec::makeStereo(pcmPayload, producedSamples);
        pcmPacket->setLength(pcmHeader->headerLength() + sampleCount * codec::Opus::channelsPerFrame * sizeof(int16_t));
    }
    else if (rtpHeader->payloadType == codec::Pcmu::payloadType)
    {
        codec::PcmuCodec::decode(rtpHeader->getPayload(), pcmPayload, sampleCount);
        auto producedSamples = _ssrcContext._resampler->resample(pcmPayload, sampleCount, pcmPayload);
        if (producedSamples < sampleCount)
        {
            return;
        }
        codec::makeStereo(pcmPayload, producedSamples);
        pcmPacket->setLength(pcmHeader->headerLength() + sampleCount * codec::Opus::channelsPerFrame * sizeof(int16_t));
    }

    _engineMixer.onMixerAudioRtpPacketDecoded(_sender, std::move(pcmPacket));
}

AudioForwarderReceiveJob::AudioForwarderReceiveJob(memory::UniquePacket packet,
    memory::AudioPacketPoolAllocator& audioPacketAllocator,
    transport::RtcTransport* sender,
    bridge::EngineMixer& engineMixer,
    bridge::SsrcInboundContext& ssrcContext,
    ActiveMediaList& activeMediaList,
    const int32_t silenceThresholdLevel,
    const bool hasMixedAudioStreams,
    const uint32_t extendedSequenceNumber)
    : CountedJob(sender->getJobCounter()),
      _packet(std::move(packet)),
      _audioPacketAllocator(audioPacketAllocator),
      _engineMixer(engineMixer),
      _sender(sender),
      _ssrcContext(ssrcContext),
      _activeMediaList(activeMediaList),
      _silenceThresholdLevel(silenceThresholdLevel),
      _hasMixedAudioStreams(hasMixedAudioStreams),
      _extendedSequenceNumber(extendedSequenceNumber)
{
    assert(_packet);
    assert(_packet->getLength() > 0);
}

void AudioForwarderReceiveJob::run()
{
    auto rtpHeader = rtp::RtpHeader::fromPacket(*_packet);
    if (!rtpHeader)
    {
        return;
    }

    const auto rtpHeaderExtensions = rtpHeader->getExtensionHeader();
    if (rtpHeaderExtensions)
    {
        int32_t audioLevel = -1;

        for (const auto& rtpHeaderExtension : rtpHeaderExtensions->extensions())
        {
            if (rtpHeaderExtension.getId() != _ssrcContext._audioLevelExtensionId)
            {
                continue;
            }

            audioLevel = rtpHeaderExtension.data[0] & 0x7F;
            break;
        }

        _activeMediaList.onNewAudioLevel(_sender->getEndpointIdHash(), audioLevel);

        if (audioLevel >= _silenceThresholdLevel)
        {
            _ssrcContext._markNextPacket = true;
            return;
        }
    }

    const auto oldRolloverCounter = _ssrcContext._lastUnprotectedExtendedSequenceNumber >> 16;
    const auto newRolloverCounter = _extendedSequenceNumber >> 16;
    if (newRolloverCounter > oldRolloverCounter)
    {
        logger::debug("Setting new rollover counter for ssrc %u", "AudioForwarderReceiveJob", _ssrcContext._ssrc);
        if (!_sender->setSrtpRemoteRolloverCounter(_ssrcContext._ssrc, newRolloverCounter))
        {
            logger::error("Failed to set rollover counter srtp %u, mixer %s",
                "AudioForwarderReceiveJob",
                _ssrcContext._ssrc,
                _engineMixer.getLoggableId().c_str());
            return;
        }
    }

    if (!_sender->unprotect(*_packet))
    {
        logger::error("Failed to unprotect srtp %u, mixer %s",
            "AudioForwarderReceiveJob",
            _ssrcContext._ssrc,
            _engineMixer.getLoggableId().c_str());
        return;
    }
    _ssrcContext._lastUnprotectedExtendedSequenceNumber = _extendedSequenceNumber;

    if (_hasMixedAudioStreams && rtpHeader->payloadType == static_cast<uint16_t>(bridge::RtpMap::Format::OPUS))
    {
        decodeOpus(*_packet);
    }
    else if (_hasMixedAudioStreams &&
        (rtpHeader->payloadType == codec::Pcma::payloadType ||
            rtpHeader->payloadType == static_cast<uint16_t>(bridge::RtpMap::Format::PCMU)))
    {
        decodeG711(*_packet);
    }

    if (_ssrcContext._markNextPacket)
    {
        rtpHeader->marker = 1;
        _ssrcContext._markNextPacket = false;
    }

    _engineMixer.onForwarderAudioRtpPacketDecrypted(_sender, std::move(_packet), _extendedSequenceNumber);
}

} // namespace bridge
