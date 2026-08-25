/* json - a small read-only DOM parser.
 *
 * The engine's index is assets/manifest.json, written by tools/manifest.py: it
 * is the one file that ties scene names, sets, world records, models and films
 * together.  This is enough of a parser to read it - objects, arrays, strings,
 * numbers, true/false/null - and nothing more.
 *
 * Values live in one flat array owned by the document and refer to their
 * children by index, so the whole document is freed in one call.
 */
#ifndef HL_JSON_H
#define HL_JSON_H

#include <stddef.h>

typedef enum {
    JS_NULL, JS_BOOL, JS_NUM, JS_STR, JS_ARR, JS_OBJ
} JsonType;

typedef struct {
    JsonType type;
    double   num;           /* JS_NUM, and JS_BOOL as 0 or 1              */
    char    *str;           /* JS_STR: NUL-terminated, unescaped          */
    int      first;         /* JS_ARR / JS_OBJ: index of the first child  */
    int      count;         /* number of children (members for an object) */
    int      next;          /* index of the next sibling, -1 at the end   */
    char    *key;           /* the member name, when inside an object     */
} JsonVal;

typedef struct {
    JsonVal *v;
    int      n;
    int      cap;
    char    *text;          /* owns the unescaped strings                 */
    int      root;
} Json;

/* Parses a copy of text.  Returns 0 on failure. */
int  json_parse(Json *j, const char *text, size_t len);
void json_free(Json *j);

/* Navigation.  These take and return value indices; -1 means absent. */
int  json_member(const Json *j, int obj, const char *key);
int  json_at(const Json *j, int arr, int index);
int  json_count(const Json *j, int val);

const char *json_str(const Json *j, int val, const char *fallback);
double      json_num(const Json *j, int val, double fallback);

/* Shorthands for "the number or string at obj.key". */
double      json_numf(const Json *j, int obj, const char *key, double fallback);
const char *json_strf(const Json *j, int obj, const char *key, const char *fallback);

#endif
