#include"console.h"
#include<io.h>
#include<stdio.h>
#include<Windows.h>

struct ConsoleInfo::ConsoleMetadata {
    HANDLE hTerminal{ INVALID_HANDLE_VALUE };

    ConsoleMetadata() : hTerminal(GetStdHandle(STD_OUTPUT_HANDLE)) {};
};

ConsoleInfo::ConsoleInfo() noexcept : meta(new ConsoleInfo::ConsoleMetadata()), isRedirected(_isatty(_fileno(stdout))) {}

ConsoleInfo::~ConsoleInfo() noexcept
{
    delete meta;
}

int ConsoleInfo::GetConsoleWidth() const
{
    CONSOLE_SCREEN_BUFFER_INFO cInfo;

    if (meta->hTerminal == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(meta->hTerminal, &cInfo))
        return 0;

    return cInfo.dwMaximumWindowSize.X;
}

