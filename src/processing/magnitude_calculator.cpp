#include "acceltool/processing/magnitude_calculator.h"

#include <cmath>

namespace acceltool
{
    MagnitudeCalculator::MagnitudeCalculator(const AppConfig& config)
        : m_axisMode(config.axisMode),
          m_spec(config.spec)
    {
    }

    ProcessedSample MagnitudeCalculator::process(const RawSample& sample) const
    {
        ProcessedSample out{};

        out.sampleIndex = sample.sampleIndex;
        out.nodeAddress = sample.nodeAddress;
        
        out.deviceTick = sample.deviceTick;
        out.deviceTimestampUnixNs = sample.deviceTimestampUnixNs;

        out.tickGapDetected = sample.tickGapDetected;
        out.tickGapCount = sample.tickGapCount;

        out.timestampGapDetected = sample.timestampGapDetected;
        out.timestampGapNs = sample.timestampGapNs;
        out.expectedTimestampStepNs = sample.expectedTimestampStepNs;


        out.x = sample.x;
        out.y = sample.y;
        out.z = sample.z;

        out.magnitudeXY = std::sqrt(sample.x * sample.x + sample.y * sample.y);
        out.magnitudeXYZ = std::sqrt(sample.x * sample.x + sample.y * sample.y + sample.z * sample.z);
        
        if (sample.z != 0.0)
        {
            out.normLatG = out.magnitudeXY / sample.z;
        }
        else
        {
            out.normLatG = 0.0;
        }

        out.appliedSpec = m_spec;

        if (m_axisMode == AxisMode::XY)
        {
            out.exceedsSpec = (out.magnitudeXY > m_spec);
        }
        else
        {
            out.exceedsSpec = (out.magnitudeXYZ > m_spec);
        }

        out.baseRssi = sample.baseRssi;
        out.nodeRssi = sample.nodeRssi;

        return out;
    }
}
