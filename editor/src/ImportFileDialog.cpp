#include "idk/editor/ImportFileDialog.hpp"
#include <SDL3/SDL_error.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace idk::editor
{
    bool ImportFileDialog::show(SDL_Window *parent, const char *initialPath)
    {
        State expected = State::Idle;
        if (!mState.compare_exchange_strong(expected, State::Pending, std::memory_order_acq_rel))
            return false;
        mResult = {};
        // Native backends expect an absolute default location. Reuse the selected
        // file, or begin in the content directory, without allocating a path string.
        mLocation[0] = 0;
        if (initialPath && initialPath[0] == '/')
            std::snprintf(mLocation, sizeof(mLocation), "%s", initialPath);
        else if (!getcwd(mLocation, sizeof(mLocation)))
            mLocation[0] = 0;
        else if (initialPath && initialPath[0])
        {
            size_t used = std::strlen(mLocation);
            int length = std::snprintf(mLocation + used, sizeof(mLocation) - used, "/%s", initialPath);
            if (length < 0 || size_t(length) >= sizeof(mLocation) - used)
                mLocation[used] = 0;
        }
        else
        {
            // A trailing slash distinguishes the initial folder from a filename
            // for native backends such as Zenity.
            size_t used = std::strlen(mLocation);
            if (used && mLocation[used - 1] != '/' && used + 1 < sizeof(mLocation))
            {
                mLocation[used] = '/';
                mLocation[used + 1] = 0;
            }
        }
        static constexpr SDL_DialogFileFilter filters[] = {{"glTF models (GLB / glTF)", "glb;gltf"},
                                                           {"All files", "*"}};
        SDL_ShowOpenFileDialog(callback, this, parent, filters, int(SDL_arraysize(filters)),
                               mLocation[0] ? mLocation : nullptr, false);
        return true;
    }

    bool ImportFileDialog::poll(Result &result)
    {
        if (mState.load(std::memory_order_acquire) != State::Ready)
            return false;
        result = mResult;
        mState.store(State::Idle, std::memory_order_release);
        return true;
    }

    bool ImportFileDialog::busy() const
    {
        return mState.load(std::memory_order_acquire) != State::Idle;
    }

    void SDLCALL ImportFileDialog::callback(void *userdata, const char *const *files, int)
    {
        auto &dialog = *static_cast<ImportFileDialog *>(userdata);
        auto &result = dialog.mResult;
        if (!files)
        {
            result.outcome = Outcome::Error;
            const char *error = SDL_GetError();
            std::snprintf(result.error, sizeof(result.error), "%s",
                          error[0] ? error : "Could not open the file dialog");
        }
        else if (!files[0] || !files[0][0])
            result.outcome = Outcome::Cancelled;
        else if (std::strlen(files[0]) >= sizeof(result.path))
        {
            result.outcome = Outcome::Error;
            std::snprintf(result.error, sizeof(result.error), "Selected path exceeds the editor's %zu-byte limit",
                          sizeof(result.path) - 1);
        }
        else
        {
            result.outcome = Outcome::Selected;
            std::memcpy(result.path, files[0], std::strlen(files[0]) + 1);
        }
        // SDL owns files and frees them on return. No callback accesses editor,
        // renderer or ImGui state, including after shutdown.
        dialog.mState.store(State::Ready, std::memory_order_release);
    }
}
