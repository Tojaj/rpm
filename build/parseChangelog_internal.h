#ifndef _PARSECHANGELOG_INTERNAL_H
#define _PARSECHANGELOG_INTERNAL_H

#include "build/rpmbuild_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse date string to seconds.
 * @param datestr	date string (e.g. 'Wed Jan 1 1997')
 * @retval secs		secs since the unix epoch
 * @return 		0 on success, -1 on error
 */
int rpmDateToTimet(const char * datestr, time_t * secs);

void addChangelogEntry(Header h, time_t time, const char *name, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* _PARSECHANGELOG_INTERNAL_H */
