#include "json.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Json       *j;
    const char *p;
    const char *end;
    char       *out;        /* cursor into j->text, where strings are built */
    int         ok;
} P;

static int emit(P *p)
{
    Json *j = p->j;
    if (j->n == j->cap) {
        int cap = j->cap ? j->cap * 2 : 256;
        JsonVal *v = realloc(j->v, (size_t)cap * sizeof *v);
        if (!v) { p->ok = 0; return 0; }
        j->v = v;
        j->cap = cap;
    }
    JsonVal *v = &j->v[j->n];
    memset(v, 0, sizeof *v);
    v->first = v->next = -1;
    return j->n++;
}

static void skip(P *p)
{
    while (p->p < p->end && (unsigned char)*p->p <= ' ')
        p->p++;
}

static char *parse_string(P *p)
{
    if (p->p >= p->end || *p->p != '"') { p->ok = 0; return NULL; }
    p->p++;
    char *start = p->out;
    while (p->p < p->end && *p->p != '"') {
        if (*p->p == '\\' && p->p + 1 < p->end) {
            p->p++;
            switch (*p->p) {
            case 'n': *p->out++ = '\n'; break;
            case 't': *p->out++ = '\t'; break;
            case 'r': *p->out++ = '\r'; break;
            case 'b': *p->out++ = '\b'; break;
            case 'f': *p->out++ = '\f'; break;
            case 'u': {
                /* Enough of \u for the BMP; the manifest is mostly ASCII. */
                unsigned cp = 0;
                for (int i = 1; i <= 4 && p->p + i < p->end; i++) {
                    char c = p->p[i];
                    cp <<= 4;
                    if (c >= '0' && c <= '9')      cp |= (unsigned)(c - '0');
                    else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
                }
                p->p += 4;
                if (cp < 0x80) {
                    *p->out++ = (char)cp;
                } else if (cp < 0x800) {
                    *p->out++ = (char)(0xC0 | (cp >> 6));
                    *p->out++ = (char)(0x80 | (cp & 0x3F));
                } else {
                    *p->out++ = (char)(0xE0 | (cp >> 12));
                    *p->out++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                    *p->out++ = (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: *p->out++ = *p->p; break;
            }
            p->p++;
        } else {
            *p->out++ = *p->p++;
        }
    }
    if (p->p >= p->end) { p->ok = 0; return NULL; }
    p->p++;                             /* the closing quote */
    *p->out++ = 0;
    return start;
}

static int parse_value(P *p);

static int parse_container(P *p, char close, int is_obj)
{
    int self = emit(p);
    if (!p->ok) return -1;
    p->j->v[self].type = is_obj ? JS_OBJ : JS_ARR;
    p->p++;                             /* the opening bracket or brace */
    int prev = -1, count = 0, first = -1;
    for (;;) {
        skip(p);
        if (p->p >= p->end) { p->ok = 0; return -1; }
        if (*p->p == close) { p->p++; break; }
        char *key = NULL;
        if (is_obj) {
            key = parse_string(p);
            if (!p->ok) return -1;
            skip(p);
            if (p->p >= p->end || *p->p != ':') { p->ok = 0; return -1; }
            p->p++;
        }
        int child = parse_value(p);
        if (!p->ok) return -1;
        p->j->v[child].key = key;
        if (prev >= 0) p->j->v[prev].next = child; else first = child;
        prev = child;
        count++;
        skip(p);
        if (p->p < p->end && *p->p == ',') p->p++;
    }
    p->j->v[self].first = first;
    p->j->v[self].count = count;
    return self;
}

static int parse_value(P *p)
{
    skip(p);
    if (p->p >= p->end) { p->ok = 0; return -1; }
    char c = *p->p;
    if (c == '{') return parse_container(p, '}', 1);
    if (c == '[') return parse_container(p, ']', 0);
    int self = emit(p);
    if (!p->ok) return -1;
    JsonVal *v = &p->j->v[self];
    if (c == '"') {
        v->type = JS_STR;
        v->str = parse_string(p);
    } else if (c == 't' || c == 'f') {
        v->type = JS_BOOL;
        v->num = c == 't';
        p->p += c == 't' ? 4 : 5;
    } else if (c == 'n') {
        v->type = JS_NULL;
        p->p += 4;
    } else {
        char *e = NULL;
        v->type = JS_NUM;
        v->num = strtod(p->p, &e);
        if (e == p->p) { p->ok = 0; return -1; }
        p->p = e;
    }
    return self;
}

int json_parse(Json *j, const char *text, size_t len)
{
    memset(j, 0, sizeof *j);
    j->root = -1;
    j->text = malloc(len + 1);          /* unescaping never grows a string */
    if (!j->text)
        return 0;
    P p = { j, text, text + len, j->text, 1 };
    j->root = parse_value(&p);
    if (!p.ok) {
        json_free(j);
        return 0;
    }
    return 1;
}

void json_free(Json *j)
{
    free(j->v);
    free(j->text);
    memset(j, 0, sizeof *j);
    j->root = -1;
}

int json_member(const Json *j, int obj, const char *key)
{
    if (obj < 0 || j->v[obj].type != JS_OBJ)
        return -1;
    for (int i = j->v[obj].first; i >= 0; i = j->v[i].next)
        if (j->v[i].key && strcmp(j->v[i].key, key) == 0)
            return i;
    return -1;
}

int json_at(const Json *j, int arr, int index)
{
    if (arr < 0 || (j->v[arr].type != JS_ARR && j->v[arr].type != JS_OBJ))
        return -1;
    int i = j->v[arr].first;
    while (i >= 0 && index-- > 0)
        i = j->v[i].next;
    return i;
}

int json_count(const Json *j, int val)
{
    return val < 0 ? 0 : j->v[val].count;
}

const char *json_str(const Json *j, int val, const char *fallback)
{
    return (val >= 0 && j->v[val].type == JS_STR) ? j->v[val].str : fallback;
}

double json_num(const Json *j, int val, double fallback)
{
    return (val >= 0 && (j->v[val].type == JS_NUM || j->v[val].type == JS_BOOL))
           ? j->v[val].num : fallback;
}

double json_numf(const Json *j, int obj, const char *key, double fallback)
{
    return json_num(j, json_member(j, obj, key), fallback);
}

const char *json_strf(const Json *j, int obj, const char *key, const char *fallback)
{
    return json_str(j, json_member(j, obj, key), fallback);
}
