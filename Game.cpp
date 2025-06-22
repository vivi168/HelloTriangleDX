#include "stdafx.h"
#include "Game.h"
#include "Mesh.h"
#include "Camera.h"
#include "Collider.h"
#include "Renderer.h"
#include "Input.h"

using namespace DirectX;

struct Player {
  XMFLOAT3 position;
  XMFLOAT3 velocity;
  float lookYaw, lookPitch;
  float floorNormalY;

  enum class State { Unknown, Standing, Walking, Falling, Jumping, Swimming };

  State currentState;

  Player();
  void ProcessKeyboard(float dt);
  void Update();
};

static Model3D cube, cylinder, yuka, terrain, stairs, unitCube, knight, brainstem, sponza, cesium;

static std::vector<Model3D> trees;
static std::vector<Model3D> knights;

static Camera camera;
static Collider collider;
static Player player;

static const float PLAYER_ROT_SPEED = 2.f;
static const float PLAYER_SPEED = 20.0f;
static const float FALLING_SPEED = 30.0f;

static float plateformY = 0.0f;
static float plateformPitch = 0.0f;

struct {
  bool freeLook;
  bool thirdPerson;
  float height;
  float thirdPersonDistance;
  float thirdPersonPitch;
} g_CameraSettings{false, false, 4.0f, 20.f, 0.35f};

Player::Player()
{
  lookYaw = -XM_PIDIV2;
  lookPitch = 0.0f;
  floorNormalY = 1.0f;

  position = {0.f, 0.f, 0.f};
  velocity = {0.f, 0.f, 0.f};

  currentState = State::Standing;
}

// TODO: DRY this with camera (Controller class?)
void Player::ProcessKeyboard(float dt)
{
  if (Input::IsHeld(Input::KB::Up)) {
    lookPitch += PLAYER_ROT_SPEED * dt;
  }
  if (Input::IsHeld(Input::KB::Down)) {
    lookPitch -= PLAYER_ROT_SPEED * dt;
  }
  if (Input::IsHeld(Input::KB::Left)) {
    lookYaw -= PLAYER_ROT_SPEED * dt;
  }
  if (Input::IsHeld(Input::KB::Right)) {
    lookYaw += PLAYER_ROT_SPEED * dt;
  }

  // TODO: align this with Camera.cpp
  static constexpr float epsilon = 0.001f;
  static constexpr float upper = XM_PIDIV2 - epsilon;
  static constexpr float lower = -XM_PIDIV2 + epsilon;

  if (lookPitch > upper)
    lookPitch = upper;
  else if (lookPitch < lower)
    lookPitch = lower;

  float forwardX = cosf(lookYaw);
  float forwardZ = sinf(lookYaw);
  float rightX = -sinf(lookYaw);
  float rightZ = cosf(lookYaw);

  velocity = {0.f, 0.f, 0.f};

  if (currentState == State::Falling) {
    velocity.y = -FALLING_SPEED * dt;
    return;
  }

  if (Input::IsHeld(Input::KB::W)) {
    velocity.x = forwardX * PLAYER_SPEED * dt;
    velocity.z = forwardZ * PLAYER_SPEED * dt;
  }

  if (Input::IsHeld(Input::KB::S)) {
    velocity.x = -forwardX * PLAYER_SPEED * dt;
    velocity.z = -forwardZ * PLAYER_SPEED * dt;
  }

  if (Input::IsHeld(Input::KB::A)) {
    velocity.x = -rightX * PLAYER_SPEED * dt;
    velocity.z = -rightZ * PLAYER_SPEED * dt;
  }

  if (Input::IsHeld(Input::KB::D)) {
    velocity.x = rightX * PLAYER_SPEED * dt;
    velocity.z = rightZ * PLAYER_SPEED * dt;
  }
}

void Player::Update()
{
  XMVECTOR playerStart = XMLoadFloat3(&position);
  // the steeper the slope the slower the speed
  XMVECTOR playerVel = XMLoadFloat3(&velocity) * floorNormalY;
  XMVECTOR playerEnd = playerStart + playerVel;
  XMVECTOR direction = XMVector3Normalize(playerVel);
  float velMag;
  XMStoreFloat(&velMag, XMVector3Length(playerVel));

  Surface* wall = nullptr;
  float distance;
  if (velMag > 0.f)
    wall = collider.FindWall(playerStart, direction, 2.0f, distance);

  XMStoreFloat3(&position, playerEnd);

  if (wall) {
    float distanceMoved;
    XMStoreFloat(&distanceMoved, XMVector3Length(playerVel));
    distance -= 1.1f;  // player radius
    if (distanceMoved > distance) {
      XMVECTOR wallNormal = XMLoadFloat3(&wall->normal);
      XMVECTOR hitPoint = playerStart + direction * distance;
      XMVECTOR directionAlongWall =
          direction - XMVector3Dot(direction, wallNormal) * wallNormal;

      static constexpr float epsilon = 0.000001f;
      XMVECTOR finalPos = hitPoint +
                          directionAlongWall * (distanceMoved - distance) +
                          wallNormal * epsilon;

      XMStoreFloat3(&position, finalPos);
    }
  }

  float height;
  Surface* floor = collider.FindFloor(position, 2.0f, height);
  assert(floor);  // should not happen
  floorNormalY = floor->normal.y;

  if (position.y - height > 2.5f) {
    currentState = State::Falling;
  } else {
    position.y = height;
    velocity.y = 0.0f;
    currentState = velMag > 0.f ? State::Walking : State::Standing;
  }
}

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

  cylinder.AddMesh("assets/OPTIM_garden_gnome_1k_mesh_1.mesh");
  cylinder.Scale(5.0f);
  Renderer::AppendToScene(&cylinder);

  stairs.AddMesh("assets/OPTIM_stairs_mesh_1.mesh").Translate(-50.f, 0.f, 20.f);
  Renderer::AppendToScene(&stairs);

  unitCube.AddMesh("assets/OPTIM_plateform_mesh_1.mesh")
      .Translate(10.f, plateformY, -10.f)
      .Rotate(plateformPitch, 0.f, 0.f);
  Renderer::AppendToScene(&unitCube);

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
  
  // static
  collider.AppendModel(&terrain);
  collider.AppendModel(&yuka);
  collider.AppendModel(&stairs);

  // dynamic
  collider.AppendModel(&unitCube);
  collider.AppendModel(&cube);
}

void Game::Update(float time, float deltaTime)
{
  // Dynamic models update
  {
    if (Input::IsHeld(Input::KB::I)) {
      plateformY += 10 * deltaTime;
      unitCube.Translate(10.f, plateformY, -10.f);
    } else if (Input::IsHeld(Input::KB::K)) {
      plateformY -= 10 * deltaTime;
      unitCube.Translate(10.f, plateformY, -10.f);
    }

    if (Input::IsHeld(Input::KB::J)) {
      plateformPitch += 0.25 * deltaTime;
      unitCube.Rotate(plateformPitch, 0.f, 0.f);
    } else if (Input::IsHeld(Input::KB::L)) {
      plateformPitch -= 0.25 * deltaTime;
      unitCube.Rotate(plateformPitch, 0.f, 0.f);
    }

    cube.Rotate(time * .25f, 0.f, 0.f);
    collider.RefreshDynamicModels();
  }

  if (g_CameraSettings.freeLook)
    camera.ProcessKeyboard(deltaTime);
  else {
    player.ProcessKeyboard(deltaTime);
    player.Update();

    XMFLOAT3 offset{0.f, g_CameraSettings.height, 0.f};
    float pitch = player.lookPitch;
    if (g_CameraSettings.thirdPerson) {
      pitch = g_CameraSettings.thirdPersonPitch;

      float distance = g_CameraSettings.thirdPersonDistance;
      float verticalOffset = distance * sinf(pitch);
      float horizontalDistance = distance * cosf(pitch);

      float offsetX = -cosf(player.lookYaw) * horizontalDistance;
      float offsetZ = -sinf(player.lookYaw) * horizontalDistance;
      offset = {offsetX, verticalOffset + 2.0f, offsetZ};

      camera.Follow(player.position, offset);
      camera.Target(player.position.x, player.position.y + 4.0f,
                    player.position.z);
    } else {
      camera.Follow(player.position, offset);
      camera.Orient(pitch, player.lookYaw);
    }
  }

  cylinder.Translate(player.position.x, player.position.y, player.position.z);
  cylinder.Rotate(0.0f, -player.lookYaw + XM_PIDIV2, 0.f);
}

void Game::DebugWindow()
{
  {
    ImGui::Begin("Player");
    ImGui::Text("position x: %f y: %f z: %f\ndir: %f", player.position.x,
                player.position.y, player.position.z, player.lookYaw);

    ImGui::Text("velocity x: %f y: %f z: %f", player.velocity.x,
                player.velocity.y, player.velocity.z);
    // ImGui::Text("floor normY %f", player.floor->normal.y);

    ImGui::Text("State: %d", player.currentState);
    ImGui::Text("Is Held: %d", Input::IsHeld(Input::KB::S));
    ImGui::End();
  }

  {
    camera.DebugWindow();

    ImGui::Begin("Controls");
    ImGui::Checkbox("Free look", &g_CameraSettings.freeLook);
    ImGui::Checkbox("Third person camera", &g_CameraSettings.thirdPerson);

    ImGui::SliderFloat("height", &g_CameraSettings.height, 0, 100);
    ImGui::SliderFloat("thirdPersonDistance",
                       &g_CameraSettings.thirdPersonDistance, 0, 100);
    ImGui::SliderFloat("thirdPersonPitch", &g_CameraSettings.thirdPersonPitch,
                       -1.57, 1.57);

    ImGui::End();
  }
}
