#include "idk/mapedit/PlaytestProcess.hpp"
#include "idk/level/Level.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace idk::mapedit
{
    PlaytestProcess::~PlaytestProcess()
    {
        stop();
    }

    void PlaytestProcess::cleanSnapshot()
    {
        if (mPath[0])
            unlink(mPath);
        if (mDirectory[0])
            rmdir(mDirectory);
        mPath[0] = mDirectory[0] = 0;
    }

    void PlaytestProcess::stop()
    {
        if (mProcess)
        {
            SDL_KillProcess(mProcess, true);
            SDL_WaitProcess(mProcess, true, nullptr);
            SDL_DestroyProcess(mProcess);
            mProcess = nullptr;
        }
        cleanSnapshot();
    }

    bool PlaytestProcess::poll(int &exitCode)
    {
        if (!mProcess || !SDL_WaitProcess(mProcess, false, &exitCode))
            return false;
        SDL_DestroyProcess(mProcess);
        mProcess = nullptr;
        cleanSnapshot();
        return true;
    }

    bool PlaytestProcess::start(const level::Level &snapshot, const char *executableName, level::Error &error)
    {
        if (running())
            return error.set("A playtest is already running.");
        if (!executableName || !*executableName)
            return error.set("Playtest executable name is empty.");
        const char *base = SDL_GetBasePath();
        if (!base)
            return error.set("Could not locate the playtest executable directory.");
        char executable[4096];
        int length = std::snprintf(executable, sizeof(executable), "%s%s", base, executableName);
        if (length < 0 || size_t(length) >= sizeof(executable))
            return error.set("Playtest executable path is too long.");

        std::snprintf(mDirectory, sizeof(mDirectory), "/tmp/idk-playtest-XXXXXX");
        if (!mkdtemp(mDirectory))
        {
            mDirectory[0] = 0;
            return error.set("Could not create a temporary playtest directory.");
        }
        std::snprintf(mPath, sizeof(mPath), "%s/level.idklvl", mDirectory);
        if (!level::Save(snapshot, mPath, error))
        {
            cleanSnapshot();
            return false;
        }
        const char *arguments[] = {executable, "--level", mPath, nullptr};
        mProcess = SDL_CreateProcess(arguments, false);
        if (!mProcess)
        {
            std::snprintf(error.message, sizeof(error.message),
                          "Playtest launch failed: %s. Build and install the game alongside mapedit.", SDL_GetError());
            cleanSnapshot();
            return false;
        }
        return true;
    }
}
