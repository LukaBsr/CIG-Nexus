#ifndef CIG_NEXUS_GUILD_GUILD_HPP
#define CIG_NEXUS_GUILD_GUILD_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace guild {

// docs/guilds/social-presence-design.md §1.7/§1.8.
enum class GuildVisibility { OPEN, APPLICATION, PRIVATE };

struct Guild {
    std::string id; // "g_<uuid>" — assigned by Postgres, not generated here (design doc §8.1)
    std::string name;
    std::string owner_id; // user_id of the creator
    GuildVisibility visibility = GuildVisibility::OPEN;
    uint64_t created_at;
};

inline std::string toString(GuildVisibility visibility) {
    switch (visibility) {
    case GuildVisibility::OPEN:
        return "open";
    case GuildVisibility::APPLICATION:
        return "application";
    case GuildVisibility::PRIVATE:
        return "private";
    }
    return "open";
}

// nullopt for anything other than the three known wire values — mirrors
// ChannelType's inline parsing convention (ChannelHandler validates
// channel_type the same way), pulled out to a shared helper here since
// visibility parsing happens in more than one place (CREATE_GUILD,
// SET_GUILD_VISIBILITY, and catalog hydration).
inline std::optional<GuildVisibility> guildVisibilityFromString(const std::string& value) {
    if (value == "open") {
        return GuildVisibility::OPEN;
    }
    if (value == "application") {
        return GuildVisibility::APPLICATION;
    }
    if (value == "private") {
        return GuildVisibility::PRIVATE;
    }
    return std::nullopt;
}

} // namespace guild

#endif // CIG_NEXUS_GUILD_GUILD_HPP
