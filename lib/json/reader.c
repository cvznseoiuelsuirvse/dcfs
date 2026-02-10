#include "json.h"

#include <stdlib.h>
#include <string.h>

static json_object *json_parse_object(const char *blob, size_t *offset);

static int json_parse_string(const char *blob, size_t *offset, char *buffer,
                             size_t buffer_size) {
  (*offset)++;
  size_t blob_size = strlen(blob);
  if (*offset >= blob_size)
    return -1;

  memset(buffer, 0, buffer_size);

  for (int i = 0; *offset < blob_size; (*offset)++, i++) {
    char c = blob[*offset];
    if (c == '"') {
      buffer[i + 1] = 0;
      return i;
    } else
      buffer[i] = c;
  }

  return -1;
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
      char token[token_len + 1];
      token[token_len] = 0;
      memcpy(token, &blob[start], token_len);

      int number = strtol(token, NULL, 10);

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
  if (!array)
    return NULL;

  for (; *offset < blob_size; (*offset)++) {
    char c = blob[*offset];

    if (c == '"') {
      char buffer[1024];
      int string_size = json_parse_string(blob, offset, buffer, sizeof(buffer));
      if (string_size == -1) {
        json_array_destroy(array);
        fprintf(stderr, "failed to parse string starting at %ld\n", *offset);
        return NULL;
      }
      json_array_push(array, buffer, string_size + 1, JSON_STRING);

    } else if (c == '{') {
      (*offset)++;
      json_object *value = json_parse_object(blob, offset);
      if (!value) {
        json_array_destroy(array);
        fprintf(stderr, "failed to parse object starting at %ld\n", *offset);
        return NULL;
      }
      json_array_push(array, value, 0, JSON_OBJECT);

    } else if (c == '[') {
      (*offset)++;
      json_array *value = json_parse_array(blob, offset);
      if (!value) {
        json_array_destroy(array);
        fprintf(stderr, "failed to parse array starting at %ld\n", *offset);
        return NULL;
      }
      json_array_push(array, value, 0, JSON_ARRAY);

    } else if (c == 'n' || c == 'f' || c == 't') {
      json_word value = json_parse_word(blob, offset);
      if (!value) {
        json_array_destroy(array);
        fprintf(stderr, "failed to parse word starting at %ld\n", *offset);
        return NULL;
      }
      json_array_push(array, &value, sizeof(json_word), JSON_WORD);

    } else if ((48 <= c && c <= 57) || c == '-') {
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
  json_object *object = json_object_new();
  if (!object)
    return NULL;

  char key[1024];
  memset(key, 0, sizeof(key));

  for (; *offset < blob_size; (*offset)++) {
    char c = blob[*offset];

    if (c == '"') {
      if (!*key) {
        int key_size = json_parse_string(blob, offset, key, sizeof(key));
        if (key_size == -1) {
          json_object_destroy(object);
          fprintf(stderr, "failed to parse key starting at %ld\n", *offset);
          return NULL;
        }

      } else {
        char buffer[1024];
        int string_size =
            json_parse_string(blob, offset, buffer, sizeof(buffer));

        if (string_size == -1) {
          json_object_destroy(object);
          fprintf(stderr, "failed to parse string starting at %ld\n", *offset);
          return NULL;
        }

        if (string_size)
          json_object_set(object, key, buffer, string_size + 1, JSON_STRING);
        else
          json_object_set(object, key, NULL, 0, JSON_STRING);

        key[0] = 0;
      }
    } else if (c == '{') {
      if (!*key) {
        json_object_destroy(object);
        fprintf(stderr,
                "can't start parsing object value without a key. pos %ld\n",
                *offset);
        return NULL;
      }

      (*offset)++;
      json_object *value = json_parse_object(blob, offset);
      if (!value) {
        json_object_destroy(object);
        fprintf(stderr, "failed to parse object starting at %ld\n", *offset);
        return NULL;
      }

      json_object_set(object, key, value, 0, JSON_OBJECT);
      key[0] = 0;

    } else if (c == '[') {
      if (!*key) {
        json_object_destroy(object);
        fprintf(stderr,
                "can't start parsing array value without a key. pos %ld\n",
                *offset);
        return NULL;
      }

      (*offset)++;
      json_array *value = json_parse_array(blob, offset);
      if (!value) {
        json_object_destroy(object);
        fprintf(stderr, "failed to parse array starting at %ld\n", *offset);
        return NULL;
      }

      json_object_set(object, key, value, 0, JSON_ARRAY);
      key[0] = 0;

    } else if (c == 'n' || c == 'f' || c == 't') {
      if (!*key) {
        json_object_destroy(object);
        fprintf(stderr,
                "can't start parsing word value without a key. pos %ld\n",
                *offset);
        return NULL;
      }

      json_word value = json_parse_word(blob, offset);
      if (!value) {
        json_object_destroy(object);
        fprintf(stderr, "failed to parse word starting at %ld\n", *offset);
        return NULL;
      }

      json_object_set(object, key, &value, sizeof(json_word), JSON_WORD);
      key[0] = 0;

    } else if (48 <= c && c <= 57) {
      if (!*key) {
        json_object_destroy(object);
        fprintf(stderr,
                "can't start parsing number value without a key. pos %ld\n",
                *offset);
        return NULL;
      }

      json_number value = json_parse_number(blob, offset);
      json_object_set(object, key, &value, sizeof(json_number), JSON_NUMBER);
      key[0] = 0;

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

  return -1;
}
