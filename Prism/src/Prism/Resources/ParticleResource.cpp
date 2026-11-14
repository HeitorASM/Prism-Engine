#include "ParticleResource.h"

#include <algorithm>
#include <cstring>

namespace Prism::ParticleResource {

    namespace {

        template<size_t N>
        const char* NameOf(int value, const char* const (&names)[N]) {
            return value >= 0 && (size_t)value < N ? names[value] : names[0];
        }

        // Os NOMES sao parte do formato de arquivo: nunca renomeie, so acrescente no fim.
        constexpr const char* kSpaceNames[] = { "World", "Local" };
        constexpr const char* kShapeNames[] = { "Cone", "Sphere", "Hemisphere", "Box", "Circle", "Edge" };
        constexpr const char* kBlendNames[] = { "Alpha", "Additive", "SoftAdditive", "Multiply" };
        constexpr const char* kFlipbookNames[] = { "OverLifetime", "FixedFPS", "RandomStatic" };

        // Texto -> indice do enum; false (e 'index' intacto) se o nome nao existe.
        template<size_t N>
        bool IndexOf(const ResourceValue& map, const char* key, const char* const (&names)[N], int& index) {
            std::string text;
            if (!map.GetString(key, text))
                return false;
            for (size_t i = 0; i < N; i++) {
                if (text == names[i]) {
                    index = (int)i;
                    return true;
                }
            }
            return false;
        }

        ResourceValue CurveToValue(const ParticleCurve& curve) {
            ResourceValue list = ResourceValue::MakeArray();
            const uint32_t count = std::min<uint32_t>(curve.Count, kParticleMaxCurveKeys);
            for (uint32_t i = 0; i < count; i++) {
                const float key[2] = { curve.T[i], curve.V[i] };
                list.Add(ResourceValue::MakeFloatArray(key, 2));
            }
            return list;
        }

        // Lista de [t, valor]; qualquer item invalido descarta a curva toda (fica a atual).
        void ValueToCurve(const ResourceValue* value, ParticleCurve& curve) {
            if (!value || value->GetKind() != ResourceValue::Kind::Array)
                return;
            const auto& items = value->Items();
            if (items.empty() || items.size() > (size_t)kParticleMaxCurveKeys)
                return;
            ParticleCurve parsed;
            parsed.Count = (uint32_t)items.size();
            for (size_t i = 0; i < items.size(); i++) {
                const ResourceValue& key = items[i];
                if (key.GetKind() != ResourceValue::Kind::Array || key.Items().size() != 2
                    || !key.Items()[0].IsNumber() || !key.Items()[1].IsNumber())
                    return;
                parsed.T[i] = (float)key.Items()[0].AsDouble();
                parsed.V[i] = (float)key.Items()[1].AsDouble();
            }
            curve = parsed;
        }

        ResourceValue GradientToValue(const ParticleGradient& gradient) {
            ResourceValue stops = ResourceValue::MakeArray();
            const uint32_t count = std::min<uint32_t>(gradient.Count, kParticleMaxGradientStops);
            for (uint32_t i = 0; i < count; i++) {
                const float stop[5] = { gradient.T[i], gradient.C[i][0], gradient.C[i][1], gradient.C[i][2], gradient.C[i][3] };
                stops.Add(ResourceValue::MakeFloatArray(stop, 5));
            }
            ResourceValue object = ResourceValue::MakeObject("Gradient");
            object.Set("Stops", std::move(stops));
            return object;
        }

        void ValueToGradient(const ResourceValue* value, ParticleGradient& gradient) {
            if (!value || !value->HasFields())
                return;
            const ResourceValue* stops = value->Find("Stops");
            if (!stops || stops->GetKind() != ResourceValue::Kind::Array)
                return;
            const auto& items = stops->Items();
            if (items.empty() || items.size() > (size_t)kParticleMaxGradientStops)
                return;
            ParticleGradient parsed;
            parsed.Count = (uint32_t)items.size();
            for (size_t i = 0; i < items.size(); i++) {
                const ResourceValue& stop = items[i];
                if (stop.GetKind() != ResourceValue::Kind::Array || stop.Items().size() != 5)
                    return;
                for (const auto& n : stop.Items())
                    if (!n.IsNumber())
                        return;
                parsed.T[i] = (float)stop.Items()[0].AsDouble();
                for (int k = 0; k < 4; k++)
                    parsed.C[i][k] = (float)stop.Items()[1 + k].AsDouble();
            }
            gradient = parsed;
        }

        void ReadUInt(const ResourceValue& map, const char* key, uint32_t& out) {
            int64_t v;
            if (map.GetInt(key, v) && v >= 0 && v <= 0xFFFFFFFFll)
                out = (uint32_t)v;
        }

    }

    ResourceDocument ToResource(const ParticleEmitterComponent& c) {
        ResourceDocument doc;
        doc.Type = kTypeName;
        doc.Version = kVersion;
        ResourceValue& r = doc.Root;

        r.Set("Loop", ResourceValue::MakeBool(c.Loop));
        r.Set("Duration", ResourceValue::MakeFloat32(c.Duration));
        r.Set("MaxParticles", ResourceValue::MakeInt(c.MaxParticles));
        r.Set("EmissionRate", ResourceValue::MakeFloat32(c.EmissionRate));
        r.Set("BurstCount", ResourceValue::MakeInt(c.BurstCount));

        r.Set("Shape", ResourceValue::MakeString(NameOf((int)c.Shape, kShapeNames)));
        r.Set("ShapeRadius", ResourceValue::MakeFloat32(c.ShapeRadius));
        r.Set("ShapeThickness", ResourceValue::MakeFloat32(c.ShapeThickness));
        r.Set("ShapeBox", ResourceValue::MakeFloatArray(&c.ShapeBox.x, 3));
        r.Set("ShapeRadialBlend", ResourceValue::MakeFloat32(c.ShapeRadialBlend));
        r.Set("SpreadAngle", ResourceValue::MakeFloat32(c.SpreadAngle));

        r.Set("SimulationSpace", ResourceValue::MakeString(NameOf((int)c.SimulationSpace, kSpaceNames)));
        r.Set("LifetimeMin", ResourceValue::MakeFloat32(c.LifetimeMin));
        r.Set("LifetimeMax", ResourceValue::MakeFloat32(c.LifetimeMax));
        r.Set("SpeedMin", ResourceValue::MakeFloat32(c.SpeedMin));
        r.Set("SpeedMax", ResourceValue::MakeFloat32(c.SpeedMax));
        r.Set("GravityScale", ResourceValue::MakeFloat32(c.GravityScale));
        r.Set("Drag", ResourceValue::MakeFloat32(c.Drag));
        r.Set("SpeedOverLife", CurveToValue(c.SpeedOverLife));
        r.Set("RotationMin", ResourceValue::MakeFloat32(c.RotationMin));
        r.Set("RotationMax", ResourceValue::MakeFloat32(c.RotationMax));
        r.Set("AngularVelocityMin", ResourceValue::MakeFloat32(c.AngularVelocityMin));
        r.Set("AngularVelocityMax", ResourceValue::MakeFloat32(c.AngularVelocityMax));

        r.Set("SizeStart", ResourceValue::MakeFloat32(c.SizeStart));
        r.Set("SizeEnd", ResourceValue::MakeFloat32(c.SizeEnd));
        r.Set("SizeVariation", ResourceValue::MakeFloat32(c.SizeVariation));
        r.Set("SizeOverLife", CurveToValue(c.SizeOverLife));
        r.Set("ColorStart", ResourceValue::MakeFloatArray(&c.ColorStart.x, 4));
        r.Set("ColorEnd", ResourceValue::MakeFloatArray(&c.ColorEnd.x, 4));
        r.Set("ColorOverLife", GradientToValue(c.ColorOverLife));
        r.Set("BlendMode", ResourceValue::MakeString(NameOf((int)c.BlendMode, kBlendNames)));

        r.Set("TexturePath", ResourceValue::MakeString(c.TexturePath));
        r.Set("FlipbookColumns", ResourceValue::MakeInt(c.FlipbookColumns));
        r.Set("FlipbookRows", ResourceValue::MakeInt(c.FlipbookRows));
        r.Set("FlipbookMode", ResourceValue::MakeString(NameOf((int)c.FlipbookMode, kFlipbookNames)));
        r.Set("FlipbookFPS", ResourceValue::MakeFloat32(c.FlipbookFPS));
        return doc;
    }

    bool FromResource(const ResourceDocument& doc, ParticleEmitterComponent& emitter, std::string* error) {
        if (doc.Type != kTypeName) {
            if (error)
                *error = "o recurso e do tipo '" + doc.Type + "', esperado '" + kTypeName + "'";
            return false;
        }
        const ResourceValue& r = doc.Root;

        ParticleEmitterComponent c; // padroes do component para tudo que o recurso nao disser
        c.Enabled = emitter.Enabled;
        c.PlayOnStart = emitter.PlayOnStart;
        c.LinkedAsset = emitter.LinkedAsset;

        r.GetBool("Loop", c.Loop);
        r.GetFloat("Duration", c.Duration);
        ReadUInt(r, "MaxParticles", c.MaxParticles);
        r.GetFloat("EmissionRate", c.EmissionRate);
        ReadUInt(r, "BurstCount", c.BurstCount);

        int index = 0;
        if (IndexOf(r, "Shape", kShapeNames, index)) c.Shape = (ParticleShape)index;
        r.GetFloat("ShapeRadius", c.ShapeRadius);
        r.GetFloat("ShapeThickness", c.ShapeThickness);
        r.GetFloatArray("ShapeBox", &c.ShapeBox.x, 3);
        r.GetFloat("ShapeRadialBlend", c.ShapeRadialBlend);
        r.GetFloat("SpreadAngle", c.SpreadAngle);

        if (IndexOf(r, "SimulationSpace", kSpaceNames, index)) c.SimulationSpace = (ParticleSimulationSpace)index;
        r.GetFloat("LifetimeMin", c.LifetimeMin);
        r.GetFloat("LifetimeMax", c.LifetimeMax);
        r.GetFloat("SpeedMin", c.SpeedMin);
        r.GetFloat("SpeedMax", c.SpeedMax);
        r.GetFloat("GravityScale", c.GravityScale);
        r.GetFloat("Drag", c.Drag);
        ValueToCurve(r.Find("SpeedOverLife"), c.SpeedOverLife);
        r.GetFloat("RotationMin", c.RotationMin);
        r.GetFloat("RotationMax", c.RotationMax);
        r.GetFloat("AngularVelocityMin", c.AngularVelocityMin);
        r.GetFloat("AngularVelocityMax", c.AngularVelocityMax);

        r.GetFloat("SizeStart", c.SizeStart);
        r.GetFloat("SizeEnd", c.SizeEnd);
        r.GetFloat("SizeVariation", c.SizeVariation);
        ValueToCurve(r.Find("SizeOverLife"), c.SizeOverLife);
        r.GetFloatArray("ColorStart", &c.ColorStart.x, 4);
        r.GetFloatArray("ColorEnd", &c.ColorEnd.x, 4);
        ValueToGradient(r.Find("ColorOverLife"), c.ColorOverLife);
        if (IndexOf(r, "BlendMode", kBlendNames, index)) c.BlendMode = (ParticleBlendMode)index;

        r.GetString("TexturePath", c.TexturePath);
        ReadUInt(r, "FlipbookColumns", c.FlipbookColumns);
        ReadUInt(r, "FlipbookRows", c.FlipbookRows);
        if (IndexOf(r, "FlipbookMode", kFlipbookNames, index)) c.FlipbookMode = (ParticleFlipbookMode)index;
        r.GetFloat("FlipbookFPS", c.FlipbookFPS);

        c.Sanitize();
        emitter = c;
        return true;
    }

    bool Save(const std::filesystem::path& file, const ParticleEmitterComponent& emitter, ResourceFormat format, std::string* error) {
        return ResourceIO::Save(file, ToResource(emitter), format, error);
    }

    bool SaveByExtension(const std::filesystem::path& file, const ParticleEmitterComponent& emitter, std::string* error) {
        return ResourceIO::SaveByExtension(file, ToResource(emitter), error);
    }

    bool Load(const std::filesystem::path& file, ParticleEmitterComponent& emitter, std::string* error) {
        ResourceDocument doc;
        if (!ResourceIO::Load(file, doc, error))
            return false;
        return FromResource(doc, emitter, error);
    }

}
