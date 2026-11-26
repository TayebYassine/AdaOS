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
        // Accumulate unsigned: a long run of digits must wrap quietly rather
        // than overflow a signed value, which is undefined behaviour.
        uint32_t val = 0;

        if (c == '0' &&
            (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X')) {
            hex = 1;
            lex->pos += 2;
        }

        while (1) {
            char ch = lex->src[lex->pos];
            if (ch >= '0' && ch <= '9') {
                val = hex ? (val * 16u + (uint32_t)(ch - '0'))
                          : (val * 10u + (uint32_t)(ch - '0'));
                lex->pos++;
            } else if (hex && ch >= 'a' && ch <= 'f') {
                val = val * 16u + (uint32_t)(ch - 'a' + 10);
                lex->pos++;
            } else if (hex && ch >= 'A' && ch <= 'F') {
                val = val * 16u + (uint32_t)(ch - 'A' + 10);
                lex->pos++;
            } else {
                break;
            }
        }

        // A fractional part turns the literal into a float. The dot only
        // counts when a digit follows it, so a trailing dot stays a token of
        // its own for whoever reads the line.
        if (!hex && lex->src[lex->pos] == '.' &&
            lex->src[lex->pos + 1] >= '0' &&
            lex->src[lex->pos + 1] <= '9') {
            float fval = (float)val;
            float scale = 1.0f;

            lex->pos++;
            while (lex->src[lex->pos] >= '0' &&
                   lex->src[lex->pos] <= '9') {
                scale *= 10.0f;
                fval = fval * 10.0f + (float)(lex->src[lex->pos] - '0');
                lex->pos++;
            }
            fval /= scale;

            lex->cur.kind = TOK_FLOAT;
            lex->cur.float_val = fval;
            lex->cur.text[0] = '\0';
            return;
        }

        lex->cur.kind = TOK_INT;
        lex->cur.int_val = (int32_t)val;
        itoa((int32_t)val, lex->cur.text);
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
