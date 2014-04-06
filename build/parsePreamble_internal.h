#ifndef _PARSEPREAMBLE_INTERNAL_H
#define _PARSEPREAMBLE_INTERNAL_H

#include "build/rpmbuild_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This table has to be in a peculiar order.  If one tag is the */
/* same as another, plus a few letters, it must come first.     */

/**
 */
typedef const struct PreambleRec_s {
    rpmTagVal tag;	/*!< A tag */
    int type;		/*!<
	0 - Regular tag, no additional info e.g. "Name: foo"
	1 - Tag that can has a language specified e.g. "Summary(cs): Czech summary"
	2 - Tag that can has additional options e.g. "Requires(pre,post): bar"
	3 - Tag that can be numbered e.g.: "Source0: foo.tgz"
	*/
    int deprecated;	/*!< If not zero then the tag is deprecated*/
    size_t len;		/*!< Length of the tag token e.g. 4 for "name" */
    const char * token; /*!< Tag token e.g. "name" for RPMTAG_NAME */
} * PreambleRec;

#define LEN_AND_STR(_tag) (sizeof(_tag)-1), _tag

static struct PreambleRec_s const preambleList[] = {
    {RPMTAG_NAME,		0, 0, LEN_AND_STR("name")},
    {RPMTAG_VERSION,		0, 0, LEN_AND_STR("version")},
    {RPMTAG_RELEASE,		0, 0, LEN_AND_STR("release")},
    {RPMTAG_EPOCH,		0, 0, LEN_AND_STR("epoch")},
    {RPMTAG_SUMMARY,		1, 0, LEN_AND_STR("summary")},
    {RPMTAG_LICENSE,		0, 0, LEN_AND_STR("license")},
    {RPMTAG_DISTRIBUTION,	0, 0, LEN_AND_STR("distribution")},
    {RPMTAG_DISTURL,		0, 0, LEN_AND_STR("disturl")},
    {RPMTAG_VENDOR,		0, 0, LEN_AND_STR("vendor")},
    {RPMTAG_GROUP,		1, 0, LEN_AND_STR("group")},
    {RPMTAG_PACKAGER,		0, 0, LEN_AND_STR("packager")},
    {RPMTAG_URL,		0, 0, LEN_AND_STR("url")},
    {RPMTAG_VCS,		0, 0, LEN_AND_STR("vcs")},
    {RPMTAG_SOURCE,		3, 0, LEN_AND_STR("source")},
    {RPMTAG_PATCH,		3, 0, LEN_AND_STR("patch")},
    {RPMTAG_NOSOURCE,		0, 0, LEN_AND_STR("nosource")},
    {RPMTAG_NOPATCH,		0, 0, LEN_AND_STR("nopatch")},
    {RPMTAG_EXCLUDEARCH,	0, 0, LEN_AND_STR("excludearch")},
    {RPMTAG_EXCLUSIVEARCH,	0, 0, LEN_AND_STR("exclusivearch")},
    {RPMTAG_EXCLUDEOS,		0, 0, LEN_AND_STR("excludeos")},
    {RPMTAG_EXCLUSIVEOS,	0, 0, LEN_AND_STR("exclusiveos")},
    {RPMTAG_ICON,		0, 0, LEN_AND_STR("icon")},
    {RPMTAG_PROVIDEFLAGS,	0, 0, LEN_AND_STR("provides")},
    {RPMTAG_REQUIREFLAGS,	2, 0, LEN_AND_STR("requires")},
    {RPMTAG_RECOMMENDFLAGS,	0, 0, LEN_AND_STR("recommends")},
    {RPMTAG_SUGGESTFLAGS,	0, 0, LEN_AND_STR("suggests")},
    {RPMTAG_SUPPLEMENTFLAGS,	0, 0, LEN_AND_STR("supplements")},
    {RPMTAG_ENHANCEFLAGS,	0, 0, LEN_AND_STR("enhances")},
    {RPMTAG_PREREQ,		2, 1, LEN_AND_STR("prereq")},
    {RPMTAG_CONFLICTFLAGS,	0, 0, LEN_AND_STR("conflicts")},
    {RPMTAG_OBSOLETEFLAGS,	0, 0, LEN_AND_STR("obsoletes")},
    {RPMTAG_PREFIXES,		0, 0, LEN_AND_STR("prefixes")},
    {RPMTAG_PREFIXES,		0, 0, LEN_AND_STR("prefix")},
    {RPMTAG_BUILDROOT,		0, 0, LEN_AND_STR("buildroot")},
    {RPMTAG_BUILDARCHS,		0, 0, LEN_AND_STR("buildarchitectures")},
    {RPMTAG_BUILDARCHS,		0, 0, LEN_AND_STR("buildarch")},
    {RPMTAG_BUILDCONFLICTS,	0, 0, LEN_AND_STR("buildconflicts")},
    {RPMTAG_BUILDPREREQ,	0, 1, LEN_AND_STR("buildprereq")},
    {RPMTAG_BUILDREQUIRES,	0, 0, LEN_AND_STR("buildrequires")},
    {RPMTAG_AUTOREQPROV,	0, 0, LEN_AND_STR("autoreqprov")},
    {RPMTAG_AUTOREQ,		0, 0, LEN_AND_STR("autoreq")},
    {RPMTAG_AUTOPROV,		0, 0, LEN_AND_STR("autoprov")},
    {RPMTAG_DOCDIR,		0, 0, LEN_AND_STR("docdir")},
    {RPMTAG_DISTTAG,		0, 0, LEN_AND_STR("disttag")},
    {RPMTAG_BUGURL,		0, 0, LEN_AND_STR("bugurl")},
    {RPMTAG_COLLECTIONS,	0, 0, LEN_AND_STR("collections")},
    {RPMTAG_ORDERFLAGS,		2, 0, LEN_AND_STR("orderwithrequires")},
    {0, 0, 0, 0}
};

/** Tags wich will be copied from the main package to subpackages
 */
static const rpmTagVal copyTagsDuringParse[] = {
    RPMTAG_EPOCH,
    RPMTAG_VERSION,
    RPMTAG_RELEASE,
    RPMTAG_LICENSE,
    RPMTAG_PACKAGER,
    RPMTAG_DISTRIBUTION,
    RPMTAG_DISTURL,
    RPMTAG_VENDOR,
    RPMTAG_ICON,
    RPMTAG_URL,
    RPMTAG_VCS,
    RPMTAG_CHANGELOGTIME,
    RPMTAG_CHANGELOGNAME,
    RPMTAG_CHANGELOGTEXT,
    RPMTAG_PREFIXES,
    RPMTAG_DISTTAG,
    RPMTAG_BUGURL,
    RPMTAG_GROUP,
    0
};

/** \ingroup rpmbuild
 * Add tag to the package header.
 * @param spec          spec
 * @param pkg           package
 * @param tag           rpm tag (e.g. RPMTAG_PACKAGER)
 * @param macro         macro (e.g. "name", "buldrequires")
 * @param lang          language (e.g. "C", "", "cs")
 * @param field         data that will be added
 * @return              0 on success, other on error
 */
RPM_GNUC_INTERNAL
rpmRC applyPreambleTag(rpmSpec spec, Package pkg, rpmTagVal tag,
		const char *macro, const char *lang, const char *field);

// TODO: Doc
rpmRC checkForValidArchitectures(rpmSpec spec);

/**
 * Check that no duplicate tags are present in header.
 * @param h		header
 * @param pkgname   	package name
 * @return		RPMRC_OK if OK
 */
int checkForDuplicates(Header h, const char *pkgname);

/**
 * Check that required tags are present in header.
 * @param h		header
 * @param pkgname	package name
 * @return		RPMRC_OK if OK
 */
int checkForRequired(Header h, const char * pkname);

#ifdef __cplusplus
}
#endif

#endif /* _PARSEPREAMBLE_INTERNAL_H */
