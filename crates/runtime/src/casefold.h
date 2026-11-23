#ifndef SCUZZ_CASEFOLD_H
#define SCUZZ_CASEFOLD_H

/* Return 1 when the full case fold of prefix is a prefix of the full case fold of label. */
int sz_select_casefold_prefix(const char *label, const char *prefix, int prefix_len);

#endif
