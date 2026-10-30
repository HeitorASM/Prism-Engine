#include "LevelEditing.h"
#include "../Commands/EditorCommands.h"
#include <algorithm>
#include <set>

namespace PrismEditor::LevelEditing {

    using Prism::Level::EditableMesh;

    namespace {
        void SortUnique(std::vector<uint32_t>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        }

        template <typename Fn>
        bool Edit(EditorContext& ctx, const char* name, Fn&& fn) {
            Prism::LevelMeshComponent* level = Sync(ctx);
            if (!level || !ctx.Level.Target.HasComponent<Prism::TransformComponent>())
                return false;
            const Prism::LevelMeshComponent before = *level;
            Prism::TransformComponent& transform = ctx.Level.Target.GetComponent<Prism::TransformComponent>();
            const Prism::TransformComponent transformBefore = transform;
            if (!fn(*level, ctx.Level, transform)) {
                *level = before;
                transform = transformBefore;
                return false;
            }
            level->Touch();
            Sync(ctx);
            ctx.History.Execute(Prism::CreateScope<EditLevelMeshCommand>(
                ctx.Level.Target, name, before, *level, transformBefore, transform));
            return true;
        }
    }

    const char* ShapeLabel(Shape shape) {
        switch (shape) {
            case Shape::Box: return "Caixa";
            case Shape::Room: return "Sala (caixa invertida)";
            case Shape::Plane: return "Plano";
            case Shape::Cylinder: return "Cilindro";
            case Shape::Stairs: return "Escada";
            case Shape::Ramp: return "Rampa";
        }
        return "Forma";
    }

    EditableMesh MakeShape(Shape shape) {
        switch (shape) {
            case Shape::Box: return EditableMesh::CreateBox({ 2.0f, 2.0f, 2.0f });
            case Shape::Room: {
                EditableMesh room = EditableMesh::CreateBox({ 6.0f, 3.0f, 6.0f });
                std::vector<uint32_t> all;
                for (uint32_t f = 0; f < room.Faces.size(); ++f) all.push_back(f);
                room.FlipFaces(all);   // normais para DENTRO: a sala e vista de dentro
                return room;
            }
            case Shape::Plane: return EditableMesh::CreatePlane({ 4.0f, 4.0f }, 1, 1);
            case Shape::Cylinder: return EditableMesh::CreateCylinder(1.0f, 2.0f, 16);
            case Shape::Stairs: return EditableMesh::CreateStairs({ 2.0f, 2.0f, 3.0f }, 6);
            case Shape::Ramp: return EditableMesh::CreateRamp({ 2.0f, 2.0f, 3.0f });
        }
        return EditableMesh::CreateBox({ 1.0f, 1.0f, 1.0f });
    }

    Prism::LevelMeshComponent* Sync(EditorContext& ctx) {
        LevelEditState& state = ctx.Level;
        if (!state.Active)
            return nullptr;
        if (!ctx.SelectedEntity || !state.Target || ctx.SelectedEntity != state.Target
            || !state.Target.HasComponent<Prism::LevelMeshComponent>()) {
            state.Active = false;
            state.Dragging = false;
            state.Vertices.clear();
            state.Faces.clear();
            return nullptr;
        }
        Prism::LevelMeshComponent& level = state.Target.GetComponent<Prism::LevelMeshComponent>();
        const size_t vertexCount = level.Mesh.Vertices.size(), faceCount = level.Mesh.Faces.size();
        state.Vertices.erase(std::remove_if(state.Vertices.begin(), state.Vertices.end(), [&](uint32_t v) { return v >= vertexCount; }), state.Vertices.end());
        state.Faces.erase(std::remove_if(state.Faces.begin(), state.Faces.end(), [&](uint32_t f) { return f >= faceCount; }), state.Faces.end());
        SortUnique(state.Vertices);
        SortUnique(state.Faces);
        return &level;
    }

    void SetActive(EditorContext& ctx, bool active) {
        LevelEditState& state = ctx.Level;
        if (!active) {
            state.Active = false;
            state.Dragging = false;
            state.Vertices.clear();
            state.Faces.clear();
            return;
        }
        if (!ctx.SelectedEntity || !ctx.SelectedEntity.HasComponent<Prism::LevelMeshComponent>())
            return;
        if (state.Target != ctx.SelectedEntity) {
            state.Vertices.clear();
            state.Faces.clear();
        }
        state.Target = ctx.SelectedEntity;
        state.Active = true;
    }

    // --- selecao -------------------------------------------------------------------------

    bool HasSelection(const LevelEditState& state) {
        return state.SelectionMode == Mode::Face ? !state.Faces.empty() : !state.Vertices.empty();
    }

    std::vector<uint32_t> EffectiveFaces(const EditableMesh& mesh, const LevelEditState& state) {
        std::vector<uint32_t> result;
        if (state.SelectionMode == Mode::Face) {
            for (uint32_t f : state.Faces)
                if (f < mesh.Faces.size()) result.push_back(f);
            return result;
        }
        std::vector<char> selected(mesh.Vertices.size(), 0);
        for (uint32_t v : state.Vertices)
            if (v < selected.size()) selected[v] = 1;
        for (uint32_t f = 0; f < mesh.Faces.size(); ++f) {
            bool all = !mesh.Faces[f].Indices.empty();
            for (uint32_t index : mesh.Faces[f].Indices)
                if (index >= selected.size() || !selected[index]) { all = false; break; }
            if (all) result.push_back(f);
        }
        return result;
    }

    std::vector<uint32_t> EffectiveVertices(const EditableMesh& mesh, const LevelEditState& state) {
        if (state.SelectionMode == Mode::Face)
            return mesh.VerticesOfFaces(state.Faces);
        std::vector<uint32_t> result;
        for (uint32_t v : state.Vertices)
            if (v < mesh.Vertices.size()) result.push_back(v);
        return result;
    }

    glm::vec3 SelectionCenter(const EditableMesh& mesh, const LevelEditState& state) {
        const std::vector<uint32_t> vertices = EffectiveVertices(mesh, state);
        if (vertices.empty())
            return glm::vec3(0.0f);
        glm::vec3 sum(0.0f);
        for (uint32_t v : vertices) sum += mesh.Vertices[v];
        return sum / (float)vertices.size();
    }

    void SetMode(EditorContext& ctx, Mode mode) {
        LevelEditState& state = ctx.Level;
        Prism::LevelMeshComponent* level = Sync(ctx);
        if (level && mode != state.SelectionMode) {
            if (state.SelectionMode == Mode::Face) {
                state.Vertices = level->Mesh.VerticesOfFaces(state.Faces);
                state.Faces.clear();
            }
            else if (mode == Mode::Face) {
                state.Faces = EffectiveFaces(level->Mesh, state);
                state.Vertices.clear();
            }
        }
        state.SelectionMode = mode;
    }

    void ClearSelection(EditorContext& ctx) {
        ctx.Level.Vertices.clear();
        ctx.Level.Faces.clear();
    }

    void SelectAll(EditorContext& ctx) {
        Prism::LevelMeshComponent* level = Sync(ctx);
        if (!level) return;
        LevelEditState& state = ctx.Level;
        if (state.SelectionMode == Mode::Face) {
            state.Faces.clear();
            for (uint32_t f = 0; f < level->Mesh.Faces.size(); ++f) state.Faces.push_back(f);
        }
        else {
            state.Vertices.clear();
            for (uint32_t v = 0; v < level->Mesh.Vertices.size(); ++v) state.Vertices.push_back(v);
        }
    }

    void InvertSelection(EditorContext& ctx) {
        Prism::LevelMeshComponent* level = Sync(ctx);
        if (!level) return;
        LevelEditState& state = ctx.Level;
        std::vector<uint32_t>& list = state.SelectionMode == Mode::Face ? state.Faces : state.Vertices;
        const size_t total = state.SelectionMode == Mode::Face ? level->Mesh.Faces.size() : level->Mesh.Vertices.size();
        std::vector<char> in(total, 0);
        for (uint32_t i : list) in[i] = 1;
        list.clear();
        for (uint32_t i = 0; i < total; ++i)
            if (!in[i]) list.push_back(i);
    }

    void Pick(EditorContext& ctx, const std::vector<uint32_t>& indices, bool additive) {
        LevelEditState& state = ctx.Level;
        std::vector<uint32_t>& list = state.SelectionMode == Mode::Face ? state.Faces : state.Vertices;
        if (!additive) {
            list = indices;
            SortUnique(list);
            return;
        }
        if (indices.empty())
            return;
        // Shift: se TODOS os elementos tocados ja estao selecionados, remove; senao adiciona.
        bool allSelected = true;
        for (uint32_t i : indices)
            if (!std::binary_search(list.begin(), list.end(), i)) { allSelected = false; break; }
        if (allSelected) {
            list.erase(std::remove_if(list.begin(), list.end(), [&](uint32_t i) {
                return std::find(indices.begin(), indices.end(), i) != indices.end(); }), list.end());
        }
        else {
            list.insert(list.end(), indices.begin(), indices.end());
            SortUnique(list);
        }
    }

    // --- operacoes -----------------------------------------------------------------------

    bool Extrude(EditorContext& ctx) {
        return Edit(ctx, "Extrudir Faces", [](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const std::vector<uint32_t> faces = EffectiveFaces(level.Mesh, state);
            if (faces.empty()) return false;
            state.Faces = level.Mesh.ExtrudeFaces(faces, state.ExtrudeDistance);
            state.Vertices.clear();
            state.SelectionMode = Mode::Face;
            return true;
        });
    }

    bool Inset(EditorContext& ctx) {
        return Edit(ctx, "Inset de Faces", [](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const std::vector<uint32_t> faces = EffectiveFaces(level.Mesh, state);
            if (faces.empty() || state.InsetAmount <= 0.0f) return false;
            state.Faces = level.Mesh.InsetFaces(faces, state.InsetAmount);
            state.Vertices.clear();
            state.SelectionMode = Mode::Face;
            return true;
        });
    }

    bool Subdivide(EditorContext& ctx) {
        return Edit(ctx, "Subdividir Faces", [](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const std::vector<uint32_t> faces = EffectiveFaces(level.Mesh, state);
            if (faces.empty()) return false;
            const std::vector<uint32_t> created = level.Mesh.SubdivideFaces(faces);
            if (created.empty()) return false;
            state.Faces = created;
            state.Vertices.clear();
            state.SelectionMode = Mode::Face;
            return true;
        });
    }

    bool Detach(EditorContext& ctx) {
        return Edit(ctx, "Destacar Faces", [](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const std::vector<uint32_t> faces = EffectiveFaces(level.Mesh, state);
            if (faces.empty()) return false;
            state.Faces = level.Mesh.DetachFaces(faces);
            state.Vertices.clear();
            state.SelectionMode = Mode::Face;
            return true;
        });
    }

    bool FlipSelectedFaces(EditorContext& ctx) {
        return Edit(ctx, "Inverter Faces", [](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const std::vector<uint32_t> faces = EffectiveFaces(level.Mesh, state);
            if (faces.empty()) return false;
            level.Mesh.FlipFaces(faces);
            return true;
        });
    }

    bool DeleteSelectedFaces(EditorContext& ctx) {
        return Edit(ctx, "Excluir Faces", [](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const std::vector<uint32_t> faces = EffectiveFaces(level.Mesh, state);
            if (faces.empty()) return false;
            level.Mesh.DeleteFaces(faces);
            level.Mesh.RemoveUnusedVertices();
            state.Vertices.clear();
            state.Faces.clear();
            return true;
        });
    }

    bool MergeVertices(EditorContext& ctx) {
        return Edit(ctx, "Mesclar Vertices", [](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const size_t vertices = level.Mesh.Vertices.size(), faces = level.Mesh.Faces.size();
            level.Mesh.MergeVertices(std::max(state.MergeEpsilon, 1e-6f));
            state.Vertices.clear();
            state.Faces.clear();
            return level.Mesh.Vertices.size() != vertices || level.Mesh.Faces.size() != faces;
        });
    }

    bool MirrorWholeMesh(EditorContext& ctx, int axis) {
        return Edit(ctx, "Espelhar Malha", [axis](Prism::LevelMeshComponent& level, LevelEditState&, Prism::TransformComponent&) {
            if (axis < 0 || axis > 2 || level.Mesh.Faces.empty()) return false;
            level.Mesh.Mirror(axis);
            return true;
        });
    }

    bool RecenterPivot(EditorContext& ctx) {
        return Edit(ctx, "Centralizar Pivo", [](Prism::LevelMeshComponent& level, LevelEditState&, Prism::TransformComponent& transform) {
            if (level.Mesh.Vertices.empty()) return false;
            const glm::vec3 offset = level.Mesh.RecenterOnBounds();
            if (glm::dot(offset, offset) < 1e-12f) return false;
            // O centro antigo vira a origem: a posicao do objeto anda ate la (em espaco do pai).
            transform.Translation = glm::vec3(transform.GetTransform() * glm::vec4(offset, 1.0f));
            return true;
        });
    }

    bool AssignSlotToSelection(EditorContext& ctx, uint32_t slot) {
        return Edit(ctx, "Atribuir Material", [slot](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const std::vector<uint32_t> faces = EffectiveFaces(level.Mesh, state);
            if (faces.empty() || slot >= 64) return false;
            while (level.Materials.size() <= slot)
                level.Materials.push_back(level.Materials.empty() ? Prism::MaterialComponent{} : level.Materials.back());
            level.Mesh.SetFaceMaterial(faces, slot);
            return true;
        });
    }

    bool SetSmoothingOnSelection(EditorContext& ctx, uint32_t group) {
        return Edit(ctx, "Grupo de Suavizacao", [group](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            const std::vector<uint32_t> faces = EffectiveFaces(level.Mesh, state);
            if (faces.empty()) return false;
            level.Mesh.SetSmoothingGroup(faces, group);
            return true;
        });
    }

    bool AddMaterialSlot(EditorContext& ctx) {
        return Edit(ctx, "Novo Slot de Material", [](Prism::LevelMeshComponent& level, LevelEditState& state, Prism::TransformComponent&) {
            if (level.Materials.size() >= 64) return false;
            level.Materials.push_back(level.Materials.empty() ? Prism::MaterialComponent{} : level.Materials.back());
            state.ActiveSlot = (int)level.Materials.size() - 1;
            return true;
        });
    }

    // --- arraste do gizmo ---------------------------------------------------------------

    void BeginMove(EditorContext& ctx, const glm::vec3& pivot) {
        LevelEditState& state = ctx.Level;
        Prism::LevelMeshComponent* level = Sync(ctx);
        if (!level || state.Dragging) return;
        state.Dragging = true;
        state.DragBefore = *level;
        state.DragStartVertices = level->Mesh.Vertices;
        state.DragStartPivot = pivot;
    }

    void UpdateMove(EditorContext& ctx, const glm::vec3& newPivot) {
        LevelEditState& state = ctx.Level;
        Prism::LevelMeshComponent* level = Sync(ctx);
        if (!level || !state.Dragging || state.DragStartVertices.size() != level->Mesh.Vertices.size()) return;
        level->Mesh.Vertices = state.DragStartVertices;
        level->Mesh.MoveVertices(EffectiveVertices(level->Mesh, state), newPivot - state.DragStartPivot);
        level->Touch();
    }

    void EndMove(EditorContext& ctx) {
        LevelEditState& state = ctx.Level;
        if (!state.Dragging) return;
        state.Dragging = false;
        Prism::LevelMeshComponent* level = Sync(ctx);
        if (!level || !state.Target.HasComponent<Prism::TransformComponent>()) return;
        if (level->Mesh.Vertices == state.DragStartVertices) {
            *level = state.DragBefore;   // nada mudou: nao suja o undo
            return;
        }
        level->Touch();
        const Prism::TransformComponent transform = state.Target.GetComponent<Prism::TransformComponent>();
        ctx.History.Execute(Prism::CreateScope<EditLevelMeshCommand>(
            state.Target, "Mover Geometria", state.DragBefore, *level, transform, transform));
        state.DragStartVertices.clear();
    }

}
