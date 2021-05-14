#pragma once

#include "objtypes.h"
#include "buffile.h"
#include <cx/fs/fs.h>
#include <cx/fs/path.h>

extern sa_Interface ifaces;
extern hashtable ifidx;
extern sa_Class classes;
extern hashtable clsidx;
extern sa_string includes;
extern sa_string implincludes;
extern sa_string deps;
extern sa_string structs;
extern sa_ComplexArrayType artypes;
extern hashtable knownartypes;
extern string cpassthrough;
extern bool needmixinimpl;

char *lazyPlatformPath(string path);
bool parseFile(string fname, string *realfn, sa_string searchpath, bool included, bool required);
bool processInterfaces();
bool processClasses();
bool writeHeader(string fname);
bool writeImpl(string fname, bool mixinimpl);
bool getAnnotation(sa_string *out, sa_sarray_string annotations, string afind);

void methodImplName(string *out, Class *cls, string mname);
void methodCallName(string *out, Class *cls, string mname);
void mixinMemberName(string *out, Class *cls);
