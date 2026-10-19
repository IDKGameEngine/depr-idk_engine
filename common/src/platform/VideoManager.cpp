#include "idk/platform/VideoManager.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include "idk/EngineAPI.hpp"

idk::VideoManager::VideoManager(const char *title, int w, int h)
    : mTitle(title), mWin(nullptr), mWidth(w), mHeight(h) {};

void idk::VideoManager::onInit(idk::EngineAPI &api)
{
    if (false == SDL_Init(SDL_INIT_VIDEO))
    {
        idk::vlog(idk::LogType::ERROR, "Video", "%s", SDL_GetError());
        api.quit();
        return;
    }

    // Attributes must be set before creating the OpenGL window/context.
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
        !SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) || !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24))
    {
        idk::vlog(idk::LogType::ERROR, "Video", "%s", SDL_GetError());
        api.quit();
        return;
    }
#ifdef IDK_DEBUG
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG))
    {
        idk::vlog(idk::LogType::ERROR, "Video", "%s", SDL_GetError());
        api.quit();
        return;
    }
#endif

    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    // #ifdef IDK_OPENGL
    windowFlags |= SDL_WINDOW_OPENGL;
    // #else
    // windowFlags |= SDL_WINDOW_VULKAN;
    // #endif

    if (!(mWin = SDL_CreateWindow(mTitle, mWidth, mHeight, windowFlags)))
    {
        idk::vlog(idk::LogType::ERROR, "Video", "SDL_CreateWindow: %s", SDL_GetError());
        api.quit();
        return;
    }
}

void idk::VideoManager::onShutdown(idk::EngineAPI &)
{
    if (mWin != nullptr)
    {
        SDL_DestroyWindow((SDL_Window *)mWin);
        mWin = nullptr;
    }
}

void idk::VideoManager::setWindowFullscreen(bool fullscreen)
{
    SDL_SetWindowFullscreen((SDL_Window *)mWin, fullscreen);
}

void idk::VideoManager::setWindowVisibility(bool visible)
{
    if (mWin == nullptr)
    {
        return;
    }
    if (visible)
    {
        SDL_ShowWindow((SDL_Window *)mWin);
    }
    else
    {
        SDL_HideWindow((SDL_Window *)mWin);
    }
}

void idk::VideoManager::setWindowResolution(int w, int h)
{
    if (mWin == nullptr)
    {
        mWidth = w;
        mHeight = h;
        return;
    }

    SDL_SetWindowSize((SDL_Window *)mWin, w, h);
    mWidth = w;
    mHeight = h;
}

void idk::VideoManager::setRenderResolution(int w, int h)
{
    mWidth = w;
    mHeight = h;

    if (mWin != nullptr)
    {
        SDL_SetWindowSize((SDL_Window *)mWin, w, h);
    }
}
