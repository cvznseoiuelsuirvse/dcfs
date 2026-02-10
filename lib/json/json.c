#include "json.h"

#include <stdlib.h>
#include <string.h>

static unsigned int string_hash(const char *string) {
  unsigned int hash = 5381;
  int c;
  while ((c = *string++))
    hash = ((hash << 5) + hash) + c;

  return hash;
}

json_array *json_array_new() {
  json_array *array = malloc(sizeof(json_array));

  if (!array)
    return NULL;

  memset(array, 0, sizeof(json_array));
  return array;
}

void json_array_destroy(json_array *array) {
  json_array *node = array;
  while (node) {
    json_array *next = node->next;

    if (node->type == JSON_ARRAY)
      json_array_destroy(node->data);
    else if (node->type == JSON_OBJECT)
      json_object_destroy(node->data);
    else
      free(node->data);

    free(node);
    node = next;
  }
}

void *json_array_push(json_array *array, void *data, size_t data_size,
                      json_value_type type) {

  for (; array->next; array = array->next)
    ;

  json_array *node;
  int is_first = 0;
  if (!array->prev && !array->next && !array->data) {
    node = array;
    is_first = 1;
  } else {
    node = malloc(sizeof(json_array));
  }

  node->type = type;
  node->next = NULL;

  if (!is_first) {
    node->prev = array;
    array->next = node;
  }

  if (data_size > 0) {
    node->data = malloc(data_size);
    if (!node->data) {
      perror("malloc");
      return NULL;
    }
  } else {
    node->data = data;
  }

  memcpy(node->data, data, data_size);
  return node->data;
}
void json_array_remove_ptr(json_array **head, void *obj) {
  if (!*head)
    return;

  json_array *array = *head;

  for (; array; array = array->next) {
    if (array->data == obj) {
      if (array->prev) {
        array->prev->next = array->next;
      } else {
        *head = array->next;
      }

      if (array->next)
        array->next->prev = array->prev;

      free(array->data);
      free(array);
      break;
    }
  }
}

void json_array_remove(json_array **head, int n) {
  if (!*head)
    return;

  json_array *array = *head;

  for (int i = 0; array; array = array->next, i++) {
    if (i == n) {
      if (array->prev) {
        array->prev->next = array->next;
      } else {
        *head = array->next;
      }

      if (array->next)
        array->next->prev = array->prev;

      free(array->data);
      free(array);
      break;
    }
  }
}

void *json_array_get(json_array *array, int n) {
  if (!array)
    return NULL;

  for (int i = 0; array && array->data; array = array->next, i++) {
    if (i == n)
      return array->data;
  }

  return NULL;
}

int json_array_size(json_array *array) {
  if (!array)
    return -1;

  size_t i = 0;
  for (; array && array->data; array = array->next, i++)
    ;
  return i;
}

json_object *json_object_new(size_t size) {
  json_object *object = malloc(sizeof(json_object));
  if (object == NULL) {
    perror("malloc");
    fprintf(stderr, "json_object_new: failed to malloc\n");
    return NULL;
  }

  memset(object, 0, sizeof(json_object));
  return object;
}

void json_object_destroy(json_object *object) {
  for (size_t i = 0; i < JSON_OBJECT_SIZE; i++) {
    json_object_bucket *bucket = object->buckets[i];
    while (bucket) {
      json_object_bucket *next = bucket->next;
      if (bucket->type == JSON_OBJECT)
        json_object_destroy(bucket->value);
      else if (bucket->type == JSON_ARRAY)
        json_array_destroy(bucket->value);
      else
        free(bucket->value);

      free(bucket->key);
      free(bucket);
      bucket = next;
    }
  }
  free(object);
}

void *json_object_get(json_object *object, const char *key) {
  size_t n = string_hash(key) % JSON_OBJECT_SIZE;
  json_object_bucket *bucket = object->buckets[n];

  if (bucket != NULL) {
    for (size_t i = 0; bucket; bucket = bucket->next, i++) {
      if (strcmp(bucket->key, key) == 0) {
        return bucket->value;
      }
    }
  }

  return NULL;
}

void *json_object_set(json_object *object, json_string key, void *value,
                      size_t value_size, json_value_type type) {
  size_t n = string_hash(key) % JSON_OBJECT_SIZE;

  json_object_bucket *new_bucket = malloc(sizeof(json_object_bucket));
  if (new_bucket == NULL) {
    fprintf(stderr, "json_object_set: failed to malloc\n");
    return NULL;
  }

  new_bucket->next = NULL;
  new_bucket->key = strdup(key);
  new_bucket->type = type;

  if (value_size > 0) {
    new_bucket->value = malloc(value_size);
    if (new_bucket == NULL) {
      free(new_bucket);
      fprintf(stderr, "json_object_set: failed to malloc\n");
      return NULL;
    }
    memcpy(new_bucket->value, value, value_size);
  } else {
    new_bucket->value = value;
  }

  json_object_bucket *last_bucket = object->buckets[n];
  if (last_bucket) {
    for (; last_bucket->next; last_bucket = last_bucket->next)
      ;
    last_bucket->next = new_bucket;
  } else {
    object->buckets[n] = new_bucket;
  }

  return new_bucket->value;
}
