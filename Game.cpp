#include "stdafx.h"
#include "Game.h"
#include "Mesh.h"
#include "Camera.h"
#include "Collider.h"
#include "Renderer.h"
#include "Input.h"

using namespace DirectX;

static Mesh3D treeMesh, cubeMesh, cylinderMesh, yukaMesh, houseMesh,
    terrainMesh, stairsMesh, unitCubeMesh;
static Model3D bigTree, smallTree, cube, cylinder, yuka, house, terrain, stairs,
    unitCube;

static Camera camera;
static Collider collider;

static float playerX, playerY, playerZ;
static float playerDirection = XM_PIDIV2;
static float playerRotSpeed = 0.02f;
static float playerSpeed = 20.0f;

struct Player {
  XMFLOAT3 position;
  XMFLOAT3 velocity;
  float direction;

  Surface* floor;

  enum class State { Unknown, Grounded, Falling, Jumping, Swimming };

  State GroundStep();
  void ProcessKeyboard();
};

Player::State Player::GroundStep()
{
  // TODO
  XMFLOAT3 intendedPos{};
  State result = State::Unknown;

  intendedPos.x = position.x + floor->normal.y * velocity.x;
  intendedPos.z = position.z + floor->normal.y * velocity.z;
  intendedPos.y = position.y;

  return result;
}

void Game::Init()
{
  playerX = 0.f;
  playerY = 0.f;
  playerZ = 0.f;

  float forwardX = cosf(playerDirection);
  float forwardZ = sinf(playerDirection);
  camera.Translate(playerX, playerY + 4.0f, playerZ);
  camera.Target(playerX + forwardX, playerY + 4.0f, playerZ + forwardZ);

  Renderer::SetSceneCamera(&camera);

  treeMesh.Read("assets/tree.objb");
  yukaMesh.Read("assets/yuka.objb");
  houseMesh.Read("assets/house.objb");
  terrainMesh.Read("assets/terrain.objb");
  // terrainMesh = t.Mesh();
  cubeMesh.Read("assets/cube.objb");
  unitCubeMesh.Read("assets/unit_cube.objb");
  cylinderMesh.Read("assets/cylinder.objb");
  stairsMesh.Read("assets/stairs.objb");

  bigTree.mesh = &treeMesh;
  smallTree.mesh = &treeMesh;
  yuka.mesh = &yukaMesh;
  house.mesh = &houseMesh;
  terrain.mesh = &terrainMesh;
  cube.mesh = &cubeMesh;
  cylinder.mesh = &cylinderMesh;
  stairs.mesh = &stairsMesh;
  unitCube.mesh = &unitCubeMesh;

  smallTree.Scale(0.5f);
  smallTree.Translate(-7.f, 0.f, 0.f);
  bigTree.Translate(-7.f, 0.0f, 14.f);
  yuka.Scale(5.f);
  yuka.Translate(15.f, 0.f, 15.f);
  house.Translate(0.f, 0.f, 50.f);
  stairs.Translate(-50.f, 0.f, 20.f);
  cube.Translate(0.f, 50.f, 0.f);
  cube.Scale(5.f);
  cylinder.Translate(playerX, playerY, playerZ);

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
  camera.ProcessKeyboard(deltaTime);

  float forwardX = cosf(playerDirection);
  float forwardZ = sinf(playerDirection);

  XMVECTOR playerStart = XMVectorSet(playerX, playerY + 2.0f, playerZ, 0.0f);

  if (Input::IsHeld(Input::KB::I)) {
    playerX += playerSpeed * forwardX * deltaTime;
    playerZ += playerSpeed * forwardZ * deltaTime;
  } else if (Input::IsHeld(Input::KB::K)) {
    playerX -= playerSpeed * forwardX * deltaTime;
    playerZ -= playerSpeed * forwardZ * deltaTime;
  }
  if (Input::IsHeld(Input::KB::J)) {
    playerDirection += playerRotSpeed;
  }
  if (Input::IsHeld(Input::KB::L)) {
    playerDirection -= playerRotSpeed;
  }

  // if (Input::IsPressed(Input::KB::Space))
  //   camera.Target(playerX, playerY, playerZ);

  camera.Translate(playerX, playerY + 4.0f, playerZ);
  camera.Orient(0.f, playerDirection);

  cylinder.Translate(playerX, playerY, playerZ);
  cylinder.Rotate(0.0f, -playerDirection - XM_PIDIV2, 0.f);
  cube.Rotate(time * .5f, 0.f, 0.f);

  // update player
  {
    XMVECTOR playerEnd = XMVectorSet(playerX, playerY + 2.0f, playerZ, 0.0f);
    XMVECTOR direction = XMVector3Normalize(playerEnd - playerStart);

    Surface* wall = nullptr;
    float distance;
    if (!XMVector3Equal(playerEnd, playerStart))
      wall = collider.FindWall(playerStart, direction, distance);

    if (wall) {
      float distanceMoved =
          XMVectorGetX(XMVector3Length(playerEnd - playerStart));
      distance -= 1.0f; // player radius
      if (distanceMoved > distance) {
        XMVECTOR wallNormal = XMLoadFloat3(&wall->normal);
        XMVECTOR hitPoint = playerStart + direction * distance;
        XMVECTOR directionAlongWall =
            direction - XMVector3Dot(direction, wallNormal) * wallNormal;

        static constexpr float epsilon = 0.000001f;
        XMVECTOR finalPos = hitPoint +
                            directionAlongWall * (distanceMoved - distance) +
                            wallNormal * epsilon;
        playerX = XMVectorGetX(finalPos);
        playerY = XMVectorGetY(finalPos);
        playerZ = XMVectorGetZ(finalPos);
      }
    }

    float height;
    Surface* floor = collider.FindFloor({playerX, playerY, playerZ}, height);
    assert(floor);  // should not happen

    if (playerY - height > 2.0f) {
      printf("falling\n");
      // TODO: falling (adjust value)
    }

    playerY = height;
  }
}

void Game::DebugWindow()
{
  camera.DebugWindow();

  {
    ImGui::Begin("Player");
    ImGui::Text("x: %f y: %f z: %f\ndir: %f", playerX, playerY, playerZ,
                playerDirection);
    ImGui::End();
  }
}
