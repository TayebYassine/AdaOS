#include "as_lexer.h"

#include "libc.h"

void as_lexer_next(struct as_lexer* lex) {
    while (lex->src[lex->pos] == ' ' || lex->src[lex->pos] == '\t' ||
           lex->src[lex->pos] == '\r' || lex->src[lex->pos] == '\n') {
        lex->pos++;
    }

    char c = lex->src[lex->pos];
    if (c == '\0') {
        lex->cur.kind = TOK_EOF;
        lex->cur.text[0] = '\0';
        return;
    }

    // Integer literal; decimal or hex
    if ((c >= '0' && c <= '9') ||
        (c == '0' &&
         (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X'))) {
        int hex = 0;
        int32_t val = 0;

        if (c == '0' &&
            (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X')) {
            hex = 1;
            lex->pos += 2;
        }

        while (1) {
            char ch = lex->src[lex->pos];
            if (ch >= '0' && ch <= '9') {
                val = hex ? (val * 16 + (ch - '0')) : (val * 10 + (ch - '0'));
                lex->pos++;
            } else if (hex && ch >= 'a' && ch <= 'f') {
                val = val * 16 + (ch - 'a' + 10);
                lex->pos++;
            } else if (hex && ch >= 'A' && ch <= 'F') {
                val = val * 16 + (ch - 'A' + 10);
                lex->pos++;
            } else {
                break;
            }
        }

        lex->cur.kind = TOK_INT;
        lex->cur.int_val = val;
        itoa(val, lex->cur.text);
        return;
    }

    // String literal
    if (c == '"') {
        lex->pos++;
        int idx = 0;
        while (lex->src[lex->pos] != '\0' && lex->src[lex->pos] != '"') {
            char ch = lex->src[lex->pos++];
            if (ch == '\\') {
                if (lex->src[lex->pos] == '\0') {
                    if (idx < (int)sizeof(lex->cur.text) - 1) {
                        lex->cur.text[idx++] = ch;
                    }
                    break;
                }
                char esc = lex->src[lex->pos++];
                if (esc == 'n')
                    ch = '\n';
                else if (esc == 't')
                    ch = '\t';
                else if (esc == '"')
                    ch = '"';
                else if (esc == '\\')
                    ch = '\\';
                else
                    ch = esc;
            }
            if (idx < (int)sizeof(lex->cur.text) - 1) {
                lex->cur.text[idx++] = ch;
            }
        }
        if (lex->src[lex->pos] == '"') {
            lex->pos++;
        }
        lex->cur.text[idx] = '\0';
        lex->cur.kind = TOK_STRING;
        return;
    }

    // Identifier or keyword
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        int idx = 0;
        while ((lex->src[lex->pos] >= 'a' && lex->src[lex->pos] <= 'z') ||
               (lex->src[lex->pos] >= 'A' && lex->src[lex->pos] <= 'Z') ||
               (lex->src[lex->pos] >= '0' && lex->src[lex->pos] <= '9') ||
               lex->src[lex->pos] == '_') {
            if (idx < (int)sizeof(lex->cur.text) - 1) {
                lex->cur.text[idx++] = lex->src[lex->pos];
            }
            lex->pos++;
        }
        lex->cur.text[idx] = '\0';

        if (strcmp(lex->cur.text, "true") == 0) {
            lex->cur.kind = TOK_TRUE;
        } else if (strcmp(lex->cur.text, "false") == 0) {
            lex->cur.kind = TOK_FALSE;
        } else if (strcmp(lex->cur.text, "null") == 0) {
            lex->cur.kind = TOK_NULL;
        } else if (strcmp(lex->cur.text, "and") == 0) {
            lex->cur.kind = TOK_AND;
        } else if (strcmp(lex->cur.text, "or") == 0) {
            lex->cur.kind = TOK_OR;
        } else if (strcmp(lex->cur.text, "not") == 0) {
            lex->cur.kind = TOK_NOT;
        } else if (strcmp(lex->cur.text, "len") == 0) {
            lex->cur.kind = TOK_LEN;
        } else {
            lex->cur.kind = TOK_IDENT;
        }
        return;
    }

    // Multi-character operators
    if (c == '=' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_EQ;
        return;
    }
    if (c == '!' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_NEQ;
        return;
    }
    if (c == '<' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_LTE;
        return;
    }
    if (c == '>' && lex->src[lex->pos + 1] == '=') {
        lex->pos += 2;
        lex->cur.kind = TOK_GTE;
        return;
    }
    if (c == '&' && lex->src[lex->pos + 1] == '&') {
        lex->pos += 2;
        lex->cur.kind = TOK_AND;
        return;
    }
    if (c == '|' && lex->src[lex->pos + 1] == '|') {
        lex->pos += 2;
        lex->cur.kind = TOK_OR;
        return;
    }

    // Single-character operators and symbols
    lex->pos++;
    switch (c) {
        case '+':
            lex->cur.kind = TOK_PLUS;
            break;
        case '-':
            lex->cur.kind = TOK_MINUS;
            break;
        case '*':
            lex->cur.kind = TOK_STAR;
            break;
        case '/':
            lex->cur.kind = TOK_SLASH;
            break;
        case '%':
            lex->cur.kind = TOK_PERCENT;
            break;
        case '<':
            lex->cur.kind = TOK_LT;
            break;
        case '>':
            lex->cur.kind = TOK_GT;
            break;
        case '!':
            lex->cur.kind = TOK_NOT;
            break;
        case '(':
            lex->cur.kind = TOK_LPAREN;
            break;
        case ')':
            lex->cur.kind = TOK_RPAREN;
            break;
        case '[':
            lex->cur.kind = TOK_LBRACKET;
            break;
        case ']':
            lex->cur.kind = TOK_RBRACKET;
            break;
        case '{':
            lex->cur.kind = TOK_LBRACE;
            break;
        case '}':
            lex->cur.kind = TOK_RBRACE;
            break;
        case ',':
            lex->cur.kind = TOK_COMMA;
            break;
        default:
            lex->cur.kind = TOK_EOF;
            break;
    }
}
