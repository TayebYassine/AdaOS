#ifndef ADAOS_ADASCRIPT_LEXER_H
#define ADAOS_ADASCRIPT_LEXER_H

#include <stdint.h>

#include "as_common.h"

// The AdaScript expression lexer: it walks one line of source and reports the
// next token, skipping spaces. Numbers become integers, or floats when they
// carry a fractional part like 3.5; double quotes collect a string (with \n,
// \t, \" and \\ escapes), and words become identifiers or the keywords true,
// false, null, and, or and not.

/* Token kinds the lexer reports. */
enum as_tok_kind {
    TOK_EOF = 0,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_IDENT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
};

/* One token, with its decoded text and integer value. */
struct as_token {
    enum as_tok_kind kind;
    int32_t int_val;
    float float_val;
    char text[AS_LINE_MAX];
};

/* Lexer state: the source string, the read position, the current token and
 * how deep the parser currently is inside brackets. The depth is carried here
 * because the parser is recursive and a script could otherwise nest far enough
 * to run off a task stack. */
struct as_lexer {
    const char* src;
    int pos;
    struct as_token cur;
    int depth;
};

/* Advance lexer to next token. */
void as_lexer_next(struct as_lexer* lex);

#endif /* ADAOS_ADASCRIPT_LEXER_H */
