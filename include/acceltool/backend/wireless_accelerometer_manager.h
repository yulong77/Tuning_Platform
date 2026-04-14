#pragma once

#include <memory>
#include <string>
#include <vector>

#include <mscl/stdafx.h>
#include <mscl/MicroStrain/Wireless/BaseStation.h>
#include <mscl/MicroStrain/Wireless/Configuration/WirelessNodeConfig.h>
#include <mscl/MicroStrain/Wireless/Features/ChannelGroup.h>
#include <mscl/MicroStrain/Wireless/Features/NodeFeatures.h>
#include <mscl/MicroStrain/Wireless/SyncSamplingNetwork.h>
#include <mscl/MicroStrain/Wireless/WirelessNode.h>
#include <mscl/MicroStrain/Wireless/ChannelMask.h>

#include "acceltool/core/app_config.h"
#include "acceltool/core/data_types.h"

namespace acceltool
{
    class WirelessAccelerometerManager
    {
    public:
        WirelessAccelerometerManager();
        ~WirelessAccelerometerManager();

        void connect(const AppConfig& config);
        void initialize(bool forceSetToIdleNow = false);
        void startSampling();
        void stopSampling();

        std::vector<RawSample> readAvailableSamples(std::uint32_t timeoutMs = 10);

        bool isConnected() const noexcept;
        int nodeAddress() const noexcept;
        const ConfigApplyReport& configApplyReport() const noexcept;

    private:
        void pingNode();
        bool tryPingNode();
        void setNodeToIdle();
        void printCurrentConfig();
        void optionallyApplyConfig();
        void buildAndStartSyncNetwork();

        void waitForNodeToStabilize(int maxAttempts = 20, int sleepMs = 500);
        void applyConfigWithRetry(const mscl::WirelessNodeConfig& config, int maxAttempts = 3);

        mscl::ChannelMask buildAxisChannelMask() const;
        std::string channelMaskToString(const mscl::ChannelMask& mask) const;

        NodeConfigSnapshot readCurrentNodeConfigSnapshot() const;
        void printNodeConfigSnapshot(const std::string& title, const NodeConfigSnapshot& snapshot) const;
        void printConfigApplyReport(const ConfigApplyReport& report) const;

        std::string commProtocolToString(mscl::WirelessTypes::CommProtocol protocol) const;
        std::string defaultModeToString(mscl::WirelessTypes::DefaultMode mode) const;
        std::string samplingModeToString(mscl::WirelessTypes::SamplingMode mode) const;
        std::uint32_t fromMsclSampleRate(mscl::WirelessTypes::WirelessSampleRate rate) const;

        bool tryConvertSweepToRawSample(const mscl::DataSweep& sweep, RawSample& out);
        void dumpSweepChannels(const mscl::DataSweep& sweep) const;

        void populateTimingAndLossFields(const mscl::DataSweep& sweep, RawSample& sample);
        std::uint64_t expectedSamplePeriodNs() const;


        mscl::WirelessTypes::WirelessSampleRate toMsclSampleRate(std::uint32_t hz) const;

    private:
        AppConfig m_config{};

        bool m_connected = false;
        bool m_samplingStarted = false;
        bool m_dumpedChannels = false;

        std::uint64_t m_sampleCounter = 0;

        bool m_hasPreviousSweepMeta = false;
        std::uint32_t m_previousDeviceTick = 0;
        std::uint64_t m_previousDeviceTimestampUnixNs = 0;


        ConfigApplyReport m_configReport{};

        std::unique_ptr<mscl::Connection> m_connection;
        std::unique_ptr<mscl::BaseStation> m_baseStation;
        std::unique_ptr<mscl::WirelessNode> m_node;
        std::unique_ptr<mscl::SyncSamplingNetwork> m_network;
    };
}
