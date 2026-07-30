#include "util/FilePermissions.hpp"

#include <stdexcept>
#include <sys/stat.h>

namespace util {

bool isPrivateKeyPathVar(const std::string& path_env_var_name) {
    return path_env_var_name.find("PRIVATE_KEY") != std::string::npos;
}

void requireOwnerOnlyPermissions(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        throw std::runtime_error("Failed to stat file at " + path);
    }

    if ((st.st_mode & (S_IRGRP | S_IROTH)) != 0) {
        throw std::runtime_error(path +
                                 " is group- or world-readable; refusing to use it as a private "
                                 "key. Run: chmod 600 " +
                                 path);
    }
}

} // namespace util
