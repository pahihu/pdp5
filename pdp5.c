/* 
 * PDP-5
 *
 *
 * History
 * =======
 * 250822AP symbolic disassembler
 *          ODT-like interface with assembler
 * 250821AP initial revision
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "curterm.h"
#include "version.h"

#define O   printf
#define RANGE(x,lo,hi)  ((lo) <= (x) && (x) <= (hi))

#if defined(NDEBUG)
#define ASSERT(x)
#else
#define ASSERT(x)   __assert(__FUNCTION__,__LINE__,x,""#x)
void __assert(const char *fn, int lno, int cond, const char *expr)
{
    if (!cond) {
        fprintf(stderr,"%s:%d %s failed\n", fn, lno, expr);
        exit(1);
    }
}
#endif

typedef unsigned short  Word;
typedef enum {OFF, ON} Toggle;
Word L, AC, PC, SR, M[4096];
int IEN, INT;
Toggle HLT;
unsigned T;

/* ASR33 */
Toggle KbdFlag, PrnFlag;
int LUI, LUO;
FILE *fin, *fout;

unsigned Evt;
Toggle *EvtPtr;

#define BIT0        04000
#define BIT3        00400
#define BIT4        00200
#define BIT5        00100
#define BIT6        00040
#define BIT7        00020
#define BIT8        00010
#define BIT9        00004
#define BIT10       00002
#define BIT11       00001

#define SIGN_BIT    BIT0
#define CY_BIT     010000
#define W_MASK      07777
#define WORD(x)     (W_MASK & (x))
#define OP_MASK     07000
#define OP(x)       (OP_MASK & (x))
#define Y_MASK      00177
#define PAGE(x)     (07600 & (x))
#define I(x)        (BIT3 & (x))
#define P(x)        (BIT4 & (x))
#define DEV(x)      ((00770 & (x)) >> 3)
#define IOP(x)      (07 & (x))
#define OPR(x)      (BIT3 & (x))

#define LO(x)       ((x) & 077)
#define HI(x)       LO((x) >> 6)

#define MS          1000
#define CycleTime      6

Word INC(Word *p)
{
    return *p = WORD(*p + 1);
}

Word DEC(Word *p)
{
    return *p = WORD(*p - 1);
}

void GetKey(void)
{
    int c;

    c = 0;
    if (fin && !feof(fin)) {
        c = getc(fin);
        goto Out;
    }
    if (!has_key())
        return;
    c = getkey(); kbflush();
    putchar(c);
Out:
    LUI = 0377 & c;
    if (OFF == KbdFlag) {
        KbdFlag = ON; INT++;
    }
}
    
void Out(int ch)
{
    putchar(RANGE(ch,32,127) ? ch : '.');
    putc(ch, fout);
}

void ral(void)
{
    AC = (AC << 1) + L;
    L = (CY_BIT & AC) ? 1 : 0;
    AC = WORD(AC);
}

void rar(void)
{
    if (L) AC += CY_BIT;
    L = 01 & AC;
    AC >>= 1;
}

void iot(int dev, int iop)
{
    switch(dev){
    case 0: /*INT*/
        switch(iop){
        case 1:/*ION*/ IEN = 2; break;
        case 2:/*IOF*/ IEN = 0; break;
        }
        break;
    case 3: /*KEYBOARD/READER*/
        if (1 & iop) {/*KSR*/ if (KbdFlag) INC(&PC); }
        if (2 & iop) {/*KCC*/
            AC = 0;
            if (KbdFlag) {
                KbdFlag = OFF; INT--;
            }
        }
        if (4 & iop) /*KRS*/ AC |= LUI;
        break;
    case 4: /*TELEPRINTER/PUNCH*/
        if (1 & iop) {/*TSF*/ if (PrnFlag) INC(&PC); }
        if (2 & iop) {/*TCF*/
            if (PrnFlag) {
                PrnFlag = OFF; INT--;
            }
        }
        if (4 & iop) {/*TPC*/
            LUO = 0377 & AC;
            Out(LUO);
            Evt = T + 100*MS / CycleTime; EvtPtr = &PrnFlag;
        }
        break;
    }
}

void reset(void)
{
    L = AC = 0;
    IEN = 0; INT = 0;
    PC = SR;
    HLT = OFF;
}

typedef struct {
    const char *sym;
    Word  w;
} SYM;

SYM itab[] = {
    {"and",     0}, {"tad", 01000}, {"isz", 02000}, {"dca", 03000},
    {"jms", 04000}, {"jmp", 05000}, {"iot", 06000}, {"opr", 07000},
    {"ion", 06001}, {"iof", 06002},
    {"ksf", 06031}, {"kcc", 06032}, {"krs", 06034}, {"krb", 06036},
    {"tsf", 06041}, {"tcf", 06042}, {"tpc", 06044}, {"tls", 06046},

    {"nop", 07000}, {"cla", 07200}, {"cll", 07100}, {"cma", 07040},
    {"iac", 07001}, {"cia", 07041}, {"cml", 07020}, {"rar", 07010},
    {"rtr", 07012}, {"ral", 07004}, {"rtl", 07006},

    {"cla", 07600}, {"skp", 07410}, {"sma", 07500}, {"spa", 07510},
    {"sza", 07440}, {"sna", 07450}, {"snl", 07420}, {"szl", 07430},
    {"osr", 07404}, {"hlt", 07402},

    {  "i", 00400},
    { NULL, 0},
};

SYM *FindSym(SYM *x, Word w)
{
    SYM *y;

    if (!x) x = &itab[0];
    y = NULL;
    for (; x->sym; x++) {
        if (OP(w) != OP(x->w))
            continue;
        if ((w & x->w) == x->w) {
            if (!y || x->w > y->w)
                y = x;
        }
    }
    return y;
}

SYM *FindOp(const char *s)
{
    SYM *y;

    for (y = &itab[0]; y->sym; y++) {
        if (!strcmp(s, y->sym))
            return y;
    }
    return NULL;
}

void decode(Word a)
{
    Word IR, Y;
    SYM  *y;

    IR = M[a];
    ASSERT(0 == (IR & ~W_MASK));

    switch(IR >> 9){
    case 0: case 1: case 2: case 3: case 4: case 5:
        y = FindSym(NULL, OP(IR));
        ASSERT(NULL != y);
        Y = (Y_MASK & IR) + (P(IR) ? PAGE(a) : 0);
        O("%s %c %04o", y->sym, " i"[I(IR) >> 8], Y);
        break;
    case 6:
        y = FindSym(NULL, IR);
        if (y && IR == y->w) O("%s      ", y->sym);
        else   O("iot   %02o,%1o  ", DEV(IR), IOP(IR));
        break;
    case 7:
        for (y = &itab[0]; y->sym; y++) {
            if (OP(IR) != OP(y->w))
                continue;
            if ((IR & y->w) == y->w) {
                y = FindSym(y, IR);
                O("%s ", y->sym);
            }
        }
        break;
    }
}

void status(Word where, int regs)
{
    O("%04o/%04o   ", where, M[where]);
    decode(where);
    if (regs)
        O(" L:AC=%1d:%04o IEN:%d INT:%d (T=%u)", L, AC, IEN, INT, T);
    O("\n");
}

void step(void)
{
    Word Y, IR, w;
    int cond;

    if (0 == (T & 1023)) {
        GetKey();
    }
    if (PC & ~W_MASK) {
        status(PC, 1);
        exit(1);
    }
    IR = M[PC];
    Y = Y_MASK & IR;
    if (P(IR)) Y += PAGE(PC);
    if (I(IR)) {
        w = M[Y];
        if (00010 == (07770 & Y)) { M[Y] = INC(&w); }
        Y = w; T++;
    }
    switch (OP(IR) >> 9) {
    case 0:/*AND*/ AC &= M[Y]; T += 3; break;
    case 1:/*TAD*/ w += M[Y]; if (CY_BIT & w) L = 1 - L; AC = WORD(w); T += 3; break;
    case 2:/*ISZ*/ w = INC(&M[Y]); if (0 == w) INC(&PC); T += 3; break;
    case 3:/*DCA*/ M[Y] = AC; AC = 0; T += 3; break;
    case 4:/*JMS*/ M[Y] = PC + 1; PC = Y + 1; T += 4; return;
    case 5:/*JMP*/ PC = Y; T += 2; return;
    case 6:/*IOT*/ iot(DEV(IR), IOP(IR)); T += 2; break;
    case 7:/*OPR*/
        if (OPR(IR)) { /*OPR2*/
            cond = 0;
            /* Event1 ----------------- */
            if (BIT5 & IR) /*SMA*/ cond = cond || (SIGN_BIT & AC); /*SPA*/
            if (BIT6 & IR) /*SZA*/ cond = cond || (0 == AC);       /*SNA*/
            if (BIT7 & IR) /*SNL*/ cond = cond || (1 == L);        /*SZL*/
            if (BIT8 & IR) cond = !cond;
            /* Event2 ----------------- */
            if (BIT4 & IR) /*CLA*/ AC = 0;
            /* Event3 ----------------- */
            if (BIT9 & IR) /*OSR*/ AC |= SR;
            if (BIT10 & IR) {/*HLT*/
                HLT = ON;
                return;
            }
        } else { /*OPR1*/
            /* Event1 ----------------- */
            if (BIT4 & IR) /*CLA*/ AC = 0;
            if (BIT5 & IR) /*CLL*/  L = 0;
            /* Event2 ----------------- */
            if (BIT6 & IR) /*CMA*/ AC = ~AC;
            if (BIT7 & IR) /*CML*/  L = 1 - L;
            if (BIT8 & IR) { /*RAR*/
                rar(); if (BIT10 & IR) rar(); /*RTR*/
            } else if (BIT9 & IR) { /*RAL*/
                ral(); if (BIT10 & IR) ral(); /*RTL*/
            }
            /* Event3 ----------------- */
            if (BIT11 & IR) /*IAC*/ AC = WORD(AC + 1);
        }
    }
    if (1 == IEN && INT) {
        IEN = 0; INT--;
        M[1] = PC; PC = 2;
        return;
    }
    INC(&PC);
    if (1 < IEN) IEN--;
    if (Evt && Evt <= T) {
        if (OFF == *EvtPtr) {
            *EvtPtr = ON; INT++;
        }
        Evt = 0;
    }
}

void run(void)
{
    prepterm(1);
    while (!HLT) {
        step();
    }
    prepterm(0);
}

void finish(void)
{
    if (fin) fclose(fin);
    fclose(fout);
}

#define strpfx(x,y)  strncmp(x,y,strlen(x))
#define IsSpace(x)   RANGE(x,0,' ')
#define IsOctal(x)   RANGE(x,'0','7')
#define IsAlpha(x)   RANGE(x,'a','z')

const char *S;
Word W;
char Str[80];
int Scan(void)
{
    int n;

    while (*S && IsSpace(*S)) S++;

    n = 0; Str[n] = 0;
    while (*S && !IsSpace(*S)) {
        Str[n++] = *S++;
    }
    Str[n] = 0;
    return n;
}

int Octal(void)
{
    int i;

    if (!Scan())
        return 0;
    W = 0;
    for (i = 0; IsOctal(Str[i]); i++) {
        W = 8 * W + (07 & (Str[i] - '0'));
    }
    W = WORD(W);
    return 1;
}

void cli(void)
{
    static char* cmds[] = {"continue", "deposit", "examine", "help", "load", "quit", "start", "step", NULL};
    Word v, w;
    char buf[80], cmd[80];
    int i, open = 0;

    O("PDP-5   %s\n", VERSION);

    SR = 0; reset(); cmd[0] = 0;
    for (;;) {
        if (open) O("%04o/%04o ", SR, M[SR]); else O("] ");
        buf[0] = 0;
        if (!fgets(buf, sizeof(buf)-1, stdin))
            exit(0);
        buf[strlen(buf)-1] = 0;
        if (!strlen(buf)) {
            if (open) {
                INC(&SR); continue;
            }
            strcpy(buf, cmd);
        }
        S = buf; Octal();
        for (i = 0; cmds[i]; i++) {
            if (!strpfx(Str,cmds[i])) {
                open = 0;
                strcpy(cmd, Str);
                break;
            }
        }
        switch (i) {
        case 0: /*continue*/
Continue:   INC(&PC); HLT = OFF; run();
            break;
        case 1: /*deposit*/  while (Octal()) { M[SR] = W; INC(&SR); } break;
        case 2: /*examine*/
            if (Octal()) SR = W;
            w = Octal() ? W - SR : 8;
            for (; w; w--) {
                status(SR, 0); INC(&SR);
            }
            break;
        case 3: /*help*/
Help:       O("\tcontinue                continue program\n");
            O("\tdeposit val [...val]    deposit value(s) in memory\n");
            O("\texamine [start [end]]   examine memory\n");
            O("\thelp                    print help\n");
            O("\tload adr                load val into SR\n");
            O("\tquit                    quit\n");
            O("\tstart [adr]             start program\n");
            O("\tstep [n]                step program\n");
            O("\tn                       enter ODT\n");
            O("\tn | asm                 change word\n");
            O("\t^                       open prev. word\n");
            O("\t@                       indirect\n");
            O("\tg                       start\n");
            O("\tp                       continue\n");
            break;
        case 4: /*load*/ Octal(); SR = W; break;
        case 5: /*quit*/ exit(0); break;
        case 6: /*start*/
            if (Octal()) SR = W;
Start:      fin = fopen("reader", "rt");
            fout = fopen("punch", "w+");
            atexit(finish);

            reset(); run();
            break;
        case 7: /*step*/
            PC = SR;
            for (w = Octal() ? W : 1; w; w--) {
                status(PC, 1); step();
            }
            SR = PC;
            break;
        default:
            if (open) {
                SYM *y;
                switch(Str[0]){
                case '^': DEC(&SR); break;
                case '@': SR = M[SR]; break;
                case 'g': goto Start;
                case 'c': goto Continue;
                case 't':
Leader:             for (i = 0; i < 10; i++)
                        Out(0200);
                    break;
                case 'p':
                    for (; w; w--) {
                        v = M[SR];
                        Out(0100 + HI(SR));
                        Out(LO(SR));
                        Out(HI(v));
                        Out(LO(v));
                    }
                    break;
                case 'e': goto Leader;
                default:
                    if (IsAlpha(Str[0])) {
                        y = FindOp(Str);
                        if (y) w = y->w; else goto ErrOut;
                        while (Octal()) {
                            if (IsOctal(Str[0])) {
                                if (!PAGE(W)) w &= ~BIT4;
                                else if (PAGE(SR) == PAGE(W)) w |= BIT4;
                                else goto ErrOut;
                                w |= Y_MASK & W;
                            } else {
                                y = FindOp(Str);
                                if (y) w |= y->w; else goto ErrOut;
                            }
                        }
                        W = w;
                    }
                    M[SR] = W; INC(&SR);
                }
                continue;
ErrOut:         O(" ?"); open = 0;
                continue;
            } else if (IsOctal(Str[0])) {
                open = 1; SR = W;
                continue;
            }
            goto Help;
        }
    }
}

int main(void)
{
    int i;

    srandom(time(NULL));
    for (i = 0; i < 4096; i++)
        M[i] = WORD(random());

    T = 0; Evt = 0; EvtPtr = NULL;
        
    cli();
    return 0;
}

/* vim: set ts=4 sw=4 et: */
