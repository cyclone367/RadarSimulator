#pragma once

#include <string>
#include <vector>

namespace RadarProtocol
{

class LineBuffer
{
public:
    // 向缓冲区追加收到的数据
    void append(const std::string& data)
    {
        buffer += data;
    }

    // 取出所有已经接收到的完整行
    std::vector<std::string> takeLines()
    {
        std::vector<std::string> lines;

        while (true)
        {
            std::size_t pos = buffer.find("\r\n");

            if (pos == std::string::npos)
                break;

            lines.push_back(buffer.substr(0, pos));

            buffer.erase(0, pos + 2);
        }

        return lines;
    }

private:
    std::string buffer;
};

}