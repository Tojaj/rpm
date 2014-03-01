#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmstring.h>
#include "build/mfs.h"

#define STATICSTRLEN(s) (sizeof(s)/sizeof(s[0]))
#define SKIPSPACE(s)	{while(*(s) && risspace(*(s))) (s)++; }
#define	SKIPWHITE(s)	{while(*(s) && (risspace(*s) || *(s) == ',')) (s)++;}
#define	SKIPNONWHITE(s) {while(*(s) &&!(risspace(*s) || *(s) == ',')) (s)++;}

typedef struct LangPkg_s {
    char *lang;
    MfsPackage pkg;
    struct LangPkg_s *next;
} * LangPkg;

typedef struct SharedData_s {
    LangPkg langpkgs;
} * SharedData;

// Note: langs must be sorted
static void addSaneUniqLangs(ARGV_t *langs, ARGV_t linelangs)
{
    int count = argvCount(linelangs);
    for (int x=0; x < count; x++) {
	size_t len = strlen(linelangs[x]);

        // Ignore weird locales
	if (len < 1)
	    continue;
        if (len == 1 && *linelangs[x] != 'C')
	    continue;
	if (len >= 32)
	    continue;

	if (!argvSearch(*langs, linelangs[x], NULL)) {
	    // Insert only non existing langs
	    argvAdd(langs, linelangs[x]);
	    argvSort(*langs, NULL);
	}
    }
}

static void argvAddLang(ARGV_t *argvp, const char *val, size_t len)
{
    char buf[len+1];
    strncpy(buf, val, len);
    buf[len] = '\0';
    argvAdd(argvp, buf);
}

static rpmRC parseLineForLangs(const char *cline, ARGV_t *langs)
{
    char *line = NULL, *p, *pe;
    rpmRC rc = RPMRC_FAIL;

    if (!cline) return rc;
    line = rstrdup(cline);

    while ((p = strstr(line, "%lang")) != NULL) {
	for (pe = p; (pe-p) < (signed) STATICSTRLEN("%lang")-1; pe++)
	    *pe = ' ';
	SKIPSPACE(pe);

	if (*pe != '(')	// Missing '('
	    goto exit;

	/* Bracket %lang args */
	*pe = ' ';
	for (pe = p; *pe && *pe != ')'; pe++);

	if (*pe == '\0') // Missing ')'
	    goto exit;
	*pe = '\0';

	/* Parse multiple arguments from %lang */
	for (; *p != '\0'; p = pe) {
	    SKIPWHITE(p);
	    pe = p;
	    SKIPNONWHITE(pe);
	    argvAddLang(langs, p, (pe-p));
	    if (*pe == ',') pe++;	/* skip , if present */
	}
    }

    rc = RPMRC_OK;

exit:
    free(line);

    return rc;
}

static rpmRC parseFileForLangs(const char *path, MfsSpec spec, ARGV_t *langs)
{
    rpmRC rc = RPMRC_FAIL;
    char *fn;
    FILE *fd = NULL;
    char buf[BUFSIZ];
    rpmMacroContext mc;

    if (!path || !*path) {
	return RPMRC_OK;
    } else if (*path == '/') {
	fn = rpmGetPath(path, NULL);
    } else {
	char *buildsubdir = mfsSpecGetString(spec, MFS_SPEC_ATTR_BUILDSUBDIR);
	fn = rpmGetPath("%{_builddir}/",
			(buildsubdir ? buildsubdir : ""),
			"/",
			path,
			NULL);
	free(buildsubdir);
    }

    fd = fopen(fn, "r");
    free(fn);
    if (fd == NULL)
	goto exit;

    mc = mfsSpecGetMacroContext(spec);

    while (fgets(buf, sizeof(buf), fd)) {
	char *s = buf;

	// Skip leading whitespace chars
	SKIPSPACE(s);
	if (*s == '#')
	    continue; // The line is comment

	// Expand macros
	if (expandMacros(NULL, mc, buf, sizeof(buf)))
	    goto exit;

	ARGV_t linelangs = argvNew();
	if (parseLineForLangs(s, &linelangs) == RPMRC_OK)
	    addSaneUniqLangs(langs, linelangs);
	argvFree(linelangs);
    }

    rc = RPMRC_OK;

exit:
    return rc;
}

static void findLanguages(MfsPackage pkg, ARGV_t *langs)
{
    MfsSpec spec = mfsPackageGetSpec(pkg);
    MfsFileLines flines = mfsPackageGetFileLines(pkg);
    MfsFileFiles ffiles = mfsPackageGetFileFiles(pkg);

    // Check files defined in spec file
    for (int x=0; x < mfsFileLinesCount(flines); x++) {
	ARGV_t linelangs = argvNew();
	char *line = mfsFileLinesGetLine(flines, x);
	if (parseLineForLangs(line, &linelangs) == RPMRC_OK)
	    addSaneUniqLangs(langs, linelangs);
	free(line);
	argvFree(linelangs);
    }

    // Check files defined in filelists
    for (int x=0; x < mfsFileFilesCount(ffiles); x++) {
	ARGV_t linelangs = argvNew();
	char *fn = mfsFileFilesGetFn(ffiles, x);
	if (parseFileForLangs(fn, spec, &linelangs) == RPMRC_OK)
	    addSaneUniqLangs(langs, linelangs);
	free(fn);
    argvFree(linelangs);
    }

    mfsFileLinesFree(flines);
    mfsFileFilesFree(ffiles);
    mfsSpecFree(spec);
}

rpmRC setupPkgsFunc(MfsContext context)
{
    ARGV_t langs = argvNew();
    rpmRC rc = RPMRC_FAIL;
    MfsSpec spec = mfsContextGetSpec(context);

    if (!spec) {
	mfslog_err("Cannot get spec from context\n");
	goto exit;
    }

    // Prepare context (spec) related data
    SharedData data = calloc(1, sizeof(*data));
    mfsContextSetData(context, data);

    // Get list of used languages
    for (int x=0; x < mfsSpecPackageCount(spec); x++) {
	MfsPackage pkg = mfsSpecGetPackage(spec, x);
	if (!pkg) {
	    mfslog_err("Cannot get package from spec\n");
	    goto exit;
	}
	findLanguages(pkg, &langs);
	mfsPackageFree(pkg);
    }

    // Prepare subpackages for these languages
    for (int x=0; x < argvCount(langs); x++) {
	MfsPackage pkg;
	char name[64];
	LangPkg langpkg;

	mfslog_info("Adding subpackage for lang: %s\n", langs[x]);
	snprintf(name, 64, "lang-%s", langs[x]);
	pkg = mfsPackageNew(context, name, "Language subpackage",
			    MFS_PACKAGE_FLAG_SUBNAME);
	if (!pkg)
	    goto exit;

	if (mfsPackageFinalize(pkg) != RPMRC_OK)
	    goto exit;

	langpkg = calloc(1, sizeof(*langpkg));
	langpkg->lang = rstrdup(langs[x]);
	langpkg->pkg = pkg;
	langpkg->next = data->langpkgs;
	data->langpkgs = langpkg;
    }

    rc = RPMRC_OK;

exit:
    mfsSpecFree(spec);
    argvFree(langs);
    return rc;
}

static MfsPackage findPkg(SharedData data, const char *lang)
{
    if (!data || !lang)
	return NULL;

    for (LangPkg lpkg=data->langpkgs; lpkg; lpkg=lpkg->next)
	if (lpkg->lang && !strcmp(lpkg->lang, lang))
	    return lpkg->pkg;

    return NULL;
}

rpmRC fileFunc(MfsContext context, MfsFile file)
{
    SharedData data = mfsContextGetData(context);
    ARGV_t langs = mfsFileGetLangs(file);
    int include_in_original = 0;

    for (int x=0; langs && langs[x]; x++) {
	MfsPackage pkg = findPkg(data, langs[x]);
	if (!pkg) {
	    include_in_original = 1;
	    continue;
	}
	mfslog_info("Langsubpackage for \"%s\" will contain: %s\n",
		    langs[x], mfsFileGetPath(file));
	mfsPackageAddFile(pkg, file);
    }
    argvFree(langs);

    mfsFileSetToOriginal(file, include_in_original);

    return RPMRC_OK;
}

rpmRC finalFunc(MfsContext context)
{
    SharedData data = mfsContextGetData(context);
    if (data) {
	for (LangPkg lpkg=data->langpkgs; lpkg;) {
	    LangPkg next = lpkg->next;
	    free(lpkg->lang);
	    mfsPackageFree(lpkg->pkg);
	    free(lpkg);
	    lpkg = next;
	}
        free(data);
    }

    return RPMRC_OK;
}

rpmRC init_langsubpackagesmodule(MfsManager mm)
{
    MfsBuildHook buildhook;
    MfsFileHook filehook;

    buildhook = mfsBuildHookNew(setupPkgsFunc, MFS_HOOK_POINT_POSTINTALL);
    mfsBuildHookSetPrettyName(buildhook, "setupPkgsFunc()");
    mfsManagerRegisterBuildHook(mm, buildhook);

    filehook = mfsFileHookNew(fileFunc);
    mfsFileHookAddGlob(filehook, "*");
    mfsFileHookSetPrettyName(filehook, "fileFunc()");
    mfsManagerRegisterFileHook(mm, filehook);

    buildhook = mfsBuildHookNew(finalFunc, MFS_HOOK_POINT_FINAL);
    mfsBuildHookSetPrettyName(buildhook, "finalFunc()");
    mfsManagerRegisterBuildHook(mm, buildhook);

    return RPMRC_OK;
}
