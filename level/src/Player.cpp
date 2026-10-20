#include "idk/level/Player.hpp"
#include "PhysicsWorldInternal.hpp"
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <algorithm>
#include <cmath>

namespace idk::level
{
    namespace
    {
        constexpr float GroundAcceleration = 14.0f;
        constexpr float GroundFriction = 8.0f;
        constexpr float StopSpeed = 2.5f;
        constexpr float AirAcceleration = 10.0f;
        constexpr float AirWishSpeed = .8f;
        constexpr float Gravity = 20.0f;
        constexpr float JumpSpeed = 7.2f;
    }

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

    void Player::step(glm::vec3 wishVelocity, bool jump, float dt)
    {
        if (!mCharacter || !std::isfinite(dt) || dt <= 0)
            return;
        dt = std::min(dt, 1.0f / 30);
        mPreviousPosition = position();
        auto &character = *mCharacter;
        wishVelocity.y = 0;
        float wishSpeed = glm::length(wishVelocity);
        glm::vec3 direction = wishSpeed > 0 ? wishVelocity / wishSpeed : glm::vec3(0);
        // Keep acceleration, friction and jump arcs consistent at 30/60/120 Hz.
        unsigned steps = unsigned(std::ceil(dt * 120.0f));
        float step = dt / steps;
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        for (unsigned i = 0; i < steps; ++i)
        {
            auto velocity = character.GetLinearVelocity();
            glm::vec3 horizontal(velocity.GetX(), 0, velocity.GetZ());
            float vertical = velocity.GetY();
            bool onGround = grounded() && vertical <= .1f;
            bool jumping = onGround && jump;
            jump = false;
            if (onGround)
                vertical = jumping ? JumpSpeed : 0;
            if (onGround && !jumping)
            {
                float speed = glm::length(horizontal);
                if (speed > 0)
                {
                    float drop = std::max(speed, StopSpeed) * GroundFriction * step;
                    horizontal *= std::max(0.0f, speed - drop) / speed;
                }
            }
            // Accelerate along the requested direction, retaining perpendicular
            // momentum. Air steering only adds a small directional component.
            float limit = onGround ? wishSpeed : std::min(wishSpeed, AirWishSpeed);
            float remaining = limit - glm::dot(horizontal, direction);
            if (remaining > 0)
            {
                float acceleration = onGround ? GroundAcceleration : AirAcceleration;
                horizontal += direction * std::min(remaining, acceleration * wishSpeed * step);
            }
            character.SetLinearVelocity(JPH::Vec3(horizontal.x, vertical - Gravity * step, horizontal.z));
            character.ExtendedUpdate(step, JPH::Vec3(0, -Gravity, 0), settings,
                                     mWorld->mImpl->system.GetDefaultBroadPhaseLayerFilter(0),
                                     mWorld->mImpl->system.GetDefaultLayerFilter(0), {}, {}, mWorld->mImpl->temporary);
        }
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
