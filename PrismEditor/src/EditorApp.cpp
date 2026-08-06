#include <Prism.h>
#include <Prism/Core/EntryPoint.h> // define main() - incluir apenas aqui, uma vez

#include "PrismEditor/Layers/ProjectManagerLayer.h"

namespace Prism {

    class PrismEditorApplication : public Application {
    public:
        PrismEditorApplication(const ApplicationSpecification& spec)
            : Application(spec) {
            // O editor sempre comeca no ProjectManager. So depois que um
            // projeto for criado/aberto e que a EditorLayer entra em cena
            // (ver ProjectManagerLayer::CreateAndOpenProject/OpenProject).
            PushLayer(new PrismEditor::ProjectManagerLayer());
        }
    };

    Application* CreateApplication() {
        ApplicationSpecification spec;
        spec.Name = "Prism Editor";
        spec.WindowWidth = 1600;
        spec.WindowHeight = 900;

        return new PrismEditorApplication(spec);
    }

}
