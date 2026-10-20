#pragma once

#include "idk/platform/AudioManager.hpp"
#include "idk/platform/EventManager.hpp"
#include "idk/platform/InputManager.hpp"
#include "idk/platform/PlatformManager.hpp"
#include "idk/platform/VideoManager.hpp"
#include "libidk/dsa/Queue.hpp"

#include <atomic>


namespace idk
{
    class Engine;
    class EngineAPI;

    // class EngineEvent
    // {
    // public:
    //     static constexpr int32_t T_Invalid   = 0;
    //     static constexpr int32_t T_EngineCtl = 1;
    //     static constexpr int32_t T_Platform  = 2;

    //     static constexpr int32_t S_Pause    = 1;
    //     static constexpr int32_t S_Resume   = 2;
    //     static constexpr int32_t S_Shutdown = 3;

    //     int32_t  type    = 0;
    //     int32_t  subtype = 0;
    //     uint64_t data    = 0UL;
    // };


    class EngineAPI
    {
    public:
        ServiceManager *mOwner;
        AudioManager   *mAudio;
        EventManager   *mEvent;
        InputManager   *mInput;
        VideoManager   *mVideo;

        EngineAPI(ServiceManager *owner);

        void   quit();
        double getDeltaTimeSec();
        double getFixedDeltaTimeSec();
        // Fraction of a fixed step remaining, updated before render callbacks.
        double getFixedUpdateAlpha();

        template <typename ServiceType>
        ServiceType *getService() { return mOwner->getService<ServiceType>(); }

    private:
        friend class idk::Engine;
        // using EventQueue = idk::core::Queue<EngineEvent, 128>;

        std::atomic<bool>   mShouldQuit{ false };
        double              mDeltaTimeSec{ 0.01 };
        double              mFixedDeltaTimeSec{ 0.01 };
        double              mFixedUpdateAlpha{ 0.0 };

    };

}

