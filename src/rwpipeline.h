namespace rw {

struct PipeAttribute
{
	const char *name;
	uint32 attrib;
};

struct Atomic;

class Pipeline
{
public:
	uint32 pluginID;
	uint32 pluginData;
	uint32 platform;

	Pipeline(uint32 platform);
	Pipeline(Pipeline *p);
	~Pipeline(void);
	virtual void dump(void);
};

class ObjPipeline : public Pipeline
{
public:
	ObjPipeline(uint32 platform) : Pipeline(platform) {}
	virtual void instance(Atomic *atomic);
	virtual void uninstance(Atomic *atomic);
	virtual void render(Atomic *atomic);
};

void findMinVertAndNumVertices(uint16 *indices, uint32 numIndices, uint32 *minVert, uint32 *numVertices);

// everything xbox, d3d8 and d3d9 may want to use
enum {
	VERT_BYTE2 = 1,
	VERT_BYTE3,
	VERT_SHORT2,
	VERT_SHORT3,
	VERT_NORMSHORT2,
	VERT_NORMSHORT3,
	VERT_FLOAT2,
	VERT_FLOAT3,
	VERT_ARGB,
	VERT_COMPNORM
};

#define COLOR_ARGB(a,r,g,b) \
    ((uint32)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))

void instV3d(int type, uint8 *dst, float *src, uint32 numVertices, uint32 stride);
void instV2d(int type, uint8 *dst, float *src, uint32 numVertices, uint32 stride);
bool32 instColor(int type, uint8 *dst, uint8 *src, uint32 numVertices, uint32 stride);

}
