#include "idk/level/Build.hpp"
#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--help"))
    {
        std::puts(
            "Usage: mapc source.idkmap destination.idklvl [--force]\nPaths are relative to the calling directory.");
        return 0;
    }
    if ((argc != 3 && argc != 4) || (argc == 4 && std::strcmp(argv[3], "--force")))
    {
        std::fputs("Usage: mapc source.idkmap destination.idklvl [--force]\n", stderr);
        return 2;
    }
    idk::level::Error error;
    if (!idk::level::BuildFile(argv[1], argv[2], argc == 4, error))
    {
        std::fprintf(stderr, "Level build failed: %s\n", error.message);
        return 1;
    }
    std::printf("Built %s -> %s\n", argv[1], argv[2]);
    return 0;
}
