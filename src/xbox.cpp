#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <new>

#include "rwbase.h"
#include "rwplugin.h"
#include "rwpipeline.h"
#include "rwobjects.h"
#include "rwxbox.h"

using namespace std;

namespace rw {
namespace xbox {

enum {
	D3DPT_TRIANGLELIST    =  5,
	D3DPT_TRIANGLESTRIP   =  6,
};

void*
destroyNativeData(void *object, int32, int32)
{
	Geometry *geometry = (Geometry*)object;
	assert(geometry->instData != NULL);
	assert(geometry->instData->platform == PLATFORM_XBOX);
	InstanceDataHeader *header =
		(InstanceDataHeader*)geometry->instData;
	// TODO
	delete header;
	return object;
}

void
readNativeData(Stream *stream, int32, void *object, int32, int32)
{
	Geometry *geometry = (Geometry*)object;
	uint32 vers;
	assert(findChunk(stream, ID_STRUCT, NULL, &vers));
	assert(stream->readU32() == PLATFORM_XBOX);
	assert(vers >= 0x35000 && "can't handle native Xbox data < 0x35000");
	InstanceDataHeader *header = new InstanceDataHeader;
	geometry->instData = header;
	header->platform = PLATFORM_XBOX;

	int32 size = stream->readI32();
	// The 0x18 byte are the resentryheader.
	// We don't have it but it's used for alignment.
	header->data = new uint8[size + 0x18];
	uint8 *p = header->data+0x18+4;
	stream->read(p, size-4);

	header->size = size;
	header->serialNumber = *(uint16*)p; p += 2;
	header->numMeshes = *(uint16*)p; p += 2;
	header->primType = *(uint32*)p; p += 4;
	header->numVertices = *(uint32*)p; p += 4;
	header->stride = *(uint32*)p; p += 4;
	// RxXboxVertexFormat in 3.3 here
	p += 4;	// skip vertexBuffer pointer
	header->vertexAlpha = *(bool32*)p; p += 4;
	p += 8; // skip begin, end pointers

	InstanceData *inst  = new InstanceData[header->numMeshes];
	header->begin = inst;
	for(int i = 0; i < header->numMeshes; i++){
		inst->minVert = *(uint32*)p; p += 4;
		inst->numVertices = *(int32*)p; p += 4;
		inst->numIndices = *(int32*)p; p += 4;
		inst->indexBuffer = header->data + *(uint32*)p; p += 4;
		p += 8; // skip material and vertexShader
		inst->vertexShader = 0;
		// pixelShader in 3.3 here
		inst++;
	}
	header->end = inst;

	header->vertexBuffer = new uint8[header->stride*header->numVertices];
	stream->read(header->vertexBuffer, header->stride*header->numVertices);
}

void
writeNativeData(Stream *stream, int32 len, void *object, int32, int32)
{
	Geometry *geometry = (Geometry*)object;
	writeChunkHeader(stream, ID_STRUCT, len-12);
	assert(geometry->instData != NULL);
	assert(geometry->instData->platform == PLATFORM_XBOX);
	stream->writeU32(PLATFORM_XBOX);
	assert(rw::version >= 0x35000 && "can't write native Xbox data < 0x35000");
	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;

	// we just fill header->data and write that
	uint8 *p = header->data+0x18;
	*(int32*)p = header->size; p += 4;
	*(uint16*)p = header->serialNumber; p += 2;
	*(uint16*)p = header->numMeshes; p += 2;
	*(uint32*)p = header->primType; p += 4;
	*(uint32*)p = header->numVertices; p += 4;
	*(uint32*)p = header->stride; p += 4;
	// RxXboxVertexFormat in 3.3 here
	p += 4;	// skip vertexBuffer pointer
	*(bool32*)p = header->vertexAlpha; p += 4;
	p += 8; // skip begin, end pointers

	InstanceData *inst = header->begin;
	for(int i = 0; i < header->numMeshes; i++){
		*(uint32*)p = inst->minVert; p += 4;
		*(int32*)p = inst->numVertices; p += 4;
		*(int32*)p = inst->numIndices; p += 4;
		*(uint32*)p = (uint8*)inst->indexBuffer - header->data; p += 4;
		p += 8; // skip material and vertexShader
		// pixelShader in 3.3 here
		inst++;
	}

	stream->write(header->data+0x18, header->size);
	stream->write(header->vertexBuffer, header->stride*header->numVertices);
}

int32
getSizeNativeData(void *object, int32, int32)
{
	Geometry *geometry = (Geometry*)object;
	assert(geometry->instData != NULL);
	assert(geometry->instData->platform == PLATFORM_XBOX);
	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;
	return 12 + 4 + header->size + header->stride*header->numVertices;
}

void
registerNativeDataPlugin(void)
{
	Geometry::registerPlugin(0, ID_NATIVEDATA,
	                         NULL, destroyNativeData, NULL);
	Geometry::registerPluginStream(ID_NATIVEDATA,
	                               readNativeData,
	                               writeNativeData,
	                               getSizeNativeData);
}

ObjPipeline::ObjPipeline(uint32 platform)
 : rw::ObjPipeline(platform),
   instanceCB(NULL), uninstanceCB(NULL) { }

void
ObjPipeline::instance(Atomic *atomic)
{
	Geometry *geo = atomic->geometry;
	if(geo->geoflags & Geometry::NATIVE)
		return;
	geo->geoflags |= Geometry::NATIVE;
	InstanceDataHeader *header = new InstanceDataHeader;
	MeshHeader *meshh = geo->meshHeader;
	geo->instData = header;
	header->platform = PLATFORM_XBOX;

	header->size = 0x24 + meshh->numMeshes*0x18 + 0x10;
	Mesh *mesh = meshh->mesh;
	for(uint32 i = 0; i < meshh->numMeshes; i++)
		header->size += (mesh++->numIndices*2 + 0xF) & ~0xF;
	// The 0x18 byte are the resentryheader.
	// We don't have it but it's used for alignment.
	header->data = new uint8[header->size + 0x18];
	header->serialNumber = 0;
	header->numMeshes = meshh->numMeshes;
	header->primType = meshh->flags == 1 ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST;
	header->numVertices = geo->numVertices;
	header->vertexAlpha = 0;
	// set by the instanceCB
	header->stride = 0;
	header->vertexBuffer = NULL;

	InstanceData *inst = new InstanceData[header->numMeshes];
	header->begin = inst;
	mesh = meshh->mesh;
	uint8 *indexbuf = (uint8*)header->data + ((0x18 + 0x24 + header->numMeshes*0x18 + 0xF)&~0xF);
	for(uint32 i = 0; i < header->numMeshes; i++){
		findMinVertAndNumVertices(mesh->indices, mesh->numIndices,
		                          &inst->minVert, (uint32*)&inst->numVertices);
		inst->numIndices = mesh->numIndices;
		inst->indexBuffer = indexbuf;
		memcpy(inst->indexBuffer, mesh->indices, inst->numIndices*sizeof(uint16));
		indexbuf += (inst->numIndices*2 + 0xF) & ~0xF;
		inst->material = mesh->material;
		inst->vertexShader = 0;	// TODO?
		mesh++;
		inst++;
	}
	header->end = inst;

	this->instanceCB(geo, header);
}

void
ObjPipeline::uninstance(Atomic *atomic)
{
	assert(0 && "can't uninstance");
}

int v3dFormatMap[] = {
	-1, VERT_BYTE3, VERT_SHORT3, VERT_NORMSHORT3, VERT_COMPNORM, VERT_FLOAT3
};

int v2dFormatMap[] = {
	-1, VERT_BYTE2, VERT_SHORT2, VERT_NORMSHORT2, VERT_COMPNORM, VERT_FLOAT2
};

void
defaultInstanceCB(Geometry *geo, InstanceDataHeader *header)
{
	uint32 *vertexFmt = getVertexFmt(geo);
	if(*vertexFmt == 0)
		*vertexFmt = makeVertexFmt(geo->geoflags, geo->numTexCoordSets);
	header->stride = getVertexFmtStride(*vertexFmt);
	header->vertexBuffer = new uint8[header->stride*header->numVertices];
	uint32 offset = 0;
	uint8 *dst = (uint8*)header->vertexBuffer;

	uint32 fmt = *vertexFmt;
	uint32 sel = fmt & 0xF;
	instV3d(v3dFormatMap[sel], dst, geo->morphTargets[0].vertices,
	        header->numVertices, header->stride);
	dst += sel == 4 ? 4 : 3*vertexFormatSizes[sel];

	sel = (fmt >> 4) & 0xF;
	if(sel){
		instV3d(v3dFormatMap[sel], dst, geo->morphTargets[0].normals,
		        header->numVertices, header->stride);
		dst += sel == 4 ? 4 : 3*vertexFormatSizes[sel];
	}

	if(fmt & 0x1000000){
		header->vertexAlpha = instColor(VERT_ARGB, dst, geo->colors,
		                                header->numVertices, header->stride);
		dst += 4;
	}

	for(int i = 0; i < 4; i++){
		sel = (fmt >> (i*4 + 8)) & 0xF;
		if(sel == 0)
			break;
		instV2d(v2dFormatMap[sel], dst, geo->texCoords[i],
		        header->numVertices, header->stride);
		dst += sel == 4 ? 4 : 2*vertexFormatSizes[sel];
	}

	if(fmt & 0xE000000)
		assert(0 && "can't instance tangents or whatever it is");
}

ObjPipeline*
makeDefaultPipeline(void)
{
	ObjPipeline *pipe = new ObjPipeline(PLATFORM_XBOX);
	pipe->instanceCB = defaultInstanceCB;
	return pipe;
}

// Skin plugin

struct NativeSkin
{
	int32 table1[256];	// maps indices to bones
	int32 table2[256];	// maps bones to indices
	int32 numUsedBones;
	void *vertexBuffer;
	int32 stride;
};

void
readNativeSkin(Stream *stream, int32, void *object, int32 offset)
{
	Geometry *geometry = (Geometry*)object;
	uint32 vers;
	assert(findChunk(stream, ID_STRUCT, NULL, &vers));
	assert(vers >= 0x35000 && "can't handle native xbox skin < 0x35000");
	assert(stream->readU32() == PLATFORM_XBOX);
	Skin *skin = new Skin;
	*PLUGINOFFSET(Skin*, geometry, offset) = skin;
	skin->numBones = stream->readI32();

	// only allocate matrices
	skin->numUsedBones = 0;
	skin->allocateData(0);
	NativeSkin *natskin = new NativeSkin;
	skin->platformData = natskin;
	stream->read(natskin->table1, 256*sizeof(int32));
	stream->read(natskin->table2, 256*sizeof(int32));
	// we use our own variable for this due to allocation
	natskin->numUsedBones = stream->readI32();
	skin->maxIndex = stream->readI32();
	stream->seek(4);	// skip pointer to vertexBuffer
	natskin->stride = stream->readI32();
	int32 size = geometry->numVertices*natskin->stride;
	natskin->vertexBuffer = new uint8[size];
	stream->read(natskin->vertexBuffer, size);
	stream->read(skin->inverseMatrices, skin->numBones*64);

	// no split skins in GTA
	stream->seek(12);
}

void
writeNativeSkin(Stream *stream, int32 len, void *object, int32 offset)
{
	Geometry *geometry = (Geometry*)object;
	Skin *skin = *PLUGINOFFSET(Skin*, object, offset);
	assert(skin->platformData);
	assert(rw::version >= 0x35000 && "can't handle native xbox skin < 0x35000");
	NativeSkin *natskin = (NativeSkin*)skin->platformData;

	writeChunkHeader(stream, ID_STRUCT, len-12);
	stream->writeU32(PLATFORM_XBOX);
	stream->writeI32(skin->numBones);
	stream->write(natskin->table1, 256*sizeof(int32));
	stream->write(natskin->table2, 256*sizeof(int32));
	stream->writeI32(natskin->numUsedBones);
	stream->writeI32(skin->maxIndex);
	stream->writeU32(0xBADEAFFE);	// pointer to vertexBuffer
	stream->writeI32(natskin->stride);
	stream->write(natskin->vertexBuffer,
	              geometry->numVertices*natskin->stride);
	stream->write(skin->inverseMatrices, skin->numBones*64);
	int32 buffer[3] = { 0, 0, 0};
	stream->write(buffer, 12);
}

int32
getSizeNativeSkin(void *object, int32 offset)
{
	Geometry *geometry = (Geometry*)object;
	Skin *skin = *PLUGINOFFSET(Skin*, object, offset);
	if(skin == NULL)
		return -1;
	if(skin->platformData == NULL)
		return -1;
	NativeSkin *natskin = (NativeSkin*)skin->platformData;
	return 12 + 8 + 2*256*4 + 4*4 +
	        natskin->stride*geometry->numVertices + skin->numBones*64 + 12;
}

void
skinInstanceCB(Geometry *geo, InstanceDataHeader *header)
{
	defaultInstanceCB(geo, header);
}

ObjPipeline*
makeSkinPipeline(void)
{
	ObjPipeline *pipe = new ObjPipeline(PLATFORM_XBOX);
	pipe->instanceCB = skinInstanceCB;
	pipe->pluginID = ID_SKIN;
	pipe->pluginData = 1;
	return pipe;
}

ObjPipeline*
makeMatFXPipeline(void)
{
	ObjPipeline *pipe = new ObjPipeline(PLATFORM_XBOX);
	pipe->instanceCB = defaultInstanceCB;
	pipe->pluginID = ID_MATFX;
	pipe->pluginData = 0;
	return pipe;
}

// Vertex Format Plugin

static int32 vertexFmtOffset;

uint32 vertexFormatSizes[6] = {
	0, 1, 2, 2, 4, 4
};

uint32*
getVertexFmt(Geometry *g)
{
	return PLUGINOFFSET(uint32, g, vertexFmtOffset);
}

uint32
makeVertexFmt(int32 flags, uint32 numTexSets)
{
	if(numTexSets > 4)
		numTexSets = 4;
	uint32 fmt = 0x5;	// FLOAT3
	if(flags & Geometry::NORMALS)
		fmt |= 0x40;	// NORMPACKED3
	for(uint32 i = 0; i < numTexSets; i++)
		fmt |= 0x500 << i*4;	// FLOAT2
	if(flags & Geometry::PRELIT)
		fmt |= 0x1000000;	// D3DCOLOR
	return fmt;
}

uint32
getVertexFmtStride(uint32 fmt)
{
	uint32 stride = 0;
	uint32 v = fmt & 0xF;
	uint32 n = (fmt >> 4) & 0xF;
	stride += v == 4 ? 4 : 3*vertexFormatSizes[v];
	stride += n == 4 ? 4 : 3*vertexFormatSizes[n];
	if(fmt & 0x1000000)
		stride += 4;
	for(int i = 0; i < 4; i++){
		uint32 t = (fmt >> (i*4 + 8)) & 0xF;
		stride += t == 4 ? 4 : 2*vertexFormatSizes[t];
	}
	if(fmt & 0xE000000)
		stride += 8;
	return stride;
}

static void*
createVertexFmt(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(uint32, object, offset) = 0;
	return object;
}

static void*
copyVertexFmt(void *dst, void *src, int32 offset, int32)
{
	*PLUGINOFFSET(uint32, dst, offset) = *PLUGINOFFSET(uint32, src, offset);
	return dst;
}

static void
readVertexFmt(Stream *stream, int32, void *object, int32 offset, int32)
{
	uint32 fmt = stream->readU32();
	*PLUGINOFFSET(uint32, object, offset) = fmt;
	// TODO: ? create and attach "vertex shader"
}

static void
writeVertexFmt(Stream *stream, int32, void *object, int32 offset, int32)
{
	stream->writeI32(*PLUGINOFFSET(uint32, object, offset));
}

static int32
getSizeVertexFmt(void*, int32, int32)
{
	if(rw::platform != PLATFORM_XBOX)
		return -1;
	return 4;
}

void
registerVertexFormatPlugin(void)
{
	vertexFmtOffset = Geometry::registerPlugin(sizeof(uint32), ID_VERTEXFMT,
	                         createVertexFmt, NULL, copyVertexFmt);
	Geometry::registerPluginStream(ID_VERTEXFMT,
	                               readVertexFmt,
	                               writeVertexFmt,
	                               getSizeVertexFmt);
}

}
}
