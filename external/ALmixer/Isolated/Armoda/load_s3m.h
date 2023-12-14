#ifndef LOAD_S3M_H
#define LOAD_S3M_H

#include "tracker.h"

int ARM_LoadModule_S3M(ARM_Module* mod, ALmixer_RWops* rw_ops);//const char *filename); <- STEVE CHANGE
/* Loads a Screamtracker S3M file.
   Returns 0 on success, -1 on failure. */

#endif
