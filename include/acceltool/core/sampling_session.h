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
    };
}
