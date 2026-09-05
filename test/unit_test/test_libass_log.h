#pragma once

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include "LibassLog.h"
#include "libass_context.h"

TEST(LibassLogTest, KeepsRecentMessagesInOrder)
{
    auto log = std::make_unique<LibassLog>();
    EXPECT_TRUE(log->Snapshot().empty());
    for (int i = 0; i < LibassLog::EntryCapacity + 3; ++i) {
        log->Append(std::to_string(i).c_str());
    }
    std::string expected;
    for (int i = 3; i < LibassLog::EntryCapacity + 3; ++i) {
        expected += std::to_string(i) + "\r\n";
    }
    EXPECT_EQ(expected, log->Snapshot());
}

TEST(LibassLogTest, BoundsAndMarksLongMessages)
{
    auto log = std::make_unique<LibassLog>();
    log->Append(std::string(LibassLog::MessageCapacity * 4, 'x').c_str());
    const std::string snapshot = log->Snapshot();
    EXPECT_EQ(static_cast<size_t>(LibassLog::MessageCapacity + 1), snapshot.size());
    EXPECT_NE(std::string::npos, snapshot.find("... [truncated]\r\n"));
}

TEST(LibassLogTest, PreservesUtf8AndNormalizesNewlines)
{
    auto log = std::make_unique<LibassLog>();
    log->Append("font: \xe4\xb8\xad\xe6\x96\x87\nline 2\r\nline 3");
    EXPECT_EQ("font: \xe4\xb8\xad\xe6\x96\x87\r\nline 2\r\nline 3\r\n", log->Snapshot());
}

TEST(LibassLogTest, ConcurrentWritersAndSnapshots)
{
    auto log = std::make_unique<LibassLog>();
    const std::string message(100, 'x');
    auto append = [&]() { for (int i = 0; i < 1000; ++i) log->Append(message.c_str()); };
    std::thread first(append), second(append);
    for (int i = 0; i < 100; ++i) {
        const std::string snapshot = log->Snapshot();
        EXPECT_LE(snapshot.size(), static_cast<size_t>(LibassLog::EntryCapacity * 102));
        EXPECT_EQ(0u, snapshot.size() % 102);
        for (size_t pos = 0; pos < snapshot.size(); pos += 102) {
            EXPECT_EQ(message + "\r\n", snapshot.substr(pos, 102));
        }
    }
    first.join();
    second.join();
    EXPECT_EQ(static_cast<size_t>(LibassLog::EntryCapacity * 102), log->Snapshot().size());
}

TEST(LibassLogTest, CapturesEmbeddedTrackInitialization)
{
    GetLibassLog().Append("embedded callback test");
    ASS_Context context;
    char header[] = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 480\n";
    ASSERT_TRUE(context.LoadASSTrack(header, sizeof(header) - 1));
    const std::string snapshot = GetLibassLog().Snapshot();
    const size_t marker = snapshot.find("embedded callback test");
    ASSERT_NE(std::string::npos, marker);
    EXPECT_NE(std::string::npos, snapshot.find("[INFO] libass API version:", marker));
}

TEST(LibassLogTest, CapturesExternalFileInitialization)
{
    WCHAR folder[MAX_PATH], filename[MAX_PATH];
    ASSERT_NE(0u, GetTempPathW(MAX_PATH, folder));
    ASSERT_NE(0u, GetTempFileNameW(folder, L"ass", 0, filename));
    FILE* file = _wfopen(filename, L"wb");
    if (!file) {
        DeleteFileW(filename);
        FAIL() << "Cannot open temporary subtitle";
    }
    const char script[] = "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 480\n";
    fwrite(script, 1, sizeof(script) - 1, file);
    fclose(file);
    GetLibassLog().Append("external callback test");
    ASS_Context context;
    const bool loaded = context.LoadASSFile(CString(filename));
    DeleteFileW(filename);
    ASSERT_TRUE(loaded);
    const std::string snapshot = GetLibassLog().Snapshot();
    const size_t marker = snapshot.find("external callback test");
    ASSERT_NE(std::string::npos, marker);
    EXPECT_NE(std::string::npos, snapshot.find("[INFO] libass API version:", marker));
}
