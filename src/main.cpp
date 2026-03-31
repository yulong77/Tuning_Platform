#include <atomic>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <iostream>
#include <limits>

#include "acceltool/backend/wireless_accelerometer_manager.h"
#include "acceltool/core/app_config.h"
#include "acceltool/core/data_types.h"
#include "acceltool/processing/display_aggregator.h"
#include "acceltool/processing/magnitude_calculator.h"
#include "acceltool/utils/blocking_queue.h"
#include "acceltool/utils/csv_writer.h"
#include "acceltool/utils/display_csv_writer.h"
#include "acceltool/utils/logger.h"

namespace acceltool
{
    struct WritePayload
    {
        std::vector<ProcessedSample> processedSamples;
        std::vector<DisplayBucket> displayBuckets;
    };

    struct RuntimeStats
    {
        std::atomic<std::size_t> samplesReceivedFromDevice{0};   // Number of samples returned by manager.readAvailableSamples() and successfully converted
        std::atomic<std::size_t> samplesPushedToRawQueue{0};     // Number of samples successfully pushed into rawQueue
        std::atomic<std::size_t> samplesPoppedFromRawQueue{0};   // Number of samples popped from rawQueue by the processing thread
        
        std::atomic<std::size_t> samplesProcessed{0};            // Number of samples processed by calculator.process()
        std::atomic<std::size_t> samplesPushedToWriteQueue{0};   // Number of processed samples successfully pushed into writeQueue
        std::atomic<std::size_t> samplesPoppedFromWriteQueue{0}; // Number of processed samples popped from writeQueue by the writer thread
        std::atomic<std::size_t> samplesWrittenToCsv{0};         // Number of samples actually written to the main CSV file
        
        std::atomic<std::size_t> displayBucketsProduced{0};      // Number of display buckets produced by the aggregator
        std::atomic<std::size_t> displayBucketsWritten{0};       // Number of display buckets actually written to the display CSV file
        
        std::atomic<std::size_t> deviceReadCalls{0};             // Number of readAvailableSamples() calls
        std::atomic<std::size_t> emptyReadCount{0};              // Number of times readAvailableSamples() returned an empty batch

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

        void captureIfEmpty(const std::string& who, std::exception_ptr ex)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!exception)
            {
                exception = ex;
                source = who;
            }
        }

        bool hasError() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return static_cast<bool>(exception);
        }

        std::exception_ptr getException() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return exception;
        }

        std::string getSource() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return source;
        }
    };

    void requestStop(
        std::atomic<bool>& stopRequested,
        BlockingQueue<std::vector<RawSample>>& rawQueue,
        BlockingQueue<WritePayload>& writeQueue)
    {
        stopRequested = true;
        rawQueue.close();
        writeQueue.close();
    }

    void logFinalStats(
        const RuntimeStats& stats,
        const BlockingQueue<std::vector<RawSample>>& rawQueue,
        const BlockingQueue<WritePayload>& writeQueue)
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

        logInfo("rawQueueCurrentBatches     = " + std::to_string(rawQueue.size()));
        logInfo("rawQueuePeakBatches        = " + std::to_string(rawQueue.peakSize()));
        logInfo("writeQueueCurrentBatches   = " + std::to_string(writeQueue.size()));
        logInfo("writeQueuePeakBatches      = " + std::to_string(writeQueue.peakSize()));

        logInfo("acquisitionThreadStarted   = " + std::string(stats.acquisitionThreadStarted.load() ? "true" : "false"));
        logInfo("acquisitionThreadFinished  = " + std::string(stats.acquisitionThreadFinished.load() ? "true" : "false"));
        logInfo("processingThreadStarted    = " + std::string(stats.processingThreadStarted.load() ? "true" : "false"));
        logInfo("processingThreadFinished   = " + std::string(stats.processingThreadFinished.load() ? "true" : "false"));
        logInfo("writerThreadStarted        = " + std::string(stats.writerThreadStarted.load() ? "true" : "false"));
        logInfo("writerThreadFinished       = " + std::string(stats.writerThreadFinished.load() ? "true" : "false"));
    }

    void validateFinalStats(
        const AppConfig& config,
        const RuntimeStats& stats,
        const BlockingQueue<std::vector<RawSample>>& rawQueue,
        const BlockingQueue<WritePayload>& writeQueue)
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

        if (config.maxSamples > 0 && pushedToRaw > config.maxSamples)
        {
            throw std::runtime_error(
                "samplesPushedToRawQueue exceeded config.maxSamples. "
                "pushedToRaw=" + std::to_string(pushedToRaw) +
                ", maxAllowed=" + std::to_string(config.maxSamples));
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

    void waitForEnterToExit(const std::string& message)
    {
        std::cout << '\n' << message << '\n';
        std::cout << "Press Enter to exit...";
        std::cout.flush();

        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
    }

    std::string readCommand(const std::string& prompt)
    {
        std::cout << '\n' << prompt;
        std::cout.flush();
    
        std::string line;
        std::getline(std::cin, line);
        return line;
    }

    class SamplingSession
    {
    public:
        SamplingSession(WirelessAccelerometerManager& manager, const AppConfig& config)
            : m_manager(manager),
              m_config(config)
        {
        }
    
        ~SamplingSession()
        {
            try
            {
                stop();
            }
            catch (...)
            {
            }
        }
    
        void start()
        {
            if (m_running.load())
            {
                throw std::runtime_error("Sampling session is already running.");
            }
    
            m_stopRequested = false;
            m_sessionFinished = false;
            m_sessionException = nullptr;
            m_sessionErrorSource.clear();
    
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
    
        void stop()
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
    
        bool isRunning() const noexcept
        {
            return m_running.load();
        }
    
        bool isFinished() const noexcept
        {
            return m_sessionFinished.load();
        }
    
        void rethrowIfFailed()
        {
            std::lock_guard<std::mutex> lock(m_exceptionMutex);
            if (m_sessionException)
            {
                std::rethrow_exception(m_sessionException);
            }
        }
    
    private:
        void run()
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
                    requestStop(m_stopRequested, rawQueue, writeQueue);
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
                    requestStop(m_stopRequested, rawQueue, writeQueue);
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
                    requestStop(m_stopRequested, rawQueue, writeQueue);
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
    
            if (workerError.hasError())
            {
                logError("Worker thread failed: " + workerError.getSource());
                std::rethrow_exception(workerError.getException());
            }
    
            validateFinalStats(m_config, stats, rawQueue, writeQueue);
            logInfo("Sampling session finished successfully.");
        }
    
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
        std::string m_sessionErrorSource;
    };
}


int main(int argc, char* argv[])
{
    using namespace acceltool;

    try
    {
        AppConfig config;

        std::string configPath = "config/acceltool.ini";
        if (argc >= 2)
        {
            configPath = argv[1];
        }

        std::string loadError;
        if (!loadConfigFromFile(configPath, config, loadError))
        {
            throw std::runtime_error(
                "Failed to load config file.\nPath: " + configPath + "\n" + loadError);
        }

        std::string validateError;
        if (!validateConfig(config, validateError))
        {
            throw std::runtime_error("Invalid configuration.\n" + validateError);
        }

        initLogger(LogLevel::Debug, true, true, "acceltool.log");
        logInfo("AccelTool started.");
        logInfo("Loaded config file: " + configPath);
        logInfo("Using port: " + config.port);
        logInfo("Using node address: " + std::to_string(config.nodeAddress));
        logInfo("Using baudrate: " + std::to_string(config.baudrate));
        logInfo("Requested sample rate from ini: " + std::to_string(config.sampleRateHz));
        logInfo("Raw/result CSV will be written when sampling starts: " + config.outputCsvPath);
        logInfo("Display CSV will be written when sampling starts: " + config.outputDisplayCsvPath);

        WirelessAccelerometerManager manager;
        manager.connect(config);

        bool deviceReady = false;
        bool samplingHasRun = false;
        std::unique_ptr<SamplingSession> session;

        std::cout << "\nAccelTool interactive mode\n"
                  << "  I = Set to Idle + initialize device\n"
                  << "  S = Start receiving data\n"
                  << "  T = Stop receiving data\n"
                  << "  Q = Quit\n";

        while (true)
        {
            if (session && session->isFinished())
            {
                try
                {
                    session->stop();
                    std::cout << "\nSampling stopped. CSV files were written successfully.\n";
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Sampling session failed: ") + e.what());
                    std::cout << "\nSampling stopped with an error:\n" << e.what() << "\n";
                }

                session.reset();
                samplingHasRun = true;
                deviceReady = false;
                std::cout << "Device returned to idle. Run I again before starting another session.\n";
            }

            std::string command = readCommand("\nEnter command [I/S/T/Q]: ");

            if (command.empty())
            {
                continue;
            }

            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(command[0])));

            if (c == 'Q')
            {
                if (session && session->isRunning())
                {
                    std::cout << "Stopping active sampling session before exit...\n";
                    session->stop();
                }
                break;
            }

            if (c == 'I')
            {
                if (session && session->isRunning())
                {
                    std::cout << "Sampling is currently running. Stop it first.\n";
                    continue;
                }

                try
                {
                    std::cout << "\nRunning manual Set to Idle + initialization...\n";
                    manager.initialize(true);
                    deviceReady = true;
                    std::cout << "\nDevice is ready. You can now press S to start receiving data.\n";
                }
                catch (const mscl::Error& e)
                {
                    deviceReady = false;
                    logError(std::string("MSCL ERROR during manual initialize: ") + e.what());
                    std::cout << "\nInitialize failed. This is often recoverable; press I again to retry.\n"
                              << "MSCL details: " << e.what() << "\n";
                }
                catch (const std::exception& e)
                {
                    deviceReady = false;
                    logError(std::string("STD ERROR during manual initialize: ") + e.what());
                    std::cout << "\nInitialize failed. Press I again to retry.\n"
                              << "Details: " << e.what() << "\n";
                }

                continue;
            }

            if (c == 'S')
            {
                if (samplingHasRun)
                {
                    std::cout << "This run already wrote CSV output once. Restart the program for a fresh capture, or press I and then S if you want to overwrite the CSV files again.\n";
                }

                if (session && session->isRunning())
                {
                    std::cout << "Sampling is already running.\n";
                    continue;
                }

                if (!deviceReady)
                {
                    std::cout << "Device is not ready yet. Press I first until initialization succeeds.\n";
                    continue;
                }

                try
                {
                    session = std::make_unique<SamplingSession>(manager, config);
                    session->start();
                    std::cout << "\nSampling started. Press T when you want to stop receiving data.\n";
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Failed to start sampling session: ") + e.what());
                    std::cout << "\nFailed to start sampling:\n" << e.what() << "\n";
                    session.reset();
                    deviceReady = false;
                }

                continue;
            }

            if (c == 'T')
            {
                if (!session || !session->isRunning())
                {
                    std::cout << "Sampling is not running.\n";
                    continue;
                }

                try
                {
                    std::cout << "Stopping sampling...\n";
                    session->stop();
                    std::cout << "Sampling stopped.\n";
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Error while stopping sampling: ") + e.what());
                    std::cout << "Sampling stopped with an error:\n" << e.what() << "\n";
                }

                session.reset();
                samplingHasRun = true;
                deviceReady = false;
                std::cout << "Device returned to idle. Press I again before the next start.\n";
                continue;
            }

            std::cout << "Unknown command. Use I, S, T, or Q.\n";
        }

        logInfo("Program finished successfully.");
        shutdownLogger();
        return 0;
    }
    catch (const mscl::Error& e)
    {
        acceltool::logError(std::string("MSCL ERROR: ") + e.what());
        acceltool::shutdownLogger();
        acceltool::waitForEnterToExit(
            std::string("Program failed.\n\nDetails: ") + e.what());
        return 1;
    }
    catch (const std::exception& e)
    {
        acceltool::logError(std::string("STD ERROR: ") + e.what());
        acceltool::shutdownLogger();
        acceltool::waitForEnterToExit(
            std::string("Program failed.\n\nDetails: ") + e.what());
        return 1;
    }
}
