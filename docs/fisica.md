# Física

Jolt Physics 5.2.0. `Prism::PhysicsEngine` mantém um `JPH::PhysicsSystem` por cena em execução, criado em `Scene::OnScriptsStart()` e destruído em `Scene::OnScriptsStop()`. Física e scripts ligam e desligam juntos: é o mesmo botão Play.

## Como usar

Adicione `ColliderComponent` **e** `RigidBodyComponent` a uma entidade. Só entidades com os dois recebem um corpo físico. Ao apertar Play, corpos `Dynamic` caem sob gravidade e colidem com `Static`/`Kinematic`, e o resultado é sincronizado de volta para o `TransformComponent` a cada frame.

### ColliderComponent

| Campo | Descrição |
|---|---|
| `Shape` | Box, Sphere ou Capsule |
| `Size` | Box: meias-extensões (x, y, z). Sphere: raio em `x`. Capsule: raio em `x`, altura em `y` |
| `IsTrigger` | detecta sobreposição sem resposta física |

> **`Size` está em unidades absolutas de mundo e não acompanha `Scale`.** Esticar um mesh com `Scale` não redimensiona o collider: é preciso ajustar `Size` manualmente. Esquecer isso é a causa mais comum de "objetos se atravessam" ou de um gizmo de collider desproporcional. Selecione a entidade para ver o wireframe do collider na viewport.

### RigidBodyComponent

| Campo | Descrição |
|---|---|
| `BodyType` | `Static` (nunca se move), `Kinematic` (move só por código/script), `Dynamic` (totalmente simulado) |
| `Mass` | apenas `Dynamic`; mínimo efetivo de 0,001. A inércia continua sendo calculada a partir da forma |
| `Friction`, `Restitution` | qualquer tipo; Jolt combina os dois lados do contato |
| `LinearDamping`, `AngularDamping` | arrasto simulado a cada passo, efetivo só em `Dynamic` |
| `UseGravity` | apenas `Dynamic` |
| `ContinuousCollisionDetection` | evita que objetos rápidos atravessem paredes |
| `FixedRotation` | trava a rotação do corpo (útil para personagens) |

## Timestep fixo

`Simulate` acumula o `deltaTime` e roda `Update` em passos fixos de **1/60 s**, com no máximo **5 passos por frame**. Passando disso, o restante do acumulador é descartado: a engine prefere desacelerar a travar ("espiral da morte").

## Camadas de colisão

Há duas camadas fixas, `Static` e `Moving`, e tudo colide com tudo (exceto estático × estático, que nunca é testado). O Jolt exige que o consumidor defina as camadas e gerencie um `TempAllocator` e um `JobSystemThreadPool`, o que é feito em `PhysicsEngine.cpp`.

## Raycast

Disponível pelo Lua (`Physics.Raycast`) e pelo `RaycastComponent`, que dispara um raio de um ponto da entidade até `TargetPosition` (espaço local) e guarda o resultado (`Hit`, `HitEntity`, `HitPoint` etc.). Com `IgnoreParentAndSiblings`, o raio ignora a entidade pai e as irmãs.

## Cuidados com a implementação

- Ao criar o corpo, a rotação é extraída da matriz de mundo **normalizando as três colunas antes** de converter para quaternion. Aplicar `glm::quat_cast` direto numa matriz com escala embutida gera um quaternion distorcido, e o Jolt rejeita isso com um assert.
- `JPH::Vec3` e `JPH::Quat` usam SIMD e não têm o mesmo layout de `glm`; a conversão sempre passa pelos construtores da API, nunca por `reinterpret_cast`.
- `JPH::RegisterTypes()` e a `Factory` são globais do processo e feitos uma única vez.

## Limitações conhecidas

- **Física e parenting não se combinam.** O corpo é criado na posição de mundo da entidade, mas a sincronização escreve a posição de mundo direto no transform **local**. Isso é correto para entidades sem pai (o caso comum) e errado para uma entidade física que seja filha de outra.
- Sem callbacks de colisão em Lua. `CollisionEvent` existe no C++, mas o `ContactListener` do Jolt ainda não foi ligado.
- Sem joints expostos no editor (o Jolt os suporta).
- Camadas de colisão fixas, sem máscaras por camada de gameplay.
- Sem visualização de depuração do mundo físico além do gizmo estático de collider.
- Propriedades de física não são acessíveis em runtime pelo Lua além de força, impulso e velocidade.
