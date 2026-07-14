#ifndef CIG_NEXUS_GUILD_CHANNEL_HPP
#define CIG_NEXUS_GUILD_CHANNEL_HPP

#include <cstdint>
#include <string>

namespace guild {

// VOICE is a metadata-only placeholder this iteration: the type is modeled
// and validated, but there's no audio transport or join/leave semantics for
// it yet. See docs/rooms-spec.md, "Voice Channels".
enum class ChannelType { TEXT, VOICE };

struct Channel {
    std::string id; // "c_1", "c_2", ...
    std::string guild_id;
    std::string name;
    ChannelType type;
    uint64_t created_at;
};

// Wire representation used by both GuildHandler (channel lists embedded in
// guild responses) and ChannelHandler (channel lifecycle responses).
inline std::string toString(ChannelType type) {
    switch (type) {
    case ChannelType::TEXT:
        return "TEXT";
    case ChannelType::VOICE:
        return "VOICE";
    }
    return "TEXT";
}

} // namespace guild

#endif // CIG_NEXUS_GUILD_CHANNEL_HPP
