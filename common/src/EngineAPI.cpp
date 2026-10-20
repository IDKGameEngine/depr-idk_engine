#include "idk/EngineAPI.hpp"


idk::EngineAPI::EngineAPI(ServiceManager *O)
:   mOwner(O),
    mAudio(O->getService<AudioManager>()),
    mEvent(O->getService<EventManager>()),
    mInput(O->getService<InputManager>()),
    mVideo(O->getService<VideoManager>())
{

}


// void idk::EngineAPI::dispatchEvent(int32_t type, int32_t subtype, uint64_t data)
// {
//     if (type == EngineEvent::T_Platform)
//     {
//         mOwner->dispatchEvent(*this, reinterpret_cast<const void*>(data));
//     }
//     else
//     {
//         mEventQueue.push(EngineEvent{type, subtype, data});
//     }
// }


void idk::EngineAPI::quit()
{
    mShouldQuit.store(true);
}

double idk::EngineAPI::getDeltaTimeSec()
{
    return mDeltaTimeSec;
}

double idk::EngineAPI::getFixedDeltaTimeSec()
{
    return mFixedDeltaTimeSec;
}

double idk::EngineAPI::getFixedUpdateAlpha()
{
    return mFixedUpdateAlpha;
}

