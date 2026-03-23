#include "json_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *json_find_str(const char *json, const char *key, char *out, size_t out_sz)
{
	char needle[128];
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	const char *p = strstr(json, needle);
	if (!p) return NULL;
	p += strlen(needle);
	while (*p == ' ' || *p == ':') p++;
	if (*p != '"') return NULL;
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i < out_sz - 1)
		out[i++] = *p++;
	out[i] = '\0';
	return out;
}

bool json_find_int(const char *json, const char *key, int *out)
{
	char needle[128];
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	const char *p = strstr(json, needle);
	if (!p) return false;
	p += strlen(needle);
	while (*p == ' ' || *p == ':') p++;
	if (*p != '-' && (*p < '0' || *p > '9')) return false;
	*out = (int)strtol(p, NULL, 10);
	return true;
}

int json_parse_int_array(const char *start, int *out, int max_out)
{
	const char *p = start;
	while (*p && *p != '[') p++;
	if (!*p) return 0;
	p++;

	int count = 0;
	while (*p && *p != ']' && count < max_out) {
		while (*p == ' ' || *p == ',') p++;
		if (*p == ']') break;
		if (*p == '-' || (*p >= '0' && *p <= '9')) {
			out[count++] = (int)strtol(p, (char **)&p, 10);
		} else {
			while (*p && *p != ',' && *p != ']') p++;
		}
	}
	return count;
}
