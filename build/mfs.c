#include "system.h"

#include <dlfcn.h>
#include <sys/types.h>
#include <dirent.h>
#include <fnmatch.h>

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

#define TIME_STR_BUF	50

typedef struct MfsDlopenHandleList_s {
    void *handle;
    struct MfsDlopenHandleList_s *next;
} * MfsHandleList;

typedef struct MfsModuleLoadState_s {
    MfsHandleList handles;
} * MfsModuleLoadState;


static inline char *mstrdup(const char *str) {
    if (!str) return NULL;
    return xstrdup(str);
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
    // TODO: XXX Dealloc content of MM
    free(mm);
}

static void mfsManagerInsertSortedParserHook(MfsManager mfsm,
                                             MfsParserHook cur)
{
    MfsParserHook prev = NULL;
    MfsParserHook node = mfsm->parserhooks;

    if (!node || cur->priority < node->priority) {
        cur->next = node;
        mfsm->parserhooks = cur;
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
    MfsContext ctx = mfsm->contexts;

    while (ctx) {
        // Use Parser Hooks
        for (MfsParserHook cur = ctx->parserhooks; cur;) {
            MfsParserHook next = cur->next;
            mfsManagerInsertSortedParserHook(mfsm, cur);
            cur = next;
        }
        ctx->parserhooks = NULL;

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
    rpmlog(RPMLOG_INFO, _("Registered ParserHooks:\n"));
    for (MfsParserHook cur = mfsm->parserhooks; cur; cur = cur->next)
        rpmlog(RPMLOG_INFO, _("- Module %s registered ParserHook %p (%d)\n"),
                cur->context->modulename, cur->func, cur->priority);

    rpmlog(RPMLOG_INFO, _("Registered FileHooks:\n"));
    for (MfsFileHook cur = mfsm->filehooks; cur; cur = cur->next)
        rpmlog(RPMLOG_INFO, _("- Module %s registered FileHook %p (%d)\n"),
                cur->context->modulename, cur->func, cur->priority);
}

/* Insert (move) the current context the internal list of contexts.
 * This function keeps the list of internal contexts sorted by the
 * names of the corresponding modules.
 */
static void mfsManagerUseCurrentContext(MfsManager mfsm)
{
    MfsContext cur = mfsm->cur_context;
    MfsContext prev = NULL;
    MfsContext node = mfsm->contexts;

    cur->manager = mfsm;
    mfsm->cur_context = NULL;

    if (!node || strcmp(cur->modulename, node->modulename) < 0) {
        // Prepend item to the beginning of the list
        cur->next = node;
        mfsm->contexts = cur;
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

MfsContext mfsContextNew(void)
{
    MfsContext context = xcalloc(1, sizeof(*context));
    return context;
}

void mfsContextFree(MfsContext context)
{
    if (!context)
        return;
    free(context->modulename);
    // TODO: XXX Dealloc content of context
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
	rpmlog(RPMLOG_ERR, _("Error while loading module %s: %s\n"),
	       fullpath, dlerror());
        return NULL;
    }

    rasprintf(&initfunc_name, "init_%s", name);
    initfunc = (MfsModuleInitFunc) dlsym(handle, initfunc_name);
    free(initfunc_name);

    if (!initfunc) {
	rpmlog(RPMLOG_ERR, _("Error while loading init function "
               "of module %s: %s\n"), fullpath, dlerror());
        dlclose(handle);
        return NULL;
    }

    // Prepare context for this module
    MfsContext context = mfsContextNew();
    context->modulename = strdup(name);
    mfsm->cur_context = context;

    // Init the module
    rc = initfunc(mfsm);
    if (rc < 0) {
        rpmlog(RPMLOG_ERR, _("Error: Init function of %s returned %d\n"),
		fullpath, rc);
        dlclose(handle);
        mfsContextFree(context);
        mfsm->cur_context = NULL;
        return NULL;
    }

    // Insert the current context to the manager's list of contexts
    mfsManagerUseCurrentContext(mfsm);

    rpmlog(RPMLOG_NOTICE, _("Loaded module: %s\n"), fullpath);

    return handle;
}

rpmRC mfsLoadModules(void **modules, const char *path, MfsManager mfsm)
{
    MfsModuleLoadState load_state;
    struct dirent* direntry;
    int error_during_loading = 0;

    *modules = NULL;

    DIR *dir = opendir(path);
    if (!dir) {
	rpmlog(RPMLOG_ERR, _("Could not open directory %s: %m\n"), path);
        return RPMRC_FAIL;
    }

    load_state = xcalloc(1, sizeof(*load_state));

    while ((direntry = readdir(dir))) {
        char *name, *fullpath;

	name = getModuleName(direntry->d_name);
        if (!name)
            continue;

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

rpmRC mfsManagerCallParserHooks(MfsManager mm, rpmSpec cur_spec)
{
    rpmRC rc = RPMRC_OK;

    for (MfsParserHook hook = mm->parserhooks; hook; hook=hook->next) {
        MfsParserHookFunc func = hook->func;
        MfsContext context = hook->context;

	// Prepare the context
	context->cur_spec = cur_spec;
	context->state = MFS_CTXSTATE_PARSERHOOK;

	// Call the hook
        if ((rc = func(context)) != RPMRC_OK) {
	    rpmlog(RPMLOG_ERR, _("Module %s returned an error from parsehook\n"),
		   hook->context->modulename);
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

rpmRC mfsManagerCallFileHooks(MfsManager mm, rpmSpec cur_spec,
			      FileListRec rec, int *include_in_original)
{
    rpmRC rc = RPMRC_OK;
    int local_include_in_original = 1;

    if (!mm)
	return RPMRC_OK;

    for (MfsFileHook hook = mm->filehooks; hook; hook=hook->next) {
	MfsFileHookFunc func = hook->func;
        MfsContext context = hook->context;

	// Check the glob
	const char *diskpath = rec->diskPath;
	int match = 0; // 0 - is TRUE in this case
	for (MfsGlob glob = hook->globs; glob; glob = glob->next)
	   if ((match = fnmatch(glob->glob, diskpath, 0)) == 0)
		break;
	if (match != 0)
	    // Skip this
	    continue;

	// Prepare the MfsFile
	MfsFile mfsfile = xcalloc(1, sizeof(*mfsfile));
	mfsfile->flr = mfsDupFileListRec(rec);
	mfsfile->diskpath = rec->diskPath;
	mfsfile->include_in_original = local_include_in_original;

	// Prepare the context
	context->cur_spec = cur_spec;
	context->state = MFS_CTXSTATE_FILEHOOK;

	// Call the hook
        if ((rc = func(context, mfsfile)) != RPMRC_OK) {
	    rpmlog(RPMLOG_ERR, _("Module %s returned an error from filehook\n"),
		   hook->context->modulename);
            break;
	}

	// Get info from the MfsFile and free it
	if (!mfsfile->include_in_original)
	    local_include_in_original = 0;
	mfsFreeDuppedFileListRec(mfsfile->flr);
	free(mfsfile);

	context->state = MFS_CTXSTATE_UNKNOWN;
    }

    *include_in_original = local_include_in_original;

    return rc;
}

/*
 * Module initialization related API
 */

MfsParserHook mfsParserHookNew(MfsParserHookFunc hookfunc)
{
    MfsParserHook hook = xcalloc(1, sizeof(*hook));
    hook->func = hookfunc;
    hook->priority = MFS_HOOK_DEFAULT_PRIORITY_VAL;
    return hook;
}

rpmRC mfsParserHookSetPriority(MfsParserHook hook, int32_t priority)
{
    if (priority < MFS_HOOK_MIN_PRIORITY_VAL
            || priority > MFS_HOOK_MAX_PRIORITY_VAL)
        return RPMRC_FAIL;

    hook->priority = priority;
    return RPMRC_OK;
}

void mfsRegisterParserHook(MfsManager mm, MfsParserHook hook)
{
    MfsContext context = mm->cur_context;
    hook->context = context;
    hook->next = context->parserhooks;
    context->parserhooks = hook;
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

void mfsFileHookAddGlob(MfsFileHook hook, const char *glob)
{
    MfsGlob mfsglob = xcalloc(1, sizeof(*mfsglob));
    mfsglob->glob = strdup(glob);
    mfsglob->next = hook->globs;
    hook->globs = mfsglob;
}

void mfsRegisterFileHook(MfsManager mm, MfsFileHook hook)
{
    assert(mm);
    MfsContext context = mm->cur_context;
    hook->context = context;
    hook->next = context->filehooks;
    context->filehooks = hook;
}

void mfsSetGlobalData(MfsManager mm, void *data)
{
    assert(mm);
    MfsContext context = mm->cur_context;
    context->globaldata = data;
}

void *mfsGetModuleGlobalData(MfsContext context)
{
    return context->globaldata;
}

void mfsSetModuleGlobalData(MfsContext context, void *data)
{
    context->globaldata = data;
}

void *mfsGetContextData(MfsContext context)
{
    assert(context);
    for (MfsContextData cdata = context->contextdata; cdata; cdata = cdata->next)
	if (cdata->spec == context->cur_spec)
	    return cdata->data;
    return NULL;
}

void mfsSetContextData(MfsContext context, void *data)
{
    assert(context);
    MfsContextData cdata = context->contextdata;

    for (; cdata; cdata = cdata->next)
	if (cdata->spec == context->cur_spec)
	    break;

    if (!cdata) {
	cdata = xcalloc(1, sizeof(*cdata));
	cdata->spec = context->cur_spec;
	cdata->next = context->contextdata;
	context->contextdata = cdata;
    }

    cdata->data = data;
}

/*
 * Spec manipulation related API
 */

MfsSpec mfsSpecFromContext(MfsContext context)
{
    MfsSpec mfsspec;

    if (!context || !context->cur_spec)
	return NULL;

    mfsspec = xcalloc(1, sizeof(*mfsspec));
    mfsspec->rpmspec = context->cur_spec;

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

    mfspackage = xcalloc(1, sizeof(*mfspackage));
    mfspackage->pkg = pkg;
    mfspackage->spec = spec->rpmspec;
    return mfspackage;
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
 * Parser Hook Related API
 */

// Package

MfsPackage mfsPackageNew(MfsContext context,
			 const char *name,
			 const char *summary,
			 int flags)
{
    int flag = 0;
    MfsPackage mfs_pkg;
    rpmSpec spec = context->cur_spec;
    char *fullname;
    Package pkg;

    if (context->state != MFS_CTXSTATE_PARSERHOOK) {
	rpmlog(RPMLOG_ERR, _("Packages must be added in a parser hook. "
			     "Cannot add: %s\n"), name);
	return NULL;
    }

    if (!spec->packages) {
        // This should be a first package
        // Spec doesn't have defined any packages - nothing to do
        // This is an artificial limitation
        rpmlog(RPMLOG_ERR, _("No main package exist. Cannot add: %s\n"), name);
        return NULL;
    }

    if (flags & MFS_PACKAGE_FLAG_SUBNAME)
        flag = PART_SUBNAME;

    if (!lookupPackage(spec, name, flag, NULL)) {
        rpmlog(RPMLOG_ERR, _("Package already exists: %s\n"), name);
        return NULL;
    }

    if (flag == PART_SUBNAME) {
        rasprintf(&fullname, "%s-%s",
                headerGetString(spec->packages->header, RPMTAG_NAME), name);
    } else
        fullname = mstrdup(name);

    pkg = newPackage(fullname, spec->pool, &spec->packages);

    headerPutString(pkg->header, RPMTAG_NAME, fullname);
    addLangTag(spec, pkg->header, RPMTAG_SUMMARY, summary, RPMBUILD_DEFAULT_LANG);

    pkg->fileList = argvNew();

    mfs_pkg = xcalloc(1, sizeof(*mfs_pkg));
    mfs_pkg->pkg = pkg;
    mfs_pkg->fullname = fullname;
    mfs_pkg->spec = spec;

    return mfs_pkg;
}

Header mfsPackageGetHeader(MfsPackage pkg)
{
    return pkg->pkg->header;
}

const rpmTagVal * mfsPackageTags(void) {
#define PREAMBLELIST_SIZE   (sizeof(preambleList) / sizeof(preambleList[0]))
    static rpmTagVal array[PREAMBLELIST_SIZE];
    for (int x = 0; x < PREAMBLELIST_SIZE; x++)
	array[x] = preambleList[x].tag;
    return array;
}

// TODO: RPMTAG_DESCRIPTION

// This function mimics the findPreambleTag()
rpmRC mfsPackageSetTag(MfsPackage pkg,
		       rpmTagVal tag,
		       const char *value,
		       const char *opt)
{
    rpmRC rc;
    const char *macro;
    PreambleRec p;

    if (!value) {
	rpmlog(RPMLOG_ERR, _("No value specified for tag %d\n"), tag);
	return RPMRC_FAIL;
    }

    // Find tag in the preambleList[]
    for (p = preambleList; p->token; p++)
	if (p->tag == tag)
	    break;

    if (!p || !p->token) {
	rpmlog(RPMLOG_ERR, _("Unknown/Unsupported tag (%d)\n"), tag);
	return RPMRC_FAIL;
    }

    macro = p->token;

    if (p->deprecated)
	rpmlog(RPMLOG_WARNING, _("Tag %d: %s is deprecated\n"), tag, macro);

    if (p->type == 0) {
	if (opt && *opt)
	    rpmlog(RPMLOG_WARNING, _("Tag %d: %s doesn't support additional "
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

    rc = applyPreambleTag(pkg->spec, pkg->pkg, tag, macro, opt, value);

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
	    rpmlog(RPMLOG_ERR, _("Unexpected type of data for tag %d\n"), rec->progtag);
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
	rpmlog(RPMLOG_ERR, _("script program must be set\n"));
	return RPMRC_FAIL;
    }

    if (*script->prog == '<') {
	// Internal script specified
	if (script->prog[strlen(script->prog)-1] != '>') {
	    rpmlog(RPMLOG_ERR, _("internal script must end with \'>\': %s\n"),
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
	    rpmlog(RPMLOG_ERR, _("unsupported internal script: %s\n"), script->prog);
	    return RPMRC_FAIL;
	}
    } else if (*script->prog != '/') {
	// External script program must starts with '/'
	rpmlog(RPMLOG_ERR, _("script program must begin with \'/\': %s\n"),
		script->prog);
	return RPMRC_FAIL;
    }

    // Parse the prog argument
    if ((popt_rc = poptParseArgvString(script->prog, &progArgc, &progArgv))) {
	rpmlog(RPMLOG_ERR, _("error parsing %s: %s\n"),
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

    // Remove all script related stuff
    headerDel(hdr, rec->tag);	    // Code from header
    headerDel(hdr, rec->progtag);   // Prog from header
    headerDel(hdr, rec->flagstag);  // Flags
    file = (char **) ((size_t)pkg->pkg + (size_t)rec->fileoffset);
    free(*file);
    *file = NULL;

    return RPMRC_OK;
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
	    rpmlog(RPMLOG_ERR, _("Cannot retrieve changelog entries\n"));
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

    for (MfsChangelog e = changelogs->entries; e; e = e->next) {
	if (!e->name || !e->text) {
	    rpmlog(RPMLOG_WARNING, _("Invalid changelog entry skipped\n"));
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

    MfsDeps deps = xcalloc(1, sizeof(*deps));
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
	    rpmlog(RPMLOG_ERR, _("Cannot retrieve dependency\n"));
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

    // Set the new dependencies
    for (MfsDep e = deps->entries; e; e = e->next) {
	if (!e->name) {
	    rpmlog(RPMLOG_ERR, _("invalid dependency - Missing name\n"));
	    rc = RPMRC_FAIL;
	    goto pkg_set_dep_error;
	}

	if (e->flags & RPMSENSE_SENSEMASK && !e->version) {
	    rpmlog(RPMLOG_ERR, _("invalid dependency - Version required: "
			"%s (%d)\n"), e->name, e->flags);
	    rc = RPMRC_FAIL;
	    goto pkg_set_dep_error;
	}

	if (!(e->flags & RPMSENSE_SENSEMASK) && e->version && *e->version != '\0') {
	    rpmlog(RPMLOG_ERR, _("invalid dependency - Redundant version: "
			"%s %s (%d)\n"), e->name, e->version, e->flags);
	    rc = RPMRC_FAIL;
	    goto pkg_set_dep_error;
	}

	if (e->index && !indextag) {
	    rpmlog(RPMLOG_WARNING, _("index attribute has no effect: %s\n"),
			e->name);
	}

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

// Scripts

MfsScript mfsScriptNew(void)
{
    MfsScript script = xcalloc(1, sizeof(*script));
    script->prog = mstrdup("/bin/sh");
    return script;
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
	rpmlog(RPMLOG_ERR, _("Incomplete changelog entry\n"));
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

    rpmlog(RPMLOG_ERR, _("Changelog entry cannot be inserted to the specified index\n"));
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

    rpmlog(RPMLOG_ERR, _("Changelog entry doesn't exist\n"));
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
	rpmlog(RPMLOG_ERR, "Cannot convert \"%s\" to time\n", date);
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
	rpmlog(RPMLOG_ERR, _("Incomplete dependency\n"));
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

    rpmlog(RPMLOG_ERR, _("Dependency cannot be inserted to the specified index\n"));
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

    rpmlog(RPMLOG_ERR, _("Deps entry doesn't exist\n"));
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
    char *flags_str = NULL;
    rpmsenseFlags flags = entry->flags;

    // Comparison
    if (flags & RPMSENSE_LESS && flags & RPMSENSE_EQUAL)
	flags_str = mstrdup("=<");
    else if (flags & RPMSENSE_GREATER && flags & RPMSENSE_EQUAL)
	flags_str = mstrdup("=>");
    else if (flags & RPMSENSE_LESS)
	flags_str = mstrdup("<");
    else if (flags & RPMSENSE_GREATER)
	flags_str = mstrdup(">");
    else if (flags & RPMSENSE_EQUAL)
	flags_str = mstrdup("==");

    // TODO: Other flags

    return flags_str;
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

/*
 * Package hook related functions
 */

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
	rpmlog(RPMLOG_ERR, _("Cannot append file to the package"));
	return RPMRC_FAIL;
    }

    addFileListRecord(fl, file->flr);
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
	rpmlog(RPMLOG_ERR, _("Pointer to stat struct is NULL"));
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