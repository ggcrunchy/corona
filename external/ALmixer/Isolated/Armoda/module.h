#ifndef MODULE_H
#define MODULE_H

// STEVE CHANGE
#include "../Isolated/ALmixer_RWops.h"
// /STEVE CHANGE

#include "tracker.h"
#include "load_mod.h"
#include "load_s3m.h"
#include "load_dsm.h"

int ARM_LoadModule(ARM_Module* mod, ALmixer_RWops* rw_ops, const char *ext);//const char *filename); <- STEVE CHANGE
/* Loads a module in any supported format. */

void ARM_FreeModuleData(ARM_Module* mod);
/* Frees a module's data. */

int ARM_ConvertFinetuneToC4SPD(int finetune);

#endif
