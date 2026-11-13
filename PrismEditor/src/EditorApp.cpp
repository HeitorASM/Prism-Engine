#include <Prism.h>
#include <PrismVersion.h> // gerado a partir de Version.py
#include <Prism/Core/EntryPoint.h> // define main() - incluir apenas aqui, uma vez

#include "PrismEditor/Layers/ProjectManagerLayer.h"
#include "PrismEditor/Panels/ScriptEditorPanel.h"

namespace Prism {

    class PrismEditorApplication : public Application {
    public:
        PrismEditorApplication(const ApplicationSpecification& spec)
            : Application(spec) {
            // Preferencias guardadas no imgui.ini (zoom do editor de codigo): o handler precisa existir
            // antes do primeiro frame, que e quando o ImGui le o arquivo.
            PrismEditor::ScriptEditorPanel::RegisterSettings();
            // O editor sempre comeca no ProjectManager. So depois que um
            // projeto for criado/aberto e que a EditorLayer entra em cena
            // (ver ProjectManagerLayer::CreateAndOpenProject/OpenProject).
            PushLayer(new PrismEditor::ProjectManagerLayer());
        }
    };

    Application* CreateApplication() {
        ApplicationSpecification spec;
        spec.Name = Version::EditorFull(); // "Prism Editor 0.1.0"
        spec.WindowWidth = 1600;
        spec.WindowHeight = 900;

        return new PrismEditorApplication(spec);
    }

}
