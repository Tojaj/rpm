#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmstring.h>
#include "build/mfs.h"

rpmRC fileDepsFunc(MfsContext context)
{
    rpmRC rc = RPMRC_FAIL;
    char *buildroot = NULL;
    MfsSpec spec = mfsContextGetSpec(context);
    if (!spec) {
	mfslog_err("Cannot get spec from context\n");
	goto exit;
    }

    buildroot = mfsSpecGetString(spec, MFS_SPEC_ATTR_BUILDROOT);

    for (int x=0; x < mfsSpecPackageCount(spec); x++) {
        int gendep_rc = RPMRC_OK;
	int count;
	MfsFiles files;

	MfsPackage pkg = mfsSpecGetPackage(spec, x);
	if (!pkg) {
	    mfslog_err("Cannot get package from spec\n");
	    goto exit;
	}

	files = mfsPackageGetFiles(pkg);
        count = mfsFilesCount(files);

        if (count > 0) {
            ARGV_t fns		= calloc(count+1, sizeof(*fns));
            rpm_mode_t *fmodes	= calloc(count+1, sizeof(*fmodes));
            rpmFlags *fflags	= calloc(count+1, sizeof(*fflags));

            for (int i=0; i < count; i++) {
                struct stat st;
                MfsFile file = mfsFilesGetEntry(files, i);
                mfsFileGetStat(file, &st);
                fns[i] = rstrdup(mfsFileGetDiskPath(file));
                fmodes[i] = st.st_mode;
                fflags[i] = mfsFileGetFlags(file);
            }
            fns[count] = NULL;

            // Gen deps here
            mfslog_info("Generating dependencies for %s\n", mfsPackageName(pkg));
	    gendep_rc = mfsPackageGenerateDepends(pkg, fns, fmodes, fflags);

            argvFree(fns);
            free(fmodes);
            free(fflags);
        }

	mfsFilesFree(files);
	mfsPackageFree(pkg);

	if (gendep_rc != RPMRC_OK) {
	    rc = RPMRC_FAIL;
	    goto exit;
	}
    }

    rc = RPMRC_OK;

exit:
    free(buildroot);
    mfsSpecFree(spec);
    return rc;
}

rpmRC init_depgenmodule(MfsManager mm)
{
    MfsBuildHook buildhook;

    buildhook = mfsBuildHookNew(fileDepsFunc, MFS_HOOK_POINT_POSTFILEPROCESSING);
    mfsBuildHookSetPrettyName(buildhook, "fileDepsFunc()");
    mfsManagerRegisterBuildHook(mm, buildhook);

    return RPMRC_OK;
}
