#pragma once

#include "objtypes.h"
#include "buffile.h"
#include <cx/fs/fs.h>
#include <cx/fs/path.h>

extern Interface **ifaces;
extern hashtable ifidx;
extern Class **classes;
extern hashtable clsidx;
extern string *includes;
extern string *implincludes;
extern string *deps;
extern string *structs;
extern bool needmixinimpl;

char *lazyPlatformPath(string path);
bool parseFile(string fname, string *realfn, string *searchpath, bool included, bool required);
bool processInterfaces();
bool processClasses();
bool writeHeader(string fname);
bool writeImpl(string fname, bool mixinimpl);
string* getAnnotation(string*** annotations, string afind);

void methodImplName(string *out, Class *cls, string mname);
void methodCallName(string *out, Class *cls, string mname);
void mixinMemberName(string *out, Class *cls);
