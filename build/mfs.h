#ifndef	_H_MFS_
#define	_H_MFS_

#include "lib/rpmds.h"
#include "lib/rpmtypes.h"

/** \ingroup mfs
 * \file build/mfs.h
 *  Modular File Scanner public API
 */

#ifdef __cplusplus
extern "C" {
#endif

#define MFS_HOOK_MIN_PRIORITY_VAL       0
#define MFS_HOOK_MAX_PRIORITY_VAL       10000
#define MFS_HOOK_DEFAULT_PRIORITY_VAL   5000

typedef struct MfsManager_s * MfsManager;
typedef struct MfsContext_s * MfsContext;

typedef struct MfsParserHook_s * MfsParserHook;
typedef struct MfsFileHook_s * MfsFileHook;

typedef struct MfsSpec_s * MfsSpec;
typedef struct MfsBTScript_s * MfsBTScript; /*!<
    Build time script (Prep, Build, Install, Check, Clean) */

typedef struct MfsPackage_s * MfsPackage;
typedef struct MfsScript_s * MfsScript;
typedef struct MfsTriggers_s * MfsTriggers;
typedef struct MfsTrigger_s * MfsTrigger;
typedef struct MfsChangelogs_s * MfsChangelogs;
typedef struct MfsChangelog_s * MfsChangelog;
typedef struct MfsDeps_s * MfsDeps;
typedef struct MfsDep_s * MfsDep;
typedef struct MfsFile_s * MfsFile;

/* Module init function's name has to follow the pattern: "init_modulename"
 * Where modulename is filename of module's .so file without the ".so" suffix
 *
 * @param mm    Module Manager that could be used for hooks registration.
 * @return      RPMRC_OK on success, RPMRC_FAIL on error
 */
typedef rpmRC (*MfsModuleInitFunc)(MfsManager mm);

typedef rpmRC (*MfsParserHookFunc)(MfsContext context);
typedef rpmRC (*MfsFileHookFunc)(MfsContext context, MfsFile file);

typedef enum MfsSpecAttr_e {
    MFS_SPEC_ATTR_SPECFILE,	/*!< (String) */
    MFS_SPEC_ATTR_BUILDROOT,	/*!< (String) */
    MFS_SPEC_ATTR_BUILDSUBDIR,	/*!< (String) */
    MFS_SPEC_ATTR_ROOTDIR,	/*!< (String) */
    MFS_SPEC_ATTR_SOURCERPMNAME,/*!< (String) */
    MFS_SPEC_ATTR_PARSED,	/*!< (String) Parsed content */
} MfsSpecAttr;

typedef enum MfsBTScriptType_e {
    MFS_SPEC_SCRIPT_PREP,
    MFS_SPEC_SCRIPT_BUILD,
    MFS_SPEC_SCRIPT_INSTALL,
    MFS_SPEC_SCRIPT_CHECK,
    MFS_SPEC_SCRIPT_CLEAN,
    MFS_SPEC_SCRIPT_SENTINEL /*!< The last element of the list */
} MfsBTScriptType;

typedef enum MfsPackageFlags_e {
    MFS_PACKAGE_FLAG_NONE,
    MFS_PACKAGE_FLAG_SUBNAME,    /*!< Name will be used as package's subname */
} MfsPackageFlags;

typedef enum MfsScriptFlags_e {
    MFS_SCRIPT_FLAG_NONE	= 0,
    MFS_SCRIPT_FLAG_EXPAND 	= (1 << 0), /*!< Macro expansion */
    MFS_SCRIPT_FLAG_QFORMAT 	= (1 << 1), /*!< Header queryformat expansion */
} MfsScriptFlags;

typedef enum MfsScriptType_e {
    MFS_SCRIPT_PREIN,
    MFS_SCRIPT_POSTIN,
    MFS_SCRIPT_PREUN,
    MFS_SCRIPT_POSTUN,
    MFS_SCRIPT_PRETRANS,
    MFS_SCRIPT_POSTTRANS,
    MFS_SCRIPT_VERIFYSCRIPT,
    MFS_SCRIPT_SENTINEL /*!< The last element of the list */
} MfsScriptType;

typedef enum MfsTriggerType_e {
    MFS_TRIGGER_PREIN,
    MFS_TRIGGER_IN,
    MFS_TRIGGER_UN,
    MFS_TRIGGER_POSTUN,
    MFS_TRIGGER_SENTINEL /*!< The last element of the list */
} MfsTriggerType;

typedef enum MfsDepType_e {
    MFS_DEP_TYPE_REQUIRES,
    MFS_DEP_TYPE_PROVIDES,
    MFS_DEP_TYPE_CONFLICTS,
    MFS_DEP_TYPE_OBSOLETES,
    MFS_DEP_TYPE_TRIGGERS,
    MFS_DEP_TYPE_ORDER,
    MFS_DEP_TYPE_SENTINEL   /*!< The last element of the list */
} MfsDepType;

// Module initialization related API

/* Priority is number between 0-10000
 * Default priority is 5000
 * 0 - Maximum priority
 * 10000 - Minimal priority
 * To be deterministic:
 *  1) If there are hooks in multiple modules which have the same priority
 * then their module names are used as the second sorting key.
 * FIFO cannot be used here, because module load order depends on
 * used filesystem and thus it is not deterministic.
 *  2) If there are multiple hooks with the same priority from the same module,
 * then LIFO approach is used (late registered function has higher priority).
 */

MfsParserHook mfsParserHookNew(MfsParserHookFunc hookfunc);
rpmRC mfsParserHookSetPriority(MfsParserHook hook, int32_t priority);
void mfsRegisterParserHook(MfsManager mm, MfsParserHook hook);

MfsFileHook mfsFileHookNew(MfsFileHookFunc hookfunc);
rpmRC mfsFileHookSetPriority(MfsFileHook hook, int32_t priority);
void mfsFileHookAddGlob(MfsFileHook hook, const char *glob);
void mfsRegisterFileHook(MfsManager mm, MfsFileHook hook);

/* Set data that can be accessed by mfsGetModuleGlobalData()
 */
void mfsSetGlobalData(MfsManager mm, void *data);

// Common Context API

/* Set/Get a data that are can be accessed by all callbacks registered
 * by the module.
 */
void *mfsGetModuleGlobalData(MfsContext context);
void mfsSetModuleGlobalData(MfsContext context, void *data);

/* This is preffered method to store data that should be
 * persistent between callbacks.
 * Spec related dat a are related to the current spec file.
 * All callbacks registered by the module and working with the same
 * spec file can access this data.
 *
 * During the build process, multiple spec files can be parsed
 * and builded, thus each callback can be called multiple times, each
 * time for a different spec file.
 */
void *mfsGetContextData(MfsContext context);
void mfsSetContextData(MfsContext context, void *data);

// Spec manipulation API

MfsSpec mfsSpecFromContext(MfsContext context);
char * mfsSpecGetString(MfsSpec spec, MfsSpecAttr attr);
rpmRC mfsSpecSetString(MfsSpec spec, MfsSpecAttr attr, const char *str);
int mfsSpecPackageCount(MfsSpec spec);
MfsPackage mfsSpecGetPackage(MfsSpec spec, int index);
MfsPackage mfsSpecGetSourcePackage(MfsSpec spec);
MfsBTScript mfsSpecGetScript(MfsSpec spec, MfsBTScriptType type);
rpmRC mfsSpecSetScript(MfsSpec spec, MfsBTScript script, MfsBTScriptType type);

void mfsBTScriptFree(MfsBTScript script);
char *mfsBTScriptGetCode(MfsBTScript script);
rpmRC mfsBTScriptSetCode(MfsBTScript script, const char *code);
rpmRC mfsBTScriptAppend(MfsBTScript script, const char *code);
rpmRC mfsBTScriptAppendLine(MfsBTScript script, const char *code);

// Add Package Hook Related API

MfsPackage mfsPackageNew(MfsContext context,
			 const char *name,
			 const char *summary,
			 int flags);
/* Function that does some sanity checks and
 * fill some important values like target os, arch, platform, etc.
 */
rpmRC mfsPackageFinalize(MfsPackage mfspkg);
Header mfsPackageGetHeader(MfsPackage pkg);
const rpmTagVal * mfsPackageTags(void);

/* Set the tag value to the package.
 * List of tags supported by the function is returned by the mfsPackageTags().
 * @param pkg	    Package
 * @param tag	    Header tag (RPMTAG_NAME, etc.)
 * @param value	    Tag value
 * @param opt	    Additional attributes.
 *		    Some tags support additional attributes.
 *		    I. These additional attributes can be written in spec in
 *		    brackets right after the tag:
 *
 *		    1) RPMTAG_SUMMARY and RPMTAG_GROUP, where the additional
 *		    info is a language, i.e. "Summary(cs): Summary in Czech."
 *                  2) RPMTAG_REQUIREFLAGS and RPMTAG_ORDERFLAGS, where the
 *                  additional info is the scope of the dependency, i.e.:
 *                  "Requires(pre): foo"
 *
 *                  II. Some tags can be numbered:
 *                  RPMTAG_SOURCE and RPMTAG_PATCH, where the number
 *                  follow the tag, i.e. "Source0: foo.tgz"
 *
 *		    Pass this optional arguments always as a string.
 *
 *                  Pass NULL for default value(s).
 */
rpmRC mfsPackageSetTag(MfsPackage pkg,
		       rpmTagVal tag,
		       const char *value,
		       const char *opt);

MfsScript mfsPackageGetScript(MfsPackage pkg, MfsScriptType type);
rpmRC mfsPackageSetScript(MfsPackage pkg, MfsScript script, MfsScriptType type);
rpmRC mfsPackageDeleteScript(MfsPackage pkg, MfsScriptType type);

MfsTriggers mfsPackageGetTriggers(MfsPackage pkg);
rpmRC mfsPackageSetTriggers(MfsPackage pkg, MfsTriggers triggers);

MfsChangelogs mfsPackageGetChangelogs(MfsPackage pkg);
rpmRC mfsPackageSetChangelogs(MfsPackage pkg, MfsChangelogs changelog);

MfsDeps mfsPackageGetDeps(MfsPackage pkg, MfsDepType deptype);
rpmRC mfsPackageSetDeps(MfsPackage pkg, MfsDeps deps, MfsDepType deptype);

// Scripts

MfsScript mfsScriptNew(void);
void mfsScriptFree(MfsScript script);
char *mfsScriptGetCode(MfsScript script);
char *mfsScriptGetProg(MfsScript script);
char *mfsScriptGetFile(MfsScript script);
MfsScriptFlags mfsScriptGetFlags(MfsScript script);
rpmRC mfsScriptSetCode(MfsScript script, const char *code);
rpmRC mfsScriptSetProg(MfsScript script, const char *prog);
rpmRC mfsScriptSetFile(MfsScript script, const char *fn);
rpmRC mfsScriptSetFlags(MfsScript script, MfsScriptFlags flags);

// Triggers - Not implemented yet

/*
void mfsTriggersFree(MfsTriggers triggers);
int mfsTriggersCount(MfsTriggers triggers);
rpmRC mfsTriggersAppend(MfsTriggers triggers, MfsChangelog entry);
rpmRC mfsTriggersInsert(MfsTriggers triggers, MfsChangelog entry, int index);
rpmRC mfsTriggersDelete(MfsTriggers triggers, int index);
const MfsTrigger mfsTriggersGetEntry(MfsTriggers triggers, int index);

MfsTrigger mfsTriggerNew();
MfsTrigger mfsTriggerCopy(MfsTrigger trigger);
void mfsTriggerFree(MfsTrigger trigger);
char *mfsTriggerGetCode(MfsTrigger trigger);
char *mfsTriggerGetProg(MfsTrigger trigger);
char *mfsTriggerGetFile(MfsTrigger trigger);
MfsScriptFlags mfsTriggerGetFlags(MfsTrigger trigger);
MfsTriggerType mfsTriggerGetType(MfsTrigger trigger);
rpmRC mfsTriggerSetCode(MfsTrigger trigger, const char *code);
rpmRC mfsTriggerSetProg(MfsTrigger trigger, const char *prog);
rpmRC mfsTriggerSetFile(MfsTrigger trigger, const char *fn);
rpmRC mfsTriggerSetFlags(MfsTrigger trigger, MfsScriptFlags flags);
rpmRC mfsTriggerSetType(MfsTrigger trigger, MfsScriptType type);
*/

// Changelogs

void mfsChangelogsFree(MfsChangelogs changelogs);
int mfsChangelogsCount(MfsChangelogs changelogs);
rpmRC mfsChangelogsAppend(MfsChangelogs changelogs, MfsChangelog entry);
rpmRC mfsChangelogsInsert(MfsChangelogs changelogs, MfsChangelog entry, int index);
rpmRC mfsChangelogsDelete(MfsChangelogs changelogs, int index);
const MfsChangelog mfsChangelogsGetEntry(MfsChangelogs changelogs, int index);

MfsChangelog mfsChangelogNew(void);
MfsChangelog mfsChangelogCopy(MfsChangelog entry);
void mfsChangelogFree(MfsChangelog entry);
time_t mfsChangelogGetDate(MfsChangelog entry);
char *mfsChangelogGetDateStr(MfsChangelog entry);
char *mfsChangelogGetName(MfsChangelog entry);
char *mfsChangelogGetText(MfsChangelog entry);
rpmRC mfsChangelogSetDate(MfsChangelog entry, time_t date);
rpmRC mfsChangelogSetName(MfsChangelog entry, const char *name);
rpmRC mfsChangelogSetText(MfsChangelog entry, const char *text);

// Dependencies

void mfsDepsFree(MfsDeps deps);
int mfsDepsCount(MfsDeps deps);
rpmRC mfsDepsAppend(MfsDeps deps, MfsDep dep);
rpmRC mfsDepsInsert(MfsDeps deps, MfsDep dep, int index);
rpmRC mfsDepsDelete(MfsDeps deps, int index);
const MfsDep mfsDepsGetEntry(MfsDeps deps, int index);

MfsDep mfsDepNew(void);
MfsDep mfsDepCopy(MfsDep dep);
void mfsDepFree(MfsDep dep);
char *mfsDepGetName(MfsDep entry);
char *mfsDepGetVersion(MfsDep entry);
rpmsenseFlags mfsDepGetFlags(MfsDep entry);
uint32_t mfsDepGetIndex(MfsDep entry);
rpmRC mfsDepSetName(MfsDep entry, const char *name);
rpmRC mfsDepSetVersion(MfsDep entry, const char *version);
rpmRC mfsDepSetFlags(MfsDep entry, rpmsenseFlags flags);
rpmRC mfsDepSetIndex(MfsDep entry, uint32_t index);

// Filelist

rpmRC mfsPackageAddFilesListFilelist(MfsPackage pkg, const char *path);
rpmRC mfsPackageAddFilesListEntry(MfsPackage pkg, const char *entry);

rpmRC mfsPackageAddPolicyEntry(MfsPackage pkg, const char *policy);

// File Hook Related API

const char * mfsFileGetPath(MfsFile file);
rpmRC mfsPackageAddFile(MfsPackage pkg, MfsFile file);

// Helper/Debug function - non guaranted API

rpmRC mfsChangelogSetDateStr(MfsChangelog entry, const char *date);
char *mfsDepGetFlagsStr(MfsDep entry);

/* TODO:
 * - Set constranis to module name to [A-Za-z0-9_-]
 */

#ifdef __cplusplus
}
#endif

#endif	/* _H_MFS_ */
