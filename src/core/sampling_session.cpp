#include "acceltool/core/sampling_session.h"

#include <stdexcept>
#include <utility>
#include <cmath>

#include <mscl/mscl.h>

#include "acceltool/processing/display_aggregator.h"
#include "acceltool/processing/magnitude_calculator.h"
#include "acceltool/utils/csv_writer.h"
#include "acceltool/utils/display_csv_writer.h"
#include "acceltool/utils/logger.h"

namespace acceltool
{
    void WorkerErrorState::captureIfEmpty(const std::string& who, std::exception_ptr ex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!exception)
        {
            exception = ex;
            source = who;
        }
    }

    bool WorkerErrorState::hasError() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return static_cast<bool>(exception);
    }

    std::exception_ptr WorkerErrorState::getException() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return exception;
    }

    std::string WorkerErrorState::getSource() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return source;
    }

    SamplingSession::SamplingSession(WirelessAccelerometerManager& manager, const AppConfig& config)
        : m_manager(manager),
          m_config(config)
    {
    }

    SamplingSession::~SamplingSession()
    {
        try
        {
            stop();
        }
        catch (...)
        {
        }
    }

    void SamplingSession::start()
    {
        if (m_running.load())
        {
            throw std::runtime_error("Sampling session is already running.");
        }

        m_stopRequested = false;
        m_sessionFinished = false;
        m_sessionException = nullptr;

        m_thread = std::thread([this]() {
            try
            {
                run();
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(m_exceptionMutex);
                m_sessionException = std::current_exception();
            }

            m_running = false;
            m_sessionFinished = true;
        });
    }

    void SamplingSession::stop()
    {
        if (m_running.load())
        {
            m_stopRequested = true;

            std::lock_guard<std::mutex> lock(m_controlMutex);
            if (m_rawQueue)
            {
                m_rawQueue->close();
            }
            if (m_writeQueue)
            {
                m_writeQueue->close();
            }
        }

        if (m_thread.joinable())
        {
            m_thread.join();
        }

        rethrowIfFailed();
    }

    bool SamplingSession::isRunning() const noexcept
    {
        return m_running.load();
    }

    bool SamplingSession::isFinished() const noexcept
    {
        return m_sessionFinished.load();
    }

    SampleRateStabilitySummary SamplingSession::sampleRateStabilitySummary() const
    {
        return m_stabilitySummary;
    }

    SamplingDiagnosticsSummary SamplingSession::diagnosticsSummary() const
    {
        return m_diagnosticsSummary;
    }



    void SamplingSession::rethrowIfFailed()
    {
        std::lock_guard<std::mutex> lock(m_exceptionMutex);
        if (m_sessionException)
        {
            std::rethrow_exception(m_sessionException);
        }
    }

    void SamplingSession::requestStop(
        BlockingQueue<std::vector<RawSample>>& rawQueue,
        BlockingQueue<WritePayload>& writeQueue)
    {
        m_stopRequested = true;
        rawQueue.close();
        writeQueue.close();
    }

    void SamplingSession::logFinalStats(
        const RuntimeStats& stats,
        const BlockingQueue<std::vector<RawSample>>& rawQueue,
        const BlockingQueue<WritePayload>& writeQueue) const
    {
        logInfo("========== FINAL STATS ==========");
        logInfo("deviceReadCalls            = " + std::to_string(stats.deviceReadCalls.load()));
        logInfo("emptyReadCount             = " + std::to_string(stats.emptyReadCount.load()));

        logInfo("samplesReceivedFromDevice  = " + std::to_string(stats.samplesReceivedFromDevice.load()));
        logInfo("samplesPushedToRawQueue    = " + std::to_string(stats.samplesPushedToRawQueue.load()));
        logInfo("samplesPoppedFromRawQueue  = " + std::to_string(stats.samplesPoppedFromRawQueue.load()));

        logInfo("samplesProcessed           = " + std::to_string(stats.samplesProcessed.load()));
        logInfo("samplesPushedToWriteQueue  = " + std::to_string(stats.samplesPushedToWriteQueue.load()));
        logInfo("samplesPoppedFromWriteQueue= " + std::to_string(stats.samplesPoppedFromWriteQueue.load()));
        logInfo("samplesWrittenToCsv        = " + std::to_string(stats.samplesWrittenToCsv.load()));

        logInfo("displayBucketsProduced     = " + std::to_string(stats.displayBucketsProduced.load()));
        logInfo("displayBucketsWritten      = " + std::to_string(stats.displayBucketsWritten.load()));

        // logInfo("rawQueueCurrentBatches     = " + std::to_string(rawQueue.size()));
        logInfo("rawQueuePeakBatches        = " + std::to_string(rawQueue.peakSize()));
        //logInfo("writeQueueCurrentBatches   = " + std::to_string(writeQueue.size()));
        logInfo("writeQueuePeakBatches      = " + std::to_string(writeQueue.peakSize()));

        // logInfo("acquisitionThreadStarted   = " + std::string(stats.acquisitionThreadStarted.load() ? "true" : "false"));
        // logInfo("acquisitionThreadFinished  = " + std::string(stats.acquisitionThreadFinished.load() ? "true" : "false"));
        // logInfo("processingThreadStarted    = " + std::string(stats.processingThreadStarted.load() ? "true" : "false"));
        // logInfo("processingThreadFinished   = " + std::string(stats.processingThreadFinished.load() ? "true" : "false"));
        // logInfo("writerThreadStarted        = " + std::string(stats.writerThreadStarted.load() ? "true" : "false"));
        // logInfo("writerThreadFinished       = " + std::string(stats.writerThreadFinished.load() ? "true" : "false"));
        
        logInfo("samplesWithTickGap         = " + std::to_string(stats.samplesWithTickGap.load()));
        logInfo("totalMissingTicks          = " + std::to_string(stats.totalMissingTicks.load()));
        logInfo("samplesWithTimestampGap    = " + std::to_string(stats.samplesWithTimestampGap.load()));

    }

    SampleRateStabilitySummary SamplingSession::buildSampleRateStabilitySummary(
        const RuntimeStats& stats) const
    {
        SampleRateStabilitySummary summary{};

        const std::size_t sampleCount = stats.samplesWrittenToCsv.load();
        summary.validSampleCount = sampleCount;
        summary.expectedSampleRateHz = static_cast<double>(m_config.sampleRateHz);

        if (sampleCount < 2 || m_config.sampleRateHz == 0 || !stats.hasFirstDeviceTimestamp.load())
        {
            summary.hasEnoughSamples = false;
            return summary;
        }

        summary.hasEnoughSamples = true;
        summary.firstDeviceTimestampUnixNs = stats.firstDeviceTimestampUnixNs.load();
        summary.lastDeviceTimestampUnixNs = stats.lastDeviceTimestampUnixNs.load();

        summary.actualDurationNs =
            summary.lastDeviceTimestampUnixNs - summary.firstDeviceTimestampUnixNs;

        const double expectedStepNs =
            static_cast<double>(mscl::TimeSpan::NANOSECONDS_PER_SECOND) /
            static_cast<double>(m_config.sampleRateHz);

        summary.expectedDurationNs = static_cast<std::uint64_t>(
            (static_cast<double>(sampleCount - 1)) * expectedStepNs);

        if (summary.actualDurationNs > 0 && summary.expectedDurationNs > 0)
        {
            const double actualDurationSec =
                static_cast<double>(summary.actualDurationNs) /
                static_cast<double>(mscl::TimeSpan::NANOSECONDS_PER_SECOND);

            summary.actualSampleRateHz =
                static_cast<double>(sampleCount - 1) / actualDurationSec;
            
                // PPM error is (actual - expected) / expected * 1e6
            summary.ppmError =                              
                ((static_cast<double>(summary.actualDurationNs) -
                  static_cast<double>(summary.expectedDurationNs)) /
                 static_cast<double>(summary.expectedDurationNs)) * 1.0e6;

            summary.withinPlusMinus5Ppm = (std::abs(summary.ppmError) <= 5.0);
        }

        return summary;
    }

    void SamplingSession::validateFinalStats(
        const RuntimeStats& stats,
        const BlockingQueue<std::vector<RawSample>>& rawQueue,
        const BlockingQueue<WritePayload>& writeQueue) const
    {
        const std::size_t pushedToRaw     = stats.samplesPushedToRawQueue.load();
        const std::size_t poppedFromRaw   = stats.samplesPoppedFromRawQueue.load();
        const std::size_t processed       = stats.samplesProcessed.load();
        const std::size_t pushedToWrite   = stats.samplesPushedToWriteQueue.load();
        const std::size_t poppedFromWrite = stats.samplesPoppedFromWriteQueue.load();
        const std::size_t written         = stats.samplesWrittenToCsv.load();

        const std::size_t bucketsProduced = stats.displayBucketsProduced.load();
        const std::size_t bucketsWritten  = stats.displayBucketsWritten.load();

        if (!stats.acquisitionThreadFinished.load())
        {
            throw std::runtime_error("Acquisition thread did not finish cleanly.");
        }

        if (!stats.processingThreadFinished.load())
        {
            throw std::runtime_error("Processing thread did not finish cleanly.");
        }

        if (!stats.writerThreadFinished.load())
        {
            throw std::runtime_error("Writer thread did not finish cleanly.");
        }

        if (m_config.maxSamples > 0 && pushedToRaw > m_config.maxSamples)
        {
            throw std::runtime_error(
                "samplesPushedToRawQueue exceeded config.maxSamples. "
                "pushedToRaw=" + std::to_string(pushedToRaw) +
                ", maxAllowed=" + std::to_string(m_config.maxSamples));
        }

        if (poppedFromRaw != pushedToRaw)
        {
            throw std::runtime_error(
                "samplesPoppedFromRawQueue does not match samplesPushedToRawQueue. "
                "poppedFromRaw=" + std::to_string(poppedFromRaw) +
                ", pushedToRaw=" + std::to_string(pushedToRaw));
        }

        if (processed != poppedFromRaw)
        {
            throw std::runtime_error(
                "samplesProcessed does not match samplesPoppedFromRawQueue. "
                "processed=" + std::to_string(processed) +
                ", poppedFromRaw=" + std::to_string(poppedFromRaw));
        }

        if (pushedToWrite != processed)
        {
            throw std::runtime_error(
                "samplesPushedToWriteQueue does not match samplesProcessed. "
                "pushedToWrite=" + std::to_string(pushedToWrite) +
                ", processed=" + std::to_string(processed));
        }

        if (poppedFromWrite != pushedToWrite)
        {
            throw std::runtime_error(
                "samplesPoppedFromWriteQueue does not match samplesPushedToWriteQueue. "
                "poppedFromWrite=" + std::to_string(poppedFromWrite) +
                ", pushedToWrite=" + std::to_string(pushedToWrite));
        }

        if (written != poppedFromWrite)
        {
            throw std::runtime_error(
                "samplesWrittenToCsv does not match samplesPoppedFromWriteQueue. "
                "written=" + std::to_string(written) +
                ", poppedFromWrite=" + std::to_string(poppedFromWrite));
        }

        if (bucketsWritten != bucketsProduced)
        {
            throw std::runtime_error(
                "displayBucketsWritten does not match displayBucketsProduced. "
                "written=" + std::to_string(bucketsWritten) +
                ", produced=" + std::to_string(bucketsProduced));
        }

        if (rawQueue.size() != 0)
        {
            throw std::runtime_error(
                "rawQueue is not empty at shutdown. remaining batches=" +
                std::to_string(rawQueue.size()));
        }

        if (writeQueue.size() != 0)
        {
            throw std::runtime_error(
                "writeQueue is not empty at shutdown. remaining batches=" +
                std::to_string(writeQueue.size()));
        }
    }

    void SamplingSession::run()
    {
        m_running = true;

        CsvWriter writer;
        DisplayCsvWriter displayWriter;
        writer.open(m_config.outputCsvPath);
        writer.writeHeader();

        displayWriter.open(m_config.outputDisplayCsvPath);
        displayWriter.writeHeader();

        RuntimeStats stats;
        WorkerErrorState workerError;

        BlockingQueue<std::vector<RawSample>> rawQueue(m_config.rawQueueCapacityBatches);
        BlockingQueue<WritePayload> writeQueue(m_config.writeQueueCapacityBatches);

        {
            std::lock_guard<std::mutex> lock(m_controlMutex);
            m_rawQueue = &rawQueue;
            m_writeQueue = &writeQueue;
        }

        m_manager.startSampling();

        std::thread acquisitionThread([&]() {
            stats.acquisitionThreadStarted = true;

            try
            {
                const bool unlimitedSamples = (m_config.maxSamples == 0);

                while (!m_stopRequested.load())
                {
                    const std::size_t pushedAlready = stats.samplesPushedToRawQueue.load();

                    if (!unlimitedSamples && pushedAlready >= m_config.maxSamples)
                    {
                        break;
                    }

                    ++stats.deviceReadCalls;

                    std::vector<RawSample> batch = m_manager.readAvailableSamples(m_config.readTimeoutMs);
                    if (batch.empty())
                    {
                        ++stats.emptyReadCount;
                        continue;
                    }

                    stats.samplesReceivedFromDevice += batch.size();

                    if (!unlimitedSamples)
                    {
                        const std::size_t remaining = m_config.maxSamples - pushedAlready;
                        if (batch.size() > remaining)
                        {
                            batch.resize(remaining);
                        }
                    }

                    const std::size_t batchCount = batch.size();
                    if (batchCount == 0)
                    {
                        continue;
                    }

                    if (!rawQueue.push(std::move(batch)))
                    {
                        break;
                    }

                    stats.samplesPushedToRawQueue += batchCount;
                }
            }
            catch (...)
            {
                workerError.captureIfEmpty("acquisitionThread", std::current_exception());
                requestStop(rawQueue, writeQueue);
            }

            rawQueue.close();
            stats.acquisitionThreadFinished = true;
        });

        std::thread processingThread([&]() {
            stats.processingThreadStarted = true;

            try
            {
                MagnitudeCalculator calculator(m_config);
                DisplayAggregator aggregator(m_config.displayAggregationSamples);

                std::vector<RawSample> rawBatch;
                while (rawQueue.waitPop(rawBatch))
                {
                    stats.samplesPoppedFromRawQueue += rawBatch.size();

                    WritePayload payload;
                    payload.processedSamples.reserve(rawBatch.size());

                    for (const RawSample& raw : rawBatch)
                    {
                        const ProcessedSample processed = calculator.process(raw);
                        payload.processedSamples.push_back(processed);
                        ++stats.samplesProcessed;

                        if (!stats.hasFirstDeviceTimestamp.load())
                        {
                            stats.firstDeviceTimestampUnixNs = processed.deviceTimestampUnixNs;
                            stats.hasFirstDeviceTimestamp = true;
                        }

                        stats.lastDeviceTimestampUnixNs = processed.deviceTimestampUnixNs;


                        if (processed.tickGapDetected)
                        {
                            ++stats.samplesWithTickGap;
                            stats.totalMissingTicks += processed.tickGapCount;
                        }

                        if (processed.timestampGapDetected)
                        {
                            ++stats.samplesWithTimestampGap;
                        }

                        if (processed.x > stats.maxPeakX)
                        {
                            stats.maxPeakX = processed.x;
                        }
                        
                        if (processed.y > stats.maxPeakY)
                        {
                            stats.maxPeakY = processed.y;
                        }
                        
                        if (processed.z > stats.maxPeakZ)
                        {
                            stats.maxPeakZ = processed.z;
                        }
                        
                        if (processed.magnitudeXY > stats.maxMagnitudeXY)
                        {
                            stats.maxMagnitudeXY = processed.magnitudeXY;
                        }
                        
                        if (processed.magnitudeXYZ > stats.maxMagnitudeXYZ)
                        {
                            stats.maxMagnitudeXYZ = processed.magnitudeXYZ;
                        }
                        
                        if (processed.normLatG > stats.maxNormLatG)
                        {
                            stats.maxNormLatG = processed.normLatG;
                        }

                        auto bucketOpt = aggregator.consume(processed);
                        if (bucketOpt.has_value())
                        {
                            payload.displayBuckets.push_back(*bucketOpt);
                            ++stats.displayBucketsProduced;
                        }
                    }

                    if (!payload.processedSamples.empty())
                    {
                        const std::size_t payloadSampleCount = payload.processedSamples.size();

                        if (!writeQueue.push(std::move(payload)))
                        {
                            break;
                        }

                        stats.samplesPushedToWriteQueue += payloadSampleCount;
                    }
                }

                auto finalBucket = aggregator.flush();
                if (finalBucket.has_value() && !m_stopRequested.load())
                {
                    WritePayload tailPayload;
                    tailPayload.displayBuckets.push_back(*finalBucket);
                    ++stats.displayBucketsProduced;

                    if (!writeQueue.push(std::move(tailPayload)))
                    {
                        throw std::runtime_error("Failed to push final display bucket into writeQueue because the queue is closed.");
                    }
                }
            }
            catch (...)
            {
                workerError.captureIfEmpty("processingThread", std::current_exception());
                requestStop(rawQueue, writeQueue);
            }

            writeQueue.close();
            stats.processingThreadFinished = true;
        });

        std::thread writerThread([&]() {
            stats.writerThreadStarted = true;

            try
            {
                std::size_t nextConsolePrintAt = m_config.displayAggregationSamples;

                WritePayload payload;
                while (writeQueue.waitPop(payload))
                {
                    stats.samplesPoppedFromWriteQueue += payload.processedSamples.size();

                    for (const ProcessedSample& sample : payload.processedSamples)
                    {
                        writer.writeRow(sample);
                        ++stats.samplesWrittenToCsv;
                    }

                    for (const DisplayBucket& bucket : payload.displayBuckets)
                    {
                        displayWriter.writeRow(bucket);
                        ++stats.displayBucketsWritten;
                    }

                    if (m_config.printToConsole && !payload.processedSamples.empty())
                    {
                        while (stats.samplesWrittenToCsv.load() >= nextConsolePrintAt)
                        {
                            const ProcessedSample& s = payload.processedSamples.back();
                            logInfo(
                                "sample=" + std::to_string(s.sampleIndex) +
                                ", x=" + std::to_string(s.x) +
                                ", y=" + std::to_string(s.y) +
                                ", z=" + std::to_string(s.z) +
                                ", magXY=" + std::to_string(s.magnitudeXY) +
                                ", magXYZ=" + std::to_string(s.magnitudeXYZ) +
                                ", spec=" + std::to_string(s.appliedSpec) +
                                ", exceedsSpec=" + std::string(s.exceedsSpec ? "1" : "0"));
                            nextConsolePrintAt += m_config.displayAggregationSamples;
                        }
                    }
                }

                writer.flush();
                displayWriter.flush();
            }
            catch (...)
            {
                workerError.captureIfEmpty("writerThread", std::current_exception());
                requestStop(rawQueue, writeQueue);
            }

            stats.writerThreadFinished = true;
        });

        acquisitionThread.join();
        processingThread.join();
        writerThread.join();

        try
        {
            m_manager.stopSampling();
        }
        catch (const mscl::Error& e)
        {
            logError(std::string("MSCL error during manager.stopSampling(): ") + e.what());
        }
        catch (const std::exception& e)
        {
            logError(std::string("Failed during manager.stopSampling(): ") + e.what());
        }

        {
            std::lock_guard<std::mutex> lock(m_controlMutex);
            m_rawQueue = nullptr;
            m_writeQueue = nullptr;
        }

        logFinalStats(stats, rawQueue, writeQueue);

        m_stabilitySummary = buildSampleRateStabilitySummary(stats);

        m_diagnosticsSummary.samplesWithTickGap = stats.samplesWithTickGap.load();
        m_diagnosticsSummary.totalMissingTicks = stats.totalMissingTicks.load();
        m_diagnosticsSummary.samplesWithTimestampGap = stats.samplesWithTimestampGap.load();
        
        m_diagnosticsSummary.maxPeakX = stats.maxPeakX;
        m_diagnosticsSummary.maxPeakY = stats.maxPeakY;
        m_diagnosticsSummary.maxPeakZ = stats.maxPeakZ;
        m_diagnosticsSummary.maxMagnitudeXY = stats.maxMagnitudeXY;
        m_diagnosticsSummary.maxMagnitudeXYZ = stats.maxMagnitudeXYZ;
        m_diagnosticsSummary.maxNormLatG = stats.maxNormLatG;
        
        m_diagnosticsSummary.stability = m_stabilitySummary;

        if (workerError.hasError())
        {
            logError("Worker thread failed: " + workerError.getSource());
            std::rethrow_exception(workerError.getException());
        }

        validateFinalStats(stats, rawQueue, writeQueue);
        logInfo("Sampling session finished successfully.");

    }
}
