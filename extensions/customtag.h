#ifndef CMARK_GFM_CUSTOMTAG_H
#define CMARK_GFM_CUSTOMTAG_H

#include "cmark-gfm-core-extensions.h"

extern cmark_node_type CMARK_NODE_CUSTOMTAG;
cmark_syntax_extension *create_customtag_extension(void);

#endif