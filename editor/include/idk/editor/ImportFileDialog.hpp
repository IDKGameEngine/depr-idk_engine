#pragma once

#include <SDL3/SDL_dialog.h>
#include <atomic>

namespace idk::editor
{
    // Keep this object alive until SDL delivers its callback. The editor uses
    // process-lifetime storage, so shutting down never leaves dangling userdata.
    // show/poll run on the main thread; callback only publishes a bounded result.
    class ImportFileDialog
    {
    public:
        static constexpr size_t PathCapacity = 512;
        enum class Outcome
        {
            Selected,
            Cancelled,
            Error
        };

        struct Result
        {
            Outcome outcome = Outcome::Cancelled;
            char path[PathCapacity]{};
            char error[512]{};
        };

        bool show(SDL_Window *parent, const char *initialPath);
        bool poll(Result &);
        bool busy() const;

    private:
        enum class State
        {
            Idle,
            Pending,
            Ready
        };
        std::atomic<State> mState{State::Idle};
        Result mResult;
        char mLocation[4096]{};
        static void SDLCALL callback(void *, const char *const *, int);
    };
}
