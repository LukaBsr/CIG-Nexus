#ifndef CIG_NEXUS_GUILD_GUILD_HPP
#define CIG_NEXUS_GUILD_GUILD_HPP

#include <cstdint>
#include <string>

namespace guild {

struct Guild {
    std::string id; // "g_<uuid>" — assigned by Postgres, not generated here (design doc §8.1)
    std::string name;
    std::string owner_id; // user_id of the creator
    uint64_t created_at;
};

} // namespace guild

#endif // CIG_NEXUS_GUILD_GUILD_HPP
