#pragma once

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

// Fixed storage keeps logging memory bounded, including on the render thread.
class LibassLog
{
public:
    enum { EntryCapacity = 256, MessageCapacity = 2048 };

    void Append(const char* message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& entry = m_entries[m_next];
        const int length = std::snprintf(entry.data(), entry.size(), "%s", message);
        if (length >= MessageCapacity) {
            const char suffix[] = "... [truncated]";
            std::memcpy(entry.data() + entry.size() - sizeof(suffix), suffix, sizeof(suffix));
        }
        m_next = (m_next + 1) % EntryCapacity;
        if (m_count < EntryCapacity) ++m_count;
    }

    std::string Snapshot()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string text;
        for (size_t i = 0; i < m_count; ++i) {
            const auto& entry = m_entries[(m_next + EntryCapacity - m_count + i) % EntryCapacity];
            for (const char* p = entry.data(); *p; ++p) {
                if (*p == '\r') continue;
                if (*p == '\n') text += '\r';
                text += *p;
            }
            text += "\r\n";
        }
        return text;
    }

private:
    std::mutex m_mutex;
    std::array<std::array<char, MessageCapacity>, EntryCapacity> m_entries = {};
    size_t m_next = 0;
    size_t m_count = 0;
};

// Shared by the libass contexts and property pages in this filter DLL.
inline LibassLog& GetLibassLog()
{
    static LibassLog log;
    return log;
}
