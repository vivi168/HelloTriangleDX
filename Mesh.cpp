#include "stdafx.h"
#include "Mesh.h"

using namespace DirectX;

void Mesh3D::Read(std::string filename)
{
	FILE* fp;
	fopen_s(&fp, filename.c_str(), "rb");
	assert(fp);

	name = filename;
	
	fread(&header, sizeof(Header), 1, fp);

	vertices.resize(header.numVerts);
	indices.resize(header.numIndices);
	subsets.resize(header.numSubsets);

	fread(vertices.data(), sizeof(Vertex), header.numVerts, fp);
	fread(indices.data(), sizeof(uint16_t), header.numIndices, fp);
	fread(subsets.data(), sizeof(Subset), header.numSubsets, fp);

	fclose(fp);
}

Model3D::Model3D()
	: scale(1.f, 1.f, 1.f)
	, rotate(0.f, 0.f, 0.f)
	, translate(0.f, 0.f, 0.f)
{

}

DirectX::XMMATRIX Model3D::WorldMatrix()
{
	XMVECTOR scaleVector = XMLoadFloat3(&scale);
	XMVECTOR transVector = XMLoadFloat3(&translate);
	
	XMMATRIX scaleMatrix = XMMatrixScalingFromVector(scaleVector);
	XMMATRIX transMatrix = XMMatrixTranslationFromVector(transVector);
	
	XMMATRIX rotX = XMMatrixRotationX(rotate.x);
	XMMATRIX rotY = XMMatrixRotationY(rotate.y);
	XMMATRIX rotZ = XMMatrixRotationZ(rotate.z);

	XMMATRIX rotXY = XMMatrixMultiply(rotX, rotY);
	XMMATRIX rotXYZ = XMMatrixMultiply(rotXY, rotZ);

	XMMATRIX scaleRot = XMMatrixMultiply(scaleMatrix, rotXYZ);

	return XMMatrixMultiply(scaleRot, transMatrix);
}