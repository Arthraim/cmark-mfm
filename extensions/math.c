/**
 * Support both moonshot defined \[\] , \(\), and gfm defined $$
 * //
 * https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/writing-mathematical-expressions
 */

#include <stdbool.h>

#include "buffer.h"
#include "cmark-gfm-extension_api.h"
#include "cmark-gfm.h"
#include "ext_scanners.h"
#include "math.h"
#include <parser.h>
#include <render.h>
#include <stddef.h>
#include <string.h>

cmark_node_type CMARK_NODE_MATH_BLOCK;
cmark_node_type CMARK_NODE_MATH;

int math_ispunct(char c) {
  if (c == '[' || c == ']' || c == '(' || c == ')') {
    return false;
  } else {
    return cmark_ispunct(c);
  }
}

static void scan_math_start_or_end(unsigned char *input, bufsize_t len,
                                   bufsize_t *start_offset,
                                   bufsize_t *end_offset) {
  bufsize_t start = scan_math_start(input, len, 0);
  bufsize_t end = scan_math_end(input, len, start);
  if (end) {
    end = end + start;
  }
  if (start_offset) {
    *start_offset = start;
  }
  if (end_offset) {
    *end_offset = end;
  }
}

static void handle_math_block_content(cmark_node *math_block,
                                      cmark_parser *parser,
                                      unsigned char *input, int len,
                                      int start_offset, bool *found_end) {
  bufsize_t end_offset;
  scan_math_start_or_end(input, len, NULL, &end_offset);

  if (end_offset) {
    cmark_strbuf content;
    cmark_strbuf_init(parser->mem, &content, 0);
    cmark_strbuf_put(&content, (unsigned char *)input + start_offset,
                     end_offset - 2 - start_offset);
    cmark_strbuf_puts(&math_block->content, (char *)content.ptr);
    cmark_strbuf_free(&content);
  } else {
    cmark_strbuf_puts(&math_block->content, (char *)input + start_offset);
  }
  cmark_parser_advance_offset(parser, (char *)input, len, false);
  if (found_end) {
    *found_end = end_offset > 0;
  }
}

static int matches(cmark_syntax_extension *self, cmark_parser *parser,
                   unsigned char *input, int len,
                   cmark_node *parent_container) {
  cmark_node_type node_type = cmark_node_get_type(parent_container);
  if (node_type != CMARK_NODE_MATH_BLOCK) {
    return 0;
  }
  bool found_end = false;
  handle_math_block_content(parent_container, parser, input, len, 0,
                            &found_end);
  return found_end ? 0 : 1;
}

static cmark_node *open_math_block(cmark_syntax_extension *self, int indented,
                                   cmark_parser *parser,
                                   cmark_node *parent_container,
                                   unsigned char *input, int len) {
  // For the first time this method got invoked, we hijack the backslash_ispunct
  // function, because we don't what `\[` to be escaped to `[`.
  if (parser->backslash_ispunct == NULL) {
    cmark_parser_set_backslash_ispunct_func(parser, math_ispunct);
  }

  cmark_node *container = parent_container;
  while (container) {
    cmark_node_type parent_type = cmark_node_get_type(container);
    if (parent_type == CMARK_NODE_MATH_BLOCK) {
      return NULL;
    }
    container = container->parent;
  }

  bufsize_t start_offset;
  bufsize_t end_offset;
  scan_math_start_or_end(input, len, &start_offset, &end_offset);

  if (!start_offset && !end_offset) {
    return NULL;
  }

  // both start and end are in the same line, skip it, let inline-math to handle
  if (start_offset && end_offset) {
    return NULL;
  }

  cmark_node *math_block = NULL;
  if (start_offset) {
    math_block =
        cmark_parser_add_child(parser, parent_container, CMARK_NODE_MATH_BLOCK,
                               parent_container->start_column);
    cmark_node_set_syntax_extension(math_block, self);

    handle_math_block_content(math_block, parser, input, len, start_offset,
                              NULL);
  }

  // found the end fence, create a new paragraph to close math-block and receive
  // following lines
  if (end_offset) {
    return cmark_parser_add_child(parser, parent_container,
                                  CMARK_NODE_PARAGRAPH,
                                  parent_container->start_column);
  }

  return math_block;
}

static const char *get_type_string(cmark_syntax_extension *extension,
                                   cmark_node *node) {
  return node->type == CMARK_NODE_MATH_BLOCK ? "math_block" : "<unknown>";
}

static int can_contain(cmark_syntax_extension *extension, cmark_node *node,
                       cmark_node_type child_type) {
  return node->type == CMARK_NODE_MATH_BLOCK &&
         child_type == CMARK_NODE_PARAGRAPH;
}

//
// renderers
//

static void commonmark_render(cmark_syntax_extension *extension,
                              cmark_renderer *renderer, cmark_node *node,
                              cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  if (entering) {
    return;
  }
  renderer->out(renderer, node, "$$", false, LITERAL);
  renderer->out(renderer, node, (char *)node->content.ptr, false, LITERAL);
  renderer->out(renderer, node, "$$\n", false, LITERAL);
}

static void html_render(cmark_syntax_extension *extension,
                        cmark_html_renderer *renderer, cmark_node *node,
                        cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  if (entering) {
    return;
  }
  cmark_strbuf_puts(renderer->html, "<div class=\"math\">");
  cmark_strbuf_puts(renderer->html, (char *)node->content.ptr);
  cmark_strbuf_puts(renderer->html, "</div>");
}

static void plaintext_render(cmark_syntax_extension *extension,
                             cmark_renderer *renderer, cmark_node *node,
                             cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  if (entering) {
    return;
  }
  renderer->out(renderer, node, "$$", false, LITERAL);
  renderer->out(renderer, node, (char *)node->content.ptr, false, LITERAL);
  renderer->out(renderer, node, "$$\n", false, LITERAL);
}

cmark_syntax_extension *create_math_block_extension(void) {
  cmark_syntax_extension *ext = cmark_syntax_extension_new("mathblock");

  cmark_syntax_extension_set_match_block_func(ext, matches);
  cmark_syntax_extension_set_get_type_string_func(ext, get_type_string);
  cmark_syntax_extension_set_open_block_func(ext, open_math_block);
  cmark_syntax_extension_set_can_contain_func(ext, can_contain);

  cmark_syntax_extension_set_commonmark_render_func(ext, commonmark_render);
  cmark_syntax_extension_set_html_render_func(ext, html_render);
  cmark_syntax_extension_set_plaintext_render_func(ext, plaintext_render);

  CMARK_NODE_MATH_BLOCK = cmark_syntax_extension_add_node(0);

  return ext;
}

//
// Inline math
//

int find_math_closer(const unsigned char *str) {
  const unsigned char *ptr = str;
  int index = 0;
  while (*ptr != '\0' && *ptr != '\n') {
    if ((*ptr == '\\' && *(ptr + 1) == ']') ||
        (*ptr == '\\' && *(ptr + 1) == ')')) {
      // Found a backslash followed by ']'
      return index;
    }
    ptr++;
    index++;
  }
  return -1; // Return -1 if backslash is not found
}

struct MathMatch {
  const char *opener;
  const char *closer;
};

static struct MathMatch math_matches[] = {
    {"\\[", "\\]"}, {"\\(", "\\)"}, {"$$", "$$"}};

static const size_t MATH_MATCH_LEN =
    sizeof(math_matches) / sizeof(struct MathMatch);

static cmark_node *found_math_match(cmark_syntax_extension *self,
                                    cmark_parser *parser, cmark_node *parent,
                                    unsigned char character,
                                    cmark_inline_parser *inline_parser,
                                    const char *opener, const char *closer,
                                    int opener_len, int closer_offset) {
  int offset = cmark_inline_parser_get_offset(inline_parser);
  cmark_chunk *chunk = cmark_inline_parser_get_chunk(inline_parser);
  const unsigned char *string = (const unsigned char *)chunk->data + offset;

  cmark_inline_parser_set_offset(inline_parser, offset + closer_offset + strlen(closer));

  cmark_node *text = cmark_node_new_with_mem(CMARK_NODE_TEXT, parser->mem);
  int content_len = closer_offset - opener_len;
  cmark_strbuf_set(&text->content, string + opener_len, content_len);
  cmark_node_set_literal_with_len(text, string + opener_len, content_len);
  text->start_line = text->end_line =
      cmark_inline_parser_get_line(inline_parser);
  text->start_column = cmark_inline_parser_get_column(inline_parser);
  text->end_column = text->start_column + content_len;

  cmark_node *mathNode = cmark_node_new_with_mem(CMARK_NODE_MATH, parser->mem);
  if (!cmark_node_set_type(mathNode, CMARK_NODE_MATH)) {
    return NULL;
  }
  cmark_node_set_syntax_extension(mathNode, self);
  mathNode->start_line = mathNode->end_line =
      cmark_inline_parser_get_line(inline_parser);
  mathNode->start_column = cmark_inline_parser_get_column(inline_parser);
  mathNode->end_column = mathNode->start_column + content_len;

  cmark_node_append_child(mathNode, text);

  return mathNode;
}

static cmark_node *matches_inline(cmark_syntax_extension *self,
                                  cmark_parser *parser, cmark_node *parent,
                                  unsigned char character,
                                  cmark_inline_parser *inline_parser) {
  cmark_chunk *chunk = cmark_inline_parser_get_chunk(inline_parser);
  int offset = cmark_inline_parser_get_offset(inline_parser);
  const char *string = (const char *)chunk->data + offset;

  for (size_t i = 0; i < MATH_MATCH_LEN; i++) {
    if (character == math_matches[i].opener[0]) {
      char *ret = strstr(string, math_matches[i].opener);
      if (ret) {
        if (ret == string) {
          // Found the opener
          // Check the closer
          size_t opener_len = strlen(math_matches[i].opener);
          char *closer = strstr(string + opener_len, math_matches[i].closer);
          if (closer) {
            return found_math_match(self, parser, parent, character,
                                    inline_parser, math_matches[i].opener,
                                    math_matches[i].closer, opener_len,
                                    closer - string);
          } else {
            continue;
          }
        } else {
          continue;
        }
      } else {
        continue;
      }
    }
  }

  return NULL;
}

static const char *get_inline_type_string(cmark_syntax_extension *extension,
                                          cmark_node *node) {
  return node->type == CMARK_NODE_MATH ? "math" : "<unknown>";
}

static int can_inline_contain(cmark_syntax_extension *extension,
                              cmark_node *node, cmark_node_type child_type) {
  if (node->type != CMARK_NODE_MATH)
    return false;

  return CMARK_NODE_TYPE_INLINE_P(child_type);
}

static void inline_commonmark_render(cmark_syntax_extension *extension,
                                     cmark_renderer *renderer, cmark_node *node,
                                     cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  if (entering) {
    renderer->out(renderer, node, "\\(", false, LITERAL);
  } else {
    renderer->out(renderer, node, "\\)", false, LITERAL);
  }
}

static void inline_html_render(cmark_syntax_extension *extension,
                               cmark_html_renderer *renderer, cmark_node *node,
                               cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  if (entering) {
    cmark_strbuf_puts(renderer->html, "<span class=\"math\">");
  } else {
    cmark_strbuf_puts(renderer->html, "</span>");
  }
}

static void inline_plaintext_render(cmark_syntax_extension *extension,
                                    cmark_renderer *renderer, cmark_node *node,
                                    cmark_event_type ev_type, int options) {
  bool entering = (ev_type == CMARK_EVENT_ENTER);
  if (entering) {
    renderer->out(renderer, node, "\\(", false, LITERAL);
  } else {
    renderer->out(renderer, node, "\\)", false, LITERAL);
  }
}

cmark_syntax_extension *create_math_extension(void) {
  cmark_syntax_extension *ext = cmark_syntax_extension_new("math");

  cmark_syntax_extension_set_match_inline_func(ext, matches_inline);
  cmark_syntax_extension_set_get_type_string_func(ext, get_inline_type_string);
  cmark_syntax_extension_set_can_contain_func(ext, can_inline_contain);

  cmark_syntax_extension_set_commonmark_render_func(ext,
                                                    inline_commonmark_render);
  cmark_syntax_extension_set_html_render_func(ext, inline_html_render);
  cmark_syntax_extension_set_plaintext_render_func(ext,
                                                   inline_plaintext_render);

  CMARK_NODE_MATH = cmark_syntax_extension_add_node(1);

  return ext;
}
