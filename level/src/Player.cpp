#include "idk/level/Player.hpp"
#include "PhysicsWorldInternal.hpp"
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <algorithm>
#include <cmath>

namespace idk::level
{
    Player::~Player()
    {
        shutdown();
    }

    bool Player::initialize(PhysicsWorld &world, Error &error)
    {
        if (mCharacter || !world.mImpl)
            return error.set("Player requires an initialized physics world.");
        mWorld = &world;
        auto capsule = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0, .9f, 0), JPH::Quat::sIdentity(),
                                                           new JPH::CapsuleShape(.6f, .3f))
                           .Create();
        if (capsule.HasError())
        {
            error.set(capsule.GetError().c_str());
            shutdown();
            return false;
        }
        JPH::CharacterVirtualSettings settings;
        settings.mShape = capsule.Get();
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -.3f);
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        mCharacter =
            new JPH::CharacterVirtual(&settings, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), 0, &world.mImpl->system);
        mCharacter->AddRef();
        reset({0, 0, 0});
        return true;
    }

    void Player::shutdown()
    {
        if (mCharacter)
            mCharacter->Release();
        mCharacter = nullptr;
        mWorld = nullptr;
        mPreviousPosition = {};
    }

    void Player::reset(glm::vec3 feet)
    {
        if (!mCharacter)
            return;
        mCharacter->SetPosition(JPH::RVec3(detail::ToJolt(feet)));
        mCharacter->SetLinearVelocity(JPH::Vec3::sZero());
        mCharacter->RefreshContacts(mWorld->mImpl->system.GetDefaultBroadPhaseLayerFilter(0),
                                    mWorld->mImpl->system.GetDefaultLayerFilter(0), {}, {}, mWorld->mImpl->temporary);
        mPreviousPosition = position();
    }

    void Player::step(glm::vec3 movement, bool jump, float dt)
    {
        if (!mCharacter || !std::isfinite(dt) || dt <= 0)
            return;
        dt = std::min(dt, 1.0f / 30);
        mPreviousPosition = position();
        auto &character = *mCharacter;
        float vertical = character.GetLinearVelocity().GetY();
        if (grounded() && vertical <= .1f)
            vertical = jump ? 5.0f : 0;
        character.SetLinearVelocity(JPH::Vec3(movement.x, vertical - 9.81f * dt, movement.z));
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        character.ExtendedUpdate(dt, JPH::Vec3(0, -9.81f, 0), settings,
                                 mWorld->mImpl->system.GetDefaultBroadPhaseLayerFilter(0),
                                 mWorld->mImpl->system.GetDefaultLayerFilter(0), {}, {}, mWorld->mImpl->temporary);
    }

    glm::vec3 Player::position() const
    {
        if (!mCharacter)
            return {};
        auto p = mCharacter->GetPosition();
        return {float(p.GetX()), float(p.GetY()), float(p.GetZ())};
    }

    glm::vec3 Player::interpolatedPosition(float alpha) const
    {
        return glm::mix(mPreviousPosition, position(), std::clamp(alpha, 0.0f, 1.0f));
    }

    bool Player::grounded() const
    {
        return mCharacter && mCharacter->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    }
}
