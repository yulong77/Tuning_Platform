#include "acceltool/core/interactive_app.h"

#include <cctype>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include <mscl/mscl.h>

#include "acceltool/backend/wireless_accelerometer_manager.h"
#include "acceltool/core/app_config.h"
#include "acceltool/core/sampling_session.h"
#include "acceltool/utils/logger.h"

namespace acceltool
{
    namespace
    {
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

        void logSampleRateStabilitySummary(const SampleRateStabilitySummary& summary)
        {
            logInfo("========== SAMPLE RATE STABILITY ==========");

            if (!summary.hasEnoughSamples)
            {
                logInfo("Not enough samples to evaluate sample rate stability.");
                return;
            }

            logInfo("validSampleCount           = " + std::to_string(summary.validSampleCount));
            logInfo("firstDeviceTimestampUnixNs = " + std::to_string(summary.firstDeviceTimestampUnixNs));
            logInfo("lastDeviceTimestampUnixNs  = " + std::to_string(summary.lastDeviceTimestampUnixNs));
            logInfo("expectedDurationNs         = " + std::to_string(summary.expectedDurationNs));
            logInfo("actualDurationNs           = " + std::to_string(summary.actualDurationNs));
            logInfo("expectedSampleRateHz       = " + std::to_string(summary.expectedSampleRateHz));
            logInfo("actualSampleRateHz         = " + std::to_string(summary.actualSampleRateHz));
            logInfo("ppmError                   = " + std::to_string(summary.ppmError));
            logInfo("withinPlusMinus5Ppm        = " + std::string(summary.withinPlusMinus5Ppm ? "true" : "false"));
        }


        class InteractiveAppImpl
        {
        public:
            int run(int argc, char* argv[])
            {
                try
                {
                    loadConfig(argc, argv);
                    initializeLogging();
                    connectManager();
                    runCommandLoop();

                    logInfo("Program finished successfully.");
                    shutdownLogger();
                    return 0;
                }
                catch (const mscl::Error& e)
                {
                    logError(std::string("MSCL ERROR: ") + e.what());
                    shutdownLogger();
                    waitForEnterToExit(std::string("Program failed.\n\nDetails: ") + e.what());
                    return 1;
                }
                catch (const std::exception& e)
                {
                    logError(std::string("STD ERROR: ") + e.what());
                    shutdownLogger();
                    waitForEnterToExit(std::string("Program failed.\n\nDetails: ") + e.what());
                    return 1;
                }
            }

        private:
            void loadConfig(int argc, char* argv[])
            {
                if (argc >= 2)
                {
                    m_configPath = argv[1];
                }

                std::string loadError;
                if (!loadConfigFromFile(m_configPath, m_config, loadError))
                {
                    throw std::runtime_error(
                        "Failed to load config file.\nPath: " + m_configPath + "\n" + loadError);
                }

                std::string validateError;
                if (!validateConfig(m_config, validateError))
                {
                    throw std::runtime_error("Invalid configuration.\n" + validateError);
                }
            }

            void initializeLogging() const
            {
                initLogger(LogLevel::Debug, true, true, "acceltool.log");
                logInfo("AccelTool started.");
                logInfo("Loaded config file: " + m_configPath);
                logInfo("Using port: " + m_config.port);
                logInfo("Using node address: " + std::to_string(m_config.nodeAddress));
                logInfo("Using baudrate: " + std::to_string(m_config.baudrate));
                logInfo("Requested sample rate from ini: " + std::to_string(m_config.sampleRateHz));
                logInfo("Raw/result CSV will be written when sampling starts: " + m_config.outputCsvPath);
                logInfo("Display CSV will be written when sampling starts: " + m_config.outputDisplayCsvPath);
            }

            void connectManager()
            {
                m_manager.connect(m_config);
            }

            void printMenu() const
            {
                std::cout << "\nAccelTool interactive mode\n"
                          << "  I = Set to Idle + initialize device\n"
                          << "  S = Start receiving data\n"
                          << "  T = Stop receiving data\n"
                          << "  Q = Quit\n";
            }

            void runCommandLoop()
            {
                printMenu();

                while (true)
                {
                    finalizeFinishedSessionIfNeeded();

                    const std::string command = readCommand("\nEnter command [I/S/T/Q]: ");
                    if (command.empty())
                    {
                        continue;
                    }

                    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(command[0])));

                    if (c == 'Q')
                    {
                        stopSessionForExitIfNeeded();
                        break;
                    }

                    if (c == 'I')
                    {
                        handleInitializeCommand();
                        continue;
                    }

                    if (c == 'S')
                    {
                        handleStartCommand();
                        continue;
                    }

                    if (c == 'T')
                    {
                        handleStopCommand();
                        continue;
                    }

                    std::cout << "Unknown command. Use I, S, T, or Q.\n";
                }
            }

            void finalizeFinishedSessionIfNeeded()
            {
                if (!m_session || !m_session->isFinished())
                {
                    return;
                }

                try
                {
                    m_session->stop();
                    std::cout << "\nSampling stopped. CSV files were written successfully.\n";

                    const SamplingDiagnosticsSummary diagnostics =
                        m_session->diagnosticsSummary();

                    logSampleRateStabilitySummary(diagnostics.stability);
                    logFinalSamplingConclusions(
                        diagnostics,
                        diagnostics.stability);
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Sampling session failed: ") + e.what());
                    std::cout << "\nSampling stopped with an error:\n" << e.what() << "\n";
                }

                clearCompletedSessionState();
                std::cout << "Device returned to idle. Run I again before starting another session.\n";
            }

            void handleInitializeCommand()
            {
                if (isSessionRunning())
                {
                    std::cout << "Sampling is currently running. Stop it first.\n";
                    return;
                }

                try
                {
                    std::cout << "\nRunning manual Set to Idle + initialization...\n";
                    m_manager.initialize(true);
                    m_deviceReady = true;
                    std::cout << "\nDevice is ready. You can now press S to start receiving data.\n";
                }
                catch (const mscl::Error& e)
                {
                    m_deviceReady = false;
                    logError(std::string("MSCL ERROR during manual initialize: ") + e.what());
                    std::cout << "\nInitialize failed. This is often recoverable; press I again to retry.\n"
                              << "MSCL details: " << e.what() << "\n";
                }
                catch (const std::exception& e)
                {
                    m_deviceReady = false;
                    logError(std::string("STD ERROR during manual initialize: ") + e.what());
                    std::cout << "\nInitialize failed. Press I again to retry.\n"
                              << "Details: " << e.what() << "\n";
                }
            }

            void handleStartCommand()
            {
                if (m_samplingHasRun)
                {
                    std::cout << "This run already wrote CSV output once. Restart the program for a fresh capture, or press I and then S if you want to overwrite the CSV files again.\n";
                }

                if (isSessionRunning())
                {
                    std::cout << "Sampling is already running.\n";
                    return;
                }

                if (!m_deviceReady)
                {
                    std::cout << "Device is not ready yet. Press I first until initialization succeeds.\n";
                    return;
                }

                try
                {
                    m_session = std::make_unique<SamplingSession>(m_manager, m_config);
                    m_session->start();
                    std::cout << "\nSampling started. Press T when you want to stop receiving data.\n";
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Failed to start sampling session: ") + e.what());
                    std::cout << "\nFailed to start sampling:\n" << e.what() << "\n";
                    m_session.reset();
                    m_deviceReady = false;
                }
            }

            void handleStopCommand()
            {
                if (!isSessionRunning())
                {
                    std::cout << "Sampling is not running.\n";
                    return;
                }

                try
                {
                    std::cout << "Stopping sampling...\n";
                    m_session->stop();
                    std::cout << "Sampling stopped.\n";
                }
                catch (const std::exception& e)
                {
                    logError(std::string("Error while stopping sampling: ") + e.what());
                    std::cout << "Sampling stopped with an error:\n" << e.what() << "\n";
                }

                const SamplingDiagnosticsSummary diagnostics =
                    m_session->diagnosticsSummary();

                logSampleRateStabilitySummary(diagnostics.stability);
                logFinalSamplingConclusions(
                    diagnostics,
                    diagnostics.stability);


                clearCompletedSessionState();
                std::cout << "Device returned to idle. Press I again before the next start.\n";
            }

            void logFinalSamplingConclusions(
                    const SamplingDiagnosticsSummary& diagnostics,
                    const SampleRateStabilitySummary& summary)
            {
                const bool tickBasedDataLossDetected =
                    (diagnostics.totalMissingTicks > 0);
            
                const bool sampleRateStabilityFailed =
                    summary.hasEnoughSamples && !summary.withinPlusMinus5Ppm;
            
                const bool timestampGapRangeFailed =
                    (diagnostics.samplesWithTimestampGap > 0);
            
                logInfo("========== FINAL CONCLUSIONS ==========");
                logInfo("tickBasedDataLossCheck     = " +
                        std::string(tickBasedDataLossDetected ? "FAIL" : "PASS"));
                logInfo("sampleRateStabilityCheck   = " +
                        std::string(sampleRateStabilityFailed ? "FAIL" : "PASS"));
                logInfo("timestampGapRangeCheck     = " +
                        std::string(timestampGapRangeFailed ? "FAIL" : "PASS"));
                logInfo("maxPeakX                   = " +
                        std::to_string(diagnostics.maxPeakX));
                logInfo("maxPeakY                   = " +
                        std::to_string(diagnostics.maxPeakY));
                logInfo("maxPeakZ                   = " +
                        std::to_string(diagnostics.maxPeakZ));
                logInfo("maxMagnitudeXY             = " +
                        std::to_string(diagnostics.maxMagnitudeXY));
                logInfo("maxMagnitudeXYZ            = " +
                        std::to_string(diagnostics.maxMagnitudeXYZ));
                logInfo("maxNormLatG                = " +
                        std::to_string(diagnostics.maxNormLatG));
            }


            void stopSessionForExitIfNeeded()
            {
                if (!isSessionRunning())
                {
                    return;
                }

                std::cout << "Stopping active sampling session before exit...\n";
                m_session->stop();
            }

            bool isSessionRunning() const
            {
                return m_session && m_session->isRunning();
            }

            void clearCompletedSessionState()
            {
                m_session.reset();
                m_samplingHasRun = true;
                m_deviceReady = false;
            }

        private:
            AppConfig m_config;
            std::string m_configPath = "config/acceltool.ini";
            WirelessAccelerometerManager m_manager;
            std::unique_ptr<SamplingSession> m_session;
            bool m_deviceReady = false;
            bool m_samplingHasRun = false;
        };
    }

    int InteractiveApp::run(int argc, char* argv[])
    {
        InteractiveAppImpl impl;
        return impl.run(argc, argv);
    }
}
