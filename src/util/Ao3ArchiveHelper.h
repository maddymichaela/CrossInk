#pragma once

#include <string>

namespace Ao3ArchiveHelper {

bool isAo3Fic(const std::string& path);
bool isArchived(const std::string& path);
void forgetOriginalPath(const std::string& archivedPath);
std::string buildDestinationPath(const std::string& sourcePath);
std::string moveToReadFolder(const std::string& sourcePath, bool keepInRecents);
std::string restoreOriginalFolder(const std::string& archivedPath, bool keepInRecents);

}  // namespace Ao3ArchiveHelper
