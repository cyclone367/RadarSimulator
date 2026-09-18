#include <iostream>
#include <string>

#include "../shared/LineProtocol.hpp"
#include "../shared/LineBuffer.hpp"

using namespace RadarProtocol;

// 测试一个指令是否被正确解析
bool testCommand(const std::string& input, Command expected)
{
    Frame frame = parseLine(input);

    if (frame.command == expected)
    {
        std::cout << "[PASS] "
                  << input
                  << '\n';

        return true;
    }

    std::cout << "[FAIL] "
              << input
              << " -> expected "
              << commandToString(expected)
              << ", got "
              << commandToString(frame.command)
              << '\n';

    return false;
}

// 测试指令 + 参数
bool testPosition()
{
    Frame frame = parseLine("POS 125 350");

    if (frame.command != Command::Position)
    {
        std::cout << "[FAIL] POS command\n";
        return false;
    }

    if (frame.params.size() != 2)
    {
        std::cout << "[FAIL] POS parameter count\n";
        return false;
    }

    if (frame.params[0] != "125" ||
        frame.params[1] != "350")
    {
        std::cout << "[FAIL] POS parameters\n";
        return false;
    }

    std::cout << "[PASS] POS 125 350\n";
    return true;
}

// 测试未知指令
bool testUnknownCommand()
{
    Frame frame = parseLine("HELLO 123");

    if (frame.command == Command::Unknown)
    {
        std::cout << "[PASS] Unknown command\n";
        return true;
    }

    std::cout << "[FAIL] Unknown command\n";
    return false;
}

// 测试 \r\n 行结束符
bool testLineEnding()
{
    Frame frame = parseLine("MOVE 100 200\r\n");

    if (frame.command != Command::Move)
    {
        std::cout << "[FAIL] CRLF command\n";
        return false;
    }

    if (frame.params.size() != 2)
    {
        std::cout << "[FAIL] CRLF parameter count\n";
        return false;
    }

    if (frame.params[0] != "100" ||
        frame.params[1] != "200")
    {
        std::cout << "[FAIL] CRLF parameters\n";
        return false;
    }

    std::cout << "[PASS] CRLF line ending\n";
    return true;
}

// 测试只有指令、没有参数
bool testEmptyParameters()
{
    Frame frame = parseLine("STOP\r\n");

    if (frame.command != Command::Stop)
    {
        std::cout << "[FAIL] Empty parameters command\n";
        return false;
    }

    if (!frame.params.empty())
    {
        std::cout << "[FAIL] Empty parameters count\n";
        return false;
    }

    std::cout << "[PASS] Empty parameters\n";
    return true;
}

// 测试空行
bool testEmptyLine()
{
    Frame frame = parseLine("\r\n");

    if (frame.command != Command::Unknown)
    {
        std::cout << "[FAIL] Empty line\n";
        return false;
    }

    if (!frame.params.empty())
    {
        std::cout << "[FAIL] Empty line parameters\n";
        return false;
    }

    std::cout << "[PASS] Empty line\n";
    return true;
}

// 测试正常接收一条完整消息
bool testNormalLine()
{
    LineBuffer buffer;

    buffer.append("MOVE 100 200\r\n");

    auto lines = buffer.takeLines();

    if (lines.size() != 1)
    {
        std::cout << "[FAIL] Normal line count\n";
        return false;
    }

    if (lines[0] != "MOVE 100 200")
    {
        std::cout << "[FAIL] Normal line content\n";
        return false;
    }

    std::cout << "[PASS] Normal line\n";
    return true;
}

// 测试 TCP 拆包
bool testSplitPacket()
{
    LineBuffer buffer;

    // 第一次只收到半条消息
    buffer.append("MOVE 100");

    auto lines1 = buffer.takeLines();

    if (!lines1.empty())
    {
        std::cout << "[FAIL] Split packet first part\n";
        return false;
    }

    // 第二次收到剩余部分
    buffer.append(" 200\r\n");

    auto lines2 = buffer.takeLines();

    if (lines2.size() != 1)
    {
        std::cout << "[FAIL] Split packet second part\n";
        return false;
    }

    if (lines2[0] != "MOVE 100 200")
    {
        std::cout << "[FAIL] Split packet content\n";
        return false;
    }

    std::cout << "[PASS] Split packet\n";
    return true;
}

// 测试 TCP 粘包
bool testMultiplePackets()
{
    LineBuffer buffer;

    buffer.append("MOVE 100 200\r\nSTOP\r\n");

    auto lines = buffer.takeLines();

    if (lines.size() != 2)
    {
        std::cout << "[FAIL] Multiple packets count\n";
        return false;
    }

    if (lines[0] != "MOVE 100 200" ||
        lines[1] != "STOP")
    {
        std::cout << "[FAIL] Multiple packets content\n";
        return false;
    }

    std::cout << "[PASS] Multiple packets\n";
    return true;
}

// 测试完整通信链路：TCP数据 -> LineBuffer -> LineProtocol
bool testFullPipeline()
{
    LineBuffer buffer;

    // 模拟第一次 TCP 收到的数据
    buffer.append("MOVE 100");

    auto lines1 = buffer.takeLines();

    // 第一部分不是完整的一帧
    if (!lines1.empty())
    {
        std::cout << "[FAIL] Full pipeline first packet\n";
        return false;
    }

    // 模拟第二次 TCP 收到的数据
    buffer.append(" 200\r\nPOS 125 350\r\n");

    auto lines2 = buffer.takeLines();

    // 应该得到两条完整消息
    if (lines2.size() != 2)
    {
        std::cout << "[FAIL] Full pipeline line count\n";
        return false;
    }

    // 第一条消息交给协议解析器
    Frame frame1 = parseLine(lines2[0]);

    if (frame1.command != Command::Move ||
        frame1.params.size() != 2 ||
        frame1.params[0] != "100" ||
        frame1.params[1] != "200")
    {
        std::cout << "[FAIL] Full pipeline MOVE\n";
        return false;
    }

    // 第二条消息交给协议解析器
    Frame frame2 = parseLine(lines2[1]);

    if (frame2.command != Command::Position ||
        frame2.params.size() != 2 ||
        frame2.params[0] != "125" ||
        frame2.params[1] != "350")
    {
        std::cout << "[FAIL] Full pipeline POS\n";
        return false;
    }

    std::cout << "[PASS] Full pipeline\n";
    return true;
}

int main()
{
    bool allPassed = true;

    std::cout << "===== Radar Protocol Tests =====\n\n";

    allPassed &= testCommand("MOVE 100 200", Command::Move);
    allPassed &= testCommand("STOP", Command::Stop);
    allPassed &= testCommand("GET_STATUS", Command::GetStatus);
    allPassed &= testCommand("GET_POSITION", Command::GetPosition);

    allPassed &= testPosition();

    allPassed &= testCommand("STATUS OK", Command::Status);
    allPassed &= testCommand("ERROR 404", Command::Error);

    allPassed &= testUnknownCommand();
    allPassed &= testLineEnding();
    allPassed &= testEmptyParameters();
    allPassed &= testEmptyLine();

    allPassed &= testNormalLine();
    allPassed &= testSplitPacket();
    allPassed &= testMultiplePackets();

    allPassed &= testFullPipeline();

    std::cout << "\n================================\n";

    if (allPassed)
    {
        std::cout << "All tests passed!\n";
        return 0;
    }

    std::cout << "Some tests failed!\n";
    return 1;
}
