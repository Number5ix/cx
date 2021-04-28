#pragma once

typedef union sa_gen {
	void *_is_sarray;
	void *a;
} sa_gen;
typedef union sa_gen* sahandle;

#define SAHANDLE(h) ((sahandle)((h) && &((h)->_is_sarray), (h)))
