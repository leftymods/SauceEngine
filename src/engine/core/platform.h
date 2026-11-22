#pragma once

#include <string>

// Small cross-platform helpers shared by the engine sources.
namespace Plat {

// Shows a blocking error/dialog to the user. Windows uses a message box,
// other platforms print to stderr.
void ShowMessage(const std::string& title, const std::string& text);

// Absolute path of the running executable (empty when unknown).
std::string ExecutablePath();

} // namespace Plat
