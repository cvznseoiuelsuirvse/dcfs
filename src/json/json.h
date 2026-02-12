#ifndef JSON_H
#define JSON_H

#include <stdio.h>

#define JSON_OBJECT_SIZE 256

typedef enum json_value_type {
  JSON_UNKNOWN,
  JSON_STRING,
  JSON_ARRAY,
  JSON_OBJECT,
  JSON_NUMBER,
  JSON_WORD,
} json_value_type;

typedef enum json_word {
  JSON_TRUE = 1,
  JSON_FALSE,
  JSON_NULL,
} json_word;

typedef char *json_string;
typedef int json_number;

typedef struct json_array {
  void *data;
  json_value_type type;
  struct json_array *next;
  struct json_array *prev;
} json_array;

typedef struct json_object_bucket {
  json_string key;
  void *value;
  json_value_type type;
  struct json_object_bucket *next;
} json_object_bucket;

typedef struct json_object {
  json_object_bucket *buckets[JSON_OBJECT_SIZE];
} json_object;

#define json_array_for_each(pos, member)                                       \
  for (; pos && pos->data && ((member = pos->data), 1); pos = pos->next)

#define json_object_for_each(m, p)                                             \
  for (size_t __i = 0; __i < JSON_OBJECT_SIZE; __i++)                          \
    for ((p) = (m)->buckets[__i]; (p) != NULL; (p) = (p)->next)

json_value_type json_load(const char *blob, void **object);

json_object *json_object_new();
void json_object_destroy(json_object *object);
void *json_object_get(json_object *object, const char *key);
void *json_object_set(json_object *object, json_string key, void *value,
                      size_t value_size, json_value_type type);

json_array *json_array_new();
void json_array_destroy(json_array *array);
void *json_array_push(json_array *array, void *data, size_t data_size,
                      json_value_type type);
void json_array_remove(json_array **head, int n);
void json_array_remove_ptr(json_array **head, void *obj);
void *json_array_get(json_array *array, int n);
int json_array_size(json_array *array);

#endif
