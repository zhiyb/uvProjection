/* {{{ Includes */
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <math.h>
#include "escape.h"

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
/* }}} */

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
		float l = sqrtf(x * x + y * y + z * z);
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

	static float warp(const float v) { return v + -floorf(v); }
	void *uv(const vec2 &uv)
	{
		int u = (int)roundf(warp(uv.x) * w) % w;
		int v = (int)roundf(warp(uv.y) * h) % h;
		return (uint8_t *)ptr + (v * w + u) * n;
	}
	const void *uv(const vec2 &uv) const
	{
		int u = (int)roundf(warp(uv.x) * w) % w;
		int v = (int)roundf(warp(uv.y) * h) % h;
		return (uint8_t *)ptr + (v * w + u) * n;
	}
	vec2 uvToCoordinate(const vec2 &uv) { return vec2((int)roundf(warp(uv.x) * w) % w, (int)roundf(warp(uv.y) * h) % h); }

	int w, h, n;
	void *ptr;
};
/* }}} */

/* {{{ Transformations */
static inline vec2 euclideanToLatLong(const vec3 &vec)
{
	return vec2(atan2f(vec.z, vec.x), acosf(vec.normalized().dot(vec3(0., 1., 0.))));
}

/* {{{ LatLong transformations */
static inline vec2 latLong_latLongToUV(const vec2 &vec)
{
	return vec2(vec.x / 2. / M_PI, vec.y / M_PI);
}
/* }}} */

/* {{{ Cubemap transformations */
static inline void cubemap_targetSize(Image *img, int *w, int *h)
{
	float x = sqrtf((float)(img->w * img->h) / 6.);
	*h = roundf(x);
	*w = *h * 6;
}

static inline vec3 cubemap_uvToEuclidean(const vec2 &vec, const unsigned int face)
{
	float u = vec.x * 2. - 1.;
	float v = vec.y * 2. - 1.;
	switch (face) {
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
	};
}

static inline vec3 cubemap_uvToEuclidean(const vec2 &vec)
{
	float u = vec.x * 6.;
	int n = (int)u;
	u = (u - n) * 2. - 1.;
	float v = vec.y * 2. - 1.;
	const vec3 faces[6] = {
		vec3(1., -v, u),	// +X
		vec3(-1., -v, -u),	// -X
		vec3(-u, 1., v),	// +Y
		vec3(-u, -1., -v),	// -Y
		vec3(-u, -v, 1),	// +Z
		vec3(u, -v, -1),	// -Z
	};
	return faces[n % 6];
}

static inline vec2 cubemap_uvToLatLong(const vec2 &vec, int face)
{
	return euclideanToLatLong(cubemap_uvToEuclidean(vec, face));
}

static inline vec2 cubemap_uvToLatLong(const vec2 &vec)
{
	return euclideanToLatLong(cubemap_uvToEuclidean(vec));
}
/* }}} */

static void (*const targetSize)(Image *img, int *w, int *h)
	= cubemap_targetSize;

// Source texture transformation
static vec2 (*const latLongToUV)(const vec2 &vec)
	= latLong_latLongToUV;

// Target texture transformation
static vec2 (*const uvToLatLong)(const vec2 &vec)
	= cubemap_uvToLatLong;
/* }}} */

/* {{{ Rendering */
static inline void generic_rendering(const Image *src, Image *dst)
{
	uint8_t *ptr = (uint8_t *)dst->ptr;
	for (int v = 0; v != dst->h; v++)
		for (int u = 0; u != dst->w; u++) {
			vec2 dstUV(((float)u + 0.5) / (float)dst->w, ((float)v + 0.5) / (float)dst->h);
			memcpy(ptr, src->uv(latLongToUV(uvToLatLong(dstUV))), src->n);
			ptr += 3;
		}
}

static inline void cubemap_rendering(const Image *src, Image *dst)
{
	const int s = dst->h, w = dst->w, n = dst->n;
	uint8_t *line = (uint8_t *)dst->ptr;
	for (int v = 0; v != s; v++) {
		uint8_t *ptr = line;
		for (int u = 0; u != s; u++) {
			vec2 dstUV(((float)u + 0.5) / (float)s, ((float)v + 0.5) / (float)s);
#if 0
			for (int f = 0; f != 6; f++)
				memcpy(ptr + (u + f * s) * n, src->uv(latLongToUV(cubemap_uvToLatLong(dstUV, f))), src->n);
#else
			memcpy(ptr + s * n * 0, src->uv(latLongToUV(cubemap_uvToLatLong(dstUV, 0))), src->n);
			memcpy(ptr + s * n * 1, src->uv(latLongToUV(cubemap_uvToLatLong(dstUV, 1))), src->n);
			memcpy(ptr + s * n * 2, src->uv(latLongToUV(cubemap_uvToLatLong(dstUV, 2))), src->n);
			memcpy(ptr + s * n * 3, src->uv(latLongToUV(cubemap_uvToLatLong(dstUV, 3))), src->n);
			memcpy(ptr + s * n * 4, src->uv(latLongToUV(cubemap_uvToLatLong(dstUV, 4))), src->n);
			memcpy(ptr + s * n * 5, src->uv(latLongToUV(cubemap_uvToLatLong(dstUV, 5))), src->n);
			ptr += n;
#endif
		}
		line += w * n;
	}
}

// Target specific rendering loop
static void (*const rendering)(const Image *src, Image *dst)
	= cubemap_rendering;
/* }}} */

/* {{{ main */
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

	puts(ESC_YELLOW "Loading input image..." ESC_DEFAULT);
	Image src, dst;
	gettimeofday(&tStart, NULL);
	if (!src.load(argv[1])) {
		fputs(ESC_RED "Error loading input image\n" ESC_DEFAULT, stderr);
		return 2;
	}
	gettimeofday(&tEnd, NULL);
	timersub(&tEnd, &tStart, &tElapsed);
	printf(ESC_CYAN "Time elapsed: %ld.%06ld\n" ESC_DEFAULT, tElapsed.tv_sec, tElapsed.tv_usec);

	dst.n = src.n;
	targetSize(&src, &dst.w, &dst.h);
	if (!dst.alloc()) {
		fputs(ESC_RED "Error allocating image memory\n" ESC_DEFAULT, stderr);
		stbi_image_free(src.ptr);
		return 4;
	}
	printf(ESC_BLUE "Output image size: %ux%u\n" ESC_DEFAULT, dst.w, dst.h);

	puts(ESC_YELLOW "Rendering..." ESC_DEFAULT);
	gettimeofday(&tStart, NULL);
	rendering(&src, &dst);
	gettimeofday(&tEnd, NULL);
	timersub(&tEnd, &tStart, &tElapsed);
	puts(ESC_GREEN "Rendering finished." ESC_DEFAULT);
	printf(ESC_CYAN "Time elapsed: %ld.%06ld\n" ESC_DEFAULT, tElapsed.tv_sec, tElapsed.tv_usec);

	puts(ESC_YELLOW "Saving output image..." ESC_DEFAULT);
	gettimeofday(&tStart, NULL);
	//stbi_write_png(argv[2], dst.w, dst.h, dst.n, dst.ptr, dst.w * dst.n);
	stbi_write_bmp(argv[2], dst.w, dst.h, dst.n, dst.ptr);
	gettimeofday(&tEnd, NULL);
	timersub(&tEnd, &tStart, &tElapsed);
	printf(ESC_CYAN "Time elapsed: %ld.%06ld\n" ESC_DEFAULT, tElapsed.tv_sec, tElapsed.tv_usec);

	stbi_image_free(src.ptr);
	free(dst.ptr);
	return 0;
}
/* }}} */
