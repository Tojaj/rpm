#include <stdio.h>
#include <stdlib.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmlib.h>
#include "build/mfs.h"

typedef const struct DepType_s {
    MfsDepType type;
    const char *name;
} * DepType;

static struct DepType_s const deptypes[] = {
    { MFS_DEP_TYPE_REQUIRES,	"Requires" },
    { MFS_DEP_TYPE_PROVIDES,	"Provides" },
    { MFS_DEP_TYPE_CONFLICTS,	"Conflicts" },
    { MFS_DEP_TYPE_OBSOLETES,	"Obsoletes" },
    { MFS_DEP_TYPE_TRIGGERS,	"Triggers" },
    { MFS_DEP_TYPE_ORDER,	"Order" },
    { MFS_DEP_TYPE_SENTINEL,	NULL }
};

typedef const struct ScriptType_s {
    MfsScriptType type;
    const char *name;
} * ScriptType;

static struct ScriptType_s const scripttypes[] = {
    { MFS_SCRIPT_PREIN,		"pre" },
    { MFS_SCRIPT_POSTIN,	"post" },
    { MFS_SCRIPT_PREUN,		"preun" },
    { MFS_SCRIPT_POSTUN,	"postun" },
    { MFS_SCRIPT_PRETRANS,	"pretrans" },
    { MFS_SCRIPT_POSTTRANS,	"posttrans" },
    { MFS_SCRIPT_VERIFYSCRIPT,	"verifyscript" },
    { MFS_SCRIPT_SENTINEL,	NULL },
};

rpmRC parserfunc_newpkg(MfsContext context)
{
    rpmRC rc;

    MfsPackage pkg = mfsPackageNew(context,
                       "foo",
                       "Just foo package",
                       MFS_PACKAGE_FLAG_SUBNAME);
    rc = mfsPackageSetTag(pkg, RPMTAG_PACKAGER, "Anon", NULL);
    if (rc) return rc;
    rc = mfsPackageSetTag(pkg, RPMTAG_SUMMARY, "Český popisek", "cs");
    if (rc) return rc;
    rc = mfsPackageSetTag(pkg, RPMTAG_REQUIREFLAGS, "bash >= 3", "pre,post");
    if (rc) return rc;
    rc = mfsPackageSetTag(pkg, RPMTAG_SOURCE, "librepo-3c0ece7.tar.xz", "1");
    if (rc) return rc;

    //MfsChangelogEntry entry = mfsChangelogEntryNew();
    //mfsChangelogEntrySetDate(entry, "Tue Oct  9 2012");
    //mfsChangelogEntrySetAuthor(entry, "Tomas Mlcoch <tmlcoch at redhat.com> - 0.0.1-1.gitc69642e");
    //mfsChangelogEntrySetText(entry, "Bla bla bla");
    //mfsPackageAddChangelogEntry(pkg, entry);

    mfsSetContextData(context, "Package was added");

    return RPMRC_OK;
}

rpmRC parserfunc_specmod(MfsContext context)
{
    MfsSpec spec;
    MfsBTScript prepscript;
    char *buildroot, *prepcode;
    int pkgs;

    spec = mfsSpecFromContext(context);
    buildroot = mfsSpecGetString(spec, MFS_SPEC_ATTR_BUILDROOT);

    prepscript = mfsSpecGetScript(spec, MFS_SPEC_SCRIPT_PREP);
    mfsBTScriptAppendLine(prepscript, "echo -e \"It works\\n\"\n");
    mfsSpecSetScript(spec, prepscript, MFS_SPEC_SCRIPT_PREP);
    prepcode = mfsBTScriptGetCode(prepscript);
    pkgs = mfsSpecPackageCount(spec);

    rpmlog(RPMLOG_INFO, "# TestModule\n");
    rpmlog(RPMLOG_INFO, "# ----------\n");
    rpmlog(RPMLOG_INFO, "# Buildroot: %s\n", buildroot);
    rpmlog(RPMLOG_INFO, "# Prep script:\n%s\n", prepcode);
    rpmlog(RPMLOG_INFO, "# Packages: %d\n", pkgs);
    rpmlog(RPMLOG_INFO, "#\n");
    rpmlog(RPMLOG_INFO, "# Global data: %s\n", (char *) mfsGetModuleGlobalData(context));
    rpmlog(RPMLOG_INFO, "# Context data: %s\n", (char *) mfsGetContextData(context));

    free(buildroot);
    free(prepcode);
    mfsBTScriptFree(prepscript);

    return RPMRC_OK;
}

void print_pkginfo(MfsPackage pkg)
{
    Header hdr = mfsPackageGetHeader(pkg);

    rpmlog(RPMLOG_INFO, "\n========================================\n");
    rpmlog(RPMLOG_INFO, " %s\n", headerGetString(hdr, RPMTAG_NAME));

    // Scripts

    for (ScriptType scripttype = scripttypes; scripttype->name; scripttype++) {
	rpmlog(RPMLOG_INFO, "\nScript (%s):\n", scripttype->name);
	rpmlog(RPMLOG_INFO, "----------------------------------------\n");

	MfsScript script = mfsPackageGetScript(pkg, scripttype->type);

	if (scripttype->type == MFS_SCRIPT_POSTIN) {
	    mfsScriptFree(script);
	    script = mfsScriptNew();
	    mfsScriptSetProg(script, "/bin/python --version");
	    mfsScriptSetCode(script, "print \"Huh\"\n");
	    mfsPackageSetScript(pkg, script, scripttype->type);
	    script = mfsPackageGetScript(pkg, scripttype->type);
	}

	char *prog = mfsScriptGetProg(script);
	if (!prog) {
	    rpmlog(RPMLOG_INFO, "Not defined\n");
	    continue;
	}

	char *code = mfsScriptGetCode(script);
	char *file = mfsScriptGetFile(script);
	MfsScriptFlags flags = mfsScriptGetFlags(script);
	rpmlog(RPMLOG_INFO, "Code:\n%s\n", code);
	rpmlog(RPMLOG_INFO, "Prog: \"%s\"\n", prog);
	rpmlog(RPMLOG_INFO, "File: \"%s\"\n", file);
	rpmlog(RPMLOG_INFO, "Flags: %d\n", flags);
	rpmlog(RPMLOG_INFO, "\n----------------------------------------\n");
	if (code) free(code);
	if (prog) free(prog);
	if (file) free(file);
    }

    // Dependencies

    for (DepType deptype = deptypes; deptype->name; deptype++) {
	rpmlog(RPMLOG_INFO, "\nDependencies (%s):\n", deptype->name);
	rpmlog(RPMLOG_INFO, "----------------------------------------\n");

	MfsDeps deps = mfsPackageGetDeps(pkg, deptype->type);

	if (deptype->type == MFS_DEP_TYPE_REQUIRES) {
	    MfsDep entry = mfsDepNew();
	    mfsDepSetName(entry, "bash");
	    //mfsDepSetFlags(entry, RPMSENSE_EQUAL | RPMSENSE_GREATER);
	    mfsDepSetFlags(entry, 1548);
	    mfsDepSetVersion(entry, "3");
	    mfsDepsAppend(deps, entry);
	    mfsPackageSetDeps(pkg, deps, deptype->type);
	    mfsDepsFree(deps);
	    deps = mfsPackageGetDeps(pkg, deptype->type);
	}

	int count = mfsDepsCount(deps);
	for (int i = 0; i < count; i++) {
	    MfsDep entry = mfsDepsGetEntry(deps, i);
	    char *name = mfsDepGetName(entry);
	    char *version = mfsDepGetVersion(entry);
	    char *flags_str = mfsDepGetFlagsStr(entry);
	    rpmsenseFlags flags = mfsDepGetFlags(entry);
	    uint32_t index = mfsDepGetIndex(entry);
	    rpmlog(RPMLOG_INFO, "Name: \"%s\"\n", name);
	    rpmlog(RPMLOG_INFO, "Flags: %s (%d)\n", flags_str, flags);
	    rpmlog(RPMLOG_INFO, "Version: \"%s\"\n", version);
	    if (deptype->type == MFS_DEP_TYPE_TRIGGERS)
		rpmlog(RPMLOG_INFO, "Index: %d\n", index);
	    rpmlog(RPMLOG_INFO, "\n----------------------------------------\n");
	    free(name);
	    free(version);
	}

	mfsDepsFree(deps);
    }

    // Changelog
    rpmlog(RPMLOG_INFO, "Changelogs:\n");
    rpmlog(RPMLOG_INFO, "\n----------------------------------------\n");
    MfsChangelogs changelogs = mfsPackageGetChangelogs(pkg);

    MfsChangelog entry = mfsChangelogNew();
    mfsChangelogSetDateStr(entry, "Fri Jan 31 2014");
    mfsChangelogSetName(entry, "Tomas Mlcoch");
    mfsChangelogSetText(entry, "- Some description");
    mfsChangelogsInsert(changelogs, entry, 1);
    mfsPackageSetChangelogs(pkg, changelogs);
    mfsChangelogsFree(changelogs);

    changelogs = mfsPackageGetChangelogs(pkg);

    int count = mfsChangelogsCount(changelogs);
    for (int i = 0; i < count; i++) {
	MfsChangelog entry = mfsChangelogsGetEntry(changelogs, i);
	char *time = mfsChangelogGetDateStr(entry);
	char *name = mfsChangelogGetName(entry);
	char *text = mfsChangelogGetText(entry);
	rpmlog(RPMLOG_INFO, "Time: \"%s\"\n", time);
	rpmlog(RPMLOG_INFO, "Name: \"%s\"\n", name);
	rpmlog(RPMLOG_INFO, "Text:\n%s\n", text);
	rpmlog(RPMLOG_INFO, "\n----------------------------------------\n");
	free(text);
	free(name);
	free(time);
    }

    mfsChangelogsFree(changelogs);
}

rpmRC parserfunc_pkgsinfo(MfsContext context)
{
    MfsSpec spec;
    int pkgs;
    rpmRC rc = RPMRC_OK;

    spec = mfsSpecFromContext(context);
    pkgs = mfsSpecPackageCount(spec);

    const rpmTagVal *supportedtags = mfsPackageTags();
    for (int x = 0; supportedtags[x]; x++)
	rpmlog(RPMLOG_INFO, "Supported tags: %d\n", supportedtags[x]);

    rpmlog(RPMLOG_INFO, "################################################\n");

    for (int x = 0; x < pkgs; x++) {
	MfsPackage pkg = mfsSpecGetPackage(spec, x);
	if (!pkg) {
	    rpmlog(RPMLOG_ERR, "Cannot get package from a spec");
	    rc = RPMRC_FAIL;
	    break;
	}
	print_pkginfo(pkg);
	rpmlog(RPMLOG_INFO, "\n");
    }

    return rc;
}

rpmRC filefunc(MfsContext context, MfsFile file)
{
    rpmlog(RPMLOG_INFO, "File: %s\n", mfsFileGetPath(file));
    return RPMRC_OK;
}

rpmRC init_testmodule(MfsManager mm)
{
    MfsParserHook parserhook;
    MfsFileHook filehook;

    parserhook = mfsParserHookNew(parserfunc_newpkg);
    mfsParserHookSetPriority(parserhook, 1000);
    mfsRegisterParserHook(mm, parserhook);

    parserhook = mfsParserHookNew(parserfunc_pkgsinfo);
    mfsParserHookSetPriority(parserhook, 2000);
    mfsRegisterParserHook(mm, parserhook);

    parserhook = mfsParserHookNew(parserfunc_specmod);
    mfsParserHookSetPriority(parserhook, 3000);
    mfsRegisterParserHook(mm, parserhook);

    filehook = mfsFileHookNew(filefunc);
    mfsFileHookAddGlob(filehook, "*.h");
    mfsRegisterFileHook(mm, filehook);

    mfsSetGlobalData(mm, "Global data");

    return RPMRC_OK;
}
