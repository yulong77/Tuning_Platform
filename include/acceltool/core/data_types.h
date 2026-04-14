#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace acceltool
{
    struct RawSample
    {
        std::uint64_t sampleIndex = 0;
        int nodeAddress = 0;
    
        std::uint32_t deviceTick = 0;
    
        std::uint64_t deviceTimestampUnixNs = 0;
    
        bool tickGapDetected = false;
        std::uint32_t tickGapCount = 0;
    
        bool timestampGapDetected = false;
        std::int64_t timestampGapNs = 0;
        std::uint64_t expectedTimestampStepNs = 0;
    
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    
        bool hasX = false;
        bool hasY = false;
        bool hasZ = false;
    
        int baseRssi = 0;
        int nodeRssi = 0;
    };


    struct ProcessedSample
    {
        std::uint64_t sampleIndex = 0;
        int nodeAddress = 0;
    
        std::uint32_t deviceTick = 0;
    
        std::uint64_t deviceTimestampUnixNs = 0;
    
        bool tickGapDetected = false;
        std::uint32_t tickGapCount = 0;
    
        bool timestampGapDetected = false;
        std::int64_t timestampGapNs = 0;
        std::uint64_t expectedTimestampStepNs = 0;
    
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    
        double magnitudeXY = 0.0;
        double magnitudeXYZ = 0.0;
        double normLatG = 0.0;
        double appliedSpec = 0.0;
        bool exceedsSpec = false;
    
        int baseRssi = 0;
        int nodeRssi = 0;
    };


    struct DisplayBucket
    {
        std::uint64_t bucketIndex = 0;
    
        std::uint64_t startSampleIndex = 0;
        std::uint64_t endSampleIndex = 0;
    
        std::uint64_t startDeviceTimestampUnixNs = 0;
        std::uint64_t endDeviceTimestampUnixNs = 0;
    
        std::size_t sampleCount = 0;
    
        double peakX = 0.0;
        double peakY = 0.0;
        double peakZ = 0.0;
    
        double maxMagnitudeXY = 0.0;
        double maxMagnitudeXYZ = 0.0;
        double maxNormLatG = 0.0;
    };



    struct NodeConfigSnapshot
    {
        int nodeAddress = 0;

        std::string modelNumber;
        std::string serial;
        std::string firmware;

        std::string communicationProtocol;
        std::string defaultMode;
        std::string samplingMode;

        std::uint32_t sampleRateHz = 0;
        std::uint32_t inactivityTimeoutSeconds = 0;
        bool unlimitedDuration = false;

        std::uint32_t numSweeps = 0;
        std::size_t activeChannelCount = 0;
        std::uint16_t activeChannelMask = 0;
        std::string activeChannelSummary;
    };

    struct ConfigApplyReport
    {
        bool verifyPassed = false;
        bool applyCompletedWithoutException = false;
        bool configureNodeEnabled = false;

        std::uint32_t requestedSampleRateHz = 0;
        std::uint32_t requestedInactivityTimeoutSeconds = 0;
        bool requestedUnlimitedDuration = false;
        bool requestedUseLxrsPlus = false;

        NodeConfigSnapshot before;
        NodeConfigSnapshot after;
    };
}
