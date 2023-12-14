#include <stdlib.h>
#include "module.h"

#include "../Isolated/SoundDecoder_Internal.h" // <- STEVE CHANGE

void ARM_FreeSample(ARM_Sample* sam)
{
    if (sam != NULL) {
	free(sam->name);
	free(sam->data);
    }
}


void ARM_FreeModuleData(ARM_Module* mod)
{
    int i;

    free(mod->title);
    if (mod->samples != NULL) {
	for (i = 0; i < mod->num_samples; i++) {
	    ARM_FreeSample(&mod->samples[i]);
	}
    }
    free(mod->samples);
    free(mod->order);
    if (mod->patterns != NULL) {
	for (i = 0; i < mod->num_patterns; i++) {
	    ARM_FreePatternData(&mod->patterns[i]);
	}
    }
    free(mod->patterns);
    free(mod->default_pan);
    memset(mod, 0, sizeof (ARM_Module));
}


typedef int (*LoaderProc)(ARM_Module* mod, ALmixer_RWops* rw_ops);//const char* filename); <- STEVE CHANGE

int ARM_LoadModule(ARM_Module* mod, ALmixer_RWops* rw_ops, const char *ext)//const char* filename) <- STEVE CHANGE
{
//    char *ext; <- STEVE CHANGE
    int i;
    const char *extensions[] =
	{ ".mod",
	  ".s3m",
	  ".dsm",
	  NULL };
    LoaderProc loaders[] =
	{ ARM_LoadModule_MOD,
	  ARM_LoadModule_S3M,
	  ARM_LoadModule_DSM,
	  NULL };

    /* Find the file extension. */
    /* STEVE CHANGE
    ext = strrchr(filename, '.');
    if (ext == NULL)
	return -1;
    /STEVE CHANGE */
    
    /* Look up the extension. */
    for (i = 0; extensions[i] != NULL; i++) {
	if (!__Sound_strcasecmp(ext, extensions[i]+1)) // <- STEVE CHANGE (skip dot)
	    return loaders[i](mod, rw_ops);//filename); <- STEVE CHANGE
    }
    
    return -1;
}
