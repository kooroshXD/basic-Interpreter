#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <setjmp.h>

#ifdef _WIN32
#include <conio.h>
#else
static int getche(void)
{
    int c = getchar();
    if (c != EOF)
        putchar(c);
    return c;
}
#endif

#define NUM_LAB 100
#define LAB_LEN 25
#define FOR_NEST 25
#define SUB_NEST 25
#define PROG_SIZE 10000

#define DELIMITER 1
#define VARIABLE 2
#define NUMBER 3
#define COMMAND 4
#define STRING 5
#define QUOTE 6

#define PRINT 1
#define INPUT 2
#define IF 3
#define THEN 4
#define FOR 5
#define NEXT 6
#define TO 7
#define GOTO 8
#define EOL 9
#define FINISHED 10
#define GOSUB 11
#define RETURN 12
#define END 13

int load_program(char *p, char *fname);
char get_token(void);
void assignment(void);
int get_next_label(char *s);
int iswhite(char c);
int isdelim(char c);
int look_up(char *s);
int find_var(char *s);
char *gpop(void);
char *find_label(char *s);
struct for_stack fpop(void);
void print(void), scan_labels(void), find_eol(void), exec_goto(void);
void exec_if(void), exec_for(void), next(void), fpush(struct for_stack i), input(void);
void gosub(void), greturn(void), gpush(char *s), label_init(void);
void putback(void), serror(int), get_exp(int *), primitive(int *result);
void level2(int *), level3(int *), level4(int *), level5(int *), level6(int *);
void arith(char o, int *r, int *h), unary(char o, int *r);

char *prog;
jmp_buf e_buf;
int variable[26] = {0};

struct commands {
    char commands[20];
    char tok;
} table[] = {
    {"print", PRINT}, {"input", INPUT}, {"if", IF}, {"then", THEN},
    {"for", FOR}, {"next", NEXT}, {"to", TO}, {"goto", GOTO},
    {"gosub", GOSUB}, {"return", RETURN}, {"end", END}, {"", END}
};

char token[80], token_type, tok;

struct label {
    char name[LAB_LEN];
    char *p;
};

struct label label_table[NUM_LAB];

struct for_stack {
    int var;
    int target;
    char *Ioc;
} fstack[FOR_NEST];

char *gstack[SUB_NEST];
int ftos, gtos;

int main(int argc, char *argv[])
{
    char *p_buf;

    if (argc != 2) {
        printf("\nusage: run <filename>\n");
        return 1;
    }

    p_buf = (char *)malloc(PROG_SIZE);
    if (!p_buf) {
        printf("\nallocation failure\n");
        return 1;
    }

    if (!load_program(p_buf, argv[1])) {
        printf("\nfile not loaded\n");
        free(p_buf);
        return 1;
    }

    if (setjmp(e_buf)) {
        free(p_buf);
        return 1;
    }

    prog = p_buf;
    scan_labels();
    ftos = 0;
    gtos = 0;

    do {
        token_type = get_token();

        if (token_type == VARIABLE) {
            putback();
            assignment();
        } else if (token_type == NUMBER) {
            /* A leading number is a BASIC line label. */
        } else {
            switch (tok) {
                case PRINT: print(); break;
                case GOTO: exec_goto(); break;
                case IF: exec_if(); break;
                case FOR: exec_for(); break;
                case NEXT: next(); break;
                case INPUT: input(); break;
                case GOSUB: gosub(); break;
                case RETURN: greturn(); break;
                case END:
                    free(p_buf);
                    return 0;
                case EOL:
                    break;
                case FINISHED:
                    break;
                default:
                    serror(0);
            }
        }
    } while (tok != FINISHED);

    free(p_buf);
    return 0;
}

int load_program(char *p, char *fname)
{
    FILE *fp;
    size_t n;

    fp = fopen(fname, "rb");
    if (!fp)
        return 0;

    n = fread(p, 1, PROG_SIZE - 1, fp);
    p[n] = '\0';

    fclose(fp);
    return 1;
}

void assignment(void)
{
    int var, value;

    get_token();
    if (token_type != VARIABLE || !isalpha((unsigned char)*token)) {
        serror(4);
        return;
    }

    var = toupper((unsigned char)*token) - 'A';

    get_token();
    if (*token != '=') {
        serror(3);
        return;
    }

    get_exp(&value);
    variable[var] = value;
}

void print(void)
{
    int answer, len = 0, spaces;
    char last_delim = 0;

    do {
        get_token();

        if (tok == EOL || tok == FINISHED)
            break;

        if (token_type == QUOTE) {
            printf("%s", token);
            len += (int)strlen(token);
            get_token();
        } else {
            putback();
            get_exp(&answer);
            get_token();
            len += printf("%d", answer);
        }

        last_delim = *token;

        if (*token == ';') {
            spaces = 8 - (len % 8);
            len += spaces;
            while (spaces-- > 0)
                putchar(' ');
        } else if (*token == ',') {
            /* Comma separates PRINT arguments. */
        } else if (tok != EOL && tok != FINISHED) {
            serror(0);
        }
    } while (*token == ';' || *token == ',');

    if (tok == EOL || tok == FINISHED) {
        if (last_delim != ';' && last_delim != ',')
            putchar('\n');
    } else {
        serror(0);
    }
}

void scan_labels(void)
{
    char *temp;
    char *line_start;
    int addr;

    label_init();
    temp = prog;

    while (*prog != '\0') {
        line_start = prog;
        get_token();

        if (token_type == NUMBER) {
            addr = get_next_label(token);
            if (addr == -1)
                serror(5);
            if (addr == -2)
                serror(6);

            strcpy(label_table[addr].name, token);
            label_table[addr].p = line_start;
        }

        find_eol();
    }

    prog = temp;
}

void find_eol(void)
{
    while (*prog != '\n' && *prog != '\r' && *prog != '\0')
        ++prog;

    if (*prog == '\r') {
        ++prog;
        if (*prog == '\n')
            ++prog;
    } else if (*prog == '\n') {
        ++prog;
    }
}

int get_next_label(char *s)
{
    int t;

    for (t = 0; t < NUM_LAB; t++) {
        if (label_table[t].name[0] == '\0')
            return t;
        if (!strcmp(label_table[t].name, s))
            return -2;
    }

    return -1;
}

char *find_label(char *s)
{
    int i;

    for (i = 0; i < NUM_LAB; i++) {
        if (label_table[i].name[0] == '\0')
            break;

        if (!strcmp(label_table[i].name, s))
            return label_table[i].p;
    }

    return NULL;
}

void exec_goto(void)
{
    char *loc;

    get_token();
    loc = find_label(token);

    if (loc == NULL)
        serror(7);
    else
        prog = loc;
}

void label_init(void)
{
    int t;

    for (t = 0; t < NUM_LAB; t++) {
        label_table[t].name[0] = '\0';
        label_table[t].p = NULL;
    }
}

void exec_if(void)
{
    int x, y, cond;
    char op;

    get_exp(&x);
    get_token();

    if (token_type != DELIMITER || !strchr("=<>", *token)) {
        serror(0);
        return;
    }

    op = *token;
    get_exp(&y);

    cond = 0;

    switch (op) {
        case '<': cond = (x < y); break;
        case '>': cond = (x > y); break;
        case '=': cond = (x == y); break;
    }

    if (cond) {
        get_token();
        if (tok != THEN) {
            serror(8);
            return;
        }
    } else {
        find_eol();
    }
}

void exec_for(void)
{
    struct for_stack i;
    int value;

    get_token();
    if (token_type != VARIABLE || !isalpha((unsigned char)*token)) {
        serror(4);
        return;
    }

    i.var = toupper((unsigned char)*token) - 'A';

    get_token();
    if (*token != '=') {
        serror(3);
        return;
    }

    get_exp(&value);
    variable[i.var] = value;

    get_token();
    if (tok != TO) {
        serror(9);
        return;
    }

    get_exp(&i.target);

    if (value <= i.target) {
        /* Save the first executable position after the FOR line. */
        if (*prog == '\r') {
            if (prog[1] == '\n')
                prog += 2;
            else
                prog++;
        } else if (*prog == '\n') {
            prog++;
        }

        i.Ioc = prog;
        fpush(i);
    } else {
        /* Skip the loop body and stop at its NEXT. */
        find_eol();
        while (*prog != '\0') {
            get_token();
            if (tok == NEXT)
                break;
            if (tok == FINISHED)
                break;
        }
    }
}

void next(void)
{
    struct for_stack i;

    i = fpop();
    variable[i.var]++;

    if (variable[i.var] > i.target)
        return;

    fpush(i);
    prog = i.Ioc;
}

void fpush(struct for_stack i)
{
    if (ftos >= FOR_NEST) {
        serror(10);
        return;
    }

    fstack[ftos++] = i;
}

struct for_stack fpop(void)
{
    if (ftos == 0) {
        serror(11);
        return fstack[0];
    }

    return fstack[--ftos];
}

void input(void)
{
    char str[80];
    char var;
    char *end;
    long value;

    get_token();

    if (token_type == QUOTE) {
        printf("%s", token);
        get_token();
        if (*token != ',') {
            serror(1);
            return;
        }
        get_token();
    } else {
        printf("?");
    }

    if (token_type != VARIABLE || !isalpha((unsigned char)*token)) {
        serror(4);
        return;
    }

    var = (char)(toupper((unsigned char)*token) - 'A');

    for (;;) {
        if (!fgets(str, sizeof(str), stdin)) {
            serror(0);
            return;
        }

        value = strtol(str, &end, 10);

        while (isspace((unsigned char)*end))
            ++end;

        if (*str != '\0' && end != str && *end == '\0')
            break;

        printf("Redo from start?\n");
    }

    variable[(unsigned char)var] = (int)value;
}

void gosub(void)
{
    char *loc;

    get_token();
    loc = find_label(token);

    if (loc == NULL) {
        serror(7);
    } else {
        /* Return to the position immediately after the GOSUB target. */
        gpush(prog);
        prog = loc;
    }
}

void greturn(void)
{
    prog = gpop();
}

void gpush(char *s)
{
    if (gtos >= SUB_NEST) {
        serror(12);
        return;
    }

    gstack[gtos++] = s;
}

char *gpop(void)
{
    if (gtos == 0) {
        serror(13);
        return NULL;
    }

    return gstack[--gtos];
}

void putback(void)
{
    char *t = token;

    while (*t) {
        --prog;
        ++t;
    }
}

void serror(int error)
{
    static const char *e[] = {
        "syntax error",
        "unbalanced parentheses",
        "no expression present",
        "equals sign expected",
        "not a variable",
        "label table full",
        "duplicate label",
        "undefined label",
        "THEN expected",
        "TO expected",
        "too many nested FOR loops",
        "NEXT without FOR",
        "too many nested GOSUB",
        "RETURN without GOSUB"
    };

    if (error < 0 || error >= (int)(sizeof(e) / sizeof(e[0])))
        error = 0;

    printf("\n%s\n", e[error]);
    longjmp(e_buf, 1);
}

char get_token(void)
{
    char *temp = token;
    int c;

    token_type = 0;
    tok = 0;
    *token = '\0';

    /* Skip spaces and tabs. */
    while (iswhite(*prog))
        ++prog;

    if (*prog == '\0') {
        *token = '\0';
        tok = FINISHED;
        return tok;
    }

    /* End of line: support both CRLF and LF files. */
    if (*prog == '\r' || *prog == '\n') {
        if (*prog == '\r') {
            ++prog;
            if (*prog == '\n')
                ++prog;
        } else {
            ++prog;
        }

        tok = EOL;
        token[0] = '\n';
        token[1] = '\0';
        return (token_type = DELIMITER);
    }

    /* Single-character delimiters. */
    if (strchr("+-*^/%=;(),><", *prog)) {
        *temp++ = *prog++;
        *temp = '\0';
        return (token_type = DELIMITER);
    }

    /* Quoted string. */
    if (*prog == '"') {
        ++prog;

        while (*prog != '"' && *prog != '\0' && *prog != '\r' && *prog != '\n')
            *temp++ = *prog++;

        if (*prog == '\0' || *prog == '\r' || *prog == '\n')
            serror(1);

        ++prog;
        *temp = '\0';
        return (token_type = QUOTE);
    }

    /* Number. */
    if (isdigit((unsigned char)*prog)) {
        while (!isdelim(*prog))
            *temp++ = *prog++;

        *temp = '\0';
        return (token_type = NUMBER);
    }

    /* Identifier / command. */
    if (isalpha((unsigned char)*prog)) {
        while (!isdelim(*prog))
            *temp++ = *prog++;

        *temp = '\0';
        token_type = STRING;

        tok = (char)look_up(token);
        if (!tok)
            token_type = VARIABLE;
        else
            token_type = COMMAND;

        return token_type;
    }

    /* Unknown character. */
    c = (unsigned char)*prog++;
    token[0] = (char)c;
    token[1] = '\0';
    serror(0);
    return 0;
}

int look_up(char *s)
{
    int i;

    for (i = 0; s[i]; i++)
        s[i] = (char)tolower((unsigned char)s[i]);

    for (i = 0; table[i].commands[0]; i++) {
        if (!strcmp(table[i].commands, s))
            return table[i].tok;
    }

    return 0;
}

int isdelim(char c)
{
    return strchr(";,+-<>/*%^=()", c) != NULL ||
           c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0';
}

int iswhite(char c)
{
    return c == ' ' || c == '\t';
}

void get_exp(int *result)
{
    get_token();

    if (!*token) {
        serror(2);
        return;
    }

    level2(result);
    putback();
}

void level2(int *result)
{
    char op;
    int hold;

    level3(result);

    while ((op = *token) == '+' || op == '-') {
        get_token();
        level3(&hold);
        arith(op, result, &hold);
    }
}

void level3(int *result)
{
    char op;
    int hold;

    level4(result);

    while ((op = *token) == '*' || op == '/' || op == '%') {
        get_token();
        level4(&hold);
        arith(op, result, &hold);
    }
}

void level4(int *result)
{
    int hold;

    level5(result);

    if (*token == '^') {
        get_token();
        level4(&hold);
        arith('^', result, &hold);
    }
}

void level5(int *result)
{
    char op = 0;

    if (token_type == DELIMITER && (*token == '+' || *token == '-')) {
        op = *token;
        get_token();
    }

    level6(result);

    if (op)
        unary(op, result);
}

void level6(int *result)
{
    if (token_type == DELIMITER && *token == '(') {
        get_token();
        level2(result);

        if (*token != ')')
            serror(1);

        get_token();
    } else {
        primitive(result);
    }
}

void primitive(int *result)
{
    switch (token_type) {
        case VARIABLE:
            *result = find_var(token);
            get_token();
            return;

        case NUMBER:
            *result = atoi(token);
            get_token();
            return;

        default:
            serror(0);
    }
}

void arith(char o, int *r, int *h)
{
    int t, ex;

    switch (o) {
        case '-': *r = *r - *h; break;
        case '+': *r = *r + *h; break;
        case '*': *r = *r * *h; break;

        case '/':
            if (*h == 0)
                serror(0);
            *r = *r / *h;
            break;

        case '%':
            if (*h == 0)
                serror(0);
            t = *r / *h;
            *r = *r - (t * *h);
            break;

        case '^':
            ex = *r;

            if (*h < 0) {
                serror(0);
                break;
            }

            if (*h == 0) {
                *r = 1;
                break;
            }

            for (t = *h - 1; t > 0; --t)
                *r = *r * ex;
            break;
    }
}

void unary(char o, int *r)
{
    if (o == '-')
        *r = -(*r);
}

int find_var(char *s)
{
    if (!isalpha((unsigned char)*s)) {
        serror(4);
        return 0;
    }

    return variable[toupper((unsigned char)*s) - 'A'];
}
