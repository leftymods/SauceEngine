#pragma once
#include <GLFW/glfw3.h>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct ConsoleLineEntry {
    std::string text;
    bool error = false;
};

extern std::deque<std::string> consoleQueue;
extern std::function<bool(const std::string&)> consolePassthrough;
extern std::string networkMapName;
extern std::string consoleJoinMap;
extern bool consoleJoinArmed;
extern std::mutex consoleMutex;
extern std::mutex consoleLogMutex;
extern std::deque<ConsoleLineEntry> consoleLines;
extern std::vector<std::string> consoleHistory;
extern std::string consoleInput;
extern int consoleHistoryIndex;
extern bool consoleOpen;
extern double consoleInputLockUntil;
extern bool inGameplay;
extern bool pauseMenuRequest;
extern bool pauseMenuActive;
// Bind IDs: GLFW keyboard keys use their positive key code; mouse buttons are
// encoded as kBindMouseBase + GLFW_MOUSE_BUTTON_*, mouse wheel as kBindWheelUp/
// kBindWheelDown. This keeps the map integer-keyed without changing the file
// format or the rest of the console plumbing.
constexpr int kBindMouseBase = 10000;
constexpr int kBindWheelUp    = 20001;
constexpr int kBindWheelDown  = 20002;

extern std::unordered_map<int, std::string> consoleBinds;
extern std::unordered_map<int, bool> consoleBindStates;
// Active +actions (e.g. "+forward"). A bind whose command starts with '+'
// is held while the key/button is down and released on up.
extern std::unordered_set<std::string> consoleActiveActions;

int ConsoleParseBindKey(std::string keyName);
void ConsolePushLine(const std::string& text, bool forceError = false);
void InstallGameConsoleOutputCapture();
bool GameConsole_IsOpen();
bool GameConsole_ActionDown(const std::string& action);
std::vector<std::pair<std::string, bool>> GameConsole_GetLines();
std::string GameConsole_GetInput();
void GameConsole_SetInput(const std::string& value);
std::vector<std::string> GameConsole_GetSuggestions();
bool GameConsole_TakeMapCommand(std::string& outMap);
bool GameConsole_TakeDemoCommand(std::string& outDemo);
void GameConsole_RequestDemo(const std::string& demoName);
bool GameConsole_TakeDemoRequest(std::string& outDemo);
bool GameConsole_PollPassthrough();
bool GameConsole_TakeJoinMapRequest(std::string& outMap);
void GameConsole_HandleMenuToggle(GLFWwindow* window);
void GameConsoleProcessBinds(GLFWwindow* window);
void GameConsoleScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
void GameConsoleApplyOpenState(GLFWwindow* window);
void GameConsole_LoadBinds(const std::string& path);
void GameConsole_SaveBinds(const std::string& path);
void GameConsoleKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void GameConsoleCharCallback(GLFWwindow* window, unsigned int codepoint);
std::string TrimCommand(std::string s);
std::string LowerCommand(std::string s);
