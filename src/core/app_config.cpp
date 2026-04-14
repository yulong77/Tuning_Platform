#include "acceltool/core/app_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace acceltool
{
    namespace
    {
        std::string trim(const std::string& s)
        {
            const auto isNotSpace = [](unsigned char ch) { return !std::isspace(ch); };

            auto begin = std::find_if(s.begin(), s.end(), isNotSpace);
            if (begin == s.end())
            {
                return "";
            }

            auto end = std::find_if(s.rbegin(), s.rend(), isNotSpace).base();
            return std::string(begin, end);
        }

        std::string toLower(std::string s)
        {
            std::transform(
                s.begin(),
                s.end(),
                s.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool parseBool(const std::string& value, bool& out)
        {
            const std::string v = toLower(trim(value));

            if (v == "true" || v == "1" || v == "yes" || v == "on")
            {
                out = true;
                return true;
            }

            if (v == "false" || v == "0" || v == "no" || v == "off")
            {
                out = false;
                return true;
            }

            return false;
        }

        bool parseAxisMode(const std::string& value, AxisMode& out)
        {
            const std::string v = toLower(trim(value));

            if (v == "xy")
            {
                out = AxisMode::XY;
                return true;
            }

            if (v == "xyz")
            {
                out = AxisMode::XYZ;
                return true;
            }

            return false;
        }
    }

    bool loadConfigFromFile(const std::string& path, AppConfig& config, std::string& errorMessage)
    {
        std::ifstream in(path);
        if (!in)
        {
            errorMessage = "Failed to open config file: " + path;
            return false;
        }

        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(in, line))
        {
            ++lineNumber;

            std::string s = trim(line);

            if (s.empty())
                continue;

            if (s[0] == '#' || s[0] == ';')
                continue;

            const std::size_t eqPos = s.find('=');
            if (eqPos == std::string::npos)
            {
                errorMessage = "Invalid config line " + std::to_string(lineNumber) + ": missing '='";
                return false;
            }

            const std::string key = trim(s.substr(0, eqPos));
            const std::string value = trim(s.substr(eqPos + 1));

            if (key.empty())
            {
                errorMessage = "Invalid config line " + std::to_string(lineNumber) + ": empty key";
                return false;
            }

            try
            {
                if (key == "port")
                {
                    config.port = value;
                }
                else if (key == "baudrate")
                {
                    config.baudrate = static_cast<std::uint32_t>(std::stoul(value));
                }
                else if (key == "nodeAddress")
                {
                    config.nodeAddress = std::stoi(value);
                }
                else if (key == "forceSetToIdle")
                {
                    bool b = false;
                    if (!parseBool(value, b))
                        throw std::runtime_error("invalid bool");
                    config.forceSetToIdle = b;
                }
                else if (key == "configureNode")
                {
                    bool b = false;
                    if (!parseBool(value, b))
                        throw std::runtime_error("invalid bool");
                    config.configureNode = b;
                }
                else if (key == "useSyncSampling")
                {
                    bool b = false;
                    if (!parseBool(value, b))
                        throw std::runtime_error("invalid bool");
                    config.useSyncSampling = b;
                }
                else if (key == "useLxrsPlus")
                {
                    bool b = false;
                    if (!parseBool(value, b))
                        throw std::runtime_error("invalid bool");
                    config.useLxrsPlus = b;
                }
                else if (key == "sampleRateHz")
                {
                    config.sampleRateHz = static_cast<std::uint32_t>(std::stoul(value));
                }
                else if (key == "timestampGapTolerancePercent")
                {
                    config.timestampGapTolerancePercent = std::stod(value);
                }

                else if (key == "inactivityTimeoutSeconds")
                {
                    config.inactivityTimeoutSeconds = static_cast<std::uint32_t>(std::stoul(value));
                }
                else if (key == "unlimitedDuration")
                {
                    bool b = false;
                    if (!parseBool(value, b))
                        throw std::runtime_error("invalid bool");
                    config.unlimitedDuration = b;
                }
                else if (key == "maxSamples")
                {
                    config.maxSamples = static_cast<std::size_t>(std::stoull(value));
                }
                else if (key == "readTimeoutMs")
                {
                    config.readTimeoutMs = static_cast<std::uint32_t>(std::stoul(value));
                }
                else if (key == "axisMode")
                {
                    AxisMode mode{};
                    if (!parseAxisMode(value, mode))
                        throw std::runtime_error("invalid axisMode, expected XY or XYZ");
                    config.axisMode = mode;
                }
                else if (key == "spec")
                {
                    config.spec = std::stod(value);
                }
                else if (key == "rawQueueCapacityBatches")
                {
                    config.rawQueueCapacityBatches = static_cast<std::size_t>(std::stoull(value));
                }
                else if (key == "writeQueueCapacityBatches")
                {
                    config.writeQueueCapacityBatches = static_cast<std::size_t>(std::stoull(value));
                }
                else if (key == "displayAggregationSamples")
                {
                    config.displayAggregationSamples = static_cast<std::size_t>(std::stoull(value));
                }
                else if (key == "outputDisplayCsvPath")
                {
                    config.outputDisplayCsvPath = value;
                }
                else if (key == "outputCsvPath")
                {
                    config.outputCsvPath = value;
                }
                else if (key == "printToConsole")
                {
                    bool b = false;
                    if (!parseBool(value, b))
                        throw std::runtime_error("invalid bool");
                    config.printToConsole = b;
                }
                else if (key == "dumpSweepChannelsAtStartup")
                {
                    bool b = false;
                    if (!parseBool(value, b))
                        throw std::runtime_error("invalid bool");
                    config.dumpSweepChannelsAtStartup = b;
                }
                else if (key == "printCurrentNodeConfig")
                {
                    bool b = false;
                    if (!parseBool(value, b))
                        throw std::runtime_error("invalid bool");
                    config.printCurrentNodeConfig = b;
                }
                else
                {
                    errorMessage = "Unknown config key at line " + std::to_string(lineNumber) + ": " + key;
                    return false;
                }
            }
            catch (const std::exception& e)
            {
                errorMessage =
                    "Invalid value at line " + std::to_string(lineNumber) +
                    " for key '" + key + "': " + e.what();
                return false;
            }
        }

        return true;
    }

    bool validateConfig(const AppConfig& config, std::string& errorMessage)
    {
        std::ostringstream oss;
        bool ok = true;

        if (config.port.empty())
        {
            ok = false;
            oss << "- port is required\n";
        }

        if (config.baudrate == 0)
        {
            ok = false;
            oss << "- baudrate is required and must be > 0\n";
        }

        if (config.nodeAddress <= 0)
        {
            ok = false;
            oss << "- nodeAddress is required and must be > 0\n";
        }

        if (!config.useSyncSampling)
        {
            ok = false;
            oss << "- useSyncSampling must currently be true for this version\n";
        }

        if (config.sampleRateHz != 1024 &&
            config.sampleRateHz != 2048 &&
            config.sampleRateHz != 4096)
        {
            ok = false;
            oss << "- sampleRateHz must be one of: 1024, 2048, 4096\n";
        }

        if (config.timestampGapTolerancePercent <= 0.0)
        {
            errorMessage = "timestampGapTolerancePercent must be > 0.";
            return false;
        }

        if (config.timestampGapTolerancePercent > 10.0)
        {
            errorMessage = "timestampGapTolerancePercent must be <= 10.0.";
            return false;
        }


        if (config.inactivityTimeoutSeconds == 0)
        {
            ok = false;
            oss << "- inactivityTimeoutSeconds is required and must be > 0\n";
        }

        // maxSamples:
        // > 0 : stop automatically after this many samples
        // = 0 : unlimited

        if (config.readTimeoutMs == 0)
        {
            ok = false;
            oss << "- readTimeoutMs is required and must be > 0\n";
        }
        if (config.spec <= 0.0)
        {
            ok = false;
            oss << "- spec is required and must be > 0\n";
        }

        if (config.rawQueueCapacityBatches == 0)
        {
            ok = false;
            oss << "- rawQueueCapacityBatches must be > 0\n";
        }

        if (config.writeQueueCapacityBatches == 0)
        {
            ok = false;
            oss << "- writeQueueCapacityBatches must be > 0\n";
        }

        if (config.displayAggregationSamples == 0)
        {
            ok = false;
            oss << "- displayAggregationSamples must be > 0\n";
        }

        if (config.outputCsvPath.empty())
        {
            ok = false;
            oss << "- outputCsvPath is required\n";
        }

        if (config.outputDisplayCsvPath.empty())
        {
            ok = false;
            oss << "- outputDisplayCsvPath is required\n";
        }

        if (config.displayAggregationSamples == 0)
        {
            throw std::runtime_error("displayAggregationSamples must be greater than 0.");
        }

        errorMessage = oss.str();
        return ok;
    }
}
