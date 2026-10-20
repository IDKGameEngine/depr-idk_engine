#include "idk/engine/Engine.hpp"
#include "idk/gfx/RenderEngine.hpp"


static void EngineServiceRaiiFunc(idk::Engine *E)
{
    E->addService<idk::PlatformManager>();
    E->addService<idk::AudioManager>();
    E->addService<idk::EventManager>();
    E->addService<idk::InputManager>();
    E->addService<idk::VideoManager>("GameWindow", 1280, 720);
    E->addService<idk::gfx::RenderEngine>();
}


idk::Engine::Engine()
:   mServiceRaii(EngineServiceRaiiFunc, this),
    mApi(this)
{
    setFixedUpdateRate(30.0);

    mApi.mEvent->addEngineCallback(Engine::onEngineEventCallback);
    // mApi.mEvent->addEngineCallback()
}


idk::Engine::~Engine()
{
    
}


void idk::Engine::setFixedUpdateRate(double rateHz)
{
    mFixedTimer.setRateHz(rateHz);
    mApi.mFixedDeltaTimeSec = mFixedTimer.getStepSec();
}


void idk::Engine::startApplication(idk::Service *app)
{
    addService(app);

    dispatchInit(mApi);
    while (!mApi.mShouldQuit.load())
    {
        engineDeltaTime();
        engineEvents();
        engineUpdate();
        engineRender();
    }
    dispatchShutdown(mApi);
}


void idk::Engine::engineDeltaTime()
{
    constexpr double MaxDeltaTimeSec = 0.25;
    mApi.mDeltaTimeSec = std::min(mClock.tick(), MaxDeltaTimeSec);
}


void idk::Engine::engineEvents()
{
    // EngineEvent e;
    // while (mApi.mEventQueue.pop(e))
    // {
    //     processEvent(e);
    // }
}

void idk::Engine::engineUpdate()
{
    // Variable rate update
    // -----------------------------------------------------
    dispatchUpdate(mApi);
    // -----------------------------------------------------

    // Fixed rate update
    // -----------------------------------------------------
    mFixedTimer.addDeltaTimeSec(mApi.getDeltaTimeSec());
    while (mFixedTimer.step())
    {
        dispatchFixedUpdate(mApi);
    }
    mApi.mFixedUpdateAlpha = mFixedTimer.getInterpolationAlpha();
    // -----------------------------------------------------
}

void idk::Engine::engineRender()
{
    dispatchPreRender(mApi);
    dispatchMidRender(mApi);
    dispatchPostRender(mApi);
}



void idk::Engine::onEngineEventCallback(idk::EngineAPI &api, const idk::EngineEvent &e)
{
    using namespace idk;

    switch (e.type)
    {
        case EngineEvent::Quit:
            api.quit();
            break;
        case EngineEvent::Pause:
            break;
        case EngineEvent::Resume:
            break;
        default:
            break;
    }
}

