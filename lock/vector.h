#ifndef VECTOR_H_
#define VECTOR_H_

#if __SSE__
#include <xmmintrin.h>
#define vec_malloc(s) _mm_malloc(s, 16)
#define vec_free(p)   _mm_free(p)
#elif __ALTIVEC__
#include <altivec.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef vec_malloc
void* vec_malloc(size_t size);
void vec_free(void* ptr);
#endif

typedef union
{
  float v[2];
} vec2;

typedef union
{
#if __SSE__
  __m128 m __attribute__((aligned(16)));
  float v[4];
#else
  float v[3];
#endif
} vec3;

typedef union
{
#if __SSE__
  __m128 m __attribute__((aligned(16)));
#endif
  float v[4];
} quat;

typedef struct
{
  vec3 transl;
  quat rot;
} transf;

typedef union
{
  /* m[column][row] */
  float v[3][3];
} mat3x3;

typedef union
{
#if __SSE__
  __m128 m[4] __attribute__((aligned(16)));
  float v[4][4];
#else
  /* m[column][row] */
  float v[4][4];
#endif
} mat4x4;

void vec2_set2f(vec2* v, float x, float y);
void vec2_set2fv(vec2* v, const float* a);

void vec3_copy(vec3* dest, const vec3* src);
void vec3_set3f(vec3* v, float x, float y, float z);
void vec3_set3fv(vec3* v, const float* a);
void vec3_add(vec3* dest, const vec3* a, const vec3* b);
void vec3_sub(vec3* dest, const vec3* a, const vec3* b);
void vec3_midpoint(vec3* dest, const vec3* a, const vec3* b);
float vec3_dot(const vec3* a, const vec3* b);
void vec3_cross(vec3* dest, const vec3* a, const vec3* b);
void vec3_normalize(vec3* v);
float vec3_mag(const vec3* v);
void vec3_mat4x4_mul_point(vec3* dest, const mat4x4* m, const vec3* v);
void vec3_mat4x4_mul_normal(vec3* dest, const mat4x4* m, const vec3* v);
void vec3_quat_mul(vec3* result, const quat* q, const vec3* v);
void vec3_mat4x4_col(vec3* dest, const mat4x4* m, int col);
void vec3_mat4x4_row(vec3* dest, const mat4x4* m, int row);

void quat_copy(quat* dest, const quat* src);
void quat_identity(quat* q);
void quat_set4f(quat* v, float x, float y, float z, float w);
void quat_set4fv(quat* v, const float* a);
void quat_rotation(quat* dest, float x, float y, float z, float angle);
void quat_normalize(quat* v);
void quat_negate(quat* v);
void quat_conjungate(quat* dest, const quat* src);
void quat_mul(quat* result, const quat* a, const quat* b);

void mat3x3_from_quat(mat3x3* m, const quat* q);
void mat3x3_from_vec_rows(mat3x3* m, const vec3* a, const vec3* b, const vec3* c);
void mat3x3_from_vec_cols(mat3x3* m, const vec3* a, const vec3* b, const vec3* c);
float mat3x3_determinant(const mat3x3* m);

void mat4x4_copy(mat4x4* dest, const mat4x4* src);
void mat4x4_identity(mat4x4* m);
void mat4x4_from_quat(mat4x4* m, const quat* q);
void mat4x4_translate(mat4x4* m, const vec3* v);
void mat4x4_translate3f(mat4x4* m, float x, float y, float z);
void mat4x4_rotate3f(mat4x4* m, float x, float y, float z, float angle);
void mat4x4_mul(mat4x4* result, const mat4x4* a, const mat4x4* b);
int mat4x4_invert(mat4x4* dest, const mat4x4* src);

/* Camera */
void quat_pitch_yaw_roll(quat* result, float pitch, float yaw, float roll);
void mat4x4_projection(mat4x4* result, float znear,
                       float fovx_deg, float fovy_deg);

/* Intersection tests */
int int_aabox_aabox(const vec3* min0, const vec3* max0,
                    const vec3* min1, const vec3* max1);
int int_ray_ray(const vec3* ray0_origin, const vec3* ray0_direction,
                const vec3* ray1_origin, const vec3* ray1_direction,
                float* t0, float* t1);
int int_ray_aabox(const vec3* ray_origin, const vec3* ray_direction,
                  const vec3* box_min, const vec3* box_max,
                  float* enter, float* leave);
int int_ray_triangle(const vec3* ray_origin, const vec3* ray_direction,
                     const vec3* tri_a, const vec3* tri_b, const vec3* tri_c,
                     float* alpha, float* beta, float* distance);
int int_ray_plane(const vec3* ray_origin, const vec3* ray_direction,
                  const vec3* plane_normal, float plane_distance,
                  float* distance);

#ifdef __cplusplus
}
#endif

#if __SSE__

#define vec3_set3f(v, x, y, z) \
  do { (v)->m = _mm_set_ps(0.0f, (z), (y), (x)); } while(0)

#define vec3_add(dest, a, b) \
  do { (dest)->m = _mm_add_ps((a)->m, (b)->m); } while(0)

#define vec3_sub(dest, a, b) \
  do { (dest)->m = _mm_sub_ps((a)->m, (b)->m); } while(0)

#define vec3_midpoint(dest, a, b) \
  do { (dest)->m = _mm_mul_ps(_mm_add_ps((a)->m, (b)->m), _mm_set1_ps(0.5f)); } while(0)

#define quat_identity(q) \
  do { (q)->m = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f); } while(0)

#endif

#endif /* !VECTOR_H_ */
