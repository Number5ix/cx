#pragma once

#include <cx/fs/fs.h>
#include <cx/fs/path.h>
#include <cx/serialize/lineparse.h>
#include <cx/serialize/sbfsfile.h>
#include <cx/serialize/sbstring.h>
#include "objtypes.h"

extern sa_Interface ifaces;
extern hashtable ifidx;
extern sa_Class classes;
extern sa_StructDef structs;
extern sa_StructSetDef structsets;
extern hashtable clsidx;
extern hashtable weakrefidx;
extern sa_string includes;
extern sa_string implincludes;
extern sa_string deps;
extern sa_string fwdstruct;
extern sa_string fwdclass;
extern sa_string globaldocs;
extern sa_string globaldocs_end;
extern sa_ComplexArrayType artypes;
extern hashtable knownartypes;
extern string cpassthrough;
extern bool needmixinimpl;
extern bool usedocs;

uint8* lazyPlatformPath(string path);
bool parseFile(strref fname, string* realfn, string srcpath, sa_string searchpath, bool included,
               bool required);
bool processInterfaces();
bool processClasses();
bool processStructs();
bool writeHeader(string fname, string srcpath, string binpath);
bool writeImpl(string fname, string srcpath, string binpath, bool mixinimpl);
bool getAnnotation(sa_string* out, sa_sarray_string annotations, string afind);

void methodImplName(string* out, Class* cls, string mname);
void methodCallName(string* out, Class* cls, string mname);
void mixinMemberName(string* out, Class* cls);
void methodAnnotations(string* out, Method* m);
void paramAnnotations(string* out, Param* p);

void relSrcPath(string* out, strref fname, strref srcpath);
void binPath(string* out, strref fname, strref srcpath, strref binpath);

// Compound type name helpers (defined in impl.c, used by header.c)
bool isCompoundNode(TypeNode* node);
void buildTypeKey(string* out, TypeNode* node);
void buildTypeName(string* out, TypeNode* node);
void buildCompoundDescName(string* out, strref sname, TypeNode* node);
bool isStructName(strref name);
bool isStructSetName(strref name);