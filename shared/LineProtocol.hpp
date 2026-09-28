#pragma once

#include <charconv>
#include <string>
#include <system_error>
#include <vector>
#include <sstream>

namespace RadarProtocol
{

enum class Command
{
    Unknown,

    // 控制指令
    Move,
    Stop,
    Start,
    StartScan,
    StopScan,

    // 查询指令
    GetStatus,
    GetPosition,

    // 雷达上报
    Position,
    Status,
    Target,

    // 错误
    Error
};

struct Frame
{
    Command command = Command::Unknown;
    std::vector<std::string> params;
};

inline bool parseInteger(const std::string& text, int& value)
{
    if (text.empty())
        return false;

    int parsedValue = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsedValue);

    if (result.ec != std::errc() || result.ptr != end)
        return false;

    value = parsedValue;
    return true;
}

inline bool parseMoveParameters(const Frame& frame, int& x, int& y)
{
    if (frame.command != Command::Move || frame.params.size() != 2)
        return false;

    int parsedX = 0;
    int parsedY = 0;

    if (!parseInteger(frame.params[0], parsedX) ||
        !parseInteger(frame.params[1], parsedY))
    {
        return false;
    }

    x = parsedX;
    y = parsedY;
    return true;
}

inline Command commandFromString(const std::string& command)
{
    if (command == "MOVE")
        return Command::Move;

    if (command == "STOP")
        return Command::Stop;

    if (command == "START")
        return Command::Start;

    if (command == "START_SCAN")
        return Command::StartScan;

    if (command == "STOP_SCAN")
        return Command::StopScan;

    if (command == "GET_STATUS")
        return Command::GetStatus;

    if (command == "GET_POSITION")
        return Command::GetPosition;

    if (command == "POS")
        return Command::Position;

    if (command == "STATUS")
        return Command::Status;

    if (command == "TARGET")
        return Command::Target;

    if (command == "ERROR")
        return Command::Error;

    return Command::Unknown;
}

inline std::string commandToString(Command command)
{
    switch (command)
    {
    case Command::Move:
        return "MOVE";

    case Command::Stop:
        return "STOP";

    case Command::Start:
        return "START";

    case Command::StartScan:
        return "START_SCAN";

    case Command::StopScan:
        return "STOP_SCAN";

    case Command::GetStatus:
        return "GET_STATUS";

    case Command::GetPosition:
        return "GET_POSITION";

    case Command::Position:
        return "POS";

    case Command::Status:
        return "STATUS";

    case Command::Target:
        return "TARGET";

    case Command::Error:
        return "ERROR";

    default:
        return "UNKNOWN";
    }
}

inline Frame parseLine(const std::string& line)
{
    Frame frame;

    // std::istringstream iss(line);

    std::string cleanLine = line;

    while (!cleanLine.empty() &&
           (cleanLine.back() == '\r' ||
            cleanLine.back() == '\n'))
    {
        cleanLine.pop_back();
    }

    std::istringstream iss(cleanLine);

    std::string command;
    iss >> command;

    frame.command = commandFromString(command);

    std::string param;

    while (iss >> param)
    {
        frame.params.push_back(param);
    }

    return frame;
}
}
