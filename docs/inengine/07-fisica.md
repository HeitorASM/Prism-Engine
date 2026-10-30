# Física

A física usa a biblioteca **Jolt**. Ela só roda durante o **Play**: a física e os scripts ligam e desligam juntos.

## Como usar

Adicione **Collider** e **Rigid Body** à entidade. Só entidades com os **dois** recebem um corpo físico. No Play, corpos `Dynamic` caem com a gravidade e colidem com os `Static` e `Kinematic`, e a posição resultante volta para o Transform a cada frame.

## Collider

| Campo | O que é |
|---|---|
| **Shape** | Box, Sphere, Capsule, ConvexHull ou TriangleMesh |
| **Size** | Box: meias-extensões (x, y, z). Sphere: raio em `x`. Capsule: raio em `x` e altura **do cilindro** em `y` (altura total = `y` + 2 × raio). ConvexHull e TriangleMesh ignoram o `Size` |
| **IsTrigger** | Detecta sobreposição **sem** colidir (portas, moedas, áreas de dano) |

> **O `Size` está em unidades do mundo e não acompanha a escala (`Scale`).** Esticar um mesh não estica o collider: ajuste o `Size` à mão. É a causa mais comum de "os objetos se atravessam". Selecione a entidade para ver o contorno amarelo do collider na viewport.

Para malhas, **ConvexHull** (envoltória convexa, serve para qualquer tipo de corpo) e **TriangleMesh** (malha exata, só para `Static`/`Kinematic`) usam a forma do Mesh Renderer e **acompanham a escala**. O botão **Ajustar à malha** copia o tamanho do mesh para o `Size`.

## Rigid Body

| Campo | O que é |
|---|---|
| **BodyType** | `Static` (nunca se move), `Kinematic` (só se move por código/script) ou `Dynamic` (simulado) |
| **Mass** | Massa (só `Dynamic`) |
| **Friction / Restitution** | Atrito e quique |
| **LinearDamping / AngularDamping** | "Freio" do movimento (só `Dynamic`) |
| **UseGravity** | Gravidade (só `Dynamic`) |
| **ContinuousCollisionDetection** | Evita que objetos rápidos atravessem paredes finas |
| **FixedRotation** | Trava a rotação (útil para personagens) |

## Eventos de colisão

Nos scripts: `OnCollisionEnter(other)` / `OnCollisionExit(other)` para colliders sólidos, e `OnTriggerEnter(other)` / `OnTriggerExit(other)` quando um dos corpos é Trigger. Veja [Scripting em Lua](04-scripting-lua.md).

## Mover pela física

`entity:ApplyForce`, `ApplyImpulse` e `SetVelocity` (veja os scripts). Para personagens controlados por script, um corpo `Kinematic` com `entity:Translate` costuma ser o caminho mais simples.

## Raycast

`Physics.Raycast(origem, direcao, distanciaMaxima, ignorar)` em Lua, ou o component **Raycast**, que dispara um raio da entidade até um ponto e guarda o resultado (`Hit`, `HitEntity`, `HitPoint`...). Serve para mira, sensores de chão, visão de inimigos etc.

## Limitações

- Física e **parenting** não se combinam: use corpos físicos sem pai.
- Há só duas camadas fixas (estático e móvel); não dá para escolher quem colide com quem.
- Não há juntas (joints) no editor.
- A forma do collider é criada ao apertar Play: mudar o mesh ou a escala durante o Play não a atualiza.
