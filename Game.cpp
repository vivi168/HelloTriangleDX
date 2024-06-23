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

  Surface* floor = nullptr;

  enum class State { Unknown, Standing, Walking, Falling, Jumping, Swimming };

  State currentState;

  Player();
  void ProcessKeyboard(float dt);
  void Update();
};

static Mesh3D treeMesh, cubeMesh, cylinderMesh, yukaMesh, houseMesh,
    terrainMesh, stairsMesh, unitCubeMesh, fieldMesh;
static Model3D bigTree, smallTree, cube, cylinder, yuka, house, terrain, stairs,
    unitCube;

static Camera camera;
static Collider collider;
static Player player;

static const float PLAYER_ROT_SPEED = 2.f;
static const float PLAYER_SPEED = 20.0f;
static const float FALLING_SPEED = 30.0f;

struct {
  bool freeLook;
  bool thirdPerson;
  float height;
  float thirdPersonDistance;
  float thirdPersonPitch;
} g_CameraSettings{false, false, 4.0f, 20.f, 0.35f};

Player::Player()
{
  lookYaw = XM_PIDIV2;
  lookPitch = 0.0f;

  position = {0.f, 0.f, 0.f};
  velocity = {0.f, 0.f, 0.f};

  currentState = State::Standing;
}

void Player::ProcessKeyboard(float dt)
{
  if (Input::IsHeld(Input::KB::Up)) {
    lookPitch += PLAYER_ROT_SPEED * dt;
  }
  if (Input::IsHeld(Input::KB::Down)) {
    lookPitch -= PLAYER_ROT_SPEED * dt;
  }
  if (Input::IsHeld(Input::KB::A)) {
    lookYaw += PLAYER_ROT_SPEED * dt;
  }
  if (Input::IsHeld(Input::KB::D)) {
    lookYaw -= PLAYER_ROT_SPEED * dt;
  }

  static constexpr float epsilon = std::numeric_limits<float>::epsilon();
  static constexpr float upper = XM_PIDIV2 - epsilon;
  static constexpr float lower = -XM_PIDIV2 + epsilon;

  if (lookPitch > upper)
    lookPitch = upper;
  else if (lookPitch < lower)
    lookPitch = lower;

  float forwardX = cosf(lookYaw);
  float forwardZ = sinf(lookYaw);
  float rightX = sinf(lookYaw);
  float rightZ = -cosf(lookYaw);

  velocity = {0.f, 0.f, 0.f};

  if (currentState == State::Falling) {
    velocity.y = -FALLING_SPEED * dt;
    return;
  } else {
    currentState = State::Standing;
  }

  if (Input::IsHeld(Input::KB::W)) {
    velocity.x = forwardX * PLAYER_SPEED * dt;
    velocity.z = forwardZ * PLAYER_SPEED * dt;
    currentState = State::Walking;
  }

  if (Input::IsHeld(Input::KB::S)) {
    velocity.x = -forwardX * PLAYER_SPEED * dt;
    velocity.z = -forwardZ * PLAYER_SPEED * dt;
    currentState = State::Walking;
  }

  if (Input::IsHeld(Input::KB::Left)) {
    velocity.x = -rightX * PLAYER_SPEED * dt;
    velocity.z = -rightZ * PLAYER_SPEED * dt;
    currentState = State::Walking;
  }

  if (Input::IsHeld(Input::KB::Right)) {
    velocity.x = rightX * PLAYER_SPEED * dt;
    velocity.z = rightZ * PLAYER_SPEED * dt;
    currentState = State::Walking;
  }
}

void Player::Update()
{
  float velMod = 1.0f;
  if (floor)
    velMod = floor->normal.y;  // the steeper the slope the slower the speed

  XMVECTOR playerStart = XMLoadFloat3(&position);
  XMVECTOR playerVel = XMLoadFloat3(&velocity) * velMod;
  XMVECTOR playerEnd = playerStart + playerVel;
  XMVECTOR direction = XMVector3Normalize(playerVel);
  float velMag = XMVectorGetX(XMVector3Length(playerVel));

  Surface* wall = nullptr;
  float distance;
  if (velMag > 0.f)
    wall = collider.FindWall(playerStart, direction, 2.0f, distance);

  XMStoreFloat3(&position, playerEnd);

  if (wall) {
    float distanceMoved = XMVectorGetX(XMVector3Length(playerVel));
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
  floor = collider.FindFloor(position, 2.0f, height);
  assert(floor);  // should not happen

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

  treeMesh.Read("assets/tree.objb");
  yukaMesh.Read("assets/yuka.objb");
  houseMesh.Read("assets/tower.objb");
  terrainMesh.Read("assets/terrain.objb");
  // terrainMesh = t.Mesh();
  cubeMesh.Read("assets/cube.objb");
  unitCubeMesh.Read("assets/unit_cube.objb");
  cylinderMesh.Read("assets/cylinder.objb");
  stairsMesh.Read("assets/stairs.objb");
  fieldMesh.Read("assets/bf.objb");

  bigTree.mesh = &treeMesh;
  smallTree.mesh = &treeMesh;
  yuka.mesh = &yukaMesh;
  house.mesh = &houseMesh;
  terrain.mesh = &terrainMesh;
  // terrain.mesh = &fieldMesh;
  cube.mesh = &cubeMesh;
  cylinder.mesh = &cylinderMesh;
  stairs.mesh = &stairsMesh;
  unitCube.mesh = &unitCubeMesh;

  smallTree.Scale(0.5f);
  smallTree.Translate(-7.f, 0.f, 0.f);
  bigTree.Translate(-7.f, 0.0f, 14.f);
  yuka.Scale(5.f);
  yuka.Translate(15.f, 0.f, 15.f);
  house.Translate(20.f, 0.f, 50.f);
  stairs.Translate(-50.f, 0.f, 20.f);
  cube.Translate(0.f, 50.f, 0.f);
  cube.Scale(5.f);

  unitCube.Translate(10.f, 0.0f, -10.f);

  Renderer::AppendToScene(&bigTree);
  Renderer::AppendToScene(&smallTree);
  Renderer::AppendToScene(&yuka);
  Renderer::AppendToScene(&house);
  Renderer::AppendToScene(&terrain);
  Renderer::AppendToScene(&cube);
  Renderer::AppendToScene(&cylinder);
  Renderer::AppendToScene(&stairs);
  Renderer::AppendToScene(&unitCube);

  collider.AppendStaticModel(&terrain);
  collider.AppendStaticModel(&house);
  collider.AppendStaticModel(&yuka);
  collider.AppendStaticModel(&stairs);
  collider.AppendStaticModel(&unitCube);
}

void Game::Update(float time, float deltaTime)
{
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
  cylinder.Rotate(0.0f, -player.lookYaw - XM_PIDIV2, 0.f);
  cube.Rotate(time * .5f, 0.f, 0.f);
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
