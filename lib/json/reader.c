#include "json/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static json_object *json_parse_object(const char *blob, size_t *offset);

static json_string json_parse_string(const char *blob, size_t *offset) {
  (*offset)++;
  size_t blob_size = strlen(blob);
  if (*offset >= blob_size)
    return NULL;

  size_t start = *offset;
  for (; *offset < blob_size; (*offset)++) {
    char c = blob[*offset];
    if (c == '"') {
      size_t string_len = *(offset)-start;
      json_string string;

      if (string_len == 0) {
        string = NULL;

      } else {
        string = malloc(string_len + 1);
        string[string_len] = 0;
        memcpy(string, &blob[start], string_len);
      }

      return string;
    }
  }

  return NULL;
}

static json_number json_parse_number(const char *blob, size_t *offset) {
  size_t blob_size = strlen(blob);
  if (*offset >= blob_size)
    return 0;

  size_t start = *offset;
  for (; *offset < blob_size; (*offset)++) {
    char c = blob[*offset];

    if (!(48 <= c && c <= 57)) {
      size_t token_len = *(offset)-start;
      char *token = malloc(token_len + 1);
      memcpy(token, &blob[start], token_len);

      int number = strtol(token, NULL, 10);
      free(token);

      *offset -= 1;
      return number;
    }
  }

  return 0;
}

static json_word json_parse_word(const char *blob, size_t *offset) {
  size_t blob_size = strlen(blob);
  if (*offset >= blob_size)
    return 0;

  json_word word = 0;
  switch (blob[*offset]) {
  case 't':
    word = JSON_TRUE;
    break;
  case 'f':
    word = JSON_FALSE;
    break;
  case 'n':
    word = JSON_NULL;
    break;
  }

  for (; *offset < blob_size; (*offset)++) {
    char c = blob[*offset];
    if (!(c >= 97 && c <= 122)) {
      *offset -= 1;
      break;
    }
  }

  return word;
}

static json_array *json_parse_array(const char *blob, size_t *offset) {
  size_t blob_size = strlen(blob);
  json_array *array = json_array_new();
  if (array == NULL)
    return NULL;

  for (; *offset < blob_size; (*offset)++) {
    char c = blob[*offset];

    if (c == '"') {
      json_string value = json_parse_string(blob, offset);
      json_array_push(array, value, strlen(value) + 1, JSON_STRING);

    } else if (c == '{') {
      (*offset)++;
      json_object *value = json_parse_object(blob, offset);
      if (value == NULL) {
        json_array_destroy(array);
        fprintf(stderr, "failed to parse object at pos %ld\n", *offset);
        return NULL;
      }
      json_array_push(array, value, sizeof(json_object), JSON_OBJECT);

    } else if (c == '[') {
      (*offset)++;
      json_array *value = json_parse_array(blob, offset);
      if (value == NULL) {
        json_array_destroy(array);
        fprintf(stderr, "failed to parse array at pos %ld\n", *offset);
        return NULL;
      }
      json_array_push(array, value, sizeof(json_array), JSON_ARRAY);

    } else if (c == 'n' || c == 'f' || c == 't') {
      json_word value = json_parse_word(blob, offset);
      if (value == 0) {
        json_array_destroy(array);
        fprintf(stderr, "failed to parse word at pos %ld\n", *offset);
        return NULL;
      }
      json_array_push(array, &value, sizeof(json_word), JSON_WORD);

    } else if (48 <= c && c <= 57) {
      json_number value = json_parse_number(blob, offset);
      json_array_push(array, &value, sizeof(json_number), JSON_NUMBER);

    } else if (c == ']') {
      return array;
    }
  }

  json_array_destroy(array);
  return NULL;
}

static json_object *json_parse_object(const char *blob, size_t *offset) {
  size_t blob_size = strlen(blob);
  json_object *object = json_object_new(128);
  if (object == NULL)
    return NULL;
  json_string key = NULL;

  for (; *offset < blob_size; (*offset)++) {
    char c = blob[*offset];

    if (c == '"') {
      if (key == NULL) {
        key = json_parse_string(blob, offset);
        if (key == NULL) {
          json_object_destroy(object);
          fprintf(stderr, "failed to parse key at pos %ld\n", *offset);
          return NULL;
        }

      } else {
        json_string value = json_parse_string(blob, offset);
        if (value != NULL) {
          json_object_set(object, key, value, strlen(value) + 1, JSON_STRING);
        } else {
          json_object_set(object, key, value, 0, JSON_STRING);
        }
        key = NULL;
      }

    } else if (c == '{') {
      if (key == NULL) {
        json_object_destroy(object);
        fprintf(stderr,
                "can't start parsing object value without a key. pos %ld\n",
                *offset);
        return NULL;
      }

      (*offset)++;
      json_object *value = json_parse_object(blob, offset);
      if (value == NULL) {
        json_object_destroy(object);
        free(key);
        fprintf(stderr, "failed to parse object at pos %ld\n", *offset);
        return NULL;
      }
      json_object_set(object, key, value, sizeof(json_object), JSON_OBJECT);
      key = NULL;

    } else if (c == '[') {
      if (key == NULL) {
        json_object_destroy(object);
        fprintf(stderr,
                "can't start parsing array value without a key. pos %ld\n",
                *offset);
        return NULL;
      }

      (*offset)++;
      json_array *value = json_parse_array(blob, offset);
      if (value == NULL) {
        json_object_destroy(object);
        free(key);
        fprintf(stderr, "failed to parse array at pos %ld\n", *offset);
        return NULL;
      }
      json_object_set(object, key, value, sizeof(json_array), JSON_ARRAY);
      key = NULL;

    } else if (c == 'n' || c == 'f' || c == 't') {
      if (key == NULL) {
        json_object_destroy(object);
        fprintf(stderr,
                "can't start parsing word value without a key. pos %ld\n",
                *offset);
        return NULL;
      }
      json_word value = json_parse_word(blob, offset);
      if (value == 0) {
        json_object_destroy(object);
        free(key);
        fprintf(stderr, "failed to parse word at pos %ld\n", *offset);
        return NULL;
      }
      json_object_set(object, key, &value, sizeof(json_word), JSON_WORD);
      key = NULL;

    } else if (48 <= c && c <= 57) {
      if (key == NULL) {
        json_object_destroy(object);
        fprintf(stderr,
                "can't start parsing number value without a key. pos %ld\n",
                *offset);
        return NULL;
      }

      json_number value = json_parse_number(blob, offset);
      json_object_set(object, key, &value, sizeof(json_number), JSON_NUMBER);
      key = NULL;
    } else if (c == '}') {
      return object;
    }
  }
  json_object_destroy(object);
  return NULL;
}

json_value_type json_load(const char *blob, void **object) {
  size_t offset = 1;
  if (blob[0] == '{') {
    *object = json_parse_object(blob, &offset);
    return JSON_OBJECT;

  } else if (blob[0] == '[') {
    *object = json_parse_array(blob, &offset);
    return JSON_ARRAY;
  }

  return JSON_UNKNOWN;
}
