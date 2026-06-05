#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "cJSON.h"
#include "cJSON_Utils.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define OFFSET(structure, member) ((size_t)(&(((structure *)0)->member)))

#ifndef offsetof
#define offsetof(type, member) OFFSET(type, member)
#endif

/*
 * \code
 *  struct my_struct = {
 *      int  m1;
 *      char m2;
 *  };
 *  struct my_struct  my_st;
 *  char             *p_m2 = &my_st.m2;
 *  struct my_struct *p_st = CONTAINER_OF(p_m2, struct my_struct, m2);
 * \endcode
 */
#define CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr)-offsetof(type, member)))

void *tf_malloc(size_t sz);
void tf_free(void *ptr);

bool tf_cJSON_IsGeneralBool(const cJSON * const item);
bool tf_cJSON_IsGeneralTrue(const cJSON * const item);

char *tf_strdup(const char *s);

#ifdef __cplusplus
}
#endif
