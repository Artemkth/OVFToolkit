#include"console.h"
#include<unistd.h>
#include<stdio.h>
#include<sys/ioctl.h>

//nothing needed on *nix systems
struct ConsoleInfo::ConsoleMetadata {};

ConsoleInfo::ConsoleInfo() noexcept : isRedirected(isatty(STDOUT_FILENO)) {}

ConsoleInfo::~ConsoleInfo() noexcept {}

int ConsoleInfo::GetConsoleWidth() const
{
    struct winsize ws;

    if( ioctl(0, TIOCGWINSZ, &ws) != 0 )
        return 0;

    return ws.ws_col;
}

