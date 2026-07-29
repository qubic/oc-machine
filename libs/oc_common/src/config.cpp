#include "oc_common/config.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace oc_common
{

namespace
{

const char* envOr(const char* name, const char* fallback)
{
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

std::vector<std::string> splitCsv(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        // trim surrounding whitespace
        const auto begin = item.find_first_not_of(" \t");
        if (begin == std::string::npos)
        {
            continue;
        }
        const auto end = item.find_last_not_of(" \t");
        out.push_back(item.substr(begin, end - begin + 1));
    }
    return out;
}

// Parse a uint16 from an env value. On non-numeric or out-of-range input, warn and return the
// (already-valid) default instead of silently wrapping or yielding 0, as atoi + a cast would.
std::uint16_t parseUint16(const char* name, const char* value, std::uint16_t defaultValue)
{
    try
    {
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed);
        if (consumed != std::string(value).size() || parsed > 0xFFFF)
        {
            throw std::out_of_range(value);
        }
        return static_cast<std::uint16_t>(parsed);
    }
    catch (const std::exception&)
    {
        std::cerr << "Config: invalid value \"" << value << "\" for " << name << "; using default "
                  << defaultValue << "\n";
        return defaultValue;
    }
}

// Parse "scheme://host[:port]" into cfg. Anything unparsable disables forwarding rather than
// silently targeting the wrong endpoint. A trailing path is rejected too: the POST path is
// always /ingest, so a URL carrying one would not mean what its author expects.
void parseServiceUrl(const std::string& url, Config& cfg)
{
    if (url.empty())
    {
        return;
    }

    bool tls = false;
    std::string rest;
    if (url.rfind("https://", 0) == 0)
    {
        tls = true;
        rest = url.substr(8);
    }
    else if (url.rfind("http://", 0) == 0)
    {
        rest = url.substr(7);
    }
    else
    {
        std::cerr << "Config: OC_MACHINE_MOCK_SERVICE_URL \"" << url
                  << "\" must start with http:// or https://; forwarding disabled\n";
        return;
    }

    if (const auto slash = rest.find('/'); slash != std::string::npos)
    {
        // Tolerate a bare trailing slash, reject a real path.
        if (rest.size() != slash + 1)
        {
            std::cerr << "Config: OC_MACHINE_MOCK_SERVICE_URL \"" << url
                      << "\" must not contain a path (the POST target is always /ingest);"
                         " forwarding disabled\n";
            return;
        }
        rest.resize(slash);
    }

    std::uint16_t port = tls ? 443 : 80;
    if (const auto colon = rest.rfind(':'); colon != std::string::npos)
    {
        const std::string portText = rest.substr(colon + 1);
        rest.resize(colon);

        // Parsed strictly, not via parseUint16: a bad port here must disable forwarding rather
        // than fall back to the scheme default, which would silently target the wrong port.
        unsigned long parsed = 0;
        try
        {
            std::size_t consumed = 0;
            parsed = std::stoul(portText, &consumed);
            if (consumed != portText.size())
            {
                parsed = 0;
            }
        }
        catch (const std::exception&)
        {
            parsed = 0;
        }

        if (parsed == 0 || parsed > 0xFFFF)
        {
            std::cerr << "Config: OC_MACHINE_MOCK_SERVICE_URL \"" << url
                      << "\" has an invalid port \"" << portText << "\"; forwarding disabled\n";
            return;
        }
        port = static_cast<std::uint16_t>(parsed);
    }

    if (rest.empty())
    {
        std::cerr << "Config: OC_MACHINE_MOCK_SERVICE_URL \"" << url
                  << "\" has an empty host; forwarding disabled\n";
        return;
    }

    cfg.mockServiceHost = rest;
    cfg.mockServicePort = port;
    cfg.mockServiceTls = tls;
}

} // namespace

Config Config::fromEnvironment()
{
    Config cfg;
    cfg.port = parseUint16("OC_MACHINE_PORT", envOr("OC_MACHINE_PORT", "21841"), 21841);
    cfg.bindAddress = envOr("OC_MACHINE_BIND", "0.0.0.0");
    cfg.whitelist = splitCsv(envOr("OC_MACHINE_WHITELIST", "127.0.0.1"));
    cfg.verifySignatures = std::atoi(envOr("OC_MACHINE_VERIFY_SIGNATURES", "1")) != 0;
    cfg.interfaceIndex = parseUint16("OC_MACHINE_INTERFACE_INDEX", envOr("OC_MACHINE_INTERFACE_INDEX", "0"), 0);
    // Empty disables forwarding; example_env ships the public service as the value operators
    // start from, so a stock deployment forwards without them having to look the URL up.
    parseServiceUrl(envOr("OC_MACHINE_MOCK_SERVICE_URL", ""), cfg);
    cfg.machineId = envOr("OC_MACHINE_ID", "");
    return cfg;
}

} // namespace oc_common
