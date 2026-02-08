#include "request.h"
#include "util.h"
#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static size_t write_cb(void *content, size_t size, size_t nmemb, void *data) {
  size_t realsize = size * nmemb;
  struct response *mem = data;
  char *ptr = realloc(mem->raw, mem->size + realsize + 1);
  if (!ptr) {
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->raw = ptr;
  memcpy(&(mem->raw[mem->size]), content, realsize);
  mem->size += realsize;
  mem->raw[mem->size] = 0;

  return realsize;
}

static struct curl_slist *append_auth_header(struct curl_slist *headers) {
  size_t auth_string_size =
      strlen("Authorization: ") + strlen(get_auth_token()) + 1;
  char auth_string[auth_string_size];

  snprintf(auth_string, auth_string_size, "Authorization: %s",
           get_auth_token());

  return curl_slist_append(headers, auth_string);
}

int request_get(const char *url, struct response *resp, char user_auth) {
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_ALL);
  if (res)
    return res;

  CURL *curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    if (user_auth != 0) {
      struct curl_slist *headers = append_auth_header(NULL);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      fprintf(stderr, "failed to GET: %s\n", curl_easy_strerror(res));

    } else {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_code);
    }

    curl_easy_cleanup(curl);
  }

  curl_global_cleanup();
  return res;
}

int request_post_files(const char *url, const struct file *files,
                       size_t files_n, struct response *resp) {
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_ALL);
  if (res)
    return res;

  CURL *curl = curl_easy_init();
  if (curl) {
    curl_mime *form = curl_mime_init(curl);

    curl_mimepart *part;
    struct curl_slist *headers = NULL;
    headers = append_auth_header(headers);

    for (size_t i = 0; i < files_n; i++) {
      struct file file = files[i];
      part = curl_mime_addpart(form);
      curl_mime_data(part, file.buffer, file.buffer_size);
      curl_mime_filename(part, file.filename);
      char name[64];
      snprintf(name, sizeof(name), "files[%ld]", i);
      curl_mime_name(part, name);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      fprintf(stderr, "failed to POST FILE: %s\n", curl_easy_strerror(res));

    } else {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_code);
    }
    curl_easy_cleanup(curl);
  }

  curl_global_cleanup();
  return res;
}

int request_post(const char *url, char *data, struct response *resp,
                 char user_auth) {
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_ALL);
  if (res)
    return res;

  CURL *curl = curl_easy_init();
  if (curl) {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    if (user_auth != 0) {
      headers = append_auth_header(headers);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      fprintf(stderr, "failed to POST: %s\n", curl_easy_strerror(res));

    } else {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_code);
    }
    curl_easy_cleanup(curl);
  }

  curl_global_cleanup();
  return res;
}

int request_patch(const char *url, char *data, struct response *resp,
                  char user_auth) {
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_ALL);
  if (res)
    return res;

  CURL *curl = curl_easy_init();
  if (curl) {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    if (user_auth != 0) {
      headers = append_auth_header(headers);
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      fprintf(stderr, "failed to PATCH: %s\n", curl_easy_strerror(res));

    } else {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_code);
    }
    curl_easy_cleanup(curl);
  }

  curl_global_cleanup();
  return res;
}

int request_delete(const char *url, struct response *resp, char user_auth) {
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_ALL);
  if (res)
    return res;

  CURL *curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    if (user_auth != 0) {
      struct curl_slist *headers = append_auth_header(NULL);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      fprintf(stderr, "failed to DELETE: %s\n", curl_easy_strerror(res));

    } else {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp->http_code);
    }
    curl_easy_cleanup(curl);
  }

  curl_global_cleanup();
  return res;
}
