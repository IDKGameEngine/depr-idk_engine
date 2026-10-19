#include "idk/engine/Engine.hpp"
#include "idk/mapedit/MapEditor.hpp"

int main()
{
    idk::Engine engine;
    idk::mapedit::MapEditorApplication editor;
    engine.startApplication(&editor);
    return 0;
}
