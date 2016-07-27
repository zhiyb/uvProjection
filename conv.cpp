#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifndef timersub
/* This is a copy from GNU C Library (GNU LGPL 2.1), sys/time.h. */
# define timersub(a, b, result)                                               \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                          \
    if ((result)->tv_usec < 0) {                                              \
      --(result)->tv_sec;                                                     \
      (result)->tv_usec += 1000000;                                           \
    }                                                                         \
  } while (0)
#endif

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

/* {{{ Image storage */
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
/* }}} */

/* {{{ Transformations */
#ifndef CLASS
static inline vec2 euclideanToLatLong(const vec3 &vec)
{
	return vec2(atan2(vec.z, vec.x), acos(vec.normalized().dot(vec3(0., 1., 0.))));
}

/* {{{ LatLong transformations */
static inline void latLong_targetSize(Image *img, int *w, int *h)
{
	float x = sqrt((float)img->pixels() / 6.);
	*h = round(x);
	*w = *h * 6;
}

static inline vec2 latLong_latLongToUV(const vec2 &vec)
{
	return vec2(vec.x / 2. / M_PI, vec.y / M_PI);
}
/* }}} */

/* {{{ Cubemap transformations */
static inline vec3 cubemap_uvToEuclidean(const vec2 &vec)
{
	float u = vec.x * 6.;
	int n = (int)u;
	u -= n;
	float v = vec.y * 2. - 1.;
#if 0
	switch (n) {
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
#else
#if 0
	float p1 = 1., n1 = -1.;
	float nu = -u, nv = -v;
	const float *x[6] = {&p1, &n1, &nu, &nu, &nu, &u};
	const float *y[6] = {&nv, &nv, &p1, &n1, &nv, &nv};
	const float *z[6] = {&u, &nu, &v, &nv, &p1, &n1};
	//n %= 6;
	return vec3(*x[n], *y[n], *z[n]);
#else
	vec3 faces[6] = {
		vec3(1., -v, u),	// +X
		vec3(-1., -v, -u),	// -X
		vec3(-u, 1., v),	// +Y
		vec3(-u, -1., -v),	// -Y
		vec3(-u, -v, 1),	// +Z
		vec3(u, -v, -1),	// -Z
	};
	return faces[n % 6];
#endif
#endif
}

static inline vec2 cubemap_uvToLatLong(const vec2 &vec)
{
	return euclideanToLatLong(cubemap_uvToEuclidean(vec));
}
/* }}} */

static void (*const targetSize)(Image *img, int *w, int *h)	= latLong_targetSize;
// Source texture transformation
static vec2 (*const latLongToUV)(const vec2 &vec)		= latLong_latLongToUV;
// Target texture transformation
static vec2 (*const uvToLatLong)(const vec2 &vec)		= cubemap_uvToLatLong;
#else
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
		float u = vec.x * 6.;
		int n = (int)u;
		u -= n;
		float v = vec.y * 2. - 1.;
	}
};
#endif
/* }}} */

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

	struct timeval tStart, tEnd, tElapsed;

	puts("Loading input image...");
	Image src, dst;
	gettimeofday(&tStart, NULL);
	if (!src.load(argv[1])) {
		fputs("Error loading input image\n", stderr);
		return 2;
	}
	gettimeofday(&tEnd, NULL);
	timersub(&tEnd, &tStart, &tElapsed);
	printf("Time elapsed: %ld.%06ld\n", tElapsed.tv_sec, tElapsed.tv_usec);

#ifdef CLASS
	LatLong *source = new LatLong;
	Cubemap *target = new Cubemap;
	if (!source || !target) {
		fputs("Error creating texture transformation\n", stderr);
		stbi_image_free(src.ptr);
		return 3;
	}
#endif

	dst.n = src.n;
#ifndef CLASS
	targetSize(&src, &dst.w, &dst.h);
#else
	target->targetSize(&src, &dst.w, &dst.h);
#endif
	if (!dst.alloc()) {
		fputs("Error allocating image memory\n", stderr);
		stbi_image_free(src.ptr);
		return 4;
	}
	printf("Output image size: %ux%u\n", dst.w, dst.h);

	puts("Rendering...");
	gettimeofday(&tStart, NULL);
	uint8_t *ptr = (uint8_t *)dst.ptr;
	for (int u = 0; u != dst.w; u++)
		for (int v = 0; v != dst.h; v++) {
			vec2 dstUV(((float)u + 0.5) / (float)dst.w, ((float)v + 0.5) / (float)dst.h);
#ifndef CLASS
			vec2 srcUV(latLongToUV(uvToLatLong(dstUV)));
#else
			vec2 srcUV(source->latLongToUV(target->uvToLatLong(dstUV)));
#endif
			memcpy(ptr, src.uv(srcUV), src.n);
			ptr += 3;

		}
	gettimeofday(&tEnd, NULL);
	timersub(&tEnd, &tStart, &tElapsed);
	puts("Rendering finished.");
	printf("Time elapsed: %ld.%06ld\n", tElapsed.tv_sec, tElapsed.tv_usec);

	puts("Saving output image...");
	gettimeofday(&tStart, NULL);
	//stbi_write_png(argv[2], dst.w, dst.h, dst.n, dst.ptr, dst.w * dst.n);
	gettimeofday(&tEnd, NULL);
	timersub(&tEnd, &tStart, &tElapsed);
	printf("Time elapsed: %ld.%06ld\n", tElapsed.tv_sec, tElapsed.tv_usec);

	stbi_image_free(src.ptr);
	free(dst.ptr);
	return 0;
}
