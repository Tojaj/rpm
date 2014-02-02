#ifndef _FILES_INTERNAL_H
#define _FILES_INTERNAL_H

#include "build/rpmbuild_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 */
enum specfFlags_e {
    SPECD_DEFFILEMODE	= (1 << 0),
    SPECD_DEFDIRMODE	= (1 << 1),
    SPECD_DEFUID	= (1 << 2),
    SPECD_DEFGID	= (1 << 3),
    SPECD_DEFVERIFY	= (1 << 4),

    SPECD_FILEMODE	= (1 << 8),
    SPECD_DIRMODE	= (1 << 9),
    SPECD_UID		= (1 << 10),
    SPECD_GID		= (1 << 11),
    SPECD_VERIFY	= (1 << 12)
};

typedef rpmFlags specfFlags;

/**
 */
typedef struct AttrRec_s {
    rpmsid	ar_fmodestr;
    rpmsid	ar_dmodestr;
    rpmsid	ar_user;
    rpmsid	ar_group;
    mode_t	ar_fmode;
    mode_t	ar_dmode;
} * AttrRec;

/**
 */
typedef struct FileListRec_s {
    struct stat fl_st;
#define	fl_dev	fl_st.st_dev
#define	fl_ino	fl_st.st_ino
#define	fl_mode	fl_st.st_mode
#define	fl_nlink fl_st.st_nlink
#define	fl_uid	fl_st.st_uid
#define	fl_gid	fl_st.st_gid
#define	fl_rdev	fl_st.st_rdev
#define	fl_size	fl_st.st_size
#define	fl_mtime fl_st.st_mtime

    char *diskPath;		/* get file from here       */
    char *cpioPath;		/* filename in cpio archive */
    rpmsid uname;
    rpmsid gname;
    unsigned	flags;
    specfFlags	specdFlags;	/* which attributes have been explicitly specified. */
    rpmVerifyFlags verifyFlags;
    char *langs;		/* XXX locales separated with | */
    char *caps;
} * FileListRec;

typedef struct FileEntry_s {
    rpmfileAttrs attrFlags;
    specfFlags specdFlags;
    rpmVerifyFlags verifyFlags;
    struct AttrRec_s ar;

    ARGV_t langs;
    char *caps;

    /* these are only ever relevant for current entry */
    unsigned devtype;
    unsigned devmajor;
    int devminor;
    int isDir;
} * FileEntry;


typedef struct FileRecords_s {
    FileListRec recs;
    int alloced;
    int used;
} * FileRecords;

/**
 * Package file tree walk data.
 */
typedef struct FileList_s {
    /* global filelist state */
    char * buildRoot;
    size_t buildRootLen;
    int processingFailed;
    int haveCaps;
    int largeFiles;
    ARGV_t docDirs;
    rpmBuildPkgFlags pkgFlags;
    rpmstrPool pool;

    /* actual file records */
    struct FileRecords_s files;

    /* active defaults */
    struct FileEntry_s def;

    /* current file-entry state */
    struct FileEntry_s cur;
} * FileList;

void addFileListRecord(FileList fl, FileListRec flr);

#ifdef __cplusplus
}
#endif

#endif /* _FILES_INTERNAL_H */
