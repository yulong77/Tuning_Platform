#include "acceltool/processing/display_aggregator.h"

#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace acceltool
{
    DisplayAggregator::DisplayAggregator(std::size_t bucketSampleCount)
        : m_bucketSampleCount(bucketSampleCount)
    {
        if (m_bucketSampleCount == 0)
        {
            throw std::invalid_argument("bucketSampleCount must be > 0");
        }
    }

    std::optional<DisplayBucket> DisplayAggregator::consume(const ProcessedSample& sample)
    {
        if (!m_hasOpenBucket)
        {
            m_current = {};
            m_current.bucketIndex = m_bucketIndex;
            m_current.startSampleIndex = sample.sampleIndex;
            m_current.endSampleIndex = sample.sampleIndex;
            m_current.startDeviceTimestampUnixNs = sample.deviceTimestampUnixNs;
            m_current.endDeviceTimestampUnixNs = sample.deviceTimestampUnixNs;
            m_current.sampleCount = 1;
    
            m_current.peakX = sample.x;
            m_current.peakY = sample.y;
            m_current.peakZ = sample.z;
    
            m_current.maxMagnitudeXY = sample.magnitudeXY;
            m_current.maxMagnitudeXYZ = sample.magnitudeXYZ;
            m_current.maxNormLatG = sample.normLatG;
            m_hasOpenBucket = true;
        }
        else
        {
            m_current.endSampleIndex = sample.sampleIndex;
            m_current.endDeviceTimestampUnixNs = sample.deviceTimestampUnixNs;
            ++m_current.sampleCount;
    
            if (std::abs(sample.x) > std::abs(m_current.peakX))
            {
                m_current.peakX = sample.x;
            }
    
            if (std::abs(sample.y) > std::abs(m_current.peakY))
            {
                m_current.peakY = sample.y;
            }
    
            if (std::abs(sample.z) > std::abs(m_current.peakZ))
            {
                m_current.peakZ = sample.z;
            }
    
            m_current.maxMagnitudeXY = std::max(m_current.maxMagnitudeXY, sample.magnitudeXY);
            m_current.maxMagnitudeXYZ = std::max(m_current.maxMagnitudeXYZ, sample.magnitudeXYZ);
            m_current.maxNormLatG = std::max(m_current.maxNormLatG, sample.normLatG);
        }
    
        if (m_current.sampleCount >= m_bucketSampleCount)
        {
            DisplayBucket finished = m_current;
            ++m_bucketIndex;
            m_hasOpenBucket = false;
            m_current = {};
            return finished;
        }
    
        return std::nullopt;
    }


    std::optional<DisplayBucket> DisplayAggregator::flush()
    {
        if (!m_hasOpenBucket)
        {
            return std::nullopt;
        }

        DisplayBucket finished = m_current;
        ++m_bucketIndex;
        m_hasOpenBucket = false;
        m_current = {};
        return finished;
    }
}
