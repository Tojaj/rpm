#ifndef	_MFS_INTERNAL_H_
#define	_MFS_INTERNAL_H_

#include "mfs.h"
#include "rpmfc.h"
#include "rpmfc_internal.h"
#include "rpmbuild_internal.h"
#include "lib/rpmscript.h"	/* script flags */

#ifdef __cplusplus
extern "C" {
#endif

#define LOGPREFIX	"mfs: "
#define MFSMODULESDIR   "mfsmodules"

typedef struct MfsModuleContext_s * MfsModuleContext;

/** State of this context
 */
typedef enum MfsCtxState_e {
    MFS_CTXSTATE_UNKNOWN,	/*!< Context isn't currently used */
    MFS_CTXSTATE_BUILDHOOK,	/*!< A parser hook is using the context */
    MFS_CTXSTATE_FILEHOOK,	/*!< A file hook is using the context */
} MfsCtxState;

/** Context related to a module and a spec file.
 */
struct MfsContext_s {
    MfsModuleContext modulecontext; /*!< Parent module context */
    MfsCtxState state;		    /*!< State of this context */
    MfsHookPoint lastpoint;	    /*!< Last point the context was used at
					 or current point at which the context
					 is being used */
    rpmSpec spec;		    /*!< Related spec file */
    void *userdata;		    /*!< Context related user's data */

    struct MfsContext_s *next;
};

/** Context related to a module.
 */
struct MfsModuleContext_s {
    MfsManager manager;	    /*!< Manager */
    char *modulename;	    /*!< Name of the module */
    void *globaldata;	    /*!< User's global data (module related) */
    MfsContext contexts;    /*!< MfsContexts */

    // Stuff related to the module loading stage
    // Hooks related to the context
    // Used only during registration, will be NULL after
    // the call of mfsManagerSortHooks()
    MfsBuildHook buildhooks;
    MfsFileHook filehooks;

    struct MfsModuleContext_s *next;
};

struct MfsManager_s {
    MfsModuleContext modulecontexts;

    // Sorted lists of all hooks
    MfsBuildHook buildhooks;
    MfsFileHook filehooks;

    rpmSpec mainspec; /*!<
	The spec returned from parseSpec (the one inserted to the buildSpec) */

    rpmfc fc; /*!< File classificator used during call of file hooks. */

    MfsModuleContext cur_context;  /*!< Used during loading of modules */
};

// Hooks

struct MfsBuildHook_s {
    MfsModuleContext modulecontext;
    MfsHookPoint point;
    MfsBuildHookFunc func;
    char *prettyname;
    int32_t priority;
    struct MfsBuildHook_s * next;
};

struct MfsGlob_s {
    char *glob;
    struct MfsGlob_s *next;
};

typedef struct MfsGlob_s * MfsGlob;

struct MfsFileHook_s {
    MfsModuleContext modulecontext;
    MfsFileHookFunc func;
    char *prettyname;
    int32_t priority;
    MfsGlob globs;
    struct MfsFileHook_s * next;
};

// Types related to the packaging process

struct MfsSpec_s {
    rpmSpec rpmspec;
};

struct MfsBTScript_s {
    StringBuf code;
};

struct MfsPackage_s {
    Package pkg;
    char *fullname;
    rpmSpec spec;
};

struct MfsScript_s {
    char *code;
    char *prog;
    char *file;
    rpmscriptFlags flags;
};

/* Trigger is combination of script and dependencies.
 */
struct MfsTrigger_s {
    MfsTriggerType type;
    MfsScript script;
    MfsDeps deps;
    struct MfsTrigger_s *next;
};

struct MfsTriggers_s {
    struct MfsTrigger_s *entries;
};

struct MfsChangelog_s {
    rpm_time_t time;  // uint32_t
    char *name;
    char *text;
    struct MfsChangelog_s *next;
};

struct MfsChangelogs_s {
    struct MfsChangelog_s *entries;
};

struct MfsDep_s {
    char *name;
    char *version;
    rpmsenseFlags flags; // uint32_t
    uint32_t index;	 // Only relevant for Trigger dependencies
    struct MfsDep_s *next;
};

struct MfsDeps_s {
    struct MfsDep_s *entries;
};

struct MfsFileLines_s {
    ARGV_t filelines;
};

struct MfsFileFiles_s {
    ARGV_t filefiles;
};

struct MfsPolicies_s {
    ARGV_t policies;
};

/** List for packages that includes a file
 */
struct MfsFilePackageList_s {
    Package pkg;
    rpmSpec spec;
    struct MfsFilePackageList_s *next;
};

typedef struct MfsFilePackageList_s * MfsFilePackageList;

struct MfsFile_s {
    struct FileListRec_s *flr;	/*!< RPM's internal structure representing the file */
    const char *diskpath;	/*!< Path of the file on the disk */
    int include_in_original;	/*!< Flag (0 - False, otherwise - True) */
    rpmcf classified_file;	/*!< Information from file classificator */
    Package originalpkg;	/*!< Original destination package */
    rpmSpec spec;		/*!< Current spec */
    MfsFilePackageList pkglist; /*!< List of packages that include the file */
    struct MfsFile_s *next;	/*!< Used when stored in MfsFile_s */
};

struct MfsFiles_s {
    Package pkg;
    struct MfsFile_s *files;
};

typedef const struct MfsDepMapRec_s {
    MfsDepType deptype;
    rpmTagVal nametag;
    rpmTagVal versiontag;
    rpmTagVal flagstag;
    rpmTagVal indextag;
    size_t dsoffset;
} * MfsDepMapRec;

static struct MfsDepMapRec_s const depTypeMapping[] = {
    { MFS_DEP_TYPE_REQUIRES,
      RPMTAG_REQUIRENAME,
      RPMTAG_REQUIREVERSION,
      RPMTAG_REQUIREFLAGS,
      0,
      offsetof(struct Package_s, requires) },
    { MFS_DEP_TYPE_PROVIDES,
      RPMTAG_PROVIDENAME,
      RPMTAG_PROVIDEVERSION,
      RPMTAG_PROVIDEFLAGS,
      0,
      offsetof(struct Package_s, provides) },
    { MFS_DEP_TYPE_CONFLICTS,
      RPMTAG_CONFLICTNAME,
      RPMTAG_CONFLICTVERSION,
      RPMTAG_CONFLICTFLAGS,
      0,
      offsetof(struct Package_s, conflicts) },
    { MFS_DEP_TYPE_OBSOLETES,
      RPMTAG_OBSOLETENAME,
      RPMTAG_OBSOLETEVERSION,
      RPMTAG_OBSOLETEFLAGS,
      0,
      offsetof(struct Package_s, obsoletes) },
    { MFS_DEP_TYPE_TRIGGERS,
      RPMTAG_TRIGGERNAME,
      RPMTAG_TRIGGERVERSION,
      RPMTAG_TRIGGERFLAGS,
      RPMTAG_TRIGGERINDEX,
      offsetof(struct Package_s, triggers) },
    { MFS_DEP_TYPE_ORDER,
      RPMTAG_ORDERNAME,
      RPMTAG_ORDERVERSION,
      RPMTAG_ORDERFLAGS,
      0,
      offsetof(struct Package_s, order) },
    { MFS_DEP_TYPE_RECOMMENDS,
      RPMTAG_RECOMMENDNAME,
      RPMTAG_RECOMMENDVERSION,
      RPMTAG_RECOMMENDFLAGS,
      0,
      offsetof(struct Package_s, recommends) },
    { MFS_DEP_TYPE_SUGGESTS,
      RPMTAG_SUGGESTNAME,
      RPMTAG_SUGGESTVERSION,
      RPMTAG_SUGGESTFLAGS,
      0,
      offsetof(struct Package_s, suggests) },
    { MFS_DEP_TYPE_SUPPLEMENTS,
      RPMTAG_SUPPLEMENTNAME,
      RPMTAG_SUPPLEMENTVERSION,
      RPMTAG_SUPPLEMENTFLAGS,
      0,
      offsetof(struct Package_s, supplements) },
    { MFS_DEP_TYPE_ENHANCES,
      RPMTAG_ENHANCENAME,
      RPMTAG_ENHANCEVERSION,
      RPMTAG_ENHANCEFLAGS,
      0,
      offsetof(struct Package_s, enhances) },
    { MFS_DEP_TYPE_SENTINEL, 0, 0, 0, 0, 0 }
};

typedef const struct MfsScriptRec_s {
    MfsScriptType scripttype;
    rpmTagVal tag;
    rpmTagVal progtag;
    rpmTagVal flagstag;
    rpmsenseFlags senseflags;
    size_t fileoffset;
} * MfsScriptRec;

static struct MfsScriptRec_s const scriptMapping[] = {
    { MFS_SCRIPT_PREIN,
      RPMTAG_PREIN,
      RPMTAG_PREINPROG,
      RPMTAG_PREINFLAGS,
      RPMSENSE_SCRIPT_PRE,
      offsetof(struct Package_s, preInFile) },
    { MFS_SCRIPT_POSTIN,
      RPMTAG_POSTIN,
      RPMTAG_POSTINPROG,
      RPMTAG_POSTINFLAGS,
      RPMSENSE_SCRIPT_POST,
      offsetof(struct Package_s, postInFile) },
    { MFS_SCRIPT_PREUN,
      RPMTAG_PREUN,
      RPMTAG_PREUNPROG,
      RPMTAG_PREUNFLAGS,
      RPMSENSE_SCRIPT_PREUN,
      offsetof(struct Package_s, preUnFile) },
    { MFS_SCRIPT_POSTUN,
      RPMTAG_POSTUN,
      RPMTAG_POSTUNPROG,
      RPMTAG_POSTUNFLAGS,
      RPMSENSE_SCRIPT_POSTUN,
      offsetof(struct Package_s, postUnFile) },
    { MFS_SCRIPT_PRETRANS,
      RPMTAG_PRETRANS,
      RPMTAG_PRETRANSPROG,
      RPMTAG_PRETRANSFLAGS,
      RPMSENSE_PRETRANS,
      offsetof(struct Package_s, preTransFile) },
    { MFS_SCRIPT_POSTTRANS,
      RPMTAG_POSTTRANS,
      RPMTAG_POSTTRANSPROG,
      RPMTAG_POSTTRANSFLAGS,
      RPMSENSE_POSTTRANS,
      offsetof(struct Package_s, postTransFile) },
    { MFS_SCRIPT_VERIFYSCRIPT,
      RPMTAG_VERIFYSCRIPT,
      RPMTAG_VERIFYSCRIPTPROG,
      RPMTAG_VERIFYSCRIPTFLAGS,
      RPMSENSE_SCRIPT_VERIFY,
      offsetof(struct Package_s, verifyFile) },
    { MFS_SCRIPT_SENTINEL, 0, 0, 0, 0, 0 },
};

MfsManager mfsManagerNew(rpmSpec spec);
void mfsManagerFree(MfsManager mm);

MfsContext mfsContextNew(void);
void mfsContextFree(MfsContext context);

char *mfsModulesDirectory(void);

rpmRC mfsLoadModules(void **modules, const char *path, MfsManager msfm);
void mfsUnloadModules(void *modules);

/** Function that initialize shared file classificator.
 * This function must be called before call(s) of mfsManagerCallFileHooks().
 * I.e. It should be called before processBinaryFiles().
 */
rpmRC mfsMangerInitFileClassificator(MfsManager mm, rpmSpec spec);
void mfsMangerFreeFileClassificator(MfsManager mm);

/* Call all registered build hooks */
rpmRC mfsManagerCallBuildHooks(MfsManager mm, rpmSpec cur_spec,
			       MfsHookPoint point);

rpmRC mfsManagerCallFileHooks(MfsManager mm, rpmSpec cur_spec, Package pkg,
			      FileListRec rec, int *include_in_original);

#ifdef __cplusplus
}
#endif

#endif	/* _MFS_INTERNAL_H_ */
