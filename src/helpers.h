// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _PLAINMOUTH_HELPERS_H_
#define _PLAINMOUTH_HELPERS_H_

#define __ARG_PLACEHOLDER_1 0,
#define __take_second_arg(__ignored, val, ...) val

/*
 * Getting something that works in C and CPP for an arg that may or may
 * not be defined is tricky.  Here, if we have "#define CONFIG_BOOGER 1"
 * we match on the placeholder define, insert the "0," for arg1 and generate
 * the triplet (0, 1, 0).  Then the last step cherry picks the 2nd arg (a one).
 * When CONFIG_BOOGER is not defined, we generate a (... 1, 0) pair, and when
 * the last step cherry picks the 2nd arg, we get a zero.
 */
#define __is_defined(x)			___is_defined(x)
#define ___is_defined(val)		____is_defined(__ARG_PLACEHOLDER_##val)
#define ____is_defined(arg1_or_junk)	__take_second_arg(arg1_or_junk 1, 0)

#define IS_DEBUG()	__is_defined(DEBUG)

#define _UNUSED __attribute__((unused))

#define MIN(a, b)	(((a) < (b)) ? (a) : (b))
#define MAX(a, b)	(((a) > (b)) ? (a) : (b))

#define streq(a, b)		(strcmp((a), (b)) == 0)
#define strneq(a, b, n)		(strncmp((a), (b), (n)) == 0)
#define strcaseeq(a, b)		(strcasecmp((a), (b)) == 0)
#define strncaseeq(a, b, n)	(strncasecmp((a), (b), (n)) == 0)

#endif /* _PLAINMOUTH_HELPERS_H_ */
