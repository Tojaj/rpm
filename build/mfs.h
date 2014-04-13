#ifndef	_H_MFS_
#define	_H_MFS_

#include <lib/rpmds.h>
#include <lib/rpmtypes.h>
#include <lib/rpmfiles.h>
#include <rpm/rpmvf.h>
#include <rpm/rpmmacro.h>

/** \ingroup mfs
 * \file build/mfs.h
 *  Modular File Scanner public API
 * \addtogroup mfs
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Hook significant priorities
 */
#define MFS_HOOK_MIN_PRIORITY_VAL       0 	/*!< Minimal priority value */
#define MFS_HOOK_MAX_PRIORITY_VAL       10000	/*!< Maximal priority value */
#define MFS_HOOK_DEFAULT_PRIORITY_VAL   5000	/*!< Default priority value */

/** Mfs module manager
 * Used during module registration phase (module init function)
 * and during module cleanup.
 */
typedef struct MfsManager_s * MfsManager;

/** Context
 * Context is determined by a current processed spec file.
 * During a single run of rpmbuild, multiple spec files can be built.
 */
typedef struct MfsContext_s * MfsContext;

/** Build hook
 */
typedef struct MfsBuildHook_s * MfsBuildHook;

/** File hook
 */
typedef struct MfsFileHook_s * MfsFileHook;

/** Spec file
 */
typedef struct MfsSpec_s * MfsSpec;

/** Build time script (one of %prep, %build, %install, %check, %clean)
 */
typedef struct MfsBTScript_s * MfsBTScript;

/** Package
 */
typedef struct MfsPackage_s * MfsPackage;

/** Package (install-time) script (one of %pre, %post, %preun, %postun)
 */
typedef struct MfsScript_s * MfsScript;

/** List of triggers
 */
typedef struct MfsTriggers_s * MfsTriggers;

/** Trigger
 */
typedef struct MfsTrigger_s * MfsTrigger;

/** List of changelogs
 */
typedef struct MfsChangelogs_s * MfsChangelogs;

/** Changelog
 */
typedef struct MfsChangelog_s * MfsChangelog;

/** List of dependencies
 */
typedef struct MfsDeps_s * MfsDeps;

/** Dependency
 */
typedef struct MfsDep_s * MfsDep;

/** List of lines from the %files section
 */
typedef struct MfsFileLines_s * MfsFileLines;

/** List of files specified in the %files section by -f option
 */
typedef struct MfsFileFiles_s * MfsFileFiles;

/** List of policies from the %policy section
 */
typedef struct MfsPolicies_s * MfsPolicies;

/** List of processed package files
 * Available during MFS_HOOK_POINT_POSTFILEPROCESSING
 */
typedef struct MfsFiles_s * MfsFiles;

/** Single processed (analyzed) file
 */
typedef struct MfsFile_s * MfsFile;

/** Module init function
 * Its name has to follow the pattern: "init_modulename"
 * Where the "modulename" is  the filename of .so file of the module
 * without the ".so" suffix
 *
 * @param mm	    Module Manager
 * @return	    RPMRC_OK on success, RPMRC_FAIL on error
 */
typedef rpmRC (*MfsModuleInitFunc)(MfsManager mm);

/** Module clean up function
 * Called just before the module is unloaded from memory,
 * after the building is over.
 *
 * @param mm	    Module Manager
 */
typedef void (*MfsModuleCleanupFunc)(MfsManager mm);

/** Buil hook
 *
 * @param context   Current context (curently processed spec file)
 * @return	    RPMRC_OK on success, RPMRC_FAIL on error
 */
typedef rpmRC (*MfsBuildHookFunc)(MfsContext context);

/** File hook
 *
 * @param context   Current context (curently processed spec file)
 * @param file	    Currently processed file
 * @return	    RPMRC_OK on success, RPMRC_FAIL on error
 */
typedef rpmRC (*MfsFileHookFunc)(MfsContext context, MfsFile file);

/** Points during the build process in which build hooks can be called.
 */
typedef enum MfsHookPoint_e {
    MFS_HOOK_POINT_POSTPARSE,	/*!< Called after spec is parsed/before build starts */
    MFS_HOOK_POINT_POSTPREP,	/*!< Called after %prep script */
    MFS_HOOK_POINT_POSTBUILD,	/*!< Called after %build script */
    MFS_HOOK_POINT_POSTINTALL,	/*!< Called after %install script */
    MFS_HOOK_POINT_POSTCHECK,	/*!< Called after %check script. */
    MFS_HOOK_POINT_POSTFILEPROCESSING,	/*!< All files were processed and
	prepared, but not yet put in the headers. */
    MFS_HOOK_POINT_FINAL,	/*!< Called at the end. */
    MFS_HOOK_POINT_SENTINEL	/*!< The last element of the list */
} MfsHookPoint;

/** Spec file attributes
 */
typedef enum MfsSpecAttr_e {
    MFS_SPEC_ATTR_SPECFILE,	/*!< (String) */
    MFS_SPEC_ATTR_BUILDROOT,	/*!< (String) */
    MFS_SPEC_ATTR_BUILDSUBDIR,	/*!< (String) */
    MFS_SPEC_ATTR_ROOTDIR,	/*!< (String) */
    MFS_SPEC_ATTR_SOURCERPMNAME,/*!< (String) */
    MFS_SPEC_ATTR_PARSED,	/*!< (String) Parsed content */
} MfsSpecAttr;

/** Build time script types
 */
typedef enum MfsBTScriptType_e {
    MFS_SPEC_SCRIPT_PREP,	/*!< %prep */
    MFS_SPEC_SCRIPT_BUILD,	/*!< %build */
    MFS_SPEC_SCRIPT_INSTALL,	/*!< %install */
    MFS_SPEC_SCRIPT_CHECK,	/*!< %clean */
    MFS_SPEC_SCRIPT_CLEAN,	/*!< %check */
    MFS_SPEC_SCRIPT_SENTINEL	/*!< The last element of the list */
} MfsBTScriptType;

/** Package flags
 */
typedef enum MfsPackageFlags_e {
    MFS_PACKAGE_FLAG_NONE,
    MFS_PACKAGE_FLAG_SUBNAME,   /*!< Name will be used as a subname.
	e.g. Name of the main package is "foo" and the name for a new
	subpackage is "bar" -> The result subpackage name will be "foo-bar" */
} MfsPackageFlags;

/** Install time scripts flags
 */
typedef enum MfsScriptFlags_e {
    MFS_SCRIPT_FLAG_NONE	= 0,
    MFS_SCRIPT_FLAG_EXPAND 	= (1 << 0), /*!< Macro expansion */
    MFS_SCRIPT_FLAG_QFORMAT 	= (1 << 1), /*!< Header queryformat expansion */
} MfsScriptFlags;

/** Install time script types
 */
typedef enum MfsScriptType_e {
    MFS_SCRIPT_PREIN,		/*!< %prein */
    MFS_SCRIPT_POSTIN,		/*!< %postin */
    MFS_SCRIPT_PREUN,		/*!< %preun */
    MFS_SCRIPT_POSTUN,		/*!< %postun */
    MFS_SCRIPT_PRETRANS,	/*!< %pretrans */
    MFS_SCRIPT_POSTTRANS,	/*!< %posttrans */
    MFS_SCRIPT_VERIFYSCRIPT,	/*!< %verifyscript */
    MFS_SCRIPT_SENTINEL		/*!< The last element of the list */
} MfsScriptType;

/** Trigger types
 */
typedef enum MfsTriggerType_e {
    MFS_TRIGGER_IN,	    /*!< %trigger / %triggerin */
    MFS_TRIGGER_PREIN,	    /*!< %triggerprein */
    MFS_TRIGGER_UN,	    /*!< %triggerun */
    MFS_TRIGGER_POSTUN,	    /*!< %triggerpostun */
    MFS_TRIGGER_SENTINEL    /*!< The last element of the list */
} MfsTriggerType;

/** Dependency types
 */
typedef enum MfsDepType_e {
    MFS_DEP_TYPE_REQUIRES,	/*!< Require */
    MFS_DEP_TYPE_PROVIDES,	/*!< Provide */
    MFS_DEP_TYPE_CONFLICTS,	/*!< Conflict */
    MFS_DEP_TYPE_OBSOLETES,	/*!< Obsolete */
    MFS_DEP_TYPE_TRIGGERS,	/*!< Trigger */
    MFS_DEP_TYPE_ORDER,		/*!< Order */
    MFS_DEP_TYPE_RECOMMENDS,	/*!< Recommend */
    MFS_DEP_TYPE_SUGGESTS,	/*!< Suggest */
    MFS_DEP_TYPE_SUPPLEMENTS,	/*!< Supplement */
    MFS_DEP_TYPE_ENHANCES,	/*!< Enhance */
    MFS_DEP_TYPE_SENTINEL	/*!< The last element of the list */
} MfsDepType;

/** \name Logging API
 * @{
 */

/** Generate a log message
 * @param code	    Log level, value from rpmlogLvl (e.g. RPMLOG_INFO)
 * @param fmt	    Message format string
 */
void mfslog(int code, const char *fmt, ...);

/* Shortcuts for various log levels
 */
#define mfslog_debug(...)   mfslog(RPMLOG_DEBUG, __VA_ARGS__)
#define mfslog_info(...)    mfslog(RPMLOG_INFO, __VA_ARGS__)
#define mfslog_notice(...)  mfslog(RPMLOG_NOTICE, __VA_ARGS__)
#define mfslog_warning(...) mfslog(RPMLOG_WARNING, __VA_ARGS__)
#define mfslog_err(...)     mfslog(RPMLOG_ERR, __VA_ARGS__)
#define mfslog_crit(...)    mfslog(RPMLOG_CRIT, __VA_ARGS__)
#define mfslog_alert(...)   mfslog(RPMLOG_ALERT, __VA_ARGS__)
#define mfslog_emerg(...)   mfslog(RPMLOG_EMERG, __VA_ARGS__)

/**@}*/

/** \name Module initialization API
 * @{
 */

/** About the priority attribute
 *
 * Priority is number between 0-10000
 * Default priority is 5000
 * 0 - Maximum priority
 * 10000 - Minimal priority
 * To be deterministic:
 *  1) If there are hooks in multiple modules which have the same priority
 * then their module names are used as the second sorting key.
 * FIFO cannot be used here, because module load order depends on
 * used filesystem and thus it is not deterministic.
 *  2) If there are multiple hooks with the same priority from the same module,
 * then LIFO approach is used (later registered function is called first).
 */

/** Allocates a new MfsBuildHook
 * @param hookfunc  Hook function
 * @param point	    Point of the build process where the hook will be called
 * @return	    Newly allocated MfsBuildHook
 */
MfsBuildHook mfsBuildHookNew(MfsBuildHookFunc hookfunc, MfsHookPoint point);

/** Sets a build hook priority
 * @param hook	    Hook
 * @param priority  Priority value 0-10000. 0 = Is max priority
 * @return	    RPMRC_OK on success
 */
rpmRC mfsBuildHookSetPriority(MfsBuildHook hook, int32_t priority);

/** Sets a hook name that will be printed in mfs log messages intead
 * of function memory address.
 * @param hook	    Hook
 * @param name	    Function name
 * @return	    RPMRC_OK on success
 */
rpmRC mfsBuildHookSetPrettyName(MfsBuildHook hook, const char *name);

/** Registers a hook to the MFS
 * The manager takes over ownership of the hook.
 * @param mm	    Module manager
 * @param hook	    Hook
 */
void mfsManagerRegisterBuildHook(MfsManager mm, MfsBuildHook hook);

/** Allocates a new MfsFileHook
 * @param hookfunc  Hook function
 * @return	    Newly allocated MfsFileHook
 */
MfsFileHook mfsFileHookNew(MfsFileHookFunc hookfunc);

/** Sets a build hook priority
 * @param hook	    Hook
 * @param priority  Priority value 0-10000. 0 = Is max priority
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileHookSetPriority(MfsFileHook hook, int32_t priority);

/** Sets a hook name that will be printed in mfs log messages intead
 * of function memory address.
 * @param hook	    Hook
 * @param name	    Function name
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileHookSetPrettyName(MfsFileHook hook, const char *name);

/** Adds a glob pattern
 * Multiple patterns can be set to one file hook.
 * If at least one matches with a processed file,
 * the file hook will be called.
 * @param hook	    Hook
 * @param glob	    Wildcard pattern
 */
void mfsFileHookAddGlob(MfsFileHook hook, const char *glob);

/** Registers a hook to the MFS
 * The manager take over ownership of the hook.
 * @param mm	    Module manager
 * @param hook	    Hook
 */
void mfsManagerRegisterFileHook(MfsManager mm, MfsFileHook hook);

/** Sets a module clean up function
 * Useful for cleaning up of module data or printing some statistics.
 * @param mm	    Module manager
 * @param func	    Clean up function
 * @return	    RPMRC_OK on success
 */
rpmRC mfsManagerSetCleanupFunc(MfsManager mm, MfsModuleCleanupFunc func);

/** Sets a global data related to the module
 * @param mm	    Module manager
 * @return	    Pointer to global data or NULL if no data are set
 */
void *mfsManagerGetGlobalData(MfsManager mm);

/** Gets a global data related to the module
 * @param mm	    Module manager
 * @param data	    Pointer to global data
 */
void mfsManagerSetGlobalData(MfsManager mm, void *data);

/**@}*/

/** \name Context API
 * @{
 */

/** Gets a global data related to the module
 * @param context   Current context
 * @return	    Pointer to global data or NULL if no data are set
 */
void *mfsContextGetGlobalData(MfsContext context);

/** Sets a global data related to the module
 * @param context   Current context
 * @param data	    Pointer to global data
 */
void mfsContextSetGlobalData(MfsContext context, void *data);

/** Gets a context related data
 *
 * This is preffered method to store data that should be
 * persistent between callbacks.
 * Context related data are bound to the current spec file.
 * All callbacks registered by the module and working with the same
 * spec file can access this data.
 *
 * During the build process, multiple spec files can be parsed
 * and builded, thus each callback can be called multiple times,
 * for different spec files.
 *
 * @param context   Current context
 * @return	    Pointer to context related data
 */
void *mfsContextGetData(MfsContext context);

/** Sets a context related data
 * For more information see mfsContextGetData()
 * @param context   Current context
 * @param data	    Pointer to context related data
 */
void mfsContextSetData(MfsContext context, void *data);

/** Gets a current spec file
 * @param context   Current context
 * @return	    Newly allocated  MfsSpec representing the current context
 */
MfsSpec mfsContextGetSpec(MfsContext context);

/**@}*/

/** \name Spec API
 * @{
 */

/** Gets a string with spec attribute
 * @param spec	    Spec file
 * @param attr	    Attribute
 * @return	    Newly allocated string with attribute value or NULL
 */
char * mfsSpecGetString(MfsSpec spec, MfsSpecAttr attr);

/** Sets (override) a string with spec attribute
 * @param spec	    Spec file
 * @param attr	    Attribute
 * @param str	    New value for the attribute
 * @return	    RPMRC_OK on success
 */
rpmRC mfsSpecSetString(MfsSpec spec, MfsSpecAttr attr, const char *str);

/** Returns the number of (sub)packages defined in the spec file
 * (the source package is not included in the count)
 * @param spec	    Spec file
 * @return	    Number of packages (except for the source package)
 */
int mfsSpecPackageCount(MfsSpec spec);

/** Gets a newly allocated representation of the selected package
 * @param spec	    Spec file
 * @param index	    Index of the package
 * @return	    Newly allocated MfsPackage
 *		    or NULL if the index is bad
 */
MfsPackage mfsSpecGetPackage(MfsSpec spec, int index);

/** Gets a newly allocated representation of the source package
 * @param spec	    Spec file
 * @return	    Newly allocated MfsPackage
 *		    or NULL if source package doesn't exist
 */
MfsPackage mfsSpecGetSourcePackage(MfsSpec spec);

/** Gets pointer to the macro context
 * @param spec	    Spec file
 * @returns	    Pointer to the macro context of the spec file
 */
rpmMacroContext mfsSpecGetMacroContext(MfsSpec spec);

/** Expands a macro by the context of a spec file
 * Note: Currently the whole RPM library uses only a single global context
 * and even each spec file context is basically the global one.
 * Therefore all functions for expanding from the RPM macro API can
 * be safely used too.
 * @param spec	    Spec file
 * @param sbuf	    Target buffer
 * @param slen	    Length of the target buffer
 * @return	    RPMRC_OK on success
 */
rpmRC mfsSpecExpandMacro(MfsSpec spec, char *sbuf, size_t slen);

/** Gets a build time script
 * @param spec	    Spec file
 * @param type	    Build time script type
 * @return	    Newly allocated build time script
 *		    or NULL if the specified type is bad
 */
MfsBTScript mfsSpecGetScript(MfsSpec spec, MfsBTScriptType type);

/** Sets a build time script
 * The function doesn't take over the ownership of the script.
 * @param spec	    Spec file
 * @param script    Script
 * @param type	    Build time script type
 * @return	    RPMRC_OK on success
 */
rpmRC mfsSpecSetScript(MfsSpec spec, MfsBTScript script, MfsBTScriptType type);

/** Frees a spec file
 * @param spec	    Spec file
 */
void mfsSpecFree(MfsSpec spec);

/**@}*/

/** \name Buil-time script API
 * @{
 */

/** Frees a build time script
 * @param script    Build time script
 */
void mfsBTScriptFree(MfsBTScript script);

/** Gets a build time of script source code
 * @param script    Build time script
 * @return	    Newly allocated string with the source code
 */
char *mfsBTScriptGetCode(MfsBTScript script);

/** Sets a build time script source code
 * @param script    Build time script
 * @param code	    Source code
 * @return	    RPMRC_OK on success
 */
rpmRC mfsBTScriptSetCode(MfsBTScript script, const char *code);

/** Appends a source code to the script
 * @param script    Build time script
 * @param code	    Source code
 * @return	    RPMRC_OK on success
 */
rpmRC mfsBTScriptAppend(MfsBTScript script, const char *code);

/** Appends a source code to the script + add new line at the end
 * @param script    Build time script
 * @param code	    Source code
 * @return	    RPMRC_OK on success
 */
rpmRC mfsBTScriptAppendLine(MfsBTScript script, const char *code);

/**@}*/

/** \name Package API
 * @{
 */

/** Gets a spec file of a package
 * @param pkg	    Package
 * @return	    Newly allocated MfsSpec representing the spec file
 */
MfsSpec mfsPackageGetSpec(MfsPackage pkg);

/** Frees a package
 * @param pkg	    Package
 */
void mfsPackageFree(MfsPackage pkg);

/** Gets the unique id of a package
 *
 * Each MfsPackage is always newly allocated and its memory address
 * thus cannot be used for unequivocal identification of the package.
 * This function returns the memory address of the underlying rpmbuild
 * package and this address can be used for a unique identification,
 * which is helpful for package comparison.
 *
 * @param pkg	    Package
 * @return	    Unique package id (the memory address of the underlaying
 *		    rpmbuild package structure)
 */
void *mfsPackageId(MfsPackage pkg);

/** Adds a new package to the context
 * @param context   Current context
 * @param name	    Name of the package
 * @param summary   Short summary
 * @param flags	    MfsPackageFlags flags
 * @return	    Newly allocated package
 */
MfsPackage mfsPackageNew(MfsContext context,
			 const char *name,
			 const char *summary,
			 int flags);

/** Does sanity checks on the newly added package
 * and fill important header fields like
 * target arch, platform, os, etc.
 *
 * Call this function on each newly package added
 * by mfsPackageNew() function right after the all
 * important items (description) have been set.
 *
 * @param mfspkg    Package
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageFinalize(MfsPackage mfspkg);

/** Gets a package header
 * @param pkg	    Package
 * @return	    Pointer to the header of the package
 */
Header mfsPackageGetHeader(MfsPackage pkg);

/** Gets a package name
 * @param pkg	    Package
 * @return	    Pointer to the package name
 */
const char *mfsPackageName(MfsPackage pkg);

/** Returns a NULL-terminated list of preable tags
 * that can be used in mfsPackageSetTag()
 * @return	    NULL-terminated list of supported preable tags
 */
const rpmTagVal * mfsPackageTags(void);

/** Sets a preamble tag to the package
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

/** Gets a description
 * @param pkg	    Package
 * @return	    Newly allocated string with the current package description
 */
char *mfsPackageGetDescription(MfsPackage pkg);

/** Sets a description
 * @param pkg		Package
 * @param description	Description for the package
 * @param lang		Language ISO 639-1 codes (locale code) or NULL
 * @return		RPMRC_OK on success
 */
rpmRC mfsPackageSetDescription(MfsPackage pkg,
			       const char *description,
			       const char *lang);

/** Gets an install-time script of a package
 * @param pkg	    Package
 * @param type	    Script type (%prein, %postin, %preun, ...)
 * @return	    Newly allocated script represented by MfsScript
 */
MfsScript mfsPackageGetScript(MfsPackage pkg, MfsScriptType type);

/** Sets an install-time script to a package
 * The function doesn't take over the ownership of the script.
 * @param pkg	    Package
 * @param script    Script
 * @param type	    Script type (%prein, %postin, %preun, ...)
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageSetScript(MfsPackage pkg, MfsScript script, MfsScriptType type);

/** Deletes an install-time script from a package
 * @param pkg	    Package
 * @param type	    Script type
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageDeleteScript(MfsPackage pkg, MfsScriptType type);

/** Gets a list of triggers from a package
 * @param pkg	    Package
 * @return	    Newly allocated list MfsTriggers or NULL
 */
MfsTriggers mfsPackageGetTriggers(MfsPackage pkg);

/** Sets a list of triggers to a package
 * The function doesn't take over the ownership of the list.
 * @param pkg	    Package
 * @param triggers  List of triggers
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageSetTriggers(MfsPackage pkg, MfsTriggers triggers);

/** Gets a list of changelogs from a package
 * @param pkg	    Package
 * @return	    Newly allocated list MfsChangelogs
 */
MfsChangelogs mfsPackageGetChangelogs(MfsPackage pkg);

/** Sets a list of changelogs to a package
 * The function doesn't take over the ownership of the list.
 * @param pkg	    Package
 * @param changelog List of changelogs
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageSetChangelogs(MfsPackage pkg, MfsChangelogs changelog);

/** Gets a list of dependencies from a package
 * @param pkg	    Package
 * @param deptype   Type of dependencies
 * @return	    Newly allocated list MfsDeps
 */
MfsDeps mfsPackageGetDeps(MfsPackage pkg, MfsDepType deptype);

/** Sets a list of dependencies to a package
 * The function doesn't take over the ownership of the list.
 * @param pkg	    Package
 * @param deps	    List of dependencies
 * @param deptype   Type of dependencies
 */
rpmRC mfsPackageSetDeps(MfsPackage pkg, MfsDeps deps, MfsDepType deptype);

/** Gets a list of lines of %files section from package
 * @param pkg	    Package
 * @return	    Newly allocated list MfsFileLines
 */
MfsFileLines mfsPackageGetFileLines(MfsPackage pkg);

/** Sets a list of lines of %files section to package
 * The function doesn't take over the ownership of the list.
 * @param pkg	    Package
 * @param flines    List of lines
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageSetFileLines(MfsPackage pkg, MfsFileLines flines);

/** Gets a list of filenames specified at %files section by the -f option
 * @param pkg	    Package
 * @return	    Newly allocated list MfsFileFiles
 */
MfsFileFiles mfsPackageGetFileFiles(MfsPackage pkg);

/** Sets a list of filenames specified at %files section by the -f option
 * The function doesn't take over the ownership of the list.
 * @param pkg	    Package
 * @param filefiles List of filenames
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageSetFileFiles(MfsPackage pkg, MfsFileFiles filefiles);

/** Gets a list of SELinux policies specified in %policies section
 * @param pkg	    Package
 * @return	    Newly allocated list MfsPolicies
 */
MfsPolicies mfsPackageGetPolicies(MfsPackage pkg);

/** Sets a list of SELinux policies specified in %policies section
 * The function doesn't take over the ownership of the list.
 * @param pkg	    Package
 * @param policies  List of policies
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageSetPolicies(MfsPackage pkg, MfsPolicies policies);

/** Gets a list of files that were processed and prepared for a package
 * This list is only available at the MFS_HOOK_POINT_POSTFILEPROCESSING
 * point of build process.
 * @param pkg	    Package
 * @return	    Newly allocated list MfsFiles
 */
MfsFiles mfsPackageGetFiles(MfsPackage pkg);

/**@}*/

/** \name Install-time script API
 * @{
 */

/** Allocates new install-time script
 * @return	    Newly allocated install-time script
 */
MfsScript mfsScriptNew(void);

/** Copies install-time script
 * @param script    Install-time script
 * @return	    Copy of the script
 */
MfsScript mfsScriptCopy(MfsScript script);

/** Frees install-time script
 * @param script    Install-time script
 */
void mfsScriptFree(MfsScript script);

/** Gets install-time script source code
 * @param script    Install-time script
 * @return	    Newly allocated string with script source code or NULL
 */
char *mfsScriptGetCode(MfsScript script);

/** Gets install-time script interpreter
 * @param script    Install-time script
 * @return	    Newly allocated string with script interpreter or NULL
 */
char *mfsScriptGetProg(MfsScript script);

/** Gets install-time script filename
 * @param script    Install-time script
 * @return	    Newly allocated string with script filename or NULL
 */
char *mfsScriptGetFile(MfsScript script);

/** Gets script flags
 * @param script    Install-time script
 * @return	    Script flag
 */
MfsScriptFlags mfsScriptGetFlags(MfsScript script);

/** Sets install-time script source code
 * @param script    Install-time script
 * @param code	    Source code
 * @return	    RPMRC_OK on success
 */
rpmRC mfsScriptSetCode(MfsScript script, const char *code);

/** Sets install-time script interpreter
 * @param script    Install-time script
 * @param prog	    Interpreter
 * @return	    RPMRC_OK on success
 */
rpmRC mfsScriptSetProg(MfsScript script, const char *prog);

/** Sets install-time script filename
 * @param script    Install-time script
 * @param code	    Filename
 * @return	    RPMRC_OK on success
 */
rpmRC mfsScriptSetFile(MfsScript script, const char *fn);

/** Sets install-time script flags
 * @param script    Install-time script
 * @param flags	    Flags
 * @return	    RPMRC_OK on success
 */
rpmRC mfsScriptSetFlags(MfsScript script, MfsScriptFlags flags);

/**@}*/

/** \name Trigger list API
 * @{
 */

/** Frees trigger
 */
void mfsTriggersFree(MfsTriggers triggers);

/** Returns a number or triggers in a list
 * @param triggers  List of triggers
 * @return	    Number of triggers in the list
 */
int mfsTriggersCount(MfsTriggers triggers);

/** Appends a trigger to a list
 * The list takes over ownership of the trigger.
 * @param triggers  List of triggers
 * @param entry	    Trigger
 * @return	    RPMRC_OK on success
 */
rpmRC mfsTriggersAppend(MfsTriggers triggers, MfsTrigger entry);

/** Inserts a trigger into a list
 * The the list takes over ownership of the trigger.
 * @param triggers  List of triggers
 * @param entry	    Trigger
 * @param index	    Index to insert
 * @return	    RPMRC_OK on success
 */
rpmRC mfsTriggersInsert(MfsTriggers triggers, MfsTrigger entry, int index);

/** Deletes a trigger from a list
 * @param triggers  List of triggers
 * @param index	    Index of trigger to delete
 * @return	    RPMRC_OK on success
 */
rpmRC mfsTriggersDelete(MfsTriggers triggers, int index);

/** Gets a reference to a trigger from a list
 * @param triggers  List of triggers
 * @param index	    Trigger index
 * @return	    Pointer to a trigger in the list
 */
const MfsTrigger mfsTriggersGetEntry(MfsTriggers triggers, int index);

/**@}*/

/** \name Trigger API
 * @{
 */

/** Allocates a new trigger
 * @return	    Newly alloced trigger
 */
MfsTrigger mfsTriggerNew(void);

/** Copies a trigger
 * @param trigger   Trigger
 * @return	    Copy of the trigger
 */
MfsTrigger mfsTriggerCopy(MfsTrigger trigger);

/** Frees a trigger
 * @param trigger   Trigger
 */
void mfsTriggerFree(MfsTrigger trigger);

/** Gets a type of a trigger
 * @param trigger   Trigger
 * @return	    Type of the trigger
 */
MfsTriggerType mfsTriggerGetType(MfsTrigger trigger);

/** Sets a type of a trigger
 * @param trigger   Trigger
 * @param type	    Type of the trigger
 * @return	    RPMRC_OK on success
 */
rpmRC mfsTriggerSetType(MfsTrigger trigger, MfsScriptType type);

/** Gets a MfsScript representing trigger script
 * @param trigger   Trigger
 * @return	    Newly allocated trigger script
 */
MfsScript mfsTriggerGetScript(MfsTrigger trigger);

/** Sets a script to a trigger
 * The trigger takes over ownership of the script
 * @param trigger   Trigger
 * @param script    Script
 * @return	    RPMRC_OK on success
 */
rpmRC mfsTriggerSetScript(MfsTrigger trigger, MfsScript script);

/** Gets a list of trigger dependencies
 * @param trigger   Trigger
 * @return	    Newly allocated list of dependencies
 */
MfsDeps mfsTriggerGetDeps(MfsTrigger trigger);

/** Sets a list of dependencies to a trigger
 * The trigger takes over ownership of the dependency list
 * @param trigger   Trigger
 * @param deps	    List of dependencies
 * @return	    RPMRC_OK on success
 */
rpmRC mfsTriggerSetDeps(MfsTrigger trigger, MfsDeps deps);

/**@}*/

/** \name Changelog list API
 * @{
 */

/** Frees a list of changelogs
 * @param changelogs	List of changelogs
 */
void mfsChangelogsFree(MfsChangelogs changelogs);

/** Returns the number of changelogs in a list
 * @param changelogs	List of changelogs
 * @return		Number of changelogs in the list
 */
int mfsChangelogsCount(MfsChangelogs changelogs);

/** Appends a changelog to a list of changelogs
 * The the list takes over ownership of the changelog.
 * @param changelogs	List of changelogs
 * @param entry		Changelog
 * @return		RPMRC_OK on success
 */
rpmRC mfsChangelogsAppend(MfsChangelogs changelogs, MfsChangelog entry);

/** Inserts a changelog into a list of changelogs
 * The the list takes over ownership of the changelog.
 * @param changelogs	List of changelogs
 * @param entry		Changelog
 * @param index		Index
 * @return		RPMRC_OK on success
 */
rpmRC mfsChangelogsInsert(MfsChangelogs changelogs, MfsChangelog entry, int index);

/** Deltes a changelog from a list
 * @param changelogs	List of changelogs
 * @param index		Index of changelog to delete
 * @return		RPMRC_OK on success
 */
rpmRC mfsChangelogsDelete(MfsChangelogs changelogs, int index);

/** Gets a reference to a changelog from a list
 * @param changelogs	List of changelogs
 * @param index		Changelog index
 * @return		Pointer to a changelog in the list
 */
const MfsChangelog mfsChangelogsGetEntry(MfsChangelogs changelogs, int index);

/**@}*/

/** \name Changelog API
 * @{
 */

/** Allocates a new changelog
 * @return	    Newly allocated changelog
 */
MfsChangelog mfsChangelogNew(void);

/** Copies a changelog
 * @param entry	    Changelog
 * @return	    Copy of the changelog
 */
MfsChangelog mfsChangelogCopy(MfsChangelog entry);

/** Frees a changelog
 * @param entry	    Changelog
 */
void mfsChangelogFree(MfsChangelog entry);

/** Gets a date of a changelog
 * @param entry	    Changelog
 * @return	    Date of the changelog
 */
time_t mfsChangelogGetDate(MfsChangelog entry);

/** Gets a date of a changelog as a string
 * @param entry	    Changelog
 * @return	    Newly allocated string with date of the changelog
 */
char *mfsChangelogGetDateStr(MfsChangelog entry);

/** Gets a name of a changelog author
 * @param entry	    Changelog
 * @return	    Newly allocated string with author's name
 */
char *mfsChangelogGetName(MfsChangelog entry);

/** Gets a text of a changelog
 * @param entry	    Changelog
 * @return	    Newly allocated string with text of the changelog
 */
char *mfsChangelogGetText(MfsChangelog entry);

/** Sets a date to a changelog
 * @param entry	    Changelog
 * @param date	    Date
 * @return	    RPMRC_OK on success
 */
rpmRC mfsChangelogSetDate(MfsChangelog entry, time_t date);

/** Sets author's name to a changelog
 * @param entry	    Changelog
 * @param name	    Author's name
 * @return	    RPMRC_OK on success
 */
rpmRC mfsChangelogSetName(MfsChangelog entry, const char *name);

/** Sets a text to a changelog
 * @param entry	    Changelog
 * @param text	    Changelog text
 * @return	    RPMRC_OK on success
 */
rpmRC mfsChangelogSetText(MfsChangelog entry, const char *text);

/**@}*/

/** \name Dependency list API
 * @{
 */

/** Allocates a new list of dependencies
 * @return	    Newly allocated list of dependencies
 */
MfsDeps mfsDepsNew(void);

/** Frees list of dependencies
 * @param deps	    List of dependencies
 */
void mfsDepsFree(MfsDeps deps);

/** Copies list of dependencies
 * @param deps	    List of dependencies
 * @return	    Copy of the list
 */
MfsDeps mfsDepsCopy(MfsDeps deps);

/** Returns the number of dependencies in a list
 * @param deps	    List of dependencies
 * @return	    Number of dependencies in the list
 */
int mfsDepsCount(MfsDeps deps);

/** Appends a dependency to a list
 * The the list takes over ownership of the dependency.
 * @param deps	    List of dependencies
 * @param dep	    Dependency
 * @return	    RPMRC_OK on success
 */
rpmRC mfsDepsAppend(MfsDeps deps, MfsDep dep);

/** Inserts a dependency into a list to a specified index
 * The the list takes over ownership of the dependency.
 * @param deps	    List of dependencies
 * @param dep	    Dependency
 * @param index	    Index
 * @return	    RPMRC_OK on success
 */
rpmRC mfsDepsInsert(MfsDeps deps, MfsDep dep, int index);

/** Deletes a dependency from a specified index of a list
 * @param deps	    List of dependencies
 * @param index	    Index of dependency to delete
 * @return	    RPMRC_OK on success
 */
rpmRC mfsDepsDelete(MfsDeps deps, int index);

/** Gets a reference to a dependency from a list
 * @param deps	    List of dependencies
 * @param index	    Dependency index
 * @return	    Pointer to a dependency in the list
 */
const MfsDep mfsDepsGetEntry(MfsDeps deps, int index);

/**@}*/

/** \name Dependency API
 * @{
 */

/** Allocates a new dependency
 * @return	    Newly allocated dependency
 */
MfsDep mfsDepNew(void);

/** Copies a dependency
 * @param dep	    Dependency
 * @return	    Copy of the dependency
 */
MfsDep mfsDepCopy(MfsDep dep);

/** Frees a dependency
 * @param dep	    Dependency
 */
void mfsDepFree(MfsDep dep);

/** Gets a name of a dependency
 * @param dep	    Dependency
 * @return	    Newly allocated string with name of the dependency
 */
char *mfsDepGetName(MfsDep dep);

/** Gets a version of a dependency
 * @param dep	    Dependency
 * @return	    Newly allocated string with version of the dependency
 */
char *mfsDepGetVersion(MfsDep dep);

/** Gets a flags of a dependency
 * Note: For available flags see enum rpmsenseFlags_e from rpmds.h
 * @param dep	    Dependency
 * @return	    Flags of dependency
 */
rpmsenseFlags mfsDepGetFlags(MfsDep dep);

/** Gets a index of a dependency.
 * Note: Only applicable on trigger dependencies
 * @param dep	    Dependency
 * @return	    Index of the related trigger
 */
uint32_t mfsDepGetIndex(MfsDep dep);

/** Sets a dependency name
 * @param dep	    Dependency
 * @param name	    Name (e.g. "python2")
 * @return	    RPMRC_OK on success
 */
rpmRC mfsDepSetName(MfsDep dep, const char *name);

/** Sets a dependency version
 * @param dep	    Dependency
 * @param version   Version (e.g. "2.6")
 * @return	    RPMRC_OK on success
 */
rpmRC mfsDepSetVersion(MfsDep dep, const char *version);

/** Sets a dependency flags
 * Note: For available flags see enum rpmsenseFlags_e from rpmds.h
 * @param dep	    Dependency
 * @param flags	    Flags (e.g. RPMSENSE_EQUAL|RPMSENSE_GREATER)
 * @return	    RPMRC_OK on success
 */
rpmRC mfsDepSetFlags(MfsDep dep, rpmsenseFlags flags);

/** Sets a dependency index
 * Note: Only applicable on trigger dependencies
 * @param dep	    Dependency
 * @param index	    Index
 * @return	    RPMRC_OK on success
 */
rpmRC mfsDepSetIndex(MfsDep dep, uint32_t index);

/**@}*/

/** \name %files API - Lines
 * @{
 */

/** Frees a list of lines from %files section
 * @param flines    List of lines
 */
void mfsFileLinesFree(MfsFileLines flines);

/** Returns the number of lines in a list
 * @param flines    List of lines
 * @return	    Number of lines in the list
 */
int mfsFileLinesCount(MfsFileLines flines);

/** Gets a line from a list
 * @param flines    List of lines
 * @param index	    Index of line
 * @return	    Pointer to a string with the line or NULL.
 *		    The string is owned by the list.
 */
const char *mfsFileLinesGetLine(MfsFileLines flines, int index);

/** Appends a line to a list of lines
 * @param fline	    List of lines
 * @param line	    Line
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileLinesAppend(MfsFileLines flines, const char *line);

/** Delete a line from a list of lines
 * @param fline	    List of lines
 * @param index	    Index
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileLinesDelete(MfsFileLines flines, int index);

/** Returns a list with all lines
 * The list is NULL-terminated list of strings.
 * @param fline	    List of lines
 * @return	    Newly allocated NULL-terminated list of strings or NULL
 */
ARGV_t mfsFileLinesGetAll(MfsFileLines flines);

/**@}*/

/** \name %files API - Files (specified by -f option)
 * @{
 */

/** Frees a list of filenames specified by -f at the %files section
 * @param ffiles    List of filenames
 */
void mfsFileFilesFree(MfsFileFiles ffiles);

/** Returns the number of items in a list
 * @param ffiles    List of files
 * @return	    Number of items in the list
 */
int mfsFileFilesCount(MfsFileFiles ffiles);

/** Gets a filename from a list of files
 * @param ffiles    List of files
 * @param index	    Index of line
 * @return	    Pointer to a string with the filename or NULL.
 *		    The string is owned by the list.
 */
const char *mfsFileFilesGetFn(MfsFileFiles ffiles, int index);

/** Appends an item to a list of files
 * @param ffiles    List of files
 * @param fn	    Filename
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileFilesAppend(MfsFileFiles ffiles, const char *fn);

/** Delete an item from a list of files
 * @param ffiles    List of files
 * @param index	    Index
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileFilesDelete(MfsFileFiles ffiles, int index);

/** Returns a list with all items.
 * The list is NULL-terminated list of strings.
 * @param ffiles    List of files
 * @return	    Newly allocated NULL-terminated list of strings or NULL
 */
ARGV_t mfsFileFilesGetAll(MfsFileFiles ffiles);

/**@}*/

/** \name Policies API
 * @{
 */

/** Frees a list of policies
 * @param policies    List of policies
 */
void mfsPoliciesFree(MfsPolicies policies);

/** Returns the number of policies in a list
 * @param policies  List of policies
 * @return	    Number of items in the list
 */
int mfsPoliciesCount(MfsPolicies policies);

/** Gets a policy from a list of policies
 * @param policies  List of policies
 * @param index	    Index of policy
 * @return	    Pointer to a string with the policy or NULL.
 *		    The string is owned by the list.
 */
const char *mfsPoliciesGetPolicy(MfsPolicies policies, int index);

/** Appends an item to a list of policies
 * @param policies  List of policies
 * @param policy    Line with policy
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPoliciesAppend(MfsPolicies policies, const char *policy);

/** Delete an item from a list of policies
 * @param policies  List of policies
 * @param index	    Index
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPoliciesDelete(MfsPolicies policies, int index);

/** Returns a list with all items.
 * The list is NULL-terminated list of strings.
 * @param policies  List of policies
 * @return	    Newly allocated NULL-terminated list of strings or NULL
 */
ARGV_t mfsPoliciesGetAll(MfsPolicies policies);

/**@}*/

/** \name processed files list API
 * @{
 */

/** Frees a list of processed files
 * @param files	    List of processed files
 */
void mfsFilesFree(MfsFiles files);

/** Returns the number of processed file in a list
 * @param files	    List of processed files
 * @return	    Number of items in the list
 */
int mfsFilesCount(MfsFiles files);

/** Gets a reference to a file from a list
 * @param file	    List of processed files
 * @param index	    Index
 * @return	    Pointer to a file in the list
 */
const MfsFile mfsFilesGetEntry(MfsFiles files, int index);

/**@}*/

/** \name Processed file API
 * @{
 */

/** Gets a disk path of a file
 * @param file	    Processed file
 * @return	    Disk path of the file
 */
const char * mfsFileGetPath(MfsFile file);

/** Adds a file to a package
 * Note: Only available in file hooks.
 * @param pkg	    Package
 * @param file	    Processed file
 * @return	    RPMRC_OK on success
 */
rpmRC mfsPackageAddFile(MfsPackage pkg, MfsFile file);

/** Checks if a file will be included into its original destination
 * (package) from which it came from.
 * @param file	    Processed file
 * @return	    0 if file won't be put to its original package
 */
int mfsFileGetToOriginal(MfsFile file);

/** Sets if a file should be included in its original destination (package)
 * @param file	    Processed file
 * @param val	    0 if file should'n be put to its original package,
 *		    1 if it should
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetToOriginal(MfsFile file, int val);

/** Gets a file status
 * @param file	    Processed file
 * @param st	    Pointer to a status structure
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileGetStat(MfsFile file, struct stat *st);

/** Sets a file status
 * @param file	    Processed files
 * @param st	    Pointer to a status structure
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetStat(MfsFile file, struct stat *st);

/** Gets a disk path of a file
 * @param file	    Processed file
 * @return	    Disk path of the file
 */
const char *mfsFileGetDiskPath(MfsFile file);

/** Sets a disk path of a file
 * @param file	    Processed file
 * @param path	    Disk path
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetDiskPath(MfsFile file, const char *path);

/** Gets a file path that will be stored in package
 * @param file	    Processed file
 * @return	    File path in package
 */
const char *mfsFileGetCpioPath(MfsFile file);

/** Sets a file path that will be stored in package
 * @param file	    Processed file
 * @param path	    File path
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetCpioPath(MfsFile file, const char *path);

/** Gets uname
 * @param file	    Processed file
 * @return	    uname
 */
const char *mfsFileGetUname(MfsFile file);

/** Sets uname
 * @param file	    Processed file
 * @param uname	    uname
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetUname(MfsFile file, const char *uname);

/** Gets gname (group name)
 * @param file	    Processed file
 * @return	    gname
 */
const char *mfsFileGetGname(MfsFile file);

/** Sets gname (group name)
 * @param fine	    Processed file
 * @param gname	    gname
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetGname(MfsFile file, const char *gname);

/** Gets file flags
 * Note: See rpmfileAttrs_e for available flags
 * @param file	    Processed file
 * @return	    File flags
 */
rpmFlags mfsFileGetFlags(MfsFile file);

/** Sets file flags
 * @param file	    Processed file
 * @param flags	    File flags
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetFlags(MfsFile file, rpmFlags flags);

/** Gets verification flags
 * @param file	    Processed file
 * @return	    Verify flags
 */
rpmVerifyFlags mfsFileGetVerifyFlags(MfsFile file);

/** Sets verification flags
 * @param file	    Processed file
 * @param flags	    Verify flags
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetVerifyFlags(MfsFile file, rpmVerifyFlags flags);

/** Gets a list of file languages
 * @param file	    Processed file
 * @return	    Newly allocated NULL-terminated list of strings or NULL.
 *		    Caller has to free the list including all its elements.
 */
ARGV_t mfsFileGetLangs(MfsFile file);

/** Sets a list of file languages
 * @param file	    Processed file
 * @param langs	    NULL-terminated list of strings or NULL
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetLangs(MfsFile file, const ARGV_t langs);

/** Gets file capabilities
 * @param file	    Processed file
 * @return	    RPMRC_OK on success
 */
const char *mfsFileGetCaps(MfsFile file);

/** Sets file capabilities
 * @param file	    Processed file
 * @param caps	    File capabilities
 * @return	    RPMRC_OK on success
 */
rpmRC mfsFileSetCaps(MfsFile file, const char *caps);

// Data filled by classificator

/** Gets file color
 * Note: See enum FCOLOR_e for available colors
 * @param file	    Processed file
 * @return	    File color
 */
rpm_color_t mfsFileGetColor(MfsFile file);

/** Gets a list of file attributes.
 * E.g. "elf", etc.
 * @param file	    Processed file
 * @return	    Pointer to NULL-terminated list of strings or NULL.
 *		    This lists is owned by the file and you shouldn't
 *		    free or modify it!
 */
const ARGV_t mfsFileGetAttrs(MfsFile file);

/** Gets a file type.
 * E.g. "C source, ASCII text", "directory", etc.
 * @param file	    Processed file
 * @return	    File type
 */
const char *mfsFileGetType(MfsFile file);

/** The number of packages which includes a file
 * Note: Only available in file hooks
 * @param file	    Processed file
 * @return	    Number of packages
 */
int mfsFileOwningPackagesCount(MfsFile file);

/** Gets a package that owns a file
 * @param file	    Processed file
 * @param index	    Index of a package
 * @return	    Newly allocated package or NULL
 */
MfsPackage mfsFileOwningPackage(MfsFile file, int index);

/** Gets a package that is original destination for a file
 * @param file	    Processed file
 * @return	    Newly allocated package or NULL
 */
MfsPackage mfsFileGetOriginalDestination(MfsFile file);

/**@}*/

/** \name Helper/Debug API
 * Non guaranted API
 * @{
 */

int mfsAsprintf(char **strp, const char *fmt, ...);
rpmRC mfsChangelogSetDateStr(MfsChangelog entry, const char *date);
char *mfsDepGetFlagsStr(MfsDep entry);
char *mfsSpecGetArch(MfsSpec spec);

/**@}*/

/** \name Experimental API
 * Experimental API
 * @{
 */

rpmRC mfsPackageGenerateDepends(MfsPackage pkg, ARGV_t files,
                                rpm_mode_t *fmodes, rpmFlags *fflags);

/**@}*/

/** @} */

#ifdef __cplusplus
}
#endif

#endif	/* _H_MFS_ */
