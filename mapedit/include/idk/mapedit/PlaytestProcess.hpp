#pragma once

#include "libidk/Types.hpp"

struct SDL_Process;

namespace idk::level
{
    struct Level;
    struct Error;
}

namespace idk::mapedit
{
    // Owns one child game and its temporary compiled snapshot.
    class PlaytestProcess final: public idk::Immobile
    {
    public:
        ~PlaytestProcess();
        bool start(const level::Level &, const char *executableName, level::Error &);
        bool poll(int &exitCode);
        void stop();

        bool running() const { return mProcess != nullptr; }

    private:
        SDL_Process *mProcess = nullptr;
        char mDirectory[64]{};
        char mPath[128]{};
        void cleanSnapshot();
    };
}
