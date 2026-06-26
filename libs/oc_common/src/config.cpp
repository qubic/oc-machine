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

} // namespace

Config Config::fromEnvironment()
{
    Config cfg;
    cfg.port = parseUint16("OC_MACHINE_PORT", envOr("OC_MACHINE_PORT", "31841"), 31841);
    cfg.bindAddress = envOr("OC_MACHINE_BIND", "0.0.0.0");
    cfg.whitelist = splitCsv(envOr("OC_MACHINE_WHITELIST", "127.0.0.1"));
    cfg.verifySignatures = std::atoi(envOr("OC_MACHINE_VERIFY_SIGNATURES", "1")) != 0;
    cfg.interfaceIndex = parseUint16("OC_MACHINE_INTERFACE_INDEX", envOr("OC_MACHINE_INTERFACE_INDEX", "0"), 0);
    return cfg;
}

} // namespace oc_common
