#ifndef DAEMON_JSON_PARSE_H
#define DAEMON_JSON_PARSE_H

#include <stdbool.h>
#include <stddef.h>

/* Find a string value for "key" in JSON. Returns out, or NULL if not found. */
const char *json_find_str(const char *json, const char *key, char *out, size_t out_sz);

/* Find an integer value for "key" in JSON. Returns true if found. */
bool json_find_int(const char *json, const char *key, int *out);

/* Parse a JSON array of ints like [1,2,3]. Returns count of values parsed. */
int json_parse_int_array(const char *start, int *out, int max_out);

#endif
