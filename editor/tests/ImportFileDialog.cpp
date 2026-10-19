#include "idk/editor/ImportFileDialog.hpp"
#include <SDL3/SDL_error.h>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
    SDL_DialogFileCallback pending = nullptr;
    void *pendingUser = nullptr;
    SDL_Window *dialogParent = nullptr;
    const SDL_DialogFileFilter *dialogFilters = nullptr;
    const char *dialogLocation = nullptr;
    int dialogFilterCount = 0, launches = 0;
    bool dialogMultiple = true, synchronous = false;

    void selectFromThread()
    {
        char path[] = "/tmp/model with spaces.glb";
        const char *files[] = {path, nullptr};
        pending(pendingUser, files, 0);
        // SDL releases its list on callback return. The consumer must own a copy.
        std::memset(path, 'x', sizeof(path) - 1);
    }

    void errorFromThread()
    {
        SDL_SetError("Native picker unavailable");
        pending(pendingUser, nullptr, -1);
        SDL_ClearError();
    }

#define CHECK(value, message)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "%s (line %d)\n", message, __LINE__);                                                 \
            return false;                                                                                              \
        }                                                                                                              \
    } while (false)
}

// Intercept the OS boundary so callback ordering, error state and lifetime can be
// checked deterministically without opening a desktop dialog in the test runner.
extern "C" void SDLCALL SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void *userdata, SDL_Window *parent,
                                               const SDL_DialogFileFilter *filters, int count, const char *location,
                                               bool multiple)
{
    pending = callback;
    pendingUser = userdata;
    dialogParent = parent;
    dialogFilters = filters;
    dialogFilterCount = count;
    dialogLocation = location;
    dialogMultiple = multiple;
    ++launches;
    if (synchronous)
    {
        const char *files[] = {"/tmp/synchronous.gltf", nullptr};
        callback(userdata, files, 0);
    }
}

bool checks()
{
    using Dialog = idk::editor::ImportFileDialog;
    // Match the editor's process-lifetime mailbox; there is no editor pointer in
    // callback userdata, and no heap allocation is needed to keep it alive.
    static Dialog dialog;
    Dialog::Result result;
    CHECK(!dialog.busy() && !dialog.poll(result), "New picker must be idle");
    auto *parent = reinterpret_cast<SDL_Window *>(uintptr_t(1));
    CHECK(dialog.show(parent, "model/sample.glb") && dialog.busy(), "Picker did not start");
    CHECK(!dialog.show(parent, nullptr) && launches == 1, "Pending picker must not launch twice");
    CHECK(dialogParent == parent && !dialogMultiple, "Picker must be parented and single-select");
    CHECK(dialogFilters && dialogFilterCount == 2 && std::strcmp(dialogFilters[0].pattern, "glb;gltf") == 0,
          "glTF filter missing");
    CHECK(dialogLocation && dialogLocation[0] == '/' && std::strstr(dialogLocation, "/model/sample.glb"),
          "Initial location must be absolute");
    CHECK(!dialog.poll(result), "Pending picker returned prematurely");
    std::thread selection(selectFromThread);
    bool ready = false;
    for (unsigned i = 0; i < 100000 && !ready; ++i)
    {
        ready = dialog.poll(result);
        if (!ready)
            std::this_thread::yield();
    }
    selection.join();
    if (!ready)
        ready = dialog.poll(result);
    CHECK(ready && result.outcome == Dialog::Outcome::Selected &&
              std::strcmp(result.path, "/tmp/model with spaces.glb") == 0,
          "Worker selection was not copied/published correctly");
    CHECK(!dialog.busy() && !dialog.poll(result), "Selection must be consumed exactly once");
    CHECK(dialog.show(parent, "/tmp/previous.glb"), "Picker must reopen");
    CHECK(std::strcmp(dialogLocation, "/tmp/previous.glb") == 0, "Remembered selection must be used");
    const char *cancelled[] = {nullptr};
    pending(pendingUser, cancelled, -1);
    CHECK(dialog.poll(result) && result.outcome == Dialog::Outcome::Cancelled && !result.path[0] && !result.error[0],
          "Cancellation must clear previous selection");
    CHECK(dialog.show(parent, nullptr), "Picker must reopen after cancellation");
    CHECK(dialogLocation && dialogLocation[std::strlen(dialogLocation) - 1] == '/',
          "Initial folder must end with a separator");
    std::thread error(errorFromThread);
    error.join();
    CHECK(dialog.poll(result) && result.outcome == Dialog::Outcome::Error &&
              std::strcmp(result.error, "Native picker unavailable") == 0,
          "Thread-local SDL errors must be copied on the callback thread");
    CHECK(dialog.show(parent, nullptr), "Picker must reopen after errors");
    char longPath[Dialog::PathCapacity + 1];
    std::memset(longPath, 'a', sizeof(longPath) - 1);
    longPath[sizeof(longPath) - 1] = 0;
    const char *longFiles[] = {longPath, nullptr};
    pending(pendingUser, longFiles, 0);
    CHECK(dialog.poll(result) && result.outcome == Dialog::Outcome::Error && !result.path[0],
          "Long paths must fail without truncation");
    CHECK(dialog.show(parent, nullptr), "Picker must reopen after long paths");
    char maximum[Dialog::PathCapacity];
    std::memset(maximum, 'a', sizeof(maximum) - 1);
    maximum[sizeof(maximum) - 1] = 0;
    const char *maxFiles[] = {maximum, nullptr};
    pending(pendingUser, maxFiles, 0);
    CHECK(dialog.poll(result) && result.outcome == Dialog::Outcome::Selected && std::strcmp(result.path, maximum) == 0,
          "Maximum-sized paths must succeed");
    synchronous = true;
    CHECK(dialog.show(parent, nullptr) && dialog.poll(result) && result.outcome == Dialog::Outcome::Selected,
          "Synchronous SDL callbacks must work too");
    synchronous = false;
    // A result can arrive after its requesting editor has stopped listening.
    CHECK(dialog.show(parent, nullptr), "Picker must reopen for late callback");
    pending(pendingUser, cancelled, -1);
    CHECK(dialog.poll(result) && !dialog.busy(), "Late callback must remain safe to drain");
    return true;
}

int main()
{
    if (!checks())
        return 1;
    std::puts("SDL import file picker callback checks passed");
    return 0;
}
