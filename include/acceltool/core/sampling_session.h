#pragma once

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "acceltool/backend/wireless_accelerometer_manager.h"
#include "acceltool/core/app_config.h"
#include "acceltool/core/data_types.h"
#include "acceltool/utils/blocking_queue.h"

namespace acceltool
{
    struct WritePayload
    {
        std::vector<ProcessedSample> processedSamples;
        std::vector<DisplayBucket> displayBuckets;
    };

    struct RuntimeStats
    {
        std::atomic<std::size_t> samplesReceivedFromDevice{0};
        std::atomic<std::size_t> samplesPushedToRawQueue{0};
        std::atomic<std::size_t> samplesPoppedFromRawQueue{0};

        std::atomic<std::size_t> samplesProcessed{0};
        std::atomic<std::size_t> samplesPushedToWriteQueue{0};
        std::atomic<std::size_t> samplesPoppedFromWriteQueue{0};
        std::atomic<std::size_t> samplesWrittenToCsv{0};

        std::atomic<std::size_t> displayBucketsProduced{0};
        std::atomic<std::size_t> displayBucketsWritten{0};

        std::atomic<std::size_t> deviceReadCalls{0};
        std::atomic<std::size_t> emptyReadCount{0};

        std::atomic<bool> acquisitionThreadStarted{false};
        std::atomic<bool> acquisitionThreadFinished{false};

        std::atomic<bool> processingThreadStarted{false};
        std::atomic<bool> processingThreadFinished{false};

        std::atomic<bool> writerThreadStarted{false};
        std::atomic<bool> writerThreadFinished{false};

        std::atomic<std::size_t> samplesWithTickGap{0};
        std::atomic<std::size_t> totalMissingTicks{0};
        std::atomic<std::size_t> samplesWithTimestampGap{0};

        double maxPeakX = 0.0;
        double maxPeakY = 0.0;
        double maxPeakZ = 0.0;
        double maxMagnitudeXY = 0.0;
        double maxMagnitudeXYZ = 0.0;
        double maxNormLatG = 0.0;

        std::atomic<std::uint64_t> firstDeviceTimestampUnixNs{0};
        std::atomic<std::uint64_t> lastDeviceTimestampUnixNs{0};
        std::atomic<bool> hasFirstDeviceTimestamp{false};

    };

    struct SampleRateStabilitySummary
    {
        bool hasEnoughSamples = false;

        std::uint64_t firstDeviceTimestampUnixNs = 0;
        std::uint64_t lastDeviceTimestampUnixNs = 0;

        std::size_t validSampleCount = 0;

        std::uint64_t expectedDurationNs = 0;
        std::uint64_t actualDurationNs = 0;

        double expectedSampleRateHz = 0.0;
        double actualSampleRateHz = 0.0;
        double ppmError = 0.0;

        bool withinPlusMinus5Ppm = false;
    };

    struct SamplingDiagnosticsSummary
    {
        std::size_t samplesWithTickGap = 0;
        std::size_t totalMissingTicks = 0;
        std::size_t samplesWithTimestampGap = 0;

        double maxPeakX = 0.0;
        double maxPeakY = 0.0;
        double maxPeakZ = 0.0;
        double maxMagnitudeXY = 0.0;
        double maxMagnitudeXYZ = 0.0;
        double maxNormLatG = 0.0;

        SampleRateStabilitySummary stability;
    };



    struct WorkerErrorState
    {
        mutable std::mutex mutex;
        std::exception_ptr exception;
        std::string source;

        void captureIfEmpty(const std::string& who, std::exception_ptr ex);
        bool hasError() const;
        std::exception_ptr getException() const;
        std::string getSource() const;
    };

    class SamplingSession
    {
    public:
        SamplingSession(WirelessAccelerometerManager& manager, const AppConfig& config);
        ~SamplingSession();

        void start();
        void stop();

        bool isRunning() const noexcept;
        bool isFinished() const noexcept;

        void rethrowIfFailed();
        SampleRateStabilitySummary sampleRateStabilitySummary() const;
        SamplingDiagnosticsSummary diagnosticsSummary() const;


    private:
        void run();

        void requestStop(
            BlockingQueue<std::vector<RawSample>>& rawQueue,
            BlockingQueue<WritePayload>& writeQueue);

        void logFinalStats(
            const RuntimeStats& stats,
            const BlockingQueue<std::vector<RawSample>>& rawQueue,
            const BlockingQueue<WritePayload>& writeQueue) const;

        void validateFinalStats(
            const RuntimeStats& stats,
            const BlockingQueue<std::vector<RawSample>>& rawQueue,
            const BlockingQueue<WritePayload>& writeQueue) const;

        SampleRateStabilitySummary buildSampleRateStabilitySummary(
            const RuntimeStats& stats) const;

    private:
        WirelessAccelerometerManager& m_manager;
        const AppConfig& m_config;

        std::thread m_thread;
        std::atomic<bool> m_running{false};
        std::atomic<bool> m_sessionFinished{false};
        std::atomic<bool> m_stopRequested{false};

        mutable std::mutex m_controlMutex;
        BlockingQueue<std::vector<RawSample>>* m_rawQueue = nullptr;
        BlockingQueue<WritePayload>* m_writeQueue = nullptr;

        mutable std::mutex m_exceptionMutex;
        std::exception_ptr m_sessionException;

        SampleRateStabilitySummary m_stabilitySummary{};
        SamplingDiagnosticsSummary m_diagnosticsSummary{};


    };
}
