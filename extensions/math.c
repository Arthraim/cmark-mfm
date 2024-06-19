/**
 * Support both moonshot defined \[\] , \(\), and gfm defined $$
 * // https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/writing-mathematical-expressions
*/

#include <stdbool.h>

#include "math.h"
#include <parser.h>
#include <render.h>
#include "ext_scanners.h"

cmark_node_type CMARK_NODE_MATH_BLOCK;
cmark_node_type CMARK_NODE_MATH;

static void scan_math_start_or_end(unsigned char *input, bufsize_t len, bufsize_t *start_offset, bufsize_t *end_offset) {
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
                                      unsigned char *input,
                                      int len,
                                      int start_offset,
                                      bool *found_end) {
  bufsize_t end_offset;
  scan_math_start_or_end(input, len, NULL, &end_offset);

  if (end_offset) {
    cmark_strbuf content;
    cmark_strbuf_init(parser->mem, &content, 0);
    cmark_strbuf_put(&content, (unsigned char *)input + start_offset, end_offset - 2 - start_offset);
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
  handle_math_block_content(parent_container, parser, input, len, 0, &found_end);
  return found_end ? 0 : 1;
}

static cmark_node *open_math_block(cmark_syntax_extension *self,
                                  int indented, cmark_parser *parser,
                                  cmark_node *parent_container,
                                  unsigned char *input, int len) {
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

  cmark_node *math_block = NULL;
  if (start_offset) {
    math_block = cmark_parser_add_child(parser, parent_container, CMARK_NODE_MATH_BLOCK, parent_container->start_column);
    cmark_node_set_syntax_extension(math_block, self);

    handle_math_block_content(math_block, parser, input, len, start_offset, NULL);
  }
  
  if (end_offset) {
    cmark_node *new_para;
    if (math_block) {
      // single line, use the same parent to close math properly
      new_para = cmark_parser_add_child(parser, math_block->parent, CMARK_NODE_PARAGRAPH, parent_container->start_column);
    } else {
      // multi line, create a new paragraph to close math
      new_para = cmark_parser_add_child(parser, parent_container, CMARK_NODE_PARAGRAPH, parent_container->start_column);
    }
    return new_para;
  }

  return math_block;
}

static const char *get_type_string(cmark_syntax_extension *extension,
                                   cmark_node *node) {
  return node->type == CMARK_NODE_MATH_BLOCK ? "math_block" : "<unknown>";
}

static int can_contain(cmark_syntax_extension *extension, cmark_node *node,
                       cmark_node_type child_type) {
  return node->type == CMARK_NODE_MATH_BLOCK && child_type == CMARK_NODE_PARAGRAPH;
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

int math_ispunct(char c) {
  if (c == '[' || c == ']' || c == '(' || c == ')') {
    return false;
  } else {
    return cmark_ispunct(c);
  }
}

int find_backslash_index(const unsigned char *str) {
    const unsigned char *ptr = str;
    int index = 0;
    while (*ptr != '\0' && *ptr != '\n') {
        if (*ptr == '\\') {
            return index;
        }
        ptr++;
        index++;
    }
    return -1; // Return -1 if backslash is not found
}

static cmark_node *matches_inline(cmark_syntax_extension *self, cmark_parser *parser,
                                  cmark_node *parent, unsigned char character,
                                  cmark_inline_parser *inline_parser) {

  // For the first time call this method, we need to set the backslash_ispunct function,
  // because we don't what to consider '[', ']', '(' and ')' as punctuations.
  // Known issue: if the whole document starts with \[ or \(, which is a inline math,
  //              then the first math will be ignored.
  if (parser->backslash_ispunct == NULL) {
    cmark_parser_set_backslash_ispunct_func(parser, math_ispunct);
  }

  bool in_bracket = cmark_inline_parser_in_bracket(inline_parser, 0)
                    || cmark_inline_parser_in_bracket(inline_parser, 1)
                    || cmark_inline_parser_in_bracket(inline_parser, 2); /*[*/ 
  bool in_parenthesis = character == '('; /*(*/ 
  if (!in_bracket && !in_parenthesis) {
    return NULL;
  }

  // 1. scan for the end of the math
  cmark_chunk *chunk = cmark_inline_parser_get_chunk(inline_parser);
  int offset = cmark_inline_parser_get_offset(inline_parser);
  int end_offset = find_backslash_index(chunk->data + offset);
  char next_char = cmark_inline_parser_peek_at(inline_parser, offset + end_offset + 1);
  if (end_offset < 0 || !(next_char == ']' || next_char == ')')) {
    return NULL;
  }
  int lower_bound = offset;
  int upper_bound = offset + end_offset; // not inclusive

  // 2. if found, handle start bracket and parenthesis
  if (in_bracket) {
    cmark_inline_parser_free_last_bracket(inline_parser);
    cmark_inline_parser_pop_bracket(inline_parser);
  }
  if (in_parenthesis) {
    cmark_inline_parser_advance_offset(inline_parser);
    lower_bound++;
  }
  cmark_node_free(parent->last_child); // remove the '\' before bracket or parenthesis

  // 3. create a new node and return it
  char substring[upper_bound - lower_bound + 1];
  memcpy(substring, chunk->data + lower_bound, upper_bound - lower_bound);
  substring[upper_bound - lower_bound] = '\0';
  // advance to the character after ]
  cmark_inline_parser_set_offset(inline_parser, upper_bound + 2);
  // create the text node within
  cmark_node *text = cmark_node_new_with_mem(CMARK_NODE_TEXT, parser->mem);
  // .. with all text between [^ and ^]
  cmark_node_set_string_content(text, substring);
  cmark_node_set_literal(text, substring);
  text->start_line = text->end_line = cmark_inline_parser_get_line(inline_parser);
  text->start_column = cmark_inline_parser_get_column(inline_parser);
  text->end_column = text->start_column + upper_bound - lower_bound;

  // create the ragtag node
  cmark_node *mathNode = cmark_node_new_with_mem(CMARK_NODE_MATH, parser->mem);
  if (!cmark_node_set_type(mathNode, CMARK_NODE_MATH)) {
    return NULL;
  }
  cmark_node_set_syntax_extension(mathNode, self);
  mathNode->start_line = mathNode->end_line = cmark_inline_parser_get_line(inline_parser);
  mathNode->start_column = cmark_inline_parser_get_column(inline_parser);
  mathNode->end_column = mathNode->start_column + upper_bound - lower_bound;

  cmark_node_append_child(mathNode, text);

  return mathNode;
}

static const char *get_inline_type_string(cmark_syntax_extension *extension,
                                          cmark_node *node) {
  return node->type == CMARK_NODE_MATH ? "math" : "<unknown>";
}

static int can_inline_contain(cmark_syntax_extension *extension, cmark_node *node,
                              cmark_node_type child_type) {
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
                               cmark_event_type ev_type, int options) {bool entering = (ev_type == CMARK_EVENT_ENTER);
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

  cmark_syntax_extension_set_commonmark_render_func(ext, inline_commonmark_render);
  cmark_syntax_extension_set_html_render_func(ext, inline_html_render);
  cmark_syntax_extension_set_plaintext_render_func(ext, inline_plaintext_render);

  CMARK_NODE_MATH = cmark_syntax_extension_add_node(1);

  return ext;
}
