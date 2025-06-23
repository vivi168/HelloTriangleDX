#include "stdafx.h"
#include "Game.h"
#include "Mesh.h"
#include "Camera.h"
#include "Renderer.h"
#include "Input.h"

using namespace DirectX;

static Model3D cube, gardenGnome, yuka, terrain, knight, brainstem, sponza, cesium;

static std::vector<Model3D> trees;
static std::vector<Model3D> knights;

static Camera camera;

void Game::Init()
{
  Renderer::SetSceneCamera(&camera);

  Model3D baseTree;
  baseTree.AddMesh("assets/OPTIM_white_oak_mesh_1.mesh");

  int ntree = 3;
  trees.resize(ntree * ntree);
  for (int y = 0; y < ntree; y++) {
    for (int x = 0; x < ntree; x++) {
      int i = y * ntree + x;

      trees[i] = baseTree.SpawnInstance().Scale(10.0f).Translate(-100 + x * 30, -10.f, -100.f + y * 30.f);

      Renderer::AppendToScene(&trees[i]);
    }
  }

  yuka.AddMesh("assets/OPTIM_yuka_mesh_1.mesh").Scale(5.f).Translate(15.f, 0.f, 15.f);
  Renderer::AppendToScene(&yuka);

  terrain.AddMesh("assets/OPTIM_ground_mesh_1.mesh");
  Renderer::AppendToScene(&terrain);

  cube.AddMesh("assets/OPTIM_issou_mesh_1.mesh").Translate(0.f, 50.f, 0.f).Scale(5.f);
  Renderer::AppendToScene(&cube);

  gardenGnome.AddMesh("assets/OPTIM_garden_gnome_1k_mesh_1.mesh");
  gardenGnome.Scale(5.0f);
  Renderer::AppendToScene(&gardenGnome);

  sponza.AddMesh("assets/OPTIM_Sponza_mesh_1.mesh").Translate(-150.f, 5.f, -150.f).Scale(5.f);
  Renderer::AppendToScene(&sponza);

  brainstem
      .AddSkinnedMesh("assets/OPTIM_BrainStem_mesh_1.mesh", "assets/OPTIM_BrainStem_skin_1.skin",
                      "assets/OPTIM_BrainStem_transforms.bin")
      .AddAnimation("assets/OPTIM_BrainStem_animation_1.anim", "dance")
      .SetCurrentAnimation("dance")
      .Scale(5.f)
      .Rotate(-XM_PIDIV2, XM_PIDIV2, 0.0f)
      .Translate(-10.f, 0.f, 0.f);
  Renderer::AppendToScene(&brainstem);

  knight
      .AddSkinnedMesh("assets/OPTIM_knight_mesh_3.mesh", "assets/OPTIM_knight_skin_1.skin",
                      "assets/OPTIM_knight_transforms.bin")
      // .AddMesh("assets/OPTIM_knight_mesh_1.mesh") // shield
      // .AddMesh("assets/OPTIM_knight_mesh_2.mesh") // sword
      .AddAnimation("assets/OPTIM_knight_animation_1.anim", "test")
      .SetCurrentAnimation("test")
      .Translate(10.f, 0, -15.f)
      .Scale(1.5f)
      .Rotate(0, XM_PI / 2, 0);
  Renderer::AppendToScene(&knight);

  int yknight = 3;
  int xknight = 5;
  knights.resize(yknight * xknight);
  for (int y = 0; y < yknight; y++) {
    for (int x = 0; x < xknight; x++) {
      int i = y * xknight + x;

      knights[i] = knight.SpawnInstance()
                         .SetCurrentAnimation("test")
                         .Scale(1.5f)
                         .Translate(80 + x * 10, 20.f, 20.0f + y * 10.f);

      Renderer::AppendToScene(&knights[i]);
    }
  }

  cesium.AddSkinnedMesh("assets/OPTIM_CesiumMan_mesh_1.mesh", "assets/OPTIM_CesiumMan_skin_1.skin")
      .AddAnimation("assets/OPTIM_CesiumMan_animation_1.anim", "walk")
      .SetCurrentAnimation("walk")
      .Scale(5.f)
      .Rotate(-XM_PIDIV2, 0.0f, 0.0f);
  Renderer::AppendToScene(&cesium);
}

void Game::Update(float time, float deltaTime)
{
  cube.Rotate(time * .25f, 0.f, 0.f);

  camera.ProcessKeyboard(deltaTime);
}

void Game::DebugWindow() { camera.DebugWindow(); }
