#include <stdbool.h>

#include "customtag.h"
#include <parser.h>
#include <render.h>
#include <string.h>
#include <dlfcn.h>

cmark_node_type CMARK_NODE_CUSTOMTAG;

// Define the function pointer type
typedef const char** (*get_supported_tags_func)(void);

// Static variable to hold the function pointer
static get_supported_tags_func get_supported_tags = NULL;

// Function to dynamically load the getter function
static void load_supported_tags_getter(void) {
    if (get_supported_tags == NULL) {
        // Try to find the function in the main executable
        void *handle = dlopen(NULL, RTLD_LAZY);
        if (handle != NULL) {
            get_supported_tags = (get_supported_tags_func)dlsym(handle, "get_supported_tags");
            dlclose(handle);
        }
    }
}

static const char *get_type_string(cmark_syntax_extension *extension, cmark_node *node) {
    return node->type == CMARK_NODE_CUSTOMTAG ? "customtag" : "<unknown>";
}

static int can_contain(cmark_syntax_extension *extension, cmark_node *node, cmark_node_type child_type) {
    return node->type == CMARK_NODE_CUSTOMTAG && CMARK_NODE_TYPE_INLINE_P(child_type);
}

static void commonmark_render(cmark_syntax_extension *extension, cmark_renderer *renderer, cmark_node *node, cmark_event_type ev_type, int options) {
    bool entering = (ev_type == CMARK_EVENT_ENTER);
    const char *tagname = cmark_node_get_custom_tag_tagname(node);
    if (entering) {
        const char *content = cmark_node_get_custom_tag_content(node);
        char *tag = (char *)malloc(strlen(tagname) + strlen(content) + 1);
        tag = "<";
        strcat(tag, tagname);
        strcat(tag, content);
        renderer->out(renderer, node, tag, false, LITERAL);
        free(tag);
    } else {
        renderer->out(renderer, node, "/>", false, LITERAL);
    }
}

static void plaintext_render(cmark_syntax_extension *extension, cmark_renderer *renderer, cmark_node *node, cmark_event_type ev_type, int options) {
    bool entering = (ev_type == CMARK_EVENT_ENTER);
    if (entering) {
        const char *tagname = cmark_node_get_custom_tag_tagname(node);
        const char *content = cmark_node_get_custom_tag_content(node);
        char *tag = (char *)malloc(strlen(tagname) + strlen(content) + 1);
        tag = "<";
        strcat(tag, tagname);
        strcat(tag, content);
        renderer->out(renderer, node, tag, false, LITERAL);
        free(tag);
    } else {
        renderer->out(renderer, node, "/>", false, LITERAL);
    }
}

static void html_render(cmark_syntax_extension *extension, cmark_html_renderer *renderer, cmark_node *node, cmark_event_type ev_type, int options) {
    bool entering = (ev_type == CMARK_EVENT_ENTER);
    if (entering) {
        const char *tagname = cmark_node_get_custom_tag_tagname(node);
        const char *content = cmark_node_get_custom_tag_content(node);
        char *tag = (char *)malloc(strlen(tagname) + strlen(content) + 10);
        tag = "<span class=\"";
        strcat(tag, tagname);
        strcat(tag, content);
        strcat(tag, "\">");
        cmark_strbuf_puts(renderer->html, tag);
        free(tag);
    } else {
        cmark_strbuf_puts(renderer->html, "</span>");
    }
}

static bool supportedChar(unsigned char character) {
    return true;
//    if ((character >= "0" && character <= "9") ||
//        (character >= "A" && character <= "Z") ||
//        (character >= "a" && character <= "z") ||
//        (character == "-") ||
//        (character == " ") ||
//        (character == "=") ||
//        (character == "\"") ||
//        (character == "\'") ||
//        (character == "_")) {
//        return true;
//    } else {
//        return false;
//    }
}

static cmark_node *match(cmark_syntax_extension *self, cmark_parser *parser, cmark_node *parent, unsigned char character, cmark_inline_parser *inline_parser) {

    // <标签无法进到extension匹配，需要手动往前偏移1，再走后面的逻辑来计算
    int realOffset = cmark_inline_parser_get_offset(inline_parser);
    if (realOffset <= 0) {
        return NULL;
    }
    int offset = realOffset - 1;
    unsigned char input_char = cmark_inline_parser_peek_at(inline_parser, offset);
    
    if (input_char != '<') {
        return NULL;
    }
    load_supported_tags_getter();

    if (!get_supported_tags) {
        return NULL;
    }

    const char **supporttags = get_supported_tags();
    // match custom tag as <tagname/>, such as <INSERTIMAGE id="123" description="a test"/>
    cmark_node *customtag;

    int column = cmark_inline_parser_get_column(inline_parser);

    char *target_tag = NULL;
    int target_tag_length = 0;
    
    // 1. found <
    // 1.1 then next series of characters should be a support tag
    for (int i = 0; i < sizeof(supporttags) / sizeof(supporttags[0]); i++) {
        const char *tag = supporttags[i];
        int len = strlen(tag);
        bool isMatch = true;
        for (int j = 0; j < len; j++) {
            int target_offset = j + offset + 1; // 从<的下一个起
            unsigned char cur_char = cmark_inline_parser_peek_at(inline_parser, target_offset);
            unsigned char cur_tag_char = tag[j];
            if (cur_char != cur_tag_char) {
                isMatch = false;
                break;
            }
        }
        if (!isMatch) {
            continue;
        } else {
            target_tag = tag;
            target_tag_length = len;
            break;
        }
    }
    
    if (target_tag == NULL) {
        return NULL;
    }
    
    // 2. then found />, then return the customtag
    int cur_offset = offset + target_tag_length + 1;
    int end_offset;
    unsigned char next_char = cmark_inline_parser_peek_at(inline_parser, cur_offset);
    while (next_char != '/') {
        if (!supportedChar(next_char)) {
            return NULL;
        }
        cur_offset += 1;
        next_char = cmark_inline_parser_peek_at(inline_parser, cur_offset);
    }
    unsigned char close_char = cmark_inline_parser_peek_at(inline_parser, cur_offset + 1);
    if (close_char != '>') {
        return NULL;
    }
    end_offset = cur_offset + 1;
    int content_length = end_offset - offset + 1 - 3; // < />
    char *content = malloc((content_length + 1) * sizeof(char));
    
    for (int i = 0; i < content_length - target_tag_length - 1; i++) {
        content[i] = cmark_inline_parser_peek_at(inline_parser, offset + 1 + target_tag_length + 1 + i); // 原文+<+tag+空格+i
    }
    content[content_length] = '\0';

    // found everything,
    // before we create the node, we should pop last <
    cmark_node *last = parent->last_child;
    if (last != NULL) {
        cmark_node_free(last);
    }
    // advance to the character after />
    cmark_inline_parser_set_offset(inline_parser, content_length + 3 + offset);
    
    customtag = cmark_node_new_with_mem(CMARK_NODE_CUSTOMTAG, parser->mem);
    if (!cmark_node_set_type(customtag, CMARK_NODE_CUSTOMTAG)) {
      return NULL;
    }
    
    cmark_node_set_syntax_extension(customtag, self);
    customtag->start_line = customtag->end_line = cmark_inline_parser_get_line(inline_parser);
    customtag->start_column = column - 1;
    customtag->end_column = customtag->start_column + content_length + 3; // < />
    cmark_chunk tagname;
    tagname.data = target_tag;
    tagname.len = target_tag_length;
    cmark_chunk content_c;
    content_c.data = content;
    content_c.len = content_length;
    customtag->as.custom_tag.tagname = tagname;
    customtag->as.custom_tag.content = content_c;
    
    return customtag;
}


cmark_syntax_extension *create_customtag_extension(void) {
    cmark_syntax_extension *ext = cmark_syntax_extension_new("customtag");

    cmark_syntax_extension_set_get_type_string_func(ext, get_type_string);
    cmark_syntax_extension_set_can_contain_func(ext, can_contain);

    cmark_syntax_extension_set_commonmark_render_func(ext, commonmark_render);
    cmark_syntax_extension_set_html_render_func(ext, html_render);
    cmark_syntax_extension_set_plaintext_render_func(ext, plaintext_render);

    CMARK_NODE_CUSTOMTAG = cmark_syntax_extension_add_node(1);

    cmark_syntax_extension_set_match_inline_func(ext, match);
    
    return ext;
}
