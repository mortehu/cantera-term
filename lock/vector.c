#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "vector.h"

#ifndef FLT_MAX
#define FLT_MAX 3.40282347e+38f
#endif
#ifndef FLT_EPSILON
#define FLT_EPSILON 1.19209290e-07F
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef vec_malloc
int posix_memalign(void **memptr, size_t alignment, size_t size);

void* vec_malloc(size_t size)
{
#if LINUX
  void* result;
  posix_memalign(&result, 16, size);

  return result;
#else
  return malloc(size); /* XXX: Not enough */
#endif
}

void vec_free(void* ptr)
{
  free(ptr);
}
#endif

void vec2_set2f(vec2* v, float x, float y)
{
  v->v[0] = x;
  v->v[1] = y;
}

void vec2_set2fv(vec2* v, const float* a)
{
  memcpy(v, a, 2 * sizeof(float));
}

void vec3_copy(vec3* dest, const vec3* src)
{
  memcpy(dest, src, 3 * sizeof(float));
}

#undef vec3_set3f
void vec3_set3f(vec3* v, float x, float y, float z)
{
#if __SSE__
  v->m = _mm_set_ps(0.0f, z, y, x);
#else
  v->v[0] = x;
  v->v[1] = y;
  v->v[2] = z;
#endif
}

void vec3_set3fv(vec3* v, const float* a)
{
  memcpy(v, a, 3 * sizeof(float));
}

#undef vec3_add
void vec3_add(vec3* dest, const vec3* a, const vec3* b)
{
#if __SSE__
  dest->m = _mm_add_ps(a->m, b->m);
#else
  dest->v[0] = a->v[0] + b->v[0];
  dest->v[1] = a->v[1] + b->v[1];
  dest->v[2] = a->v[2] + b->v[2];
#endif
}

#undef vec3_sub
void vec3_sub(vec3* dest, const vec3* a, const vec3* b)
{
#if __SSE__
  dest->m = _mm_sub_ps(a->m, b->m);
#else
  dest->v[0] = a->v[0] - b->v[0];
  dest->v[1] = a->v[1] - b->v[1];
  dest->v[2] = a->v[2] - b->v[2];
#endif
}

#undef vec3_midpoint
void vec3_midpoint(vec3* dest, const vec3* a, const vec3* b)
{
#if __SSE__
  dest->m = _mm_mul_ps(_mm_add_ps(a->m, b->m), _mm_set1_ps(0.5f));
#else
  dest->v[0] = (a->v[0] + b->v[0]) * 0.5f;
  dest->v[1] = (a->v[1] + b->v[1]) * 0.5f;
  dest->v[2] = (a->v[2] + b->v[2]) * 0.5f;
#endif
}

float vec3_dot(const vec3* a, const vec3* b)
{
  return a->v[0] * b->v[0] + a->v[1] * b->v[1] + a->v[2] * b->v[2];
}

void vec3_cross(vec3* dest, const vec3* a, const vec3* b)
{
  dest->v[0] = a->v[1] * b->v[2] - b->v[1] * a->v[2];
  dest->v[1] = a->v[2] * b->v[0] - b->v[2] * a->v[0];
  dest->v[2] = a->v[0] * b->v[1] - b->v[0] * a->v[1];
}

void vec3_normalize(vec3* v)
{
  float m = 1.0f / sqrt(v->v[0] * v->v[0] + v->v[1] * v->v[1] + v->v[2] * v->v[2]);

  v->v[0] *= m;
  v->v[1] *= m;
  v->v[2] *= m;
}

float vec3_mag(const vec3* v)
{
  return sqrt(v->v[0] * v->v[0] + v->v[1] * v->v[1] + v->v[2] * v->v[2]);
}

void vec3_mat4x4_mul_point(vec3* dest, const mat4x4* m, const vec3* v)
{
  dest->v[0] = v->v[0] * m->v[0][0] + v->v[1] * m->v[1][0] + v->v[2] * m->v[2][0] + m->v[3][0];
  dest->v[1] = v->v[0] * m->v[0][1] + v->v[1] * m->v[1][1] + v->v[2] * m->v[2][1] + m->v[3][1];
  dest->v[2] = v->v[0] * m->v[0][2] + v->v[1] * m->v[1][2] + v->v[2] * m->v[2][2] + m->v[3][2];
}

void vec3_mat4x4_mul_normal(vec3* dest, const mat4x4* m, const vec3* v)
{
  dest->v[0] = v->v[0] * m->v[0][0] + v->v[1] * m->v[1][0] + v->v[2] * m->v[2][0];
  dest->v[1] = v->v[0] * m->v[0][1] + v->v[1] * m->v[1][1] + v->v[2] * m->v[2][1];
  dest->v[2] = v->v[0] * m->v[0][2] + v->v[1] * m->v[1][2] + v->v[2] * m->v[2][2];
}

void vec3_quat_mul(vec3* result, const quat* q, const vec3* v)
{
  float ax = q->v[3] * v->v[0] + q->v[0] + q->v[1] * v->v[2] - q->v[2] * v->v[1];
  float ay = q->v[3] * v->v[1] + q->v[1] + q->v[2] * v->v[0] - q->v[0] * v->v[2];
  float az = q->v[3] * v->v[2] + q->v[2] + q->v[0] * v->v[1] - q->v[1] * v->v[0];
  float aw = q->v[3] - q->v[0] * v->v[0] - q->v[1] * v->v[1] - q->v[2] * v->v[2];

  result->v[0] = -aw * q->v[0] + ax * q->v[3] - ay * q->v[2] + az * q->v[1];
  result->v[1] = -aw * q->v[1] + ay * q->v[3] - az * q->v[0] + ax * q->v[2];
  result->v[2] = -aw * q->v[2] + az * q->v[3] - ax * q->v[1] + ay * q->v[0];
}

void vec3_mat4x4_col(vec3* dest, const mat4x4* m, int col)
{
  dest->v[0] = m->v[col][0];
  dest->v[1] = m->v[col][1];
  dest->v[2] = m->v[col][2];
}

void vec3_mat4x4_row(vec3* dest, const mat4x4* m, int row)
{
  dest->v[0] = m->v[0][row];
  dest->v[1] = m->v[1][row];
  dest->v[2] = m->v[2][row];
}

void quat_copy(quat* dest, const quat* src)
{
  memcpy(dest, src, 4 * sizeof(float));
}

#undef quat_identity
void quat_identity(quat* q)
{
#if __SSE__
  q->m = _mm_set_ps(0.0f, 0.0f, 0.0f, 1.0f);
#else
  q->v[0] = 0.0f;
  q->v[1] = 0.0f;
  q->v[2] = 0.0f;
  q->v[3] = 1.0f;
#endif
}

void quat_set4f(quat* v, float x, float y, float z, float w)
{
  v->v[0] = x;
  v->v[1] = y;
  v->v[2] = z;
  v->v[3] = w;
}

void quat_set4fv(quat* v, const float* a)
{
  memcpy(v, a, 4 * sizeof(float));
}

void quat_rotation(quat* dest, float x, float y, float z, float angle)
{
  float s = sin(angle * 0.5f);
  float c = cos(angle * 0.5f);

  dest->v[0] = s * x;
  dest->v[1] = s * y;
  dest->v[2] = s * z;
  dest->v[3] = c;
}

void quat_normalize(quat* v)
{
  float m = 1.0f / sqrt(v->v[0] * v->v[0] + v->v[1] * v->v[1] + v->v[2] * v->v[2] + v->v[3] * v->v[3]);

  v->v[0] *= m;
  v->v[1] *= m;
  v->v[2] *= m;
  v->v[3] *= m;
}

void quat_negate(quat* v)
{
  v->v[0] = -v->v[0];
  v->v[1] = -v->v[1];
  v->v[2] = -v->v[2];
  v->v[3] = -v->v[3];
}

void quat_conjungate(quat* dest, const quat* src)
{
  dest->v[0] = -src->v[0];
  dest->v[1] = -src->v[1];
  dest->v[2] = -src->v[2];
  dest->v[3] = src->v[3];
}

void quat_mul(quat* result, const quat* a, const quat* b)
{
  assert(result != a);
  assert(result != b);

  result->v[0] = a->v[3] * b->v[0] + a->v[0] * b->v[3] + a->v[1] * b->v[2] - a->v[2] * b->v[1];
  result->v[1] = a->v[3] * b->v[1] + a->v[1] * b->v[3] + a->v[2] * b->v[0] - a->v[0] * b->v[2];
  result->v[2] = a->v[3] * b->v[2] + a->v[2] * b->v[3] + a->v[0] * b->v[1] - a->v[1] * b->v[0];
  result->v[3] = a->v[3] * b->v[3] - a->v[0] * b->v[0] - a->v[1] * b->v[1] - a->v[2] * b->v[2];
}

void mat3x3_from_quat(mat3x3* m, const quat* q)
{
  float x = q->v[0];
  float y = q->v[1];
  float z = q->v[2];
  float w = q->v[3];

  m->v[0][0] = 1.0f - 2.0f * y * y - 2.0f * z * z;
  m->v[1][0] =        2.0f * x * y - 2.0f * z * w;
  m->v[2][0] =        2.0f * x * z + 2.0f * y * w;

  m->v[0][1] =        2.0f * x * y + 2.0f * z * w;
  m->v[1][1] = 1.0f - 2.0f * x * x - 2.0f * z * z;
  m->v[2][1] =        2.0f * y * z - 2.0f * x * w;

  m->v[0][2] =        2.0f * x * z - 2.0f * y * w;
  m->v[1][2] =        2.0f * y * z + 2.0f * x * w;
  m->v[2][2] = 1.0f - 2.0f * x * x - 2.0f * y * y;
}

void mat3x3_from_vec_rows(mat3x3* m, const vec3* a, const vec3* b, const vec3* c)
{
  m->v[0][0] = a->v[0]; m->v[1][0] = a->v[1]; m->v[2][0] = a->v[2];
  m->v[0][1] = b->v[0]; m->v[1][1] = b->v[1]; m->v[2][1] = b->v[2];
  m->v[0][2] = c->v[0]; m->v[1][2] = c->v[1]; m->v[2][2] = c->v[2];
}

void mat3x3_from_vec_cols(mat3x3* m, const vec3* a, const vec3* b, const vec3* c)
{
  m->v[0][0] = a->v[0]; m->v[0][1] = a->v[1]; m->v[0][2] = a->v[2];
  m->v[1][0] = b->v[0]; m->v[1][1] = b->v[1]; m->v[1][2] = b->v[2];
  m->v[2][0] = c->v[0]; m->v[2][1] = c->v[1]; m->v[2][2] = c->v[2];
}

float mat3x3_determinant(const mat3x3* m)
{
  return m->v[0][0] * m->v[1][1] * m->v[2][2]
       + m->v[1][0] * m->v[2][1] * m->v[0][2]
       + m->v[2][0] * m->v[0][1] * m->v[1][2]
       - m->v[2][0] * m->v[1][1] * m->v[0][2]
       - m->v[0][0] * m->v[2][1] * m->v[1][2]
       - m->v[1][0] * m->v[0][1] * m->v[2][2];
}

void mat4x4_copy(mat4x4* dest, const mat4x4* src)
{
  memcpy(dest, src, 16 * sizeof(float));
}

void mat4x4_identity(mat4x4* m)
{
  memset(m, 0, 16 * sizeof(float));
  m->v[0][0] = 1.0f;
  m->v[1][1] = 1.0f;
  m->v[2][2] = 1.0f;
  m->v[3][3] = 1.0f;
}

void mat4x4_from_quat(mat4x4* m, const quat* q)
{
  float x = q->v[0];
  float y = q->v[1];
  float z = q->v[2];
  float w = q->v[3];

  m->v[0][0] = 1.0f - 2.0f * y * y - 2.0f * z * z;
  m->v[1][0] =        2.0f * x * y - 2.0f * z * w;
  m->v[2][0] =        2.0f * x * z + 2.0f * y * w;

  m->v[0][1] =        2.0f * x * y + 2.0f * z * w;
  m->v[1][1] = 1.0f - 2.0f * x * x - 2.0f * z * z;
  m->v[2][1] =        2.0f * y * z - 2.0f * x * w;

  m->v[0][2] =        2.0f * x * z - 2.0f * y * w;
  m->v[1][2] =        2.0f * y * z + 2.0f * x * w;
  m->v[2][2] = 1.0f - 2.0f * x * x - 2.0f * y * y;

  m->v[3][0] = 0.0f;
  m->v[3][1] = 0.0f;
  m->v[3][2] = 0.0f;

  m->v[0][3] = 0.0f;
  m->v[1][3] = 0.0f;
  m->v[2][3] = 0.0f;

  m->v[3][3] = 1.0f;
}

void mat4x4_translate(mat4x4* m, const vec3* v)
{
  int i;

  for(i = 0; i < 4; ++i)
  {
    m->v[i][0] += m->v[3][i] * v->v[0];
    m->v[i][1] += m->v[3][i] * v->v[1];
    m->v[i][2] += m->v[3][i] * v->v[2];
  }
}

void mat4x4_translate3f(mat4x4* m, float x, float y, float z)
{
  int i;

  for(i = 0; i < 4; ++i)
  {
    m->v[i][0] += m->v[3][i] * x;
    m->v[i][1] += m->v[3][i] * y;
    m->v[i][2] += m->v[3][i] * z;
  }
}

void mat4x4_rotate3f(mat4x4* m, float x, float y, float z, float angle)
{
  quat q;
  mat4x4 rot, old_m;

  quat_rotation(&q, x, y, z, angle);
  mat4x4_from_quat(&rot, &q);
  mat4x4_copy(&old_m, m);
  mat4x4_mul(m, &old_m, &rot);
}

void mat4x4_mul(mat4x4* result, const mat4x4* a, const mat4x4* b)
{
  int i, j;

  assert(result != a);
  assert(result != b);

  for(i = 0; i < 4; ++i)
  {
    for(j = 0; j < 4; ++j)
    {
      result->v[j][i] = a->v[0][i] * b->v[j][0]
                      + a->v[1][i] * b->v[j][1]
                      + a->v[2][i] * b->v[j][2]
                      + a->v[3][i] * b->v[j][3];
    }
  }
}

void mat4x4_swap_rows(mat4x4* m, int a, int b)
{
  int i;

  for(i = 0; i < 4; ++i)
  {
    float tmp = m->v[i][a];
    m->v[i][a] = m->v[i][b];
    m->v[i][b] = tmp;
  }
}

void mat4x4_scale_row(mat4x4* m, int row, float scale)
{
  int i;

  for(i = 0; i < 4; ++i)
    m->v[i][row] *= scale;
}

void mat4x4_row_add_row_multiple(mat4x4* m, int destrow, int srcrow, float scale)
{
  int i;

  for(i = 0; i < 4; ++i)
    m->v[i][destrow] += m->v[i][srcrow] * scale;
}

/**
 * Create inverted copy of matrix.
 *
 * Matrix inversion is performed by row-reducing the matrix extended
 * with the identity matrix.  Since the matrix is square, we use a `tmp' matrix
 * for the left part, and `m' for the right part.  By always performing the same
 * operations on both matrices, the effect will be the same as if we had one
 * large matrix.
 *
 * This method is called Gauss-Jordan Elimination.
 */
int mat4x4_invert(mat4x4* dest, const mat4x4* src)
{
  int i, j;
  mat4x4 tmp;

  mat4x4_copy(&tmp, src);
  mat4x4_identity(dest);

  for(i = 0; i < 4; ++i)
  {
    /* Select row with largest element as pivot row */
    float max = 0;
    int max_idx = i;

    for(j = i; j < 4; ++j)
    {
      if(fabs(tmp.v[i][j]) > max)
      {
        max = fabs(tmp.v[i][j]);
        max_idx = j;
      }
    }

    /* Swap pivot row into the `i' position */
    if(max_idx != i)
    {
      mat4x4_swap_rows(&tmp, i, max_idx);
      mat4x4_swap_rows(dest, i, max_idx);
    }

    /* Everything below and including the diagonal is zero, no chance of
     * inverting
     */
    if(tmp.v[i][i] > -1.0e-6 && tmp.v[i][i] < 1.0e-6)
      return 0;

    /* Scale pivot row to make the `i' element equal 1 */
    float scale = 1.0f / tmp.v[i][i];

    mat4x4_scale_row(&tmp, i, scale);
    mat4x4_scale_row(dest, i, scale);

    /* Zero out the `i' column, except in the `i' position */
    for(j = 0; j < 4; ++j)
    {
      if(i == j)
        continue;

      scale = -tmp.v[i][j];

      mat4x4_row_add_row_multiple(&tmp, j, i, scale);
      mat4x4_row_add_row_multiple(dest, j, i, scale);
    }
  }

  return 1;
}

void quat_pitch_yaw_roll(quat* result, float pitch, float yaw, float roll)
{
  quat a, b;

  quat_rotation(&a, 1.0f, 0.0f, 0.0f, pitch);
  quat_rotation(result, 0.0f, 1.0f, 0.0f, yaw);
  quat_mul(&b, &a, result);
  quat_rotation(&a, 0.0f, 0.0f, 1.0f, roll);
  quat_mul(result, &a, &b);
}

void mat4x4_projection(mat4x4* result, float znear, float fovx, float fovy)
{
  float max_x = znear * tan(fovx * M_PI / 360.0f);
  float max_y = znear * tan(fovy * M_PI / 360.0f);

  float min_x = -max_x;
  float min_y = -max_y;

  result->v[0][0] = 2.0f * znear / (max_x - min_x);
  result->v[1][0] = 0.0f;
  result->v[2][0] = (max_x + min_x) / (max_x - min_x);
  result->v[3][0] = 0.0f;
  result->v[0][1] = 0.0f;
  result->v[1][1] = 2.0f * znear / (max_y - min_y);
  result->v[2][1] = (max_y + min_y) / (max_y - min_y);
  result->v[3][1] = 0.0f;
  result->v[0][2] = 0.0f;
  result->v[1][2] = 0.0f;
  result->v[2][2] = -1.0f;
  result->v[3][2] = -2.0f * znear;
  result->v[0][3] = 0.0f;
  result->v[1][3] = 0.0f;
  result->v[2][3] = -1.0f;
  result->v[3][3] = 0.0f;
}

int int_aabox_aabox(const vec3* min0, const vec3* max0, const vec3* min1, const vec3* max1)
{
#if __SSE__
  /* 0 branches */
  return (_mm_movemask_ps(_mm_and_ps(_mm_cmplt_ps(min0->m, max1->m),
                                     _mm_cmplt_ps(min1->m, max0->m))) + 1) >> 3;
#else
  /* 6 branches */
  return min0->v[0] < max1->v[0] && min0->v[1] < max1->v[1] && min0->v[2] < max1->v[2]
      && min1->v[0] < max0->v[0] && min1->v[1] < max0->v[1] && min1->v[2] < max0->v[2];
#endif
}

int int_ray_ray(const vec3* ray0_origin, const vec3* ray0_direction,
                const vec3* ray1_origin, const vec3* ray1_direction,
                float* t0, float* t1)
{
  vec3 diff, cross;

  vec3_cross(&cross, ray0_direction, ray1_direction);

  float denom = vec3_mag(&cross);

  if(denom < FLT_EPSILON)
    return 0;

  denom *= denom;

  vec3_sub(&diff, ray1_origin, ray0_origin);

  mat3x3 mat;

  mat3x3_from_vec_rows(&mat, &diff, ray1_direction, &cross);

  *t0 = mat3x3_determinant(&mat) / denom;

  mat.v[0][1] = ray0_direction->v[0];
  mat.v[1][1] = ray0_direction->v[1];
  mat.v[2][1] = ray0_direction->v[2];

  *t1 = mat3x3_determinant(&mat) / denom;

  return 1;
}

int int_ray_aabox(const vec3* ray_origin, const vec3* ray_direction,
                  const vec3* box_min, const vec3* box_max,
                  float* near, float* far)
{
  int i;
  const float* ray_origins = &ray_origin->v[0];
  const float* ray_directions = &ray_direction->v[0];
  const float* box_mins = &box_min->v[0];
  const float* box_maxs = &box_max->v[0];

  *far = FLT_MAX;
  *near = -FLT_MAX;

  for(i = 0; i < 3; ++i)
  {
    if(ray_directions[i] > -FLT_EPSILON && ray_directions[i] < FLT_EPSILON)
    {
      if(ray_origins[i] < box_mins[i] || ray_origins[i] > box_maxs[i])
        return 0;
    }
    else
    {
      float t1 = (box_mins[i] - ray_origins[i]) / ray_directions[i];
      float t2 = (box_maxs[i] - ray_origins[i]) / ray_directions[i];

      if(t2 > t1)
      {
        float tmp = t1;
        t1 = t2;
        t2 = tmp;
      }

      if(t2 > *near)
        *near = t2;

      if(t1 < *far)
        *far = t1;

      if(*far < 0.0f)
        return 0;

      if(*near > *far)
        return 0;
    }
  }

  if(*near < 0.0f)
    *near = 0.0f;

  return 1;
}

int int_ray_triangle(const vec3* ray_origin, const vec3* ray_direction,
                     const vec3* tri_a, const vec3* tri_b, const vec3* tri_c,
                     float* alpha, float* beta, float* distance)
{
  int i1, i2;
  float dot;
  float px, py, ux, uy, uz, vx, vy, vz;

  vec3 edge_ab, edge_ac, normal;

  vec3_sub(&edge_ab, tri_b, tri_a);
  vec3_sub(&edge_ac, tri_c, tri_a);
  vec3_cross(&normal, &edge_ab, &edge_ac);
  vec3_normalize(&normal);

  dot = vec3_dot(&normal, ray_direction);

  if(dot > -FLT_EPSILON && dot < FLT_EPSILON)
    return 0;

  *distance = (vec3_dot(&normal, tri_a) - vec3_dot(&normal, ray_origin)) / dot;

  if(*distance < 0.0f)
    return 0;

#if 0
  if(fabs(normal.v[0]) > fabs(normal.v[1])
  && fabs(normal.v[0]) > fabs(normal.v[2]))
    i1 = 1, i2 = 2;
  else if(fabs(normal.v[1]) > fabs(normal.v[2]))
    i1 = 0, i2 = 2;
  else
    i1 = 0, i2 = 1;
#else
  int a = fabs(normal.v[0]) > fabs(normal.v[1]);
  int b = fabs(normal.v[0]) > fabs(normal.v[2]);
  int c = fabs(normal.v[1]) > fabs(normal.v[2]);

  i1 = a & b;
  i2 = (b | c) + 1;
#endif

  px = ray_origin->v[i1] + ray_direction->v[i1] * (*distance);
  py = ray_origin->v[i2] + ray_direction->v[i2] * (*distance);

  ux = px - tri_a->v[i1];
  uy = tri_b->v[i1] - tri_a->v[i1];
  uz = tri_c->v[i1] - tri_a->v[i1];

  vx = py - tri_a->v[i2];
  vy = tri_b->v[i2] - tri_a->v[i2];
  vz = tri_c->v[i2] - tri_a->v[i2];

  if(fabs(uy) < FLT_EPSILON)
  {
    *beta = ux / uz;

    if(*beta < 0.0f || *beta > 1.0f)
      return 0;

    *alpha = (vx - *beta * vz) / vy;
  }
  else
  {
    *beta = (vx * uy - ux * vy) / (vz * uy - uz * vy);

    if(*beta < 0.0f || *beta > 1.0f)
      return 0;

    *alpha = (ux - *beta * uz) / uy;
  }

  if(*alpha < 0.0f || (*alpha + *beta) > 1.0f)
    return 0;

  return 1;
}

int int_ray_plane(const vec3* ray_origin, const vec3* ray_direction,
                  const vec3* plane_normal, float plane_distance,
                  float* distance)
{
  float dot = vec3_dot(plane_normal, ray_direction);

  if(dot > -FLT_EPSILON && dot < FLT_EPSILON)
    return 0;

  *distance = (plane_distance - vec3_dot(plane_normal, ray_origin)) / dot;

  return 1;
}
