#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* {{{ Vector maths */

struct vec2
{
	vec2() : x(0.), y(0.) {}
	vec2(float x, float y) : x(x), y(y) {}

	float x, y;
};

struct vec3
{
	vec3() : x(0.), y(0.), z(0.) {}
	vec3(float x, float y, float z) : x(x), y(y), z(z) {}
	vec3 normalized() const
	{
		float l = sqrt(x * x + y * y + z * z);
		return vec3(x / l, y / l, z / l);
	}
	float dot(const vec3 &v) const
	{
		return v.x * x + v.y * y + v.z * z;
	}

	float x, y, z;
};

/* }}} */

struct Image
{
	bool load(char *path) { return !!(ptr = stbi_load(path, &w, &h, &n, 3)); }
	bool alloc() { return !!(ptr = malloc(w * h * n)); }
	size_t pixels() const { return w * h; }

	static float warp(const float v) { return v < 0. ? fmodf(v, 1.) + 1. : v; }
	void *uv(const vec2 &uv)
	{
		int u = (int)round(warp(uv.x) * w) % w;
		int v = (int)round(warp(uv.y) * h) % h;
		return (uint8_t *)ptr + (v * w + u) * n;
	}
	vec2 uvToCoordinate(const vec2 &uv) { return vec2((int)round(warp(uv.x) * w) % w, (int)round(warp(uv.y) * h) % h); }

	int w, h, n;
	void *ptr;
};

class Texture
{
public:
	virtual void targetSize(Image *img, int *w, int *h) const = 0;

	virtual vec2 latLongToUV(const vec2 &vec) const = 0;
	virtual vec2 uvToLatLong(const vec2 &vec) const = 0;

	// Theta direction: +X to +Z, phi direction: +Z to -Z
	static vec2 euclideanToLatLong(const vec3 &vec) { return vec2(atan2(vec.z, vec.x), acos(vec.normalized().dot(vec3(0., 1., 0.)))); }
};

class LatLong : public Texture
{
public:
	void targetSize(Image *img, int *w, int *h) const { throw 0; }

	vec2 latLongToUV(const vec2 &vec) const { return vec2(vec.x / 2. / M_PI, vec.y / M_PI); }
	vec2 uvToLatLong(const vec2 &vec) const { throw 0; return vec2(); }
};

class Cubemap : public Texture
{
public:
	void targetSize(Image *img, int *w, int *h) const
	{
		float x = sqrt((float)img->pixels() / 6.);
		*h = round(x);
		*w = *h * 6;
	}

	vec2 latLongToUV(const vec2 &vec) const { throw 0; return vec2(); }
	vec2 uvToLatLong(const vec2 &vec) const { return euclideanToLatLong(uvToEuclidean(vec)); }

private:
	vec3 uvToEuclidean(const vec2 &vec) const
	{
		float u = fmodf(vec.x * 6., 1.) * 2. - 1.;
		float v = vec.y * 2. - 1.;
		switch ((int)(vec.x * 6.)) {
		case 0:		// +X
			return vec3(1., -v, u);
		case 1:		// -X
			return vec3(-1., -v, -u);
		case 2:		// +Y
			return vec3(-u, 1., v);
		case 3:		// -Y
			return vec3(-u, -1., -v);
		case 4:		// +Z
			return vec3(-u, -v, 1);
		default:	// -Z
			return vec3(u, -v, -1);
		}
	}
};

static void render(Image *src, const vec2 &srcUV, Image *dst, const vec2 &dstUV)
{
	memcpy(dst->uv(dstUV), src->uv(srcUV), src->n);
}

static void help()
{
	fputs("conv INPUT OUTPUT\n", stderr);
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		help();
		return 1;
	}

	Image src, dst;
	if (!src.load(argv[1])) {
		fputs("Error loading input image\n", stderr);
		return 2;
	}

	puts("Loading input image...");
	Texture *source = new LatLong;
	Texture *target = new Cubemap;
	if (!source || !target) {
		fputs("Error creating texture transformation\n", stderr);
		stbi_image_free(src.ptr);
		return 3;
	}

	dst.n = src.n;
	target->targetSize(&src, &dst.w, &dst.h);
	if (!dst.alloc()) {
		fputs("Error allocating image memory\n", stderr);
		stbi_image_free(src.ptr);
		return 4;
	}
	printf("Output image size: %ux%u\n", dst.w, dst.h);

	puts("Rendering...");
	for (int u = 0; u != dst.w; u++)
		for (int v = 0; v != dst.h; v++) {
			vec2 dstUV((float)u / (float)dst.w, (float)v / (float)dst.h);
			vec2 srcUV(source->latLongToUV(target->uvToLatLong(dstUV)));
			render(&src, srcUV, &dst, dstUV);
		}
	puts("Rendering finished.");

	puts("Saving output image...");
	stbi_write_png(argv[2], dst.w, dst.h, dst.n, dst.ptr, dst.w * dst.n);

	stbi_image_free(src.ptr);
	free(dst.ptr);
	return 0;
}
