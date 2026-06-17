#include "oc_common/config.h"

#include <cstdlib>
#include <sstream>

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

} // namespace

Config Config::fromEnvironment()
{
    Config cfg;
    cfg.port = static_cast<std::uint16_t>(std::atoi(envOr("OC_MACHINE_PORT", "31841")));
    cfg.whitelist = splitCsv(envOr("OC_MACHINE_WHITELIST", "127.0.0.1"));
    cfg.verifySignatures = std::atoi(envOr("OC_MACHINE_VERIFY_SIGNATURES", "1")) != 0;
    cfg.interfaceIndex = static_cast<std::uint16_t>(std::atoi(envOr("OC_MACHINE_INTERFACE_INDEX", "0")));
    return cfg;
}

} // namespace oc_common
