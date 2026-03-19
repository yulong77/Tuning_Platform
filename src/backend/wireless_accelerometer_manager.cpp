#include "acceltool/backend/wireless_accelerometer_manager.h"

#include <chrono>
#include <cctype>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sstream>

namespace acceltool
{
    namespace
    {
        bool containsIgnoreCase(const std::string& text, const std::string& token)
        {
            auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };

            std::string a;
            a.reserve(text.size());
            for (char c : text)
            {
                a.push_back(lower(static_cast<unsigned char>(c)));
            }

            std::string b;
            b.reserve(token.size());
            for (char c : token)
            {
                b.push_back(lower(static_cast<unsigned char>(c)));
            }

            return a.find(b) != std::string::npos;
        }

        bool looksLikeChannel1(const std::string& name)
        {
            return containsIgnoreCase(name, "ch1") ||
                   containsIgnoreCase(name, "channel 1");
        }

        bool looksLikeChannel2(const std::string& name)
        {
            return containsIgnoreCase(name, "ch2") ||
                   containsIgnoreCase(name, "channel 2");
        }

        bool looksLikeChannel3(const std::string& name)
        {
            return containsIgnoreCase(name, "ch3") ||
                   containsIgnoreCase(name, "channel 3");
        }
    }

    WirelessAccelerometerManager::WirelessAccelerometerManager() = default;

    WirelessAccelerometerManager::~WirelessAccelerometerManager()
    {
        try
        {
            stopSampling();
        }
        catch (...)
        {
        }
    }

    double WirelessAccelerometerManager::nowSeconds()
    {
        using clock = std::chrono::steady_clock;
        static const auto t0 = clock::now();
        const auto dt = clock::now() - t0;
        return std::chrono::duration<double>(dt).count();
    }

    mscl::WirelessTypes::WirelessSampleRate
    WirelessAccelerometerManager::toMsclSampleRate(std::uint32_t hz) const
    {
        switch (hz)
        {
        case 1024:
            return mscl::WirelessTypes::sampleRate_1024Hz;
        case 2048:
            return mscl::WirelessTypes::sampleRate_2048Hz;
        case 4096:
            return mscl::WirelessTypes::sampleRate_4096Hz;
        default:
            throw std::runtime_error(
                "Unsupported sampleRateHz: " + std::to_string(hz) +
                ". Allowed values are 1024, 2048, 4096.");
        }
    }

    mscl::ChannelMask WirelessAccelerometerManager::buildAxisChannelMask() const
    {
        mscl::ChannelMask mask;

        // channel index is 1-based in MSCL
        mask.enable(1, true);   // CH1
        mask.enable(2, true);   // CH2

        if (m_config.axisMode == AxisMode::XYZ)
        {
            mask.enable(3, true);   // CH3
        }

        return mask;
    }

    std::string WirelessAccelerometerManager::channelMaskToString(const mscl::ChannelMask& mask) const
    {
        std::ostringstream oss;
        bool first = true;

        for (std::uint8_t ch = 1; ch <= mscl::ChannelMask::MAX_CHANNELS; ++ch)
        {
            if (!mask.enabled(ch))
            {
                continue;
            }

            if (!first)
            {
                oss << ",";
            }

            oss << "CH" << static_cast<int>(ch);
            first = false;
        }

        if (first)
        {
            return "(none)";
        }

        return oss.str();
    }

    std::uint32_t WirelessAccelerometerManager::fromMsclSampleRate(
        mscl::WirelessTypes::WirelessSampleRate rate) const
    {
        switch (rate)
        {
        case mscl::WirelessTypes::sampleRate_1024Hz:
            return 1024;
        case mscl::WirelessTypes::sampleRate_2048Hz:
            return 2048;
        case mscl::WirelessTypes::sampleRate_4096Hz:
            return 4096;
        default:
            return 0;
        }
    }

    std::string WirelessAccelerometerManager::commProtocolToString(
        mscl::WirelessTypes::CommProtocol protocol) const
    {
        switch (protocol)
        {
        case mscl::WirelessTypes::commProtocol_lxrs:
            return "LXRS";
        case mscl::WirelessTypes::commProtocol_lxrsPlus:
            return "LXRS+";
        default:
            return "Unknown";
        }
    }

    std::string WirelessAccelerometerManager::defaultModeToString(
        mscl::WirelessTypes::DefaultMode mode) const
    {
        switch (mode)
        {
        case mscl::WirelessTypes::defaultMode_sleep:
            return "Sleep";
        case mscl::WirelessTypes::defaultMode_idle:
            return "Idle";
        case mscl::WirelessTypes::defaultMode_sync:
            return "Sampling";
        default:
            return "Unknown";
        }
    }

    std::string WirelessAccelerometerManager::samplingModeToString(
        mscl::WirelessTypes::SamplingMode mode) const
    {
        switch (mode)
        {
        case mscl::WirelessTypes::samplingMode_nonSync:
            return "NonSync";
        case mscl::WirelessTypes::samplingMode_sync:
            return "Sync";
        case mscl::WirelessTypes::samplingMode_syncBurst:
            return "Burst";
        case mscl::WirelessTypes::samplingMode_syncEvent:
            return "Event";
        case mscl::WirelessTypes::samplingMode_armedDatalog:
            return "ArmedDatalog";
        default:
            return "Unknown";
        }
    }

    void WirelessAccelerometerManager::connect(const AppConfig& config)
    {
        m_config = config;

        m_connection = std::make_unique<mscl::Connection>(
            mscl::Connection::Serial(m_config.port, m_config.baudrate));

        m_baseStation = std::make_unique<mscl::BaseStation>(*m_connection);
        m_baseStation->readWriteRetries(3);

        m_node = std::make_unique<mscl::WirelessNode>(m_config.nodeAddress, *m_baseStation);
        m_node->readWriteRetries(3);

        m_connected = true;
    }

    void WirelessAccelerometerManager::initialize()
    {
        if (!m_connected || !m_node)
        {
            throw std::runtime_error("WirelessAccelerometerManager not connected.");
        }

        if (m_config.forceSetToIdle)
        {
            setNodeToIdle();
        }

        pingNode();

        // Read the current config.
        m_configReport = {};
        m_configReport.configureNodeEnabled = m_config.configureNode;
        m_configReport.requestedSampleRateHz = m_config.sampleRateHz;
        m_configReport.requestedInactivityTimeoutSeconds = m_config.inactivityTimeoutSeconds;
        m_configReport.requestedUnlimitedDuration = m_config.unlimitedDuration;
        m_configReport.requestedUseLxrsPlus = m_config.useLxrsPlus;
        m_configReport.before = readCurrentNodeConfigSnapshot();

        if (m_config.printCurrentNodeConfig)
        {
            printNodeConfigSnapshot("Current Node Configuration (Before Apply)", m_configReport.before);
        }

        optionallyApplyConfig();
    }

    void WirelessAccelerometerManager::startSampling()
    {
        if (!m_connected || !m_baseStation || !m_node)
        {
            throw std::runtime_error("Cannot start sampling: manager not fully connected.");
        }

        if (!m_config.useSyncSampling)
        {
            throw std::runtime_error("This version currently requires useSyncSampling=true.");
        }

        waitForNodeToStabilize(10, 300);

        buildAndStartSyncNetwork();
    }

    void WirelessAccelerometerManager::stopSampling()
    {
        if (m_node)
        {
            try
            {
                setNodeToIdle();
            }
            catch (...)
            {
            }
        }

        m_samplingStarted = false;
    }

    std::optional<RawSample> WirelessAccelerometerManager::readNextSample(std::uint32_t timeoutMs)
    {
        if (!m_baseStation)
        {
            return std::nullopt;
        }

        for (const mscl::DataSweep& sweep : m_baseStation->getData(timeoutMs))
        {
            if (m_config.dumpSweepChannelsAtStartup && !m_dumpedChannels)
            {
                dumpSweepChannels(sweep);
                m_dumpedChannels = true;
            }

            RawSample sample{};
            if (tryConvertSweepToRawSample(sweep, sample))
            {
                return sample;
            }
        }

        return std::nullopt;
    }

    bool WirelessAccelerometerManager::isConnected() const noexcept
    {
        return m_connected;
    }

    int WirelessAccelerometerManager::nodeAddress() const noexcept
    {
        return m_config.nodeAddress;
    }

    const ConfigApplyReport& WirelessAccelerometerManager::configApplyReport() const noexcept
    {
        return m_configReport;
    }

    void WirelessAccelerometerManager::pingNode()
    {
        const mscl::PingResponse response = m_node->ping();

        if (!response.success())
        {
            throw std::runtime_error(
                "Failed to ping wireless node. Check power, range, frequency, LXRS/LXRS+, and node address.");
        }

        std::cout << "Successfully pinged Node " << m_node->nodeAddress() << '\n';
        std::cout << "Base Station RSSI: " << response.baseRssi() << '\n';
        std::cout << "Node RSSI: " << response.nodeRssi() << '\n';
        std::cout << "Model Number: " << m_node->model() << '\n';
        std::cout << "Serial: " << m_node->serial() << '\n';
        std::cout << "Firmware: " << m_node->firmwareVersion().str() << "\n\n";
    }

    bool WirelessAccelerometerManager::tryPingNode()
    {
        try
        {
            const mscl::PingResponse response = m_node->ping();
            return response.success();
        }
        catch (...)
        {
            return false;
        }
    }

    void WirelessAccelerometerManager::waitForNodeToStabilize(int maxAttempts, int sleepMs)
    {
        std::cout << "Waiting for node to stabilize...\n";

        for (int i = 0; i < maxAttempts; ++i)
        {
            if (tryPingNode())
            {
                std::cout << "Node is responsive again.\n";
                return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }

        throw std::runtime_error(
            "Node did not become responsive after configuration change.");
    }

    void WirelessAccelerometerManager::applyConfigWithRetry(
        const mscl::WirelessNodeConfig& config,
        int maxAttempts)
    {
        std::exception_ptr lastException;

        for (int attempt = 1; attempt <= maxAttempts; ++attempt)
        {
            try
            {
                std::cout << "applyConfig attempt " << attempt
                          << " / " << maxAttempts << "...\n";

                m_node->applyConfig(config);

                std::cout << "applyConfig succeeded.\n";
                return;
            }
            catch (const mscl::Error_NodeCommunication& e)
            {
                lastException = std::current_exception();

                std::cout << "applyConfig attempt " << attempt
                          << " failed with node communication error:\n"
                          << "  " << e.what() << '\n';

                if (attempt < maxAttempts)
                {
                    std::cout << "Waiting before retry...\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                    try
                    {
                        waitForNodeToStabilize(10, 300);
                    }
                    catch (...)
                    {
                    }
                }
            }
            catch (...)
            {
                throw;
            }
        }

        if (lastException)
        {
            std::rethrow_exception(lastException);
        }

        throw std::runtime_error("applyConfig failed after retries.");
    }

    void WirelessAccelerometerManager::setNodeToIdle()
    {
        mscl::SetToIdleStatus status = m_node->setToIdle();

        std::cout << "Setting Node to Idle";

        while (!status.complete(300))
        {
            std::cout << '.';
        }

        std::cout << '\n';

        switch (status.result())
        {
        case mscl::SetToIdleStatus::setToIdleResult_success:
            std::cout << "Successfully set Node to idle.\n";
            break;
        case mscl::SetToIdleStatus::setToIdleResult_canceled:
            throw std::runtime_error("Set to Idle was canceled.");
        case mscl::SetToIdleStatus::setToIdleResult_failed:
        default:
            throw std::runtime_error("Set to Idle failed.");
        }
    }

    NodeConfigSnapshot WirelessAccelerometerManager::readCurrentNodeConfigSnapshot() const
    {
        NodeConfigSnapshot snapshot{};

        snapshot.nodeAddress = m_node->nodeAddress();
        snapshot.modelNumber = std::to_string(static_cast<int>(m_node->model()));
        snapshot.serial = m_node->serial();
        snapshot.firmware = m_node->firmwareVersion().str();

        snapshot.communicationProtocol = commProtocolToString(m_node->communicationProtocol());
        snapshot.defaultMode = defaultModeToString(m_node->getDefaultMode());
        snapshot.samplingMode = samplingModeToString(m_node->getSamplingMode());

        snapshot.sampleRateHz = fromMsclSampleRate(m_node->getSampleRate());
        snapshot.inactivityTimeoutSeconds = m_node->getInactivityTimeout();
        snapshot.unlimitedDuration = m_node->getUnlimitedDuration();

        snapshot.numSweeps = m_node->getNumSweeps();

        const mscl::ChannelMask activeChannels = m_node->getActiveChannels();
        snapshot.activeChannelCount = activeChannels.count();
        snapshot.activeChannelMask = activeChannels.toMask();
        snapshot.activeChannelSummary = channelMaskToString(activeChannels);

        return snapshot;
    }

    void WirelessAccelerometerManager::printNodeConfigSnapshot(
        const std::string& title,
        const NodeConfigSnapshot& snapshot) const
    {
        std::cout << "\n========== " << title << " ==========\n";
        std::cout << "Node Address           : " << snapshot.nodeAddress << '\n';
        std::cout << "Model Number           : " << snapshot.modelNumber << '\n';
        std::cout << "Serial                 : " << snapshot.serial << '\n';
        std::cout << "Firmware               : " << snapshot.firmware << '\n';
        std::cout << "Communication Protocol : " << snapshot.communicationProtocol << '\n';
        std::cout << "Default Mode           : " << snapshot.defaultMode << '\n';
        std::cout << "Sampling Mode          : " << snapshot.samplingMode << '\n';
        std::cout << "Sample Rate            : " << snapshot.sampleRateHz << " Hz\n";
        std::cout << "Inactivity Timeout     : " << snapshot.inactivityTimeoutSeconds << " sec\n";
        std::cout << "Unlimited Duration     : " << (snapshot.unlimitedDuration ? "true" : "false") << '\n';
        std::cout << "Num Sweeps             : " << snapshot.numSweeps << '\n';
        std::cout << "Active Channel Count   : " << snapshot.activeChannelCount << '\n';
        std::cout << "Active Channel Mask    : 0x" << std::hex
                  << snapshot.activeChannelMask << std::dec << '\n';
        std::cout << "Active Channel Summary : " << snapshot.activeChannelSummary << '\n';
        std::cout << "=========================================\n\n";
    }

    void WirelessAccelerometerManager::printConfigApplyReport(const ConfigApplyReport& report) const
    {
        std::cout << "\n========== CONFIG APPLY REPORT ==========\n";
        std::cout << "configureNode                  : "
                  << (report.configureNodeEnabled ? "true" : "false") << '\n';
        std::cout << "verifyConfig passed            : "
                  << (report.verifyPassed ? "true" : "false") << '\n';
        std::cout << "applyConfig completed          : "
                  << (report.applyCompletedWithoutException ? "true" : "false") << '\n';

        std::cout << "\nRequested / Applied / Reported\n";
        std::cout << "Requested sample rate          : "
                  << report.requestedSampleRateHz << " Hz\n";
        std::cout << "Applied sample rate target     : "
                  << report.requestedSampleRateHz << " Hz\n";
        std::cout << "Device reported sample rate    : "
                  << report.after.sampleRateHz << " Hz\n";

        std::cout << "\nRequested inactivity timeout   : "
                  << report.requestedInactivityTimeoutSeconds << " sec\n";
        std::cout << "Device reported timeout        : "
                  << report.after.inactivityTimeoutSeconds << " sec\n";

        std::cout << "\nRequested unlimited duration   : "
                  << (report.requestedUnlimitedDuration ? "true" : "false") << '\n';
        std::cout << "Device reported unlimited dur. : "
                  << (report.after.unlimitedDuration ? "true" : "false") << '\n';

        std::cout << "\nRequested protocol             : "
                  << (report.requestedUseLxrsPlus ? "LXRS+" : "(no protocol change requested)") << '\n';
        std::cout << "Device reported protocol       : "
                  << report.after.communicationProtocol << '\n';

        std::cout << "\nBefore -> After summary\n";
        std::cout << "Sample Rate                    : "
                  << report.before.sampleRateHz << " Hz -> "
                  << report.after.sampleRateHz << " Hz\n";
        std::cout << "Sampling Mode                  : "
                  << report.before.samplingMode << " -> "
                  << report.after.samplingMode << '\n';
        std::cout << "Inactivity Timeout             : "
                  << report.before.inactivityTimeoutSeconds << " sec -> "
                  << report.after.inactivityTimeoutSeconds << " sec\n";
        std::cout << "Unlimited Duration             : "
                  << (report.before.unlimitedDuration ? "true" : "false") << " -> "
                  << (report.after.unlimitedDuration ? "true" : "false") << '\n';
        std::cout << "Communication Protocol         : "
                  << report.before.communicationProtocol << " -> "
                  << report.after.communicationProtocol << '\n';
        std::cout << "=========================================\n\n";
    }

    void WirelessAccelerometerManager::printCurrentConfig()
    {
        const NodeConfigSnapshot snapshot = readCurrentNodeConfigSnapshot();
        printNodeConfigSnapshot("Current Node Configuration", snapshot);
    }

    void WirelessAccelerometerManager::optionallyApplyConfig()
    {
        if (!m_config.configureNode)
        {
            std::cout << "Skipping node configuration changes (configureNode = false).\n";
            m_configReport.after = m_configReport.before;
            printConfigApplyReport(m_configReport);
            return;
        }

        std::cout << "Applying node configuration...\n";

        mscl::WirelessNodeConfig config;

        if (m_config.useLxrsPlus)
        {
            std::cout << "Setting communication protocol to LXRS+...\n";
            config.communicationProtocol(mscl::WirelessTypes::commProtocol_lxrsPlus);
        }

        std::cout << "Requested sample rate: " << m_config.sampleRateHz << " Hz\n";
        std::cout << "Requested inactivity timeout: "
                  << m_config.inactivityTimeoutSeconds << " seconds\n";
        std::cout << "Requested unlimited duration: "
                  << (m_config.unlimitedDuration ? "true" : "false") << "\n";


        const mscl::ChannelMask requestedChannels = buildAxisChannelMask();

        std::cout << "Requested active channels: "
                  << channelMaskToString(requestedChannels) << '\n';

        config.defaultMode(mscl::WirelessTypes::defaultMode_idle);
        config.inactivityTimeout(static_cast<mscl::uint16>(m_config.inactivityTimeoutSeconds));
        config.samplingMode(mscl::WirelessTypes::samplingMode_sync);
        config.sampleRate(toMsclSampleRate(m_config.sampleRateHz));
        config.activeChannels(requestedChannels);
        config.unlimitedDuration(m_config.unlimitedDuration);

        mscl::ConfigIssues issues;
        if (!m_node->verifyConfig(config, issues))
        {
            m_configReport.verifyPassed = false;

            std::string msg = "Failed to verify node configuration:\n";
            for (const mscl::ConfigIssue& issue : issues)
            {
                msg += "  - " + issue.description() + "\n";
            }
            throw std::runtime_error(msg);
        }

        m_configReport.verifyPassed = true;

        applyConfigWithRetry(config, 3);
        waitForNodeToStabilize(20, 500);

        m_configReport.applyCompletedWithoutException = true;

        std::cout << "Node configuration applied and node is stable.\n";

        // Read back the config to report what the device is actually set to.
        m_configReport.after = readCurrentNodeConfigSnapshot();

        if (m_config.printCurrentNodeConfig)
        {
            printNodeConfigSnapshot("Current Node Configuration (After Apply)", m_configReport.after);
        }

        printConfigApplyReport(m_configReport);
    }

    void WirelessAccelerometerManager::buildAndStartSyncNetwork()
    {
        m_network = std::make_unique<mscl::SyncSamplingNetwork>(*m_baseStation);

        std::cout << "Adding Node " << m_node->nodeAddress() << " to sync network...\n";
        m_network->addNode(*m_node);

        std::cout << "Network info:\n";
        std::cout << "  Network OK: " << (m_network->ok() ? "TRUE" : "FALSE") << '\n';
        std::cout << "  Percent Bandwidth: " << m_network->percentBandwidth() << "%\n";
        std::cout << "  Lossless Enabled: " << (m_network->lossless() ? "TRUE" : "FALSE") << '\n';

        std::cout << "Applying network configuration...\n";
        m_network->applyConfiguration();

        std::cout << "Starting sync sampling network...\n";
        m_network->startSampling();

        m_samplingStarted = true;
    }

    bool WirelessAccelerometerManager::tryConvertSweepToRawSample(const mscl::DataSweep& sweep, RawSample& out)
    {
        if (sweep.nodeAddress() != m_config.nodeAddress)
        {
            return false;
        }

        RawSample sample{};
        sample.sampleIndex = m_sampleCounter;
        sample.nodeAddress = sweep.nodeAddress();
        sample.timestampSeconds = nowSeconds();
        sample.baseRssi = sweep.baseRssi();
        sample.nodeRssi = sweep.nodeRssi();

        for (const mscl::WirelessDataPoint& dp : sweep.data())
        {
            const std::string name = dp.channelName();

            if (looksLikeChannel1(name))
            {
                sample.x = dp.as_float();
                sample.hasX = true;
            }
            else if (looksLikeChannel2(name))
            {
                sample.y = dp.as_float();
                sample.hasY = true;
            }
            else if (looksLikeChannel3(name))
            {
                sample.z = dp.as_float();
                sample.hasZ = true;
            }
        }

        const bool valid =
            (m_config.axisMode == AxisMode::XY)
                ? (sample.hasX && sample.hasY)
                : (sample.hasX && sample.hasY && sample.hasZ);

        if (!valid)
        {
            return false;
        }

        out = sample;
        ++m_sampleCounter;
        return true;
    }

    void WirelessAccelerometerManager::dumpSweepChannels(const mscl::DataSweep& sweep) const
    {
        std::cout << "\n========== FIRST SWEEP DUMP ==========\n";
        std::cout << "Node " << sweep.nodeAddress()
                  << " Timestamp: " << sweep.timestamp().str()
                  << " Tick: " << sweep.tick()
                  << " Sample Rate: " << sweep.sampleRate().prettyStr()
                  << " Base RSSI: " << sweep.baseRssi()
                  << " Node RSSI: " << sweep.nodeRssi()
                  << '\n';

        for (const mscl::WirelessDataPoint& dp : sweep.data())
        {
            std::cout << "  " << dp.channelName() << " = " << dp.as_string() << '\n';
        }

        std::cout << "======================================\n\n";
    }
}

