#pragma once

#include "Level.hpp"
#include "libidk/Types.hpp"

namespace idk::level
{
    // A 1.8m capsule with a 0.3m radius. Position denotes the player's feet.
    // Jolt owns collision storage; update scratch uses a bounded 8 MiB budget.
    class Player final : public idk::Immobile
    {
      public:
        ~Player();
        bool initialize(const Level&, Error&);
        void shutdown();
        void reset(glm::vec3 feet);
        void step(glm::vec3 horizontalVelocity, bool jump, float dt);
        glm::vec3 position() const;
        bool grounded() const;

      private:
        struct Impl;
        Impl* mImpl = nullptr;
        bool mOwnsFactory = false;
    };
} // namespace idk::level
