#include "system.h"

#include <dlfcn.h>
#include <sys/types.h>
#include <dirent.h>
#include <fnmatch.h>
#include <ctype.h>
#include <regex.h>

#include <rpm/header.h>
#include <rpm/rpmlog.h>

#include "build/mfs.h"
#include "build/mfs_internal.h"
#include "build/rpmbuild_internal.h"
#include "build/parsePreamble_internal.h"
#include "build/parseChangelog_internal.h"
#include "build/files_internal.h"
#include "rpmio/rpmlua.h"

#include "debug.h"

#define MODULES_ENABLED "%{?_rpmbuild_modules_enabled}"
#define MODULES_BLACKLIST "%{?_rpmbuild_modules_blacklist_regex}"
#define MODULES_DIRECTORY "%{?_rpmbuild_modules_directory}"

#define TIME_STR_BUF	50
#define STATICSTRLEN(s) (sizeof(s)/sizeof(s[0]))

typedef struct MfsDlopenHandleList_s {
    void *handle;
    struct MfsDlopenHandleList_s *next;
} * MfsHandleList;

typedef struct MfsModuleLoadState_s {
    MfsHandleList handles;
} * MfsModuleLoadState;

// Forward declaration
void mfsModuleContextFree(MfsModuleContext context);

/*
 * Helper functions
 */

static const char *enumHookPointValToStr(MfsHookPoint point)
{
    switch (point) {
    case MFS_HOOK_POINT_POSTPARSE:  return "postparse";
    case MFS_HOOK_POINT_POSTPREP:   return "postprep";
    case MFS_HOOK_POINT_POSTBUILD:  return "postbuild";
    case MFS_HOOK_POINT_POSTINTALL: return "postinstall";
    case MFS_HOOK_POINT_POSTCHECK:  return "postcheck";
    case MFS_HOOK_POINT_POSTFILEPROCESSING: return "postfileprocessing";
    case MFS_HOOK_POINT_FINAL:      return "postfinal";
    default: break;
    }
    return "UNKNOWN";
}

static const char *enumSpecAttrValToStr(MfsSpecAttr val)
{
    switch (val) {
    case MFS_SPEC_ATTR_SPECFILE:	return "specfile";
    case MFS_SPEC_ATTR_BUILDROOT:	return "buildroot";
    case MFS_SPEC_ATTR_BUILDSUBDIR:	return "buildsubdir";
    case MFS_SPEC_ATTR_ROOTDIR:		return "rootdir";
    case MFS_SPEC_ATTR_SOURCERPMNAME:	return "sourcerpmname";
    case MFS_SPEC_ATTR_PARSED:		return "parsed";
    default: break;
    }
    return "UNKNOWN";
}

static const char *enumBTScriptTypeValToStr(MfsBTScriptType val)
{
    switch (val) {
    case MFS_SPEC_SCRIPT_PREP:	    return "prep";
    case MFS_SPEC_SCRIPT_BUILD:	    return "build";
    case MFS_SPEC_SCRIPT_INSTALL:   return "install";
    case MFS_SPEC_SCRIPT_CHECK:	    return "check";
    case MFS_SPEC_SCRIPT_CLEAN:	    return "clean";
    default: break;
    }
    return "UNKNOWN";
}

static const char *enumScriptTypeValToStr(MfsScriptType val)
{
    switch (val) {
    case MFS_SCRIPT_PREIN:	    return "prein";
    case MFS_SCRIPT_POSTIN:	    return "postin";
    case MFS_SCRIPT_PREUN:	    return "preun";
    case MFS_SCRIPT_POSTUN:	    return "postun";
    case MFS_SCRIPT_PRETRANS:	    return "pretrans";
    case MFS_SCRIPT_POSTTRANS:	    return "posttrans";
    case MFS_SCRIPT_VERIFYSCRIPT:   return "verifyscript";
    default: break;
    }
    return "UNKNOWN";
}

static const char *enumDepTypeToStr(MfsDepType val)
{
    switch (val) {
    case MFS_DEP_TYPE_REQUIRES:	    return "requires";
    case MFS_DEP_TYPE_PROVIDES:	    return "provides";
    case MFS_DEP_TYPE_CONFLICTS:    return "conflicts";
    case MFS_DEP_TYPE_OBSOLETES:    return "obsoletes";
    case MFS_DEP_TYPE_TRIGGERS:	    return "triggers";
    case MFS_DEP_TYPE_ORDER:	    return "order";
    case MFS_DEP_TYPE_RECOMMENDS:   return "recommends";
    case MFS_DEP_TYPE_SUGGESTS:	    return "suggests";
    case MFS_DEP_TYPE_SUPPLEMENTS:  return "supplements";
    case MFS_DEP_TYPE_ENHANCES:	    return "enhances";
    default: break;
    }
    return "UNKNOWN";
}

static inline char *mstrdup(const char *str)
{
    if (!str) return NULL;
    return xstrdup(str);
}

static ARGV_t argvCopy(ARGV_t argv)
{
    ARGV_t copy = NULL;
    if (argv) {
        copy = argvNew();
        argvAppend(&copy, argv);
    }
    return copy;
}

static void argvDelete(ARGV_t argv, int i)
{
    int ac = argvCount(argv);
    if (i < 0) return;
    for (int x=i; x < ac; x++)
	argv[x] = argv[x+1];
}

int mfsAsprintf(char **strp, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n >= 0) {
	size_t nb = n + 1;
	char *msg = xmalloc(nb);
	va_start(ap, fmt);
	vsnprintf(msg, nb, fmt, ap);
	va_end(ap);
	*strp = msg;
    } else {
	*strp = NULL;
    }

    return n;
}

void mfslog(int code, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n >= 0) {
	char *msg_format, *msg;
	size_t nb = n + STATICSTRLEN(LOGPREFIX);
	size_t fb = strlen(fmt) + STATICSTRLEN(LOGPREFIX) ;

	msg = xmalloc(nb);
	msg_format = xmalloc(fb);

	strcpy(msg_format, LOGPREFIX);
	strcat(msg_format, fmt);

	va_start(ap, fmt);
	vsnprintf(msg, nb, msg_format, ap);
	va_end(ap);

	free(msg_format);
	rpmlog(code, "%s", msg);
	free(msg);
    }
}

/*
 * Module manager related functions
 */

MfsManager mfsManagerNew(rpmSpec spec)
{
    MfsManager mm = xcalloc(1, sizeof(*mm));
    mm->mainspec = spec;
    return mm;
}

void mfsManagerFree(MfsManager mm)
{
    if (!mm)
        return;

    for (MfsModuleContext mc=mm->modulecontexts; mc;) {
        MfsModuleContext next = mc->next;
        mm->cur_context = mc;
        if (mc->cleanupfunc)
            mc->cleanupfunc(mm);
        mc = next;
    }
    mm->cur_context = NULL;

    for (MfsModuleContext mc=mm->modulecontexts; mc;) {
	MfsModuleContext next = mc->next;
	mfsModuleContextFree(mc);
	mc = next;
    }

    for (MfsBuildHook bh=mm->buildhooks; bh;) {
	MfsBuildHook next = bh->next;
	free(bh->prettyname);
	free(bh);
	bh = next;
    }

    for (MfsFileHook fh=mm->filehooks; fh;) {
	MfsFileHook next = fh->next;
	free(fh->prettyname);
	for (MfsGlob glob=fh->globs; glob;) {
	    MfsGlob next = glob->next;
	    free(glob->glob);
	    free(glob);
	    glob = next;
	}
	free(fh);
	fh = next;
    }

    if (mm->fc)
        rpmfcFree(mm->fc);

    free(mm);
}

static void mfsManagerInsertSortedBuildHook(MfsManager mfsm,
                                             MfsBuildHook cur)
{
    MfsBuildHook prev = NULL;
    MfsBuildHook node = mfsm->buildhooks;

    if (!node || cur->priority < node->priority) {
        cur->next = node;
        mfsm->buildhooks = cur;
        return;
    }

    while (1) {
        prev = node;
        node = node->next;
        if (!node || cur->priority < node->priority)
            break;
    }

    cur->next = node;
    prev->next = cur;
}

static void mfsManagerInsertSortedFileHook(MfsManager mfsm,
                                           MfsFileHook cur)
{
    MfsFileHook prev = NULL;
    MfsFileHook node = mfsm->filehooks;

    if (!node || cur->priority < node->priority) {
        cur->next = node;
        mfsm->filehooks = cur;
        return;
    }

    while (1) {
        prev = node;
        node = node->next;
        if (!node || cur->priority < node->priority)
            break;
    }

    cur->next = node;
    prev->next = cur;
}

static void mfsManagerSortHooks(MfsManager mfsm)
{
    MfsModuleContext ctx = mfsm->modulecontexts;

    while (ctx) {
        // Use build Hooks
        for (MfsBuildHook cur = ctx->buildhooks; cur;) {
            MfsBuildHook next = cur->next;
            mfsManagerInsertSortedBuildHook(mfsm, cur);
            cur = next;
        }
        ctx->buildhooks = NULL;

        // Use File Hooks
        for (MfsFileHook cur = ctx->filehooks; cur;) {
            MfsFileHook next = cur->next;
            mfsManagerInsertSortedFileHook(mfsm, cur);
            cur = next;
        }
        ctx->filehooks = NULL;

        ctx = ctx->next;
    }

    // Debug output
    mfslog_info(_("Registered BuildHooks:\n"));
    for (MfsBuildHook cur = mfsm->buildhooks; cur; cur = cur->next)
        mfslog_info(_("- Module %s registered BuildHook %p - %s (%d)\n"),
		    cur->modulecontext->modulename, cur->func,
		    cur->prettyname ? cur->prettyname : "no prettyname", cur->priority);

    mfslog_info(_("Registered FileHooks:\n"));
    for (MfsFileHook cur = mfsm->filehooks; cur; cur = cur->next)
        mfslog_info(_("- Module %s registered FileHook %p - %s (%d)\n"),
		    cur->modulecontext->modulename, cur->func,
		    cur->prettyname ? cur->prettyname : "no prettyname", cur->priority);
}

/* Insert (move) the current context the internal list of contexts.
 * This function keeps the list of internal contexts sorted by the
 * names of the corresponding modules.
 */
static void mfsManagerUseCurrentContext(MfsManager mfsm)
{
    MfsModuleContext cur = mfsm->cur_context;
    MfsModuleContext prev = NULL;
    MfsModuleContext node = mfsm->modulecontexts;

    mfsm->cur_context = NULL;

    if (!node || strcmp(cur->modulename, node->modulename) < 0) {
        // Prepend item to the beginning of the list
        cur->next = node;
        mfsm->modulecontexts = cur;
        return;
    }

    // Find right position
    while (1) {
        prev = node;
        node = node->next;
        if (!node || strcmp(cur->modulename, node->modulename) < 0)
            break;
    }

    cur->next = node;
    prev->next = cur;
}

MfsContext mfsModuleContextGetContext(MfsModuleContext parent, rpmSpec spec)
{
    MfsContext context = NULL;

    // Find a context for the specified spec file
    for (context = parent->contexts; context; context = context->next)
	if (context->spec == spec)
	    return context;

    // Or create a new, if it doesn't already exist
    context = xcalloc(1, sizeof(*context));
    context->modulecontext = parent;
    context->state = MFS_CTXSTATE_UNKNOWN;
    context->spec = spec;

    context->next = parent->contexts;
    parent->contexts = context;

    return context;
}

void mfsContextFree(MfsContext context)
{
    free(context);
}

MfsModuleContext mfsModuleContextNew(MfsManager mm, const char *modulename)
{
    MfsModuleContext context = xcalloc(1, sizeof(*context));
    context->manager = mm;
    context->modulename = xstrdup(modulename);
    return context;
}

void mfsModuleContextFree(MfsModuleContext context)
{
    if (!context)
        return;
    free(context->modulename);
    for (MfsContext c = context->contexts; c;) {
	MfsContext next = c->next;
	mfsContextFree(c);
	c = next;
    }
    free(context);
}

/*
 * Module (un)loading related functions
 */

// Return the name of the plugin (the filename without .so extension)
// if it appears like a valid plugin path, or NULL.
static char *getModuleName(char *filename) {
    char *last_slash = strrchr(filename, '/');
    char *name_start = last_slash ? last_slash + 1 : filename;
    char *last_dot = strrchr(filename, '.');
    char *module_name;

    if (!last_dot || strcmp(last_dot, ".so"))
        return NULL;

    // Check the module name - it must fit pattern [a-zA-Z][a-zA-z0-9_]*
    if (!isalpha(*name_start))
	return NULL;
    for (char *c = name_start; c < last_dot; c++)
	if (!isalnum(*c) && *c != '_')
	    return NULL;

    module_name = strdup(name_start);
    module_name[last_dot - name_start] = '\0';

    return module_name;
}

static void *loadModule(const char *name, const char *fullpath,
			MfsManager mfsm)
{
    char *slashedpath;
    void *handle;
    char *initfunc_name;
    MfsModuleInitFunc initfunc;
    int rc;

    // Make sure the path to dlopen has a slash, for it to consider it
    // an actual filesystem path and not just a lookup name.
    if (fullpath[0] != '/')
        rasprintf(&slashedpath, "./%s", fullpath);
    else
        slashedpath = strdup(fullpath);

    // Attempt to open the module
    handle = dlopen(slashedpath, RTLD_NOW);
    free(slashedpath);
    if (!handle) {
	mfslog_err(_("Error while loading module %s: %s\n"),
	       fullpath, dlerror());
        return NULL;
    }

    rasprintf(&initfunc_name, "init_%s", name);
    initfunc = (MfsModuleInitFunc) dlsym(handle, initfunc_name);
    free(initfunc_name);

    if (!initfunc) {
	mfslog_err(_("Error while loading init function "
               "of module %s: %s\n"), fullpath, dlerror());
        dlclose(handle);
        return NULL;
    }

    // Prepare context for this module
    MfsModuleContext mcontext = mfsModuleContextNew(mfsm, name);
    mfsm->cur_context = mcontext;

    // Init the module
    rc = initfunc(mfsm);
    if (rc < 0) {
        mfslog_err(_("Error: Init function of %s returned %d\n"),
		fullpath, rc);
        dlclose(handle);
        mfsModuleContextFree(mcontext);
        mfsm->cur_context = NULL;
        return NULL;
    }

    // Insert the current context to the manager's list of contexts
    mfsManagerUseCurrentContext(mfsm);

    mfslog_info(_("Loaded module: %s\n"), fullpath);

    return handle;
}

static char *getRegerror (int errcode, regex_t *compiled)
{
    size_t length = regerror(errcode, compiled, NULL, 0);
    char *buffer = xmalloc(length);
    (void) regerror(errcode, compiled, buffer, length);
    return buffer;
}

static rpmRC mfsCompileRegEx(regex_t *reg, const char *in_pattern,
			     int expand, int *initialized)
{
    int rc = RPMRC_OK;
    int reg_rc;
    char *pattern;

    if (initialized)
        *initialized = 0;

    if (!in_pattern)
	goto exit;

    if (expand)
	pattern = rpmExpand(in_pattern, NULL);
    else
	pattern = mstrdup(in_pattern);

    if (!pattern || *pattern == '\0')
	goto exit;

    if ((reg_rc = regcomp(reg, pattern, REG_EXTENDED)) != 0) {
	char *errmsg = getRegerror(reg_rc, NULL);
	mfslog_warning(_("Cannot compile regex \"%s\": %s\n"), pattern, errmsg);
	free(errmsg);
	rc = RPMRC_FAIL;
	goto exit;
    }

    if (initialized)
	*initialized = 1;

exit:
    if (pattern)
	free(pattern);

    return rc;
}

char *mfsModulesDirectory(void)
{
    char *moduledir;

    moduledir = rpmExpand(MODULES_DIRECTORY, NULL);
    if (moduledir && *moduledir != '\0' && *moduledir == '/')
	return moduledir;

    free(moduledir);

    rasprintf(&moduledir, "%s/%s", rpmConfigDir(), MFSMODULESDIR);
    moduledir = rpmCleanPath(moduledir);
    return moduledir;
}

rpmRC mfsLoadModules(void **modules, const char *path, MfsManager mfsm)
{
    MfsModuleLoadState load_state;
    struct dirent* direntry;
    int error_during_loading = 0;
    regex_t blacklist;
    int blacklist_initialized;

    *modules = NULL;

    // Check if modules are enabled
    if (rpmExpandNumeric(MODULES_ENABLED) == 0) {
	mfslog_info(_("Modular system is disabled\n"));
	return RPMRC_OK;
    }

    DIR *dir = opendir(path);
    if (!dir) {
	mfslog_err(_("Could not open directory %s: %m\n"), path);
        return RPMRC_FAIL;
    }

    // Prepare blacklisting regexs
    if (mfsCompileRegEx(&blacklist, MODULES_BLACKLIST,
			1, &blacklist_initialized) != RPMRC_OK) {
	mfslog_warning(_("Could not compile regex from \"%s\"\n"),
			MODULES_BLACKLIST);
    }

    load_state = xcalloc(1, sizeof(*load_state));

    while ((direntry = readdir(dir))) {
        char *name, *fullpath;

	name = getModuleName(direntry->d_name);
        if (!name)
            continue;

	// Check if module is not blacklisted
	if (blacklist_initialized && regexec(&blacklist, name, 0, NULL, 0) == 0) {
	    mfslog_info(_("Module \"%s\" is blacklisted\n"), name);
	    free(name);
	    continue;
	}

	rasprintf(&fullpath, "%s/%s", path, direntry->d_name);

        // Load the plugin, get the DSO handle and add it to the list
        void *handle = loadModule(name, fullpath, mfsm);
        if (!handle) {
            free(name);
            free(fullpath);
            error_during_loading = 1;
            break;
        }

        MfsHandleList handle_node = xmalloc(sizeof(*handle_node));
        handle_node->handle = handle;
        handle_node->next = load_state->handles;
        load_state->handles = handle_node;

        free(name);
        free(fullpath);
    }

    closedir(dir);

    if (blacklist_initialized)
	regfree(&blacklist);

    if (error_during_loading) {
        mfsUnloadModules(load_state);
        return RPMRC_FAIL;
    }

    // Make sorted lists of hooks from available contexts
    mfsManagerSortHooks(mfsm);

    *modules = (void *) load_state;

    return RPMRC_OK;
}

void mfsUnloadModules(void *p_load_state)
{
    if (!p_load_state)
        return;
    MfsModuleLoadState load_state = p_load_state;
    MfsHandleList node = load_state->handles;
    while (node) {
        MfsHandleList next = node->next;
        dlclose(node->handle);
        free(node);
        node = next;
    }
    free(load_state);
}

rpmRC mfsManagerCallBuildHooks(MfsManager mm, rpmSpec cur_spec, MfsHookPoint point)
{
    rpmRC rc = RPMRC_OK;

    if (point >= MFS_HOOK_POINT_SENTINEL)
	return RPMRC_FAIL;

    for (MfsBuildHook hook = mm->buildhooks; hook; hook=hook->next) {
        MfsBuildHookFunc func = hook->func;
        MfsModuleContext modulecontext = hook->modulecontext;
	MfsContext context;

	if (hook->point != point)
	    continue;

	// Prepare the context
	context = mfsModuleContextGetContext(modulecontext, cur_spec);
	context->state = MFS_CTXSTATE_BUILDHOOK;
	context->lastpoint = point;

	// Logging
	if (hook->prettyname)
	    mfslog_info(_("Calling hook: %s at %s\n"),
			hook->prettyname, enumHookPointValToStr(point));
        else
	    mfslog_info(_("Calling hook: %p (no prettyname set) at %s\n"),
			hook->func, enumHookPointValToStr(point));

	// Call the hook
        if ((rc = func(context)) != RPMRC_OK) {
	    mfslog_err(_("Module %s returned an error from parsehook\n"),
		   hook->modulecontext->modulename);
            break;
	}

	context->state = MFS_CTXSTATE_UNKNOWN;
    }

    return rc;
}

/* Create a copy of FileListRec, where all strings of the copy are malloced.
 */
static FileListRec mfsDupFileListRec(FileListRec rec)
{
    FileListRec copy = xcalloc(1, sizeof(*copy));
    copy->fl_st = rec->fl_st;	// Struct assignment
    copy->diskPath = mstrdup(rec->diskPath);
    copy->cpioPath = mstrdup(rec->cpioPath);
    // XXX: Tricky, but FileListRec has really got strings in these variables
    // during processing by addFile() function from build/files.c
    copy->uname = mstrdup((const char *) rec->uname);
    copy->gname = mstrdup((const char *) rec->gname);
    // XXX: End of tricky part
    copy->flags = rec->flags;
    copy->specdFlags = rec->specdFlags;
    copy->verifyFlags = rec->verifyFlags;
    copy->langs = mstrdup(rec->langs);
    copy->caps = mstrdup(rec->caps);
    return copy;
}

static void mfsFreeDuppedFileListRec(FileListRec copy)
{
    if (!copy)
	return;
    free(copy->diskPath);
    free(copy->cpioPath);
    free((char *) copy->uname);
    free((char *) copy->gname);
    free(copy->langs);
    free(copy->caps);
    free(copy);
}

rpmRC mfsMangerInitFileClassificator(MfsManager mm, rpmSpec spec)
{
    mm->fc = rpmfcCreate(spec->buildRoot, 0);
    return RPMRC_OK;
}

void mfsMangerFreeFileClassificator(MfsManager mm)
{
    rpmfcFree(mm->fc);
    mm->fc = NULL;
}

rpmRC mfsManagerCallFileHooks(MfsManager mm, rpmSpec cur_spec, Package pkg,
			      FileListRec rec, int *include_in_original)
{
    rpmRC rc = RPMRC_OK;
    rpmcf classified_file;
    int local_include_in_original = 1;

    if (!mm)
	return RPMRC_OK;

    // Classify the file
    classified_file = rpmfcClassifyFile(mm->fc, rec->diskPath, rec->fl_mode);

    // Prepare the MfsFile
    MfsFile mfsfile = xcalloc(1, sizeof(*mfsfile));
    mfsfile->diskpath = rec->diskPath;
    mfsfile->classified_file = classified_file;
    mfsfile->originalpkg = pkg;
    mfsfile->spec = cur_spec;

    for (MfsFileHook hook = mm->filehooks; hook; hook=hook->next) {
	MfsFileHookFunc func = hook->func;
        MfsModuleContext modulecontext = hook->modulecontext;
	MfsContext context;

	// Check the glob
	const char *diskpath = rec->diskPath;
	int match = 0; // 0 - is TRUE in this case
	for (MfsGlob glob = hook->globs; glob; glob = glob->next) {
	    char *expanded = rpmExpand(glob->glob, NULL);
	    match = fnmatch(expanded, diskpath, 0);
	    free(expanded);
	    if (match == 0)
		break;
	}
	if (match != 0)
	    // Skip this
	    continue;

	// Prepare the MfsFile
	mfsfile->flr = mfsDupFileListRec(rec);
	mfsfile->include_in_original = local_include_in_original;

	// Prepare the context
	context = mfsModuleContextGetContext(modulecontext, cur_spec);
	context->state = MFS_CTXSTATE_FILEHOOK;

	// Logging
	if (hook->prettyname)
	    mfslog_info(_("Calling hook: %s for: %s\n"),
		        hook->prettyname, rec->diskPath);
        else
	    mfslog_info(_("Calling hook: %p (no prettyname set) for: %s\n"),
			hook->func, rec->diskPath);

	// Call the hook
        if ((rc = func(context, mfsfile)) != RPMRC_OK) {
	    mfslog_err(_("Module %s returned an error from filehook\n"),
		   hook->modulecontext->modulename);
            break;
	}

	// Get info from the MfsFile and free it
	if (!mfsfile->include_in_original && local_include_in_original) {
	    local_include_in_original = 0;
	    mfslog_info(_("File %s won't be included in its original "
			"destination package \"%s\"\n"), rec->diskPath,
			(pkg) ? headerGetString(pkg->header, RPMTAG_NAME): "(None)");
	}
	mfsFreeDuppedFileListRec(mfsfile->flr);

	context->state = MFS_CTXSTATE_UNKNOWN;
    }

    // Free MfsFile stuff
    for (MfsFilePackageList e = mfsfile->pkglist; e; ) {
	MfsFilePackageList next = e->next;
	free(e);
	e = next;
    }
    free(mfsfile);
    rpmcfFree(classified_file);

    *include_in_original = local_include_in_original;

    return rc;
}

/*
 * Module initialization related API
 */

MfsBuildHook mfsBuildHookNew(MfsBuildHookFunc hookfunc, MfsHookPoint point)
{
    MfsBuildHook hook;

    if (point >= MFS_HOOK_POINT_SENTINEL)
	return NULL;

    hook = xcalloc(1, sizeof(*hook));
    hook->point = point;
    hook->func = hookfunc;
    hook->priority = MFS_HOOK_DEFAULT_PRIORITY_VAL;
    return hook;
}

rpmRC mfsBuildHookSetPriority(MfsBuildHook hook, int32_t priority)
{
    if (priority < MFS_HOOK_MIN_PRIORITY_VAL
            || priority > MFS_HOOK_MAX_PRIORITY_VAL)
        return RPMRC_FAIL;

    hook->priority = priority;
    return RPMRC_OK;
}

rpmRC mfsBuildHookSetPrettyName(MfsBuildHook hook, const char *name)
{
    free(hook->prettyname);
    hook->prettyname = xstrdup(name);
    return RPMRC_OK;
}

void mfsManagerRegisterBuildHook(MfsManager mm, MfsBuildHook hook)
{
    MfsModuleContext modulecontext = mm->cur_context;
    hook->modulecontext = modulecontext;
    hook->next = modulecontext->buildhooks;
    modulecontext->buildhooks = hook;
}

MfsFileHook mfsFileHookNew(MfsFileHookFunc hookfunc)
{
    MfsFileHook hook = xcalloc(1, sizeof(*hook));
    hook->func = hookfunc;
    hook->priority = MFS_HOOK_DEFAULT_PRIORITY_VAL;
    return hook;
}

rpmRC mfsFileHookSetPriority(MfsFileHook hook, int32_t priority)
{
    if (priority < MFS_HOOK_MIN_PRIORITY_VAL
            || priority > MFS_HOOK_MAX_PRIORITY_VAL)
        return RPMRC_FAIL;

    hook->priority = priority;
    return RPMRC_OK;
}

rpmRC mfsFileHookSetPrettyName(MfsFileHook hook, const char *name)
{
    free(hook->prettyname);
    hook->prettyname = xstrdup(name);
    return RPMRC_OK;
}

void mfsFileHookAddGlob(MfsFileHook hook, const char *glob)
{
    MfsGlob mfsglob = xcalloc(1, sizeof(*mfsglob));
    mfsglob->glob = strdup(glob);
    mfsglob->next = hook->globs;
    hook->globs = mfsglob;
}

void mfsManagerRegisterFileHook(MfsManager mm, MfsFileHook hook)
{
    assert(mm);
    MfsModuleContext modulecontext = mm->cur_context;
    hook->modulecontext = modulecontext;
    hook->next = modulecontext->filehooks;
    modulecontext->filehooks = hook;
}

rpmRC mfsManagerSetCleanupFunc(MfsManager mm, MfsModuleCleanupFunc func)
{
    assert(mm);
    mm->cur_context->cleanupfunc = func;
    return RPMRC_OK;
}

void *mfsManagerGetGlobalData(MfsManager mm)
{
    assert(mm);
    MfsModuleContext modulecontext = mm->cur_context;
    return modulecontext->globaldata;
}

void mfsManagerSetGlobalData(MfsManager mm, void *data)
{
    assert(mm);
    MfsModuleContext modulecontext = mm->cur_context;
    modulecontext->globaldata = data;
}

void *mfsContextGetGlobalData(MfsContext context)
{
    return context->modulecontext->globaldata;
}

void mfsContextSetGlobalData(MfsContext context, void *data)
{
    context->modulecontext->globaldata = data;
}

void *mfsContextGetData(MfsContext context)
{
    assert(context);
    return context->userdata;
}

void mfsContextSetData(MfsContext context, void *data)
{
    context->userdata = data;
}

/*
 * Spec manipulation related API
 */

MfsSpec mfsContextGetSpec(MfsContext context)
{
    MfsSpec mfsspec;

    if (!context || !context->spec)
	return NULL;

    mfsspec = xcalloc(1, sizeof(*mfsspec));
    mfsspec->rpmspec = context->spec;

    return mfsspec;
}

MfsSpec mfsPackageGetSpec(MfsPackage pkg)
{
    MfsSpec mfsspec;

    if (!pkg || !pkg->spec)
	return NULL;

    mfsspec = xcalloc(1, sizeof(*mfsspec));
    mfsspec->rpmspec = pkg->spec;

    return mfsspec;
}

char * mfsSpecGetString(MfsSpec spec, MfsSpecAttr attr)
{
    const char *ptr;
    rpmSpec rpmspec;

    assert(spec);

    rpmspec = spec->rpmspec;

    switch (attr) {
    case MFS_SPEC_ATTR_SPECFILE:
	ptr = rpmspec->specFile;
	break;
    case MFS_SPEC_ATTR_BUILDROOT:
	ptr = rpmspec->buildRoot;
	break;
    case MFS_SPEC_ATTR_BUILDSUBDIR:
	ptr = rpmspec->buildSubdir;
	break;
    case MFS_SPEC_ATTR_ROOTDIR:
	ptr = rpmspec->rootDir;
	break;
    case MFS_SPEC_ATTR_SOURCERPMNAME:
	ptr = rpmspec->sourceRpmName;
	break;
    case MFS_SPEC_ATTR_PARSED:
	ptr = getStringBuf(rpmspec->parsed);
	break;
    default:
	ptr = NULL;
    }

    return mstrdup(ptr);
}

static rpmRC _replaceStringBuf(StringBuf *buf, const char *str)
{
    if (*buf) {
	freeStringBuf(*buf);
	*buf = NULL;
    }

    if (str) {
	*buf = newStringBuf();
	appendStringBuf(*buf, str);
    }

    return RPMRC_OK;
}

rpmRC mfsSpecSetString(MfsSpec spec, MfsSpecAttr attr, const char *str)
{
    rpmRC rc = RPMRC_OK;
    rpmSpec rpmspec;

    assert(spec);

    rpmspec = spec->rpmspec;

    mfslog_info("Setting spec attribute %s to: \"%s\"\n",
		enumSpecAttrValToStr(attr), str ? str : "NULL");

    switch (attr) {
    case MFS_SPEC_ATTR_SPECFILE:
	rpmspec->specFile = mstrdup(str);
	break;
    case MFS_SPEC_ATTR_BUILDROOT:
	rpmspec->buildRoot = mstrdup(str);
	break;
    case MFS_SPEC_ATTR_BUILDSUBDIR:
	rpmspec->buildSubdir = mstrdup(str);
	break;
    case MFS_SPEC_ATTR_ROOTDIR:
	rpmspec->rootDir = mstrdup(str);
	break;
    case MFS_SPEC_ATTR_SOURCERPMNAME:
	rpmspec->sourceRpmName = mstrdup(str);
	break;
    case MFS_SPEC_ATTR_PARSED:
	rc = _replaceStringBuf(&rpmspec->parsed, str);
	break;
    default:
	rc = RPMRC_FAIL;
    }

    return rc;
}

char *mfsSpecGetArch(MfsSpec spec)
{
    assert(spec && spec->rpmspec);
    // initSourceHeader() takes arch directly from the first (main) package too
    return mstrdup(headerGetString(spec->rpmspec->packages->header, RPMTAG_ARCH));
}

int mfsSpecPackageCount(MfsSpec spec)
{
    assert(spec && spec->rpmspec);
    int x = 0;
    Package pkg = spec->rpmspec->packages;
    while (pkg) {
	pkg = pkg->next;
	x++;
    }
    return x;
}

static MfsPackage mfsPackageFromPackage(rpmSpec spec, Package pkg)
{
    MfsPackage mfspackage = xcalloc(1, sizeof(*mfspackage));
    mfspackage->pkg = pkg;
    mfspackage->fullname = mstrdup(headerGetString(pkg->header, RPMTAG_NAME));
    mfspackage->spec = spec;
    return mfspackage;
}

MfsPackage mfsSpecGetPackage(MfsSpec spec, int index)
{
    assert(spec && spec->rpmspec);

    int x = 0;
    Package pkg = spec->rpmspec->packages;
    MfsPackage mfspackage;

    while (pkg) {
	if (x == index)
	    break;
	pkg = pkg->next;
	x++;
    }

    if (!pkg)
	return NULL;

    return mfsPackageFromPackage(spec->rpmspec, pkg);
}

MfsPackage mfsSpecGetSourcePackage(MfsSpec spec)
{
    assert(spec && spec->rpmspec);
    Package pkg = spec->rpmspec->sourcePackage;
    if (!pkg)
	return NULL;
    MfsPackage mfspackage = xcalloc(1, sizeof(*mfspackage));
    mfspackage->pkg = pkg;
    mfspackage->spec = spec->rpmspec;
    return mfspackage;
}

rpmMacroContext mfsSpecGetMacroContext(MfsSpec spec)
{
    assert(spec && spec->rpmspec);
    return spec->rpmspec->macros;
}

rpmRC mfsSpecExpandMacro(MfsSpec spec, char *sbuf, size_t slen)
{
    assert(spec && spec->rpmspec);
    if (expandMacros(NULL, spec->rpmspec->macros, sbuf, slen) == 0)
	return RPMRC_OK;
    return RPMRC_FAIL;
}

MfsBTScript mfsSpecGetScript(MfsSpec spec, MfsBTScriptType type)
{
    assert(spec && spec->rpmspec);

    MfsBTScript script;
    rpmSpec rpmspec = spec->rpmspec;
    const char *code = NULL;

    switch (type) {
        case MFS_SPEC_SCRIPT_PREP:
	    code = getStringBuf(rpmspec->prep);
	    break;
        case MFS_SPEC_SCRIPT_BUILD:
	    code = getStringBuf(rpmspec->build);
	    break;
        case MFS_SPEC_SCRIPT_INSTALL:
	    code = getStringBuf(rpmspec->install);
	    break;
        case MFS_SPEC_SCRIPT_CHECK:
	    code = getStringBuf(rpmspec->check);
	    break;
        case MFS_SPEC_SCRIPT_CLEAN:
	    code = getStringBuf(rpmspec->clean);
	    break;
	default:
	    return NULL;  // Programmer error
    }

    script = xcalloc(1, sizeof(*script));
    script->code = newStringBuf();
    if (code)
        appendStringBuf(script->code, code);

    return script;
}

rpmRC mfsSpecSetScript(MfsSpec spec, MfsBTScript script, MfsBTScriptType type)
{
    assert(spec && spec->rpmspec);

    rpmRC rc = RPMRC_FAIL;
    rpmSpec rpmspec = spec->rpmspec;
    const char *code = getStringBuf(script->code);

    mfslog_info("Setting spec script %s to:\n%s\n",
		enumBTScriptTypeValToStr(type), code ? code : "NULL");

    switch (type) {
    case MFS_SPEC_SCRIPT_PREP:
	rc = _replaceStringBuf(&rpmspec->prep, code);
	break;
    case MFS_SPEC_SCRIPT_BUILD:
	rc = _replaceStringBuf(&rpmspec->build, code);
	break;
    case MFS_SPEC_SCRIPT_INSTALL:
	rc = _replaceStringBuf(&rpmspec->install, code);
	break;
    case MFS_SPEC_SCRIPT_CHECK:
	rc = _replaceStringBuf(&rpmspec->check, code);
	break;
    case MFS_SPEC_SCRIPT_CLEAN:
	rc = _replaceStringBuf(&rpmspec->clean, code);
	break;
    default:
	break;
    }

    return rc;
}

void mfsSpecFree(MfsSpec spec)
{
    free(spec);
}

void mfsBTScriptFree(MfsBTScript script)
{
    if (!script)
	return;
    freeStringBuf(script->code);
    free(script);
}

char *mfsBTScriptGetCode(MfsBTScript script)
{
    return mstrdup(getStringBuf(script->code));
}

rpmRC mfsBTScriptSetCode(MfsBTScript script, const char *code)
{
    return _replaceStringBuf(&script->code, code);
}

rpmRC mfsBTScriptAppend(MfsBTScript script, const char *code)
{
    appendStringBuf(script->code, code);
    return RPMRC_OK;
}

rpmRC mfsBTScriptAppendLine(MfsBTScript script, const char *code)
{
    appendLineStringBuf(script->code, code);
    return RPMRC_OK;
}

/*
 * build Hook Related API
 */

// Package

void mfsPackageFree(MfsPackage pkg)
{
    if (pkg) {
	free(pkg->fullname);
        free(pkg);
    }
}

void *mfsPackageId(MfsPackage pkg)
{
    if (!pkg) return NULL;
    return (void*) pkg->pkg;
}

MfsPackage mfsPackageNew(MfsContext context,
			 const char *name,
			 const char *summary,
			 int flags)
{
    int flag = 0;
    MfsPackage mfs_pkg = NULL;
    rpmSpec spec = context->spec;
    char *fullname;
    char *ename = NULL; // Expanded name
    char *esummary = NULL; // Expanded summary
    Package pkg;

    ename = rpmExpand(name, NULL);

    if (context->state != MFS_CTXSTATE_BUILDHOOK) {
	mfslog_err(_("Packages must be added in a build hook. "
			     "Cannot add: %s\n"), ename);
	goto error;
    } else if (context->lastpoint > MFS_HOOK_POINT_POSTCHECK) {
	mfslog_err(_("Packages cannot be added after at this point "
			     "of process. Cannot add: %s\n"), ename);
	goto error;
    }

    if (!spec->packages) {
        // This should be a first package
        // Spec doesn't have defined any packages - nothing to do
        // This is an artificial limitation
        mfslog_err(_("No main package exist. Cannot add: %s\n"), ename);
	goto error;
    }

    if (flags & MFS_PACKAGE_FLAG_SUBNAME)
        flag = PART_SUBNAME;

    if (!lookupPackage(spec, ename, flag, NULL)) {
        mfslog_err(_("Package already exists: %s\n"), ename);
	goto error;
    }

    if (flag == PART_SUBNAME) {
        rasprintf(&fullname, "%s-%s",
                headerGetString(spec->packages->header, RPMTAG_NAME), ename);
    } else
        fullname = mstrdup(ename);

    mfslog_info("Adding new subpackage \"%s\"\n", fullname);

    pkg = newPackage(fullname, spec->pool, &spec->packages);

    headerPutString(pkg->header, RPMTAG_NAME, fullname);

    esummary = rpmExpand(summary, NULL);
    addLangTag(spec, pkg->header, RPMTAG_SUMMARY, esummary, RPMBUILD_DEFAULT_LANG);

    pkg->fileList = argvNew();

    mfs_pkg = xcalloc(1, sizeof(*mfs_pkg));
    mfs_pkg->pkg = pkg;
    mfs_pkg->fullname = fullname;
    mfs_pkg->spec = spec;

error:
    free(ename);
    free(esummary);
    return mfs_pkg;
}

Header mfsPackageGetHeader(MfsPackage pkg)
{
    return pkg->pkg->header;
}

const char *mfsPackageName(MfsPackage pkg)
{
    if (!pkg) return NULL;
    return pkg->fullname;
}

const rpmTagVal * mfsPackageTags(void) {
#define PREAMBLELIST_SIZE   (sizeof(preambleList) / sizeof(preambleList[0]))
    static rpmTagVal array[PREAMBLELIST_SIZE];
    for (int x = 0; x < PREAMBLELIST_SIZE; x++)
	array[x] = preambleList[x].tag;
    return array;
}

// This function mimics the findPreambleTag()
rpmRC mfsPackageSetTag(MfsPackage pkg,
		       rpmTagVal tag,
		       const char *value,
		       const char *opt)
{
    rpmRC rc;
    const char *macro;
    char *evalue; // Expanded value
    PreambleRec p;

    if (!value) {
	mfslog_err(_("No value specified for tag %d\n"), tag);
	return RPMRC_FAIL;
    }

    // Find tag in the preambleList[]
    for (p = preambleList; p->token; p++)
	if (p->tag == tag)
	    break;

    if (!p || !p->token) {
	mfslog_err(_("Unknown/Unsupported tag (%d)\n"), tag);
	return RPMRC_FAIL;
    }

    macro = p->token;

    if (p->deprecated)
	mfslog_warning(_("Tag %d: %s is deprecated\n"), tag, macro);

    if (p->type == 0) {
	if (opt && *opt)
	    mfslog_warning(_("Tag %d: %s doesn't support additional "
		  "info \"%s\""), tag, macro, opt);
	opt = "";
    } else if (p->type == 1) {
	// This tag supports a language specification
	// (e.g. "Summary(cs): Summary in Czech")
	if (!opt || !*opt)
	    opt = RPMBUILD_DEFAULT_LANG;
    } else if (p->type == 2) {
	// This tag supports an additional info
	// (e.g. "Requires(pre): foo")
	// See: installScriptBits array for supported values
	if (!opt)
	    opt = "";
    } else if (p->type == 3) {
	// This tag can be numbered e.g. "Patch0"
	if (!opt)
	    opt = "";
    }

    evalue = rpmExpand(value, NULL);
    mfslog_info("Setting tag %s: \"%s\" (%s) to %s\n",
		rpmTagGetName(tag), evalue, opt ? opt : "NULL", pkg->fullname);

    rc = applyPreambleTag(pkg->spec, pkg->pkg, tag, macro, opt, evalue);
    free(evalue);

    return rc;
}

char *mfsPackageGetDescription(MfsPackage pkg)
{
    assert(pkg);
    return mstrdup(headerGetString(pkg->pkg->header, RPMTAG_DESCRIPTION));
}

rpmRC mfsPackageSetDescription(MfsPackage pkg, const char *description, const char *lang)
{
    assert(pkg && description);
    rpmRC rc = RPMRC_OK;
    StringBuf sb = newStringBuf();
    char *edescription = rpmExpand(description, NULL);

    if (!lang)
	lang = RPMBUILD_DEFAULT_LANG;

    appendStringBuf(sb, edescription);
    free(edescription);
    stripTrailingBlanksStringBuf(sb);

    if (addLangTag(pkg->spec, pkg->pkg->header,
		   RPMTAG_DESCRIPTION, getStringBuf(sb), lang)) {
	rc = RPMRC_FAIL;
    }

    freeStringBuf(sb);
    return rc;
}

MfsScript mfsPackageGetScript(MfsPackage pkg, MfsScriptType type)
{
    assert(pkg);
    assert(pkg->pkg);
    assert(pkg->pkg->header);

    Header hdr = pkg->pkg->header;
    headerGetFlags hdrflags = HEADERGET_MINMEM | HEADERGET_EXT;

    MfsScript script = NULL;
    rpmtd tagdata = NULL;
    char *prog = NULL;
    char *code = NULL;
    char *file = NULL;
    rpmscriptFlags flags = RPMSCRIPT_FLAG_NONE;

    if (type >= MFS_SCRIPT_SENTINEL)
	return NULL;  // Programmer error

    // Find the script related values
    MfsScriptRec rec;
    for (rec = scriptMapping; rec->scripttype != MFS_SCRIPT_SENTINEL; rec++)
	if (rec->scripttype == type)
	    break;

    if (rec->scripttype == MFS_SCRIPT_SENTINEL)
	return NULL;  // Programmer error

    // Try to load curret script from the header
    tagdata = rpmtdNew();

    // Prog
    if (headerGet(hdr, rec->progtag, tagdata, hdrflags)) {
	// Ancient rpm uses String
	if (rpmtdType(tagdata) == RPM_STRING_TYPE) {
	    prog = mstrdup(rpmtdGetString(tagdata));
	} else if (rpmtdType(tagdata) == RPM_STRING_ARRAY_TYPE) {
	    const char *chunk = NULL;
	    rpmtdInit(tagdata);
	    while((chunk = rpmtdNextString(tagdata)) != NULL) {
		prog = rstrcat(&prog, " ");
		prog = rstrcat(&prog, chunk);
	    }
	    if (prog && *prog == ' ') prog++;
	} else {
	    mfslog_err(_("Unexpected type of data for tag %d\n"), rec->progtag);
	    goto get_script_end;
	}

	rpmtdFreeData(tagdata);
    }

    // Code
    if (headerGet(hdr, rec->tag, tagdata, hdrflags)) {
	code = mstrdup(rpmtdGetString(tagdata));
	rpmtdFreeData(tagdata);
    }

    // Flags
    if (headerGet(hdr, rec->flagstag, tagdata, hdrflags)) {
	uint32_t *p_flags = rpmtdGetUint32(tagdata);
	if (p_flags)
	    flags = *p_flags;
	rpmtdFreeData(tagdata);
    }

    // File
    file = mstrdup(*((char **) ((size_t)pkg->pkg + (size_t)rec->fileoffset)));

    // Prepare newMfsScript
    script = xcalloc(1, sizeof(*script));
    script->code = code;
    script->prog = prog;
    script->file = file;
    script->flags = flags;

get_script_end:

    rpmtdFreeData(tagdata);
    rpmtdFree(tagdata);

    return script;
}

rpmRC mfsPackageSetScript(MfsPackage pkg, MfsScript script, MfsScriptType type)
{
    assert(pkg);
    assert(pkg->pkg);
    assert(pkg->pkg->header);
    assert(script);

    Header hdr = pkg->pkg->header;
    MfsScriptRec rec;
    int rc = RPMRC_OK;
    int popt_rc;
    int with_lua = 0;
    int with_script_interpreter_args = 0;
    char *code = script->code ? script->code : "";
    int progArgc;
    const char **progArgv = NULL;

    if (type >= MFS_SCRIPT_SENTINEL)
	return RPMRC_FAIL;  // Programmer error

    // Find the script related values
    for (rec = scriptMapping; rec->scripttype != MFS_SCRIPT_SENTINEL; rec++)
	if (rec->scripttype == type)
	    break;

    if (rec->scripttype == MFS_SCRIPT_SENTINEL)
	return RPMRC_FAIL;  // Programmer error

    // Sanity checks first

    if (!script->prog || *script->prog == '\0') {
	mfslog_err(_("script program must be set\n"));
	return RPMRC_FAIL;
    }

    if (*script->prog == '<') {
	// Internal script specified
	if (script->prog[strlen(script->prog)-1] != '>') {
	    mfslog_err(_("internal script must end with \'>\': %s\n"),
		    script->prog);
	    return RPMRC_FAIL;
	}
#ifdef WITH_LUA
	if (!strcmp(script->prog, "<lua>")) {
	    rpmlua lua = NULL;
	    if (rpmluaCheckScript(lua, code, NULL) != RPMRC_OK)
		return RPMRC_FAIL;
	    with_lua = 1;
	} else
#endif
	{
	    mfslog_err(_("unsupported internal script: %s\n"), script->prog);
	    return RPMRC_FAIL;
	}
    } else if (*script->prog != '/') {
	// External script program must starts with '/'
	mfslog_err(_("script program must begin with \'/\': %s\n"),
		script->prog);
	return RPMRC_FAIL;
    }

    // Parse the prog argument
    if ((popt_rc = poptParseArgvString(script->prog, &progArgc, &progArgv))) {
	mfslog_err(_("error parsing %s: %s\n"),
	       script->prog, poptStrerror(popt_rc));
	rc = RPMRC_FAIL;
	goto set_script_end;
    }

    // Delete the old one
    if (mfsPackageDeleteScript(pkg, type) != RPMRC_OK) {
	rc = RPMRC_FAIL;
	goto set_script_end;
    }

    // Insert the script to the package

    mfslog_info("Setting script %s to %s:\n", enumScriptTypeValToStr(type), pkg->fullname);
    mfslog_info(" - Script prog:  %s\n", script->prog ? script->prog : "");
    mfslog_info(" - Script file:  %s\n", script->file ? script->file : "");
    mfslog_info(" - Script flags: %d\n", script->flags);
    mfslog_info(" - Script code:\n%s\n", script->code ? script->code : "");

    struct rpmtd_s td;
    rpmtdReset(&td);
    td.tag = rec->progtag;
    td.count = progArgc;
    if (progArgc == 1) {
	td.data = (void *) *progArgv;
	td.type = RPM_STRING_TYPE;
    } else {
	td.data = progArgv;
	td.type = RPM_STRING_ARRAY_TYPE;
	with_script_interpreter_args = 1;
    }
    headerPut(hdr, &td, HEADERPUT_DEFAULT);

    if (*code != '\0')
	headerPutString(hdr, rec->tag, code);

    if (script->flags)
	headerPutUint32(hdr, rec->flagstag, &script->flags, 1);

    if (script->file) {
	char **file = (char **) ((size_t)pkg->pkg + (size_t)rec->fileoffset);
	*file = mstrdup(script->file);
    }

    // Add the prog as a require
    if (*script->prog == '/')
	addReqProv(pkg->pkg,
		   RPMTAG_REQUIRENAME,
		   progArgv[0],
		   NULL,
		   (rec->senseflags | RPMSENSE_INTERP),
		   0);

    // Set needed features
    if (with_lua)
	rpmlibNeedsFeature(pkg->pkg, "BuiltinLuaScripts", "4.2.2-1");

    if (script->flags)
	rpmlibNeedsFeature(pkg->pkg, "ScriptletExpansion", "4.9.0-1");

    if (with_script_interpreter_args)
	rpmlibNeedsFeature(pkg->pkg, "ScriptletInterpreterArgs", "4.0.3-1");

set_script_end:

    free(progArgv);

    return rc;
}

rpmRC mfsPackageDeleteScript(MfsPackage pkg, MfsScriptType type)
{
    assert(pkg);
    assert(pkg->pkg);
    assert(pkg->pkg->header);

    Header hdr = pkg->pkg->header;
    char **file = NULL;

    if (type >= MFS_SCRIPT_SENTINEL)
	return RPMRC_FAIL;  // Programmer error

    // Find the script related values
    MfsScriptRec rec;
    for (rec = scriptMapping; rec->scripttype != MFS_SCRIPT_SENTINEL; rec++)
	if (rec->scripttype == type)
	    break;

    if (rec->scripttype == MFS_SCRIPT_SENTINEL)
	return RPMRC_FAIL;  // Programmer error

    mfslog_info("Removing script %s from %s:\n", enumScriptTypeValToStr(type), pkg->fullname);

    // Remove all script related stuff
    headerDel(hdr, rec->tag);	    // Code from header
    headerDel(hdr, rec->progtag);   // Prog from header
    headerDel(hdr, rec->flagstag);  // Flags
    file = (char **) ((size_t)pkg->pkg + (size_t)rec->fileoffset);
    free(*file);
    *file = NULL;

    return RPMRC_OK;
}

MfsTriggers mfsPackageGetTriggers(MfsPackage pkg)
{
    assert(pkg);

    MfsTriggers triggers = NULL;
    MfsDeps deps = mfsPackageGetDeps(pkg, MFS_DEP_TYPE_TRIGGERS);
    MfsTrigger *last;
    int num_of_deps;

    if (!deps)
	return NULL;

    num_of_deps = mfsDepsCount(deps);

    // Prepare trigger records
    triggers = xcalloc(1, sizeof(*triggers));
    last = &triggers->entries;
    for (struct TriggerFileEntry *e = pkg->pkg->triggerFiles; e; e = e->next) {
	int index = e->index;

	MfsDeps trigger_deps;
	MfsTrigger trigger = mfsTriggerNew();

	trigger->script = mfsScriptNew();
	mfsScriptSetCode(trigger->script, e->script);
	mfsScriptSetProg(trigger->script, e->prog);
	mfsScriptSetFile(trigger->script, e->fileName);
	mfsScriptSetFlags(trigger->script, e->flags);

	// Get deps related to the trigger
	trigger_deps = xcalloc(1, sizeof(*trigger_deps));
	for (int di=0; di < num_of_deps; di++) {
	    MfsDep dep = mfsDepsGetEntry(deps, di);
	    if (index != mfsDepGetIndex(dep))
		continue;
	    mfsDepsAppend(trigger_deps, mfsDepCopy(dep));
	    // Set trigger type
	    if (mfsDepGetFlags(dep) & RPMSENSE_TRIGGERPREIN)
		trigger->type = MFS_TRIGGER_PREIN;
	    else if (mfsDepGetFlags(dep) & RPMSENSE_TRIGGERUN)
		trigger->type = MFS_TRIGGER_UN;
	    else if (mfsDepGetFlags(dep) & RPMSENSE_TRIGGERPOSTUN)
		trigger->type = MFS_TRIGGER_POSTUN;
	    else
		trigger->type = MFS_TRIGGER_IN;
	}
	trigger->deps = trigger_deps;

	*last = trigger;
	last = &trigger->next;
    }

    mfsDepsFree(deps);
    return triggers;
}

static int addTriggerIndex(MfsPackage pkg, MfsTrigger trigger)
{
    struct TriggerFileEntry *tfe;
    struct TriggerFileEntry *list = pkg->pkg->triggerFiles;
    struct TriggerFileEntry *last = NULL;
    int index = 0;
    MfsScript script = trigger->script; // Shortcut

    while (list) {
	last = list;
	list = list->next;
    }

    if (last)
	index = last->index + 1;

    tfe = xcalloc(1, sizeof(*tfe));

    tfe->fileName = (script->file) ? xstrdup(script->file) : NULL;
    tfe->script = (script->code && *(script->code) != '\0') ? xstrdup(script->code) : NULL;
    tfe->prog = mstrdup(script->prog);
    tfe->flags = script->flags;
    tfe->index = index;
    tfe->next = NULL;

    if (last)
	last->next = tfe;
    else
	pkg->pkg->triggerFiles = tfe;

    return index;

}

rpmRC mfsPackageSetTriggers(MfsPackage pkg, MfsTriggers triggers)
{
    assert(pkg);
    assert(triggers);

    int rc = RPMRC_OK;
    MfsDeps alldeps = mfsDepsNew();

    // Free the old triggerFiles in the pkg
    for (struct TriggerFileEntry *e = pkg->pkg->triggerFiles; e;) {
	struct TriggerFileEntry *next = e->next;
	free(e->fileName);
	free(e->script);
	free(e->prog);
	free(e);
	e = next;
    }
    pkg->pkg->triggerFiles = NULL;

    mfslog_info("Setting new triggers for %s:\n", pkg->fullname);

    // Gen the new triggerFiles
    for (MfsTrigger e=triggers->entries; e; e=e->next) {
	MfsDeps deps;
	MfsScript script = e->script;
	int index, num_of_deps;

	// Gen a new entry to triggerFiles
	if (!e->script->prog)
	    e->script->prog = mstrdup("/bin/sh");

	index = addTriggerIndex(pkg, e);

	mfslog_info("%d) Trigger:\n", index);
	mfslog_info(" - Script prog:  %s\n", script->prog ? script->prog : "");
	mfslog_info(" - Script file:  %s\n", script->file ? script->file : "");
	mfslog_info(" - Script flags: %d\n", script->flags);
	mfslog_info(" - Script code:\n%s\n", script->code ? script->code : "");

	// Append trigger's deps to the global trigger deps
	deps = mfsTriggerGetDeps(e);
	num_of_deps = mfsDepsCount(deps);
	for (int x=0; x < num_of_deps; x++) {
	    MfsDep dep = mfsDepsGetEntry(deps, x);
	    dep = mfsDepCopy(dep);
	    rpmsenseFlags flags = mfsDepGetFlags(dep);

	    // Set proper trigger flags
	    flags &= ~RPMSENSE_TRIGGER;
	    if (e->type == MFS_TRIGGER_PREIN)
		flags |= RPMSENSE_TRIGGERPREIN;
	    else if (e->type == MFS_TRIGGER_UN)
		flags |= RPMSENSE_TRIGGERUN;
	    else if (e->type == MFS_TRIGGER_POSTUN)
		flags |= RPMSENSE_TRIGGERPOSTUN;
	    else
		flags |= RPMSENSE_TRIGGERIN;
	    mfsDepSetFlags(dep, flags);

	    // Set proper trigger index
	    mfsDepSetIndex(dep, index);

	    mfsDepsAppend(alldeps, dep);
	}

	mfsDepsFree(deps);
    }

    rc = mfsPackageSetDeps(pkg, alldeps, MFS_DEP_TYPE_TRIGGERS);
    mfsDepsFree(alldeps);
    return rc;
}

MfsChangelogs mfsPackageGetChangelogs(MfsPackage pkg)
{
    assert(pkg);

    headerGetFlags flags = HEADERGET_MINMEM | HEADERGET_EXT;
    Header hdr = pkg->pkg->header;

    MfsChangelogs changelogs = xcalloc(1, sizeof(*changelogs));
    MfsChangelog *last = &changelogs->entries;

    rpmtd changelogtimes = rpmtdNew();
    rpmtd changelognames = rpmtdNew();
    rpmtd changelogtexts = rpmtdNew();

    if (!headerGet(hdr, RPMTAG_CHANGELOGTIME, changelogtimes, flags) ||
        !headerGet(hdr, RPMTAG_CHANGELOGNAME, changelognames, flags) ||
        !headerGet(hdr, RPMTAG_CHANGELOGTEXT, changelogtexts, flags))
    {
	goto get_changelog_end;
    }

    rpmtdInit(changelogtimes);
    rpmtdInit(changelognames);
    rpmtdInit(changelogtexts);

    while ((rpmtdNext(changelogtimes) != -1) &&
	   (rpmtdNext(changelognames) != -1) &&
	   (rpmtdNext(changelogtexts) != -1))
    {
	MfsChangelog entry;

	uint32_t *time = rpmtdGetUint32(changelogtimes);
	const char *name = rpmtdGetString(changelognames);
	const char *text = rpmtdGetString(changelogtexts);

	if (!time || !name || !text) {
	    mfslog_err(_("Cannot retrieve changelog entries\n"));
	    goto get_changelog_end;
	}

	entry = xcalloc(1, sizeof(*entry));
	entry->time = (rpm_time_t) (*time);
	entry->name = mstrdup(name);
	entry->text = mstrdup(text);
	// Append the entry to the end of the list
	*last = entry;
	last = &entry->next;
    }

get_changelog_end:

    rpmtdFreeData(changelogtimes);
    rpmtdFreeData(changelognames);
    rpmtdFreeData(changelogtexts);

    rpmtdFree(changelogtimes);
    rpmtdFree(changelognames);
    rpmtdFree(changelogtexts);

    return changelogs;
}

rpmRC mfsPackageSetChangelogs(MfsPackage pkg, MfsChangelogs changelogs)
{
    // Here can be more sanity checks (e.g. right order
    // of changelogs, etc.) if appropriate

    assert(pkg);
    assert(pkg->pkg);
    assert(changelogs);

    Header hdr = pkg->pkg->header;

    headerDel(hdr, RPMTAG_CHANGELOGTIME);
    headerDel(hdr, RPMTAG_CHANGELOGNAME);
    headerDel(hdr, RPMTAG_CHANGELOGTEXT);

    mfslog_info("Setting new changelog for %s\n", pkg->fullname);

    for (MfsChangelog e = changelogs->entries; e; e = e->next) {
	if (!e->name || !e->text) {
	    mfslog_warning(_("Invalid changelog entry skipped\n"));
	    continue;
	}
	addChangelogEntry(hdr, e->time, e->name, e->text);
    }

    return RPMRC_OK;
}

MfsDeps mfsPackageGetDeps(MfsPackage pkg, MfsDepType deptype)
{
    assert(pkg);

    if (deptype >= MFS_DEP_TYPE_SENTINEL)
	return NULL;  // Programmer error

    headerGetFlags flags = HEADERGET_MINMEM | HEADERGET_EXT;
    Header hdr = pkg->pkg->header;

    rpmTagVal nametag;
    rpmTagVal versiontag;
    rpmTagVal flagstag;
    rpmTagVal indextag;

    // Find the tag values
    MfsDepMapRec rec;
    for (rec = depTypeMapping; rec->deptype != MFS_DEP_TYPE_SENTINEL; rec++)
	if (rec->deptype == deptype)
	    break;

    if (rec->deptype == MFS_DEP_TYPE_SENTINEL)
	return NULL;  // Programmer error

    nametag = rec->nametag;
    versiontag = rec->versiontag;
    flagstag = rec->flagstag;
    indextag = rec->indextag;

    MfsDeps deps = mfsDepsNew();
    MfsDep *last = &deps->entries;

    rpmtd depnames = rpmtdNew();
    rpmtd depversions = rpmtdNew();
    rpmtd depflags = rpmtdNew();
    rpmtd depindexes = indextag ? rpmtdNew() : NULL;

    if (!headerGet(hdr, nametag, depnames, flags) ||
        !headerGet(hdr, versiontag, depversions, flags) ||
        !headerGet(hdr, flagstag, depflags, flags) ||
	(indextag && !headerGet(hdr, indextag, depindexes, flags)))
    {
	goto get_deps_end;
    }

    rpmtdInit(depnames);
    rpmtdInit(depversions);
    rpmtdInit(depflags);
    if (indextag) rpmtdInit(depindexes);

    while ((rpmtdNext(depnames) != -1) &&
	   (rpmtdNext(depversions) != -1) &&
	   (rpmtdNext(depflags) != -1))
    {
	MfsDep dep;

	const char *name = rpmtdGetString(depnames);
	const char *version = rpmtdGetString(depversions);
	uint32_t *flags = rpmtdGetUint32(depflags);
	uint32_t *index = NULL;

	if (indextag) {
	    rpmtdNext(depindexes);
	    index = rpmtdGetUint32(depindexes);
	}

	if (!name || !version || !flags || (indextag && !index)) {
	    mfslog_err(_("Cannot retrieve dependency\n"));
	    goto get_deps_end;
	}

	dep = xcalloc(1, sizeof(*dep));
	dep->name = mstrdup(name);
	dep->version = mstrdup(version);
	dep->flags = *flags;
	dep->index = index ? *index : 0;
	// Append the entry to the end of the list
	*last = dep;
	last = &dep->next;
    }

get_deps_end:

    rpmtdFreeData(depnames);
    rpmtdFreeData(depversions);
    rpmtdFreeData(depflags);

    if (indextag) {
	rpmtdFreeData(depindexes);
	rpmtdFree(depindexes);
    }

    rpmtdFree(depnames);
    rpmtdFree(depversions);
    rpmtdFree(depflags);

    return deps;
}

rpmRC mfsPackageSetDeps(MfsPackage pkg, MfsDeps deps, MfsDepType deptype)
{
    assert(pkg);
    assert(pkg->pkg);
    assert(deps);

    rpmRC rc = RPMRC_OK;
    int ret = 0;
    Header hdr = pkg->pkg->header;

    if (deptype >= MFS_DEP_TYPE_SENTINEL)
	return RPMRC_FAIL;

    rpmTagVal nametag;
    rpmTagVal versiontag;
    rpmTagVal flagstag;
    rpmTagVal indextag;
    rpmds *ds;

    // Find the tag related values
    MfsDepMapRec rec;
    for (rec = depTypeMapping; rec->deptype != MFS_DEP_TYPE_SENTINEL; rec++)
	if (rec->deptype == deptype)
	    break;

    if (rec->deptype == MFS_DEP_TYPE_SENTINEL)
	return RPMRC_FAIL;  // Programmer error

    // Set the tag related values
    nametag = rec->nametag;
    versiontag = rec->versiontag;
    flagstag = rec->flagstag;
    indextag = rec->indextag;
    ds = ((rpmds *) ((size_t)pkg->pkg + (size_t)rec->dsoffset));

    // Remove old dependencies
    headerDel(hdr, nametag);
    headerDel(hdr, versiontag);
    headerDel(hdr, flagstag);
    if (indextag) headerDel(hdr, indextag);
    rpmdsFree(*ds);
    *ds = NULL;

    mfslog_info("Setting new \"%s\" dependencies for %s\n",
		enumDepTypeToStr(deptype), pkg->fullname);

    // Set the new dependencies
    for (MfsDep e = deps->entries; e; e = e->next) {
	char *flags_str;

	if (!e->name) {
	    mfslog_err(_("invalid dependency - Missing name\n"));
	    rc = RPMRC_FAIL;
	    goto pkg_set_dep_error;
	}

	if (e->flags & RPMSENSE_SENSEMASK && !e->version) {
	    mfslog_err(_("invalid dependency - Version required: "
			"%s (%d)\n"), e->name, e->flags);
	    rc = RPMRC_FAIL;
	    goto pkg_set_dep_error;
	}

	if (!(e->flags & RPMSENSE_SENSEMASK) && e->version && *e->version != '\0') {
	    mfslog_err(_("invalid dependency - Redundant version: "
			"%s %s (%d)\n"), e->name, e->version, e->flags);
	    rc = RPMRC_FAIL;
	    goto pkg_set_dep_error;
	}

	if (e->index && !indextag) {
	    mfslog_warning(_("index attribute has no effect: %s\n"),
			e->name);
	}

	flags_str = mfsDepGetFlagsStr(e);
	mfslog_info(" - %s %s %d (%s)\n", e->name, e->version ? e->version : "",
		    e->flags, flags_str);
	free(flags_str);

	ret = addReqProv(pkg->pkg,
			 nametag, e->name,
			 e->version ? e->version : "",
			 e->flags,
			 e->index);
	if (ret != 0) {
	    rc = RPMRC_FAIL;
	    goto pkg_set_dep_error;
	}
    }

pkg_set_dep_error:

    return rc;
}

MfsFileLines mfsPackageGetFileLines(MfsPackage pkg)
{
    MfsFileLines fl = xcalloc(1, sizeof(*fl));
    fl->filelines = argvCopy(pkg->pkg->fileList);
    return fl;
}

rpmRC mfsPackageSetFileLines(MfsPackage pkg, MfsFileLines flines)
{
    argvFree(pkg->pkg->fileList);
    mfslog_info("Setting new files to %s\n", pkg->fullname);
    for (int x=0; flines->filelines && flines->filelines[x]; x++)
	mfslog_info(" - %s\n", flines->filelines[x]);
    pkg->pkg->fileList = argvCopy(flines->filelines);
    return RPMRC_OK;
}

MfsFileFiles mfsPackageGetFileFiles(MfsPackage pkg)
{
    MfsFileFiles fl = xcalloc(1, sizeof(*fl));
    fl->filefiles = argvCopy(pkg->pkg->fileFile);
    return fl;
}

rpmRC mfsPackageSetFileFiles(MfsPackage pkg, MfsFileFiles ffiles)
{
    argvFree(pkg->pkg->fileFile);
    mfslog_info("Setting new filelists to %s\n", pkg->fullname);
    for (int x=0; ffiles->filefiles && ffiles->filefiles[x]; x++)
	mfslog_info(" - %s\n", ffiles->filefiles[x]);
    pkg->pkg->fileFile = argvCopy(ffiles->filefiles);
    return RPMRC_OK;
}

MfsPolicies mfsPackageGetPolicies(MfsPackage pkg)
{
    MfsPolicies policies = xcalloc(1, sizeof(*policies));
    policies->policies = argvCopy(pkg->pkg->policyList);
    return policies;
}

rpmRC mfsPackageSetPolicies(MfsPackage pkg, MfsPolicies policies)
{
    argvFree(pkg->pkg->policyList);
    mfslog_info("Setting new policies to %s\n", pkg->fullname);
    for (int x=0; policies->policies && policies->policies[x]; x++)
	mfslog_info(" - %s\n", policies->policies[x]);
    pkg->pkg->policyList = argvCopy(policies->policies);
    return RPMRC_OK;
}

MfsFiles mfsPackageGetFiles(MfsPackage mfspkg)
{
    Package pkg = mfspkg->pkg; // Shortcut
    MfsFiles files = xcalloc(1, sizeof(*files));
    MfsFile *last = &files->files;
    rpmfc fc = rpmfcCreate(mfspkg->spec->buildRoot, 0);

    if (!pkg->fl)
	return files;

    files->pkg = pkg;

    // Transform pkg->fl to list of MfsFile objects
    for (int x=0; x < pkg->fl->files.used; x++) {
	MfsFile mfsfile;
	FileListRec copy;
	FileListRec e = &pkg->fl->files.recs[x];

	// XXX: e->uname and e->gname in pkg are just ids
	// to string pool. Convert them to strings for the copying
	rpmsid uname = e->uname;
	rpmsid gname = e->gname;
	e->uname = rpmstrPoolStr(pkg->fl->pool, e->uname);
	e->gname = rpmstrPoolStr(pkg->fl->pool, e->gname);
	// XXX: End
	copy = mfsDupFileListRec(e);
	// XXX: Restore ids
	e->uname = uname;
	e->gname = gname;
	// XXX: End

	mfsfile = xcalloc(1, sizeof(*mfsfile));
	mfsfile->flr = copy;
	mfsfile->diskpath = copy->diskPath;
	mfsfile->spec = mfspkg->spec;
	mfsfile->include_in_original = 1;
	mfsfile->classified_file = rpmfcClassifyFile(fc, copy->diskPath, copy->fl_mode);

	// Append the new mfsfile
	*last = mfsfile;
	last = &mfsfile->next;
    }

    rpmfcFree(fc);

    return files;
}

// Scripts

MfsScript mfsScriptNew(void)
{
    MfsScript script = xcalloc(1, sizeof(*script));
    script->prog = mstrdup("/bin/sh");
    return script;
}

MfsScript mfsScriptCopy(MfsScript script)
{
    if (!script) return NULL;
    MfsScript new_script = mfsScriptNew();
    mfsScriptSetCode(new_script, script->code);
    mfsScriptSetProg(new_script, script->prog);
    mfsScriptSetFile(new_script, script->file);
    mfsScriptSetFlags(new_script, script->flags);
    return new_script;
}

void mfsScriptFree(MfsScript script)
{
    if (!script)
	return;
    free(script->code);
    free(script->prog);
    free(script->file);
    free(script);
}

char *mfsScriptGetCode(MfsScript script)
{
    assert(script);
    return mstrdup(script->code);
}

char *mfsScriptGetProg(MfsScript script)
{
    assert(script);
    return mstrdup(script->prog);
}

char *mfsScriptGetFile(MfsScript script)
{
    assert(script);
    return mstrdup(script->file);
}

MfsScriptFlags mfsScriptGetFlags(MfsScript script)
{
    assert(script);
    return script->flags;
}

rpmRC mfsScriptSetCode(MfsScript script, const char *code)
{
    assert(script);
    free(script->code);
    script->code = mstrdup(code);
    return RPMRC_OK;
}

rpmRC mfsScriptSetProg(MfsScript script, const char *prog)
{
    assert(script);
    free(script->prog);
    script->prog = mstrdup(prog);
    return RPMRC_OK;
}

rpmRC mfsScriptSetFile(MfsScript script, const char *fn)
{
    assert(script);
    free(script->file);
    script->file = mstrdup(fn);
    return RPMRC_OK;
}

rpmRC mfsScriptSetFlags(MfsScript script, MfsScriptFlags flags)
{
    assert(script);
    script->flags = flags;
    return RPMRC_OK;
}

// Triggers

void mfsTriggersFree(MfsTriggers triggers)
{
    if (!triggers)
	return;
    for (MfsTrigger e = triggers->entries; e;) {
	MfsTrigger next = e->next;
	mfsTriggerFree(e);
	e = next;
    }
    free(triggers);
}

int mfsTriggersCount(MfsTriggers triggers)
{
    assert(triggers);
    int x = 0;
    MfsTrigger entry = triggers->entries;
    while (entry) {
	entry = entry->next;
	x++;
    }
    return x;
}

rpmRC mfsTriggersAppend(MfsTriggers triggers, MfsTrigger entry)
{
    return mfsTriggersInsert(triggers, entry, -1);
}

rpmRC mfsTriggersInsert(MfsTriggers triggers, MfsTrigger entry, int index)
{
    assert(triggers);
    assert(entry);

    int x = 0;
    MfsTrigger prev = NULL;
    MfsTrigger cur_entry = triggers->entries;

    if (!entry->script || !entry->deps) {
	mfslog_err(_("Incomplete trigger entry\n"));
	return RPMRC_FAIL;
    }

    if (index == 0 || (index == -1 && !cur_entry)) {
	entry->next = triggers->entries;
        triggers->entries = entry;
	return RPMRC_OK;
    }

    if (!cur_entry)
	goto trigger_entry_insert_error;

    while (1) {
	x++;
	prev = cur_entry;
	cur_entry = cur_entry->next;

	if ((index == x) || (index == -1 && !cur_entry)) {
	    prev->next = entry;
	    entry->next = cur_entry;
	    break;
	}

	if (index != -1 && !cur_entry)
	    goto trigger_entry_insert_error;
    }

    return RPMRC_OK;

trigger_entry_insert_error:

    mfslog_err(_("Trigger entry cannot be inserted to the specified index\n"));
    return RPMRC_FAIL;
}

rpmRC mfsTriggersDelete(MfsTriggers triggers, int index)
{
    assert(triggers);

    int x = 0;
    MfsTrigger prev = NULL;
    MfsTrigger cur_entry = triggers->entries;

    if (!cur_entry)
	goto trigger_entry_delete_error;

    if (index == 0 || (index == -1 && !cur_entry->next)) {
        triggers->entries = cur_entry->next;
	mfsTriggerFree(cur_entry);
	return RPMRC_OK;
    }

    while (1) {
	x++;
	prev = cur_entry;
	cur_entry = cur_entry->next;

	if (!cur_entry)
	    goto trigger_entry_delete_error;

	if ((index == x) || (index == -1 && !cur_entry->next)) {
	    prev->next = cur_entry->next;
	    mfsTriggerFree(cur_entry);
	    break;
	}
    }

    return RPMRC_OK;

trigger_entry_delete_error:

    mfslog_err(_("Trigger entry doesn't exist\n"));
    return RPMRC_FAIL;

}

const MfsTrigger mfsTriggersGetEntry(MfsTriggers triggers, int index)
{
    assert(triggers);
    int x = 0;
    MfsTrigger entry = triggers->entries;
    while (entry) {
	if (x == index)
	    break;
	if (index == -1 && !entry->next)
	    break;
	entry = entry->next;
	x++;
    }

    return entry;
}

MfsTrigger mfsTriggerNew(void)
{
    MfsTrigger entry = xcalloc(1, sizeof(*entry));
    return entry;
}

MfsTrigger mfsTriggerCopy(MfsTrigger trigger)
{
    MfsTrigger new_trigger = mfsTriggerNew();
    new_trigger->script = mfsScriptCopy(trigger->script);
    new_trigger->deps = mfsDepsCopy(trigger->deps);
    return new_trigger;
}

void mfsTriggerFree(MfsTrigger trigger)
{
    if (!trigger)
	return;
    mfsScriptFree(trigger->script);
    mfsDepsFree(trigger->deps);
    free(trigger);
}

MfsTriggerType mfsTriggerGetType(MfsTrigger trigger)
{
    assert(trigger);
    return trigger->type;
}

rpmRC mfsTriggerSetType(MfsTrigger trigger, MfsScriptType type)
{
    assert(trigger);
    trigger->type = type;
    return RPMRC_OK;
}

MfsScript mfsTriggerGetScript(MfsTrigger trigger)
{
    assert(trigger);
    return mfsScriptCopy(trigger->script);
}

rpmRC mfsTriggerSetScript(MfsTrigger trigger, MfsScript script)
{
    assert(trigger);
    assert(script);

    mfsScriptFree(trigger->script);
    trigger->script = script;
    return RPMRC_OK;
}

MfsDeps mfsTriggerGetDeps(MfsTrigger trigger)
{
    assert(trigger);
    return mfsDepsCopy(trigger->deps);
}

rpmRC mfsTriggerSetDeps(MfsTrigger trigger, MfsDeps deps)
{
    assert(trigger);
    assert(deps);

    mfsDepsFree(trigger->deps);
    trigger->deps = deps;
    return RPMRC_OK;
}

// Changelogs

void mfsChangelogsFree(MfsChangelogs changelogs)
{
    if (!changelogs)
	return;
    for (MfsChangelog e = changelogs->entries; e;) {
	MfsChangelog next = e->next;
	mfsChangelogFree(e);
	e = next;
    }
    free(changelogs);
}

int mfsChangelogsCount(MfsChangelogs changelogs)
{
    assert(changelogs);
    int x = 0;
    MfsChangelog entry = changelogs->entries;
    while (entry) {
	entry = entry->next;
	x++;
    }
    return x;
}

rpmRC mfsChangelogsAppend(MfsChangelogs changelogs, MfsChangelog entry)
{
    return mfsChangelogsInsert(changelogs, entry, -1);
}

rpmRC mfsChangelogsInsert(MfsChangelogs changelogs,
			 MfsChangelog entry,
			 int index)
{
    assert(changelogs);
    assert(entry);

    int x = 0;
    MfsChangelog prev = NULL;
    MfsChangelog cur_entry = changelogs->entries;

    if (!entry->name || !entry->text) {
	mfslog_err(_("Incomplete changelog entry\n"));
	return RPMRC_FAIL;
    }

    if (index == 0 || (index == -1 && !cur_entry)) {
	entry->next = changelogs->entries;
        changelogs->entries = entry;
	return RPMRC_OK;
    }

    if (!cur_entry)
	goto changelog_entry_insert_error;

    while (1) {
	x++;
	prev = cur_entry;
	cur_entry = cur_entry->next;

	if ((index == x) || (index == -1 && !cur_entry)) {
	    prev->next = entry;
	    entry->next = cur_entry;
	    break;
	}

	if (index != -1 && !cur_entry)
	    goto changelog_entry_insert_error;
    }

    return RPMRC_OK;

changelog_entry_insert_error:

    mfslog_err(_("Changelog entry cannot be inserted to the specified index\n"));
    return RPMRC_FAIL;
}

rpmRC mfsChangelogsDelete(MfsChangelogs changelogs, int index)
{
    assert(changelogs);

    int x = 0;
    MfsChangelog prev = NULL;
    MfsChangelog cur_entry = changelogs->entries;

    if (!cur_entry)
	goto changelog_entry_delete_error;

    if (index == 0 || (index == -1 && !cur_entry->next)) {
        changelogs->entries = cur_entry->next;
	mfsChangelogFree(cur_entry);
	return RPMRC_OK;
    }

    while (1) {
	x++;
	prev = cur_entry;
	cur_entry = cur_entry->next;

	if (!cur_entry)
	    goto changelog_entry_delete_error;

	if ((index == x) || (index == -1 && !cur_entry->next)) {
	    prev->next = cur_entry->next;
	    mfsChangelogFree(cur_entry);
	    break;
	}
    }

    return RPMRC_OK;

changelog_entry_delete_error:

    mfslog_err(_("Changelog entry doesn't exist\n"));
    return RPMRC_FAIL;
}

const MfsChangelog mfsChangelogsGetEntry(MfsChangelogs changelogs, int index)
{
    assert(changelogs);

    int x = 0;
    MfsChangelog entry = changelogs->entries;
    while (entry) {
	if (x == index)
	    break;
	if (index == -1 && !entry->next)
	    break;
	entry = entry->next;
	x++;
    }

    if (!entry)
	return NULL;

    return entry;
}

MfsChangelog mfsChangelogNew(void)
{
    MfsChangelog entry = xcalloc(1, sizeof(*entry));
    return entry;
}

MfsChangelog mfsChangelogCopy(MfsChangelog entry)
{
    assert(entry);
    MfsChangelog copy = mfsChangelogNew();
    copy->time = entry->time;
    copy->name = mstrdup(entry->name);
    copy->text = mstrdup(entry->text);
    return copy;
}

void mfsChangelogFree(MfsChangelog entry)
{
    if (!entry)
	return;
    free(entry->name);
    free(entry->text);
    free(entry);
}

time_t mfsChangelogGetDate(MfsChangelog entry)
{
    assert(entry);
    return entry->time;
}

char *mfsChangelogGetDateStr(MfsChangelog entry)
{
    assert(entry);
    char buf[TIME_STR_BUF];
    struct tm * timeinfo;
    const time_t time = entry->time;
    timeinfo = localtime(&time);
    // Output of this function depends on locale - but this function
    // is for debbuging purpose only so it doesn't matter
    strftime(buf, TIME_STR_BUF, "%a %b %e %Y", timeinfo);
    return mstrdup(buf);
}

char *mfsChangelogGetName(MfsChangelog entry)
{
    assert(entry);
    return mstrdup(entry->name);
}

char *mfsChangelogGetText(MfsChangelog entry)
{
    assert(entry);
    return mstrdup(entry->text);
}

rpmRC mfsChangelogSetDateStr(MfsChangelog entry, const char *date)
{
    assert(entry);
    assert(date);
    time_t time;
    if (rpmDateToTimet(date, &time) == -1) {
	mfslog_err("Cannot convert \"%s\" to time\n", date);
	return RPMRC_FAIL;
    }
    return mfsChangelogSetDate(entry, time);
}

rpmRC mfsChangelogSetDate(MfsChangelog entry, time_t date)
{
    assert(entry);
    entry->time = date;
    return RPMRC_OK;
}

rpmRC mfsChangelogSetName(MfsChangelog entry, const char *name)
{
    assert(entry);
    free(entry->name);
    entry->name = mstrdup(name);
    return RPMRC_OK;
}

rpmRC mfsChangelogSetText(MfsChangelog entry, const char *text)
{
    assert(entry);
    free(entry->text);
    entry->text = mstrdup(text);
    return RPMRC_OK;
}

// Dependencies

MfsDeps mfsDepsNew(void) {
    MfsDeps deps = calloc(1, sizeof(deps));
    return deps;
}

void mfsDepsFree(MfsDeps deps) {
    if (!deps)
	return;
    for (MfsDep e = deps->entries; e;) {
	MfsDep next = e->next;
	mfsDepFree(e);
	e = next;
    }
    free(deps);
}

MfsDeps mfsDepsCopy(MfsDeps deps)
{
    MfsDeps new_deps = mfsDepsNew();
    int num_of_deps = mfsDepsCount(deps);
    for (int x=0; x < num_of_deps; x++) {
	MfsDep dep = mfsDepsGetEntry(deps, x);
	mfsDepsAppend(new_deps, mfsDepCopy(dep));
    }
    return new_deps;
}

int mfsDepsCount(MfsDeps deps)
{
    assert(deps);
    int x = 0;
    MfsDep entry = deps->entries;
    while (entry) {
	entry = entry->next;
	x++;
    }
    return x;
}

rpmRC mfsDepsAppend(MfsDeps deps, MfsDep entry)
{
    return mfsDepsInsert(deps, entry, -1);
}

rpmRC mfsDepsInsert(MfsDeps deps, MfsDep entry, int index)
{
    assert(deps);
    assert(entry);

    int x = 0;
    MfsDep prev = NULL;
    MfsDep cur_entry = deps->entries;

    if (!entry->name) {
	mfslog_err(_("Incomplete dependency\n"));
	return RPMRC_FAIL;
    }

    if (index == 0 || (index == -1 && !cur_entry)) {
	entry->next = deps->entries;
        deps->entries = entry;
	return RPMRC_OK;
    }

    if (!cur_entry)
	goto deps_entry_insert_error;

    while (1) {
	x++;
	prev = cur_entry;
	cur_entry = cur_entry->next;

	if ((index == x) || (index == -1 && !cur_entry)) {
	    prev->next = entry;
	    entry->next = cur_entry;
	    break;
	}

	if (index != -1 && !cur_entry)
	    goto deps_entry_insert_error;
    }

    return RPMRC_OK;

deps_entry_insert_error:

    mfslog_err(_("Dependency cannot be inserted to the specified index\n"));
    return RPMRC_FAIL;
}

rpmRC mfsDepsDelete(MfsDeps deps, int index)
{
    assert(deps);

    int x = 0;
    MfsDep prev = NULL;
    MfsDep cur_entry = deps->entries;

    if (!cur_entry)
	goto deps_entry_delete_error;

    if (index == 0 || (index == -1 && !cur_entry->next)) {
        deps->entries = cur_entry->next;
	mfsDepFree(cur_entry);
	return RPMRC_OK;
    }

    while (1) {
	x++;
	prev = cur_entry;
	cur_entry = cur_entry->next;

	if (!cur_entry)
	    goto deps_entry_delete_error;

	if ((index == x) || (index == -1 && !cur_entry->next)) {
	    prev->next = cur_entry->next;
	    mfsDepFree(cur_entry);
	    break;
	}
    }

    return RPMRC_OK;

deps_entry_delete_error:

    mfslog_err(_("Deps entry doesn't exist\n"));
    return RPMRC_FAIL;
}

const MfsDep mfsDepsGetEntry(MfsDeps deps, int index)
{
    assert(deps);

    int x = 0;
    MfsDep entry = deps->entries;
    while (entry) {
	if (x == index)
	    break;
	if (index == -1 && !entry->next)
	    break;
	entry = entry->next;
	x++;
    }

    if (!entry)
	return NULL;

    return entry;
}

MfsDep mfsDepNew(void)
{
    MfsDep entry = xcalloc(1, sizeof(*entry));
    entry->flags = RPMSENSE_ANY;
    return entry;
}

MfsDep mfsDepCopy(MfsDep entry)
{
    assert(entry);
    MfsDep copy = mfsDepNew();
    copy->name = mstrdup(entry->name);
    copy->version = mstrdup(entry->version);
    copy->flags = entry->flags;
    copy->index = entry->index;
    return copy;
}

void mfsDepFree(MfsDep entry)
{
    if (!entry)
	return;
    free(entry->name);
    free(entry->version);
    free(entry);
}

char *mfsDepGetName(MfsDep entry)
{
    assert(entry);
    return mstrdup(entry->name);
}

char *mfsDepGetVersion(MfsDep entry)
{
    assert(entry);
    return mstrdup(entry->version);
}

rpmsenseFlags mfsDepGetFlags(MfsDep entry)
{
    assert(entry);
    return (rpmsenseFlags) entry->flags;
}

uint32_t mfsDepGetIndex(MfsDep entry)
{
    assert(entry);
    return entry->index;
}

char *mfsDepGetFlagsStr(MfsDep entry)
{
    assert(entry);
    char *str = NULL;
    rpmsenseFlags flags = entry->flags;
    ARGV_t array = argvNew();

    // Comparison
    if (flags & RPMSENSE_LESS && flags & RPMSENSE_EQUAL)
	argvAdd(&array, "=<");
    else if (flags & RPMSENSE_GREATER && flags & RPMSENSE_EQUAL)
	argvAdd(&array, "=>");
    else if (flags & RPMSENSE_LESS)
	argvAdd(&array, "<");
    else if (flags & RPMSENSE_GREATER)
	argvAdd(&array, ">");
    else if (flags & RPMSENSE_EQUAL)
	argvAdd(&array, "==");

    // Other flags
    if (flags & RPMSENSE_POSTTRANS)
	argvAdd(&array, "%posttrans");
    if (flags & RPMSENSE_PREREQ)
	argvAdd(&array, "legacy_prereq");
    if (flags & RPMSENSE_PRETRANS)
	argvAdd(&array, "pretrans");
    if (flags & RPMSENSE_INTERP)
	argvAdd(&array, "interpreter");
    if (flags & RPMSENSE_SCRIPT_PRE)
	argvAdd(&array, "%pre");
    if (flags & RPMSENSE_SCRIPT_POST)
	argvAdd(&array, "%post");
    if (flags & RPMSENSE_SCRIPT_PREUN)
	argvAdd(&array, "%preun");
    if (flags & RPMSENSE_SCRIPT_POSTUN)
	argvAdd(&array, "%postun");
    if (flags & RPMSENSE_SCRIPT_VERIFY)
	argvAdd(&array, "%verify");
    if (flags & RPMSENSE_FIND_REQUIRES)
	argvAdd(&array, "find-requires_generated");
    if (flags & RPMSENSE_FIND_PROVIDES)
	argvAdd(&array, "find-provides_generated");
    if (flags & RPMSENSE_TRIGGERIN)
	argvAdd(&array, "%triggerin");
    if (flags & RPMSENSE_TRIGGERUN)
	argvAdd(&array, "%triggerun");
    if (flags & RPMSENSE_TRIGGERPOSTUN)
	argvAdd(&array, "%triggerpostun");
    if (flags & RPMSENSE_MISSINGOK)
	argvAdd(&array, "missingok");
    if (flags & RPMSENSE_RPMLIB)
	argvAdd(&array, "rpmlib(feature)");
    if (flags & RPMSENSE_TRIGGERPREIN)
	argvAdd(&array, "%triggerprein");
    if (flags & RPMSENSE_KEYRING)
	argvAdd(&array, "keyring");
    if (flags & RPMSENSE_CONFIG)
	argvAdd(&array, "config");

    str = argvJoin(array, ",");
    argvFree(array);

    return str ? str : mstrdup("");
}


rpmRC mfsDepSetName(MfsDep entry, const char *name)
{
    assert(entry);
    free(entry->name);
    entry->name = mstrdup(name);
    return RPMRC_OK;
}

rpmRC mfsDepSetVersion(MfsDep entry, const char *version)
{
    assert(entry);
    free(entry->version);
    entry->version = mstrdup(version);
    return RPMRC_OK;
}

rpmRC mfsDepSetFlags(MfsDep entry, rpmsenseFlags flags)
{
    assert(entry);
    entry->flags = flags;
    return RPMRC_OK;
}

rpmRC mfsDepSetIndex(MfsDep entry, uint32_t index)
{
    assert(entry);
    entry->index = index;
    return RPMRC_OK;
}

void mfsFileLinesFree(MfsFileLines flines)
{
    if (!flines)
	return;
    argvFree(flines->filelines);
    free(flines);
}

int mfsFileLinesCount(MfsFileLines flines)
{
    assert(flines);
    return argvCount(flines->filelines);
}

char *mfsFileLinesGetLine(MfsFileLines flines, int index)
{
    assert(flines);
    if (index >= 0 && index < argvCount(flines->filelines))
	return xstrdup(flines->filelines[index]);
    return NULL;
}

rpmRC mfsFileLinesAppend(MfsFileLines flines, const char *line)
{
    assert(flines);
    argvAdd(&flines->filelines, line);
    return RPMRC_OK;
}

rpmRC mfsFileLinesDelete(MfsFileLines flines, int index)
{
    assert(flines);
    argvDelete(flines->filelines, index);
    return RPMRC_OK;
}

ARGV_t mfsFileLinesGetAll(MfsFileLines flines)
{
    assert(flines);
    return argvCopy(flines->filelines);
}

void mfsFileFilesFree(MfsFileFiles ffiles)
{
    if (!ffiles)
	return;
    argvFree(ffiles->filefiles);
    free(ffiles);
}

int mfsFileFilesCount(MfsFileFiles ffiles)
{
    assert(ffiles);
    return argvCount(ffiles->filefiles);
}

char *mfsFileFilesGetFn(MfsFileFiles ffiles, int index)
{
    assert(ffiles);
    if (index >= 0 && index < argvCount(ffiles->filefiles))
    return xstrdup(ffiles->filefiles[index]);
    return NULL;
}

rpmRC mfsFileFilesAppend(MfsFileFiles ffiles, const char *flist)
{
    assert(ffiles);
    argvAdd(&ffiles->filefiles, flist);
    return RPMRC_OK;
}

rpmRC mfsFileFilesDelete(MfsFileFiles ffiles, int index)
{
    assert(ffiles);
    argvDelete(ffiles->filefiles, index);
    return RPMRC_OK;
}

ARGV_t mfsFileFilesGetAll(MfsFileFiles ffiles)
{
    assert(ffiles);
    return argvCopy(ffiles->filefiles);
}

// Policies

void mfsPoliciesFree(MfsPolicies policies)
{
    if (!policies)
	return;
    argvFree(policies->policies);
    free(policies);
}

int mfsPoliciesCount(MfsPolicies policies)
{
    assert(policies);
    return argvCount(policies->policies);
}

char *mfsPoliciesGetFn(MfsPolicies policies, int index)
{
    assert(policies);
    if (index >= 0 && index < argvCount(policies->policies))
    return xstrdup(policies->policies[index]);
    return NULL;
}

rpmRC mfsPoliciesAppend(MfsPolicies policies, const char *flist)
{
    assert(policies);
    argvAdd(&policies->policies, flist);
    return RPMRC_OK;
}

rpmRC mfsPoliciesDelete(MfsPolicies policies, int index)
{
    assert(policies);
    argvDelete(policies->policies, index);
    return RPMRC_OK;
}

ARGV_t mfsPoliciesGetAll(MfsPolicies policies)
{
    assert(policies);
    return argvCopy(policies->policies);
}

/*
 * Processed files related functions
 */

void mfsFilesFree(MfsFiles files)
{
    if (!files)
	return;
    for (MfsFile f = files->files; f;) {
	MfsFile next = f->next;
	mfsFreeDuppedFileListRec(f->flr);
	rpmcfFree(f->classified_file);
	free(f);
	f = next;
    }
    free(files);
}

int mfsFilesCount(MfsFiles files)
{
    assert(files);
    int x = 0;
    MfsFile entry = files->files;
    while (entry) {
	entry = entry->next;
	x++;
    }
    return x;
}

const MfsFile mfsFilesGetEntry(MfsFiles files, int index)
{
    assert(files);
    int x = 0;
    MfsFile entry = files->files;
    while (entry) {
	if (x == index)
	    break;
	if (index == -1 && !entry->next)
	    break;
	entry = entry->next;
	x++;
    }

    return entry;
}

const char * mfsFileGetPath(MfsFile file)
{
    assert(file);
    return file->diskpath;
}

rpmRC mfsPackageAddFile(MfsPackage pkg, MfsFile file)
{
    assert(pkg);
    assert(file);

    FileList fl = pkg->pkg->fl;

    if (fl == NULL) {
	mfslog_err(_("Cannot append file to the package"));
	return RPMRC_FAIL;
    }

    mfslog_info("Adding %s to %s\n", file->diskpath, pkg->fullname);

    addFileListRecord(fl, file->flr);

    // Prepend the package to mfsFile's list of owning packages
    MfsFilePackageList e = xcalloc(1, sizeof(*e));
    e->pkg = pkg->pkg;
    e->spec = pkg->spec;
    e->next = file->pkglist;
    file->pkglist = e;

    return RPMRC_OK;
}

int mfsFileGetToOriginal(MfsFile file)
{
    assert(file);
    return file->include_in_original;
}

rpmRC mfsFileSetToOriginal(MfsFile file, int val)
{
    assert(file);
    file->include_in_original = (val) ? 1 : 0;
    return RPMRC_OK;
}

rpmRC mfsFileGetStat(MfsFile file, struct stat *st)
{
    assert(file);
    if (!st) return RPMRC_OK;
    *st = file->flr->fl_st;
    return RPMRC_OK;
}

rpmRC mfsFileSetStat(MfsFile file, struct stat *st)
{
    assert(file);
    if (!st) {
	mfslog_err(_("Pointer to stat struct is NULL"));
	return RPMRC_FAIL;
    }
    file->flr->fl_st = *st;
    return RPMRC_OK;
}

const char *mfsFileGetDiskPath(MfsFile file)
{
    assert(file);
    return file->flr->diskPath;
}

rpmRC mfsFileSetDiskPath(MfsFile file, const char *path)
{
    assert(file);
    free(file->flr->diskPath);
    file->flr->diskPath = mstrdup(path);
    return RPMRC_OK;
}

const char *mfsFileGetCpioPath(MfsFile file)
{
    assert(file);
    return file->flr->cpioPath;
}

rpmRC mfsFileSetCpioPath(MfsFile file, const char *path)
{
    assert(file);
    free(file->flr->cpioPath);
    file->flr->cpioPath = mstrdup(path);
    return RPMRC_OK;
}

const char *mfsFileGetUname(MfsFile file)
{
    assert(file);
    return file->flr->uname;
}

rpmRC mfsFileSetUname(MfsFile file, const char *uname)
{
    assert(file);
    free(file->flr->uname);
    file->flr->uname = mstrdup(uname);
    return RPMRC_OK;
}

const char *mfsFileGetGname(MfsFile file)
{
    assert(file);
    return file->flr->gname;
}

rpmRC mfsFileSetGname(MfsFile file, const char *gname)
{
    assert(file);
    free(file->flr->gname);
    file->flr->gname = mstrdup(gname);
    return RPMRC_OK;
}

/* The flag field can contain flags from
 * rpmfileAttrs_e and parseAttrs_e
 * For some flag examples see virtualAttrs
 */
rpmFlags mfsFileGetFlags(MfsFile file)
{
    assert(file);
    return file->flr->flags;
}

rpmRC mfsFileSetFlags(MfsFile file, rpmFlags flags)
{
    assert(file);
    file->flr->flags = flags;
    return RPMRC_OK;
}

rpmVerifyFlags mfsFileGetVerifyFlags(MfsFile file)
{
    assert(file);
    return file->flr->verifyFlags;
}

rpmRC mfsFileSetVerifyFlags(MfsFile file, rpmVerifyFlags flags)
{
    assert(file);
    file->flr->verifyFlags = flags;
    return RPMRC_OK;
}

ARGV_t mfsFileGetLangs(MfsFile file)
{
    assert(file);
    ARGV_t list;
    if (!file->flr->langs)
	return NULL;
    argvSplit(&list, file->flr->langs, "|");
    return list;
}

rpmRC mfsFileSetLangs(MfsFile file, const ARGV_t langs)
{
    assert(file);
    free(file->flr->langs);
    file->flr->langs = argvJoin(langs, "|");
    return RPMRC_OK;
}

const char *mfsFileGetCaps(MfsFile file)
{
    assert(file);
    return file->flr->caps;
}

rpmRC mfsFileSet(MfsFile file, const char *caps)
{
    assert(file);
    free(file->flr->caps);
    file->flr->caps = mstrdup(caps);
    return RPMRC_OK;
}

rpm_color_t mfsFileGetColor(MfsFile file)
{
    assert(file);
    return rpmcfColor(file->classified_file);
}

const ARGV_t mfsFileGetAttrs(MfsFile file)
{
    assert(file);
    return rpmcfAttrs(file->classified_file);
}

const char *mfsFileGetType(MfsFile file)
{
    assert(file);
    return rpmcfType(file->classified_file);
}

int mfsFileOwningPackagesCount(MfsFile file)
{
    assert(file);
    int count = 0;
    for (MfsFilePackageList e = file->pkglist; e; e = e->next)
	count++;
    return count;
}

MfsPackage mfsFileOwningPackage(MfsFile file, int index)
{
    assert(file);
    int x = 0;
    for (MfsFilePackageList e = file->pkglist; e; e=e->next, x++)
	if (x == index)
	    return mfsPackageFromPackage(e->spec, e->pkg);
    return NULL;
}

MfsPackage mfsFileGetOriginalDestination(MfsFile file)
{
    assert(file);
    return mfsPackageFromPackage(file->spec, file->originalpkg);
}

/*
 * Internal functions
 */

rpmRC mfsPackageFinalize(MfsPackage mfspkg)
{
    // Should be called for each package at the end of its configuration

    rpmSpec spec = mfspkg->spec;
    Package pkg = mfspkg->pkg;
    const char *fullname = mfspkg->fullname;

    // Skip valid arch check if not building binary package
    if (!(spec->flags & RPMSPEC_ANYARCH) && checkForValidArchitectures(spec)) {
	goto errxit;
    }

    if (checkForDuplicates(pkg->header, fullname) != RPMRC_OK) {
	goto errxit;
    }

    if (pkg != spec->packages) {
	headerCopyTags(spec->packages->header, pkg->header,
			(rpmTagVal *)copyTagsDuringParse);
    }

    if (checkForRequired(pkg->header, fullname) != RPMRC_OK) {
	goto errxit;
    }

    if (!headerIsEntry(pkg->header, RPMTAG_DESCRIPTION)) {
	headerPutString(pkg->header, RPMTAG_DESCRIPTION, "Package created by module\n");
    }

    // Add targets
    {
	char *platform = rpmExpand("%{_target_platform}", NULL);
	char *arch = rpmExpand("%{_target_cpu}", NULL);
	char *os = rpmExpand("%{_target_os}", NULL);
	char *optflags = rpmExpand("%{optflags}", NULL);

	headerPutString(pkg->header, RPMTAG_OS, os);
	/* noarch subpackages already have arch set here, leave it alone */
	if (!headerIsEntry(pkg->header, RPMTAG_ARCH)) {
	    headerPutString(pkg->header, RPMTAG_ARCH, arch);
	}
	headerPutString(pkg->header, RPMTAG_PLATFORM, platform);
	headerPutString(pkg->header, RPMTAG_OPTFLAGS, optflags);

	pkg->ds = rpmdsThis(pkg->header, RPMTAG_REQUIRENAME, RPMSENSE_EQUAL);

	free(platform);
	free(arch);
	free(os);
	free(optflags);
    }

    {
	const char *arch, *name;
	char *evr, *isaprov;
	rpmsenseFlags pflags = RPMSENSE_EQUAL;

	/* <name> = <evr> provide */
	name = headerGetString(pkg->header, RPMTAG_NAME);
	arch = headerGetString(pkg->header, RPMTAG_ARCH);
	evr = headerGetAsString(pkg->header, RPMTAG_EVR);
	addReqProv(pkg, RPMTAG_PROVIDENAME, name, evr, pflags, 0);

	/*
	 * <name>(<isa>) = <evr> provide
	 * FIXME: noarch needs special casing for now as BuildArch: noarch doesn't
	 * cause reading in the noarch macros :-/ 
	 */
	isaprov = rpmExpand(name, "%{?_isa}", NULL);
	if (!rstreq(arch, "noarch") && !rstreq(name, isaprov)) {
	    addReqProv(pkg, RPMTAG_PROVIDENAME, isaprov, evr, pflags, 0);
	}
	free(isaprov);
	free(evr);
    }

    return RPMRC_OK;

errxit:
    return RPMRC_FAIL;
}

// Experimental API

rpmRC mfsPackageGenerateDepends(MfsPackage pkg, ARGV_t files,
                                rpm_mode_t *fmodes, rpmFlags *fflags)
{
    assert(pkg);
    return rpmfcGenerateDepends(pkg->spec, pkg->pkg, files, fmodes, fflags);
}
