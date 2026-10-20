#pragma once

#include "Level.hpp"
#include "PhysicsWorld.hpp"
#include "libidk/Types.hpp"

namespace JPH
{
    class CharacterVirtual;
}

namespace idk::level
{
    // A 1.8m capsule with a 0.3m radius. Position denotes the player's feet.
    // Jolt owns collision storage; update scratch uses a bounded 8 MiB budget.
    class Player final: public idk::Immobile
    {
    public:
        static constexpr float WalkSpeed = 6.0f;
        static constexpr float RunSpeed = 10.0f;

        ~Player();
        bool initialize(PhysicsWorld &, Error &);
        void shutdown();
        void reset(glm::vec3 feet);
        // Desired horizontal velocity; airborne movement preserves momentum.
        void step(glm::vec3 wishVelocity, bool jump, float dt);
        glm::vec3 position() const;
        // Render between fixed positions; reset() snaps both to the new spawn.
        glm::vec3 interpolatedPosition(float alpha) const;
        bool grounded() const;

    private:
        PhysicsWorld *mWorld = nullptr;
        JPH::CharacterVirtual *mCharacter = nullptr; // One owned Jolt reference.
        glm::vec3 mPreviousPosition{0};
    };
} // namespace idk::level
