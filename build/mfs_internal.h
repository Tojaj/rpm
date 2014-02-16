#ifndef	_MFS_INTERNAL_H_
#define	_MFS_INTERNAL_H_

#include "mfs.h"
#include "rpmbuild_internal.h"
#include "lib/rpmscript.h"	/* script flags */

#ifdef __cplusplus
extern "C" {
#endif

#define MFSMODULESDIR   "mfsmodules"

typedef enum MfsCtxState_e {
    MFS_CTXSTATE_UNKNOWN,
    MFS_CTXSTATE_PARSERHOOK,
    MFS_CTXSTATE_FILEHOOK,
} MfsCtxState;

typedef struct MfsContextData_s {
    rpmSpec spec;
    void *data;
    struct MfsContextData_s *next;
} * MfsContextData;

struct MfsContext_s {
    MfsManager manager;

    char *modulename;

    MfsCtxState state;

    // Hooks related to the context
    // Used only during registration, will be NULL after
    // the call of mfsManagerSortHooks()
    MfsParserHook parserhooks;
    MfsFileHook filehooks;

    rpmSpec cur_spec;  /*!< Current spec file during a hook call */

    // User data
    void *globaldata;
    MfsContextData contextdata;

    struct MfsContext_s *next;
};

struct MfsManager_s {
    MfsContext contexts;

    // Sorted lists of all hooks
    MfsParserHook parserhooks;
    MfsFileHook filehooks;

    rpmSpec mainspec; /*!<
	The spec returned from parseSpec (the one inserted to the buildSpec) */

    MfsContext cur_context;  /*!< Used during loading of modules */
};

// Hooks

struct MfsParserHook_s {
    MfsContext context;
    MfsParserHookFunc func;
    int32_t priority;
    struct MfsParserHook_s * next;
};

struct MfsGlob_s {
    const char *glob;
    struct MfsGlob_s *next;
};

typedef struct MfsGlob_s * MfsGlob;

struct MfsFileHook_s {
    MfsContext context;
    MfsFileHookFunc func;
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
    struct MfsPackage_s *next;
};

struct MfsScript_s {
    char *code;
    char *prog;
    char *file;
    rpmscriptFlags flags;
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

struct MfsFile_s {
    struct FileListRec_s *flr;
    const char *diskpath;
    int include_in_original;
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

rpmRC mfsLoadModules(void **modules, const char *path, MfsManager msfm);
void mfsUnloadModules(void *modules);

/* Call all registered parser hooks */
rpmRC mfsManagerCallParserHooks(MfsManager mm, rpmSpec cur_spec);
rpmRC mfsPackageFinalize(MfsPackage mfspkg);

rpmRC mfsManagerCallFileHooks(MfsManager mm, rpmSpec cur_spec,
			      FileListRec rec, int *include_in_original);

#ifdef __cplusplus
}
#endif

#endif	/* _MFS_INTERNAL_H_ */
