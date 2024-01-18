#include "stdafx.h"
#include "Mesh.h"

void Texture::Read(std::string filename)
{
	FILE* fp;
	fopen_s(&fp, filename.c_str(), "rb");
	assert(fp);

	fread(&header, sizeof(Header), 1, fp);
	pixels.resize(ImageSize());
	fread(pixels.data(), sizeof(uint8_t), ImageSize(), fp);
}

void Mesh3D::Read(std::string filename)
{
	FILE* fp;
	fopen_s(&fp, filename.c_str(), "rb");
	assert(fp);
	
	fread(&header, sizeof(Header), 1, fp);

	vertices.resize(header.numVerts);
	indices.resize(header.numIndices);
	subsets.resize(header.numSubsets);

	fread(vertices.data(), sizeof(Vertex), header.numVerts, fp);
	fread(indices.data(), sizeof(uint16_t), header.numIndices, fp);
	fread(subsets.data(), sizeof(Subset), header.numSubsets, fp);

	fclose(fp);
}
