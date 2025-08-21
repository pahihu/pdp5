/* 
 * PDP-5
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "curterm.h"
#include "version.h"

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
#define BIT1        02000
#define BIT2        01000
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
#define WORD(x)     (07777 & (x))
#define OP_MASK     07000
#define Y_MASK      00177
#define PAGE_MASK   07600
#define I_BIT       BIT3
#define P_BIT       BIT4
#define I(x)        (I_BIT & (x))
#define P(x)        (P_BIT & (x))
#define DEV(x)      ((00770 & (x)) >> 3)
#define IOP(x)      (07 & (x))
#define OPR(x)      (BIT3 & (x))

#define MS          1000
#define CycleTime      6

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
    LUI = c;
    if (OFF == KbdFlag) {
        KbdFlag = ON; INT++;
    }
}
    
void ral(void)
{
    AC <<= 1; AC += L;
    L = (CY_BIT & AC) ? 1 : 0;
    AC = WORD(AC);
}

void rar(void)
{
    if (L) AC += CY_BIT;
    L = 01 & AC ? 1 : 0;
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
        if (1 & iop) {/*KSR*/ if (KbdFlag) PC++; }
        if (2 & iop) {/*KCC*/
            AC = 0;
            if (KbdFlag) {
                KbdFlag = OFF; INT--;
            }
        }
        if (4 & iop) /*KRS*/ AC |= LUI;
        break;
    case 4: /*TELEPRINTER/PUNCH*/
        if (1 & iop) {/*TSF*/ if (PrnFlag) PC++; }
        if (2 & iop) {/*TCF*/
            if (PrnFlag) {
                PrnFlag = OFF; INT--;
            }
        }
        if (4 & iop) {/*TPC*/
            LUO = 0377 & AC;
            putc(LUO, fout);
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

void decode(Word a)
{
    static char *ops[] = {"and", "tad", "isz", "dca", "jms", "jmp", "iot", "opr"};
    Word IR, Y;
    unsigned x;

    IR = M[a];
    Y = Y_MASK & IR;
    if (P(IR)) Y += (PAGE_MASK & a);
    x = IR >> 9;
    switch(x){
    case  6: printf("iot %02o,%1o  ", DEV(IR), IOP(IR)); break;
    case  7: printf("opr %d,%03o ", OPR(IR) ? 2 : 1, 00377 & IR); break;
    default: printf("%s %c %04o", ops[x], I(IR) ? 'i' : ' ', Y);
    } 
}

void status(void)
{
    printf("%04o: %04o   ", PC, M[PC]);
    decode(PC);
    printf(" L:AC=%1d:%04o IEN:%d INT:%d (T=%u)\n", L, AC, IEN, INT, T);
}

void step(void)
{
    Word Y, IR, w;
    int cond;

    if (0 == (T & 1023)) {
        GetKey();
    }
    if (PC & ~07777) { 
        status();
        exit(1);
    }
    IR = M[PC];
    Y = Y_MASK & IR;
    if (P(IR)) Y += (PAGE_MASK & PC);
    if (I(IR)) {
        w = M[Y];
        if (00010 == (07770 & Y)) { M[Y] = ++w; }
        Y = w; T++;
    }
    switch (OP_MASK & IR) {
    case 00000:/*AND*/ AC &= M[Y]; T += 3; break;
    case 01000:/*TAD*/ w += M[Y]; if (CY_BIT & w) L = 1 - L; AC = WORD(w); T += 3; break;
    case 02000:/*ISZ*/ w = M[Y]++; if (0 == w) PC++; T += 3; break;
    case 03000:/*DCA*/ M[Y] = AC; AC = 0; T += 3; break;
    case 04000:/*JMS*/ M[Y] = PC + 1; PC = Y + 1; T += 4; return;
    case 05000:/*JMP*/ PC = Y; T += 2; return;
    case 06000:/*IOT*/ iot(DEV(IR), IOP(IR)); T += 2; break;
    case 07000:/*OPR*/
        if (OPR(IR)) { /*OPR2*/
            cond = 0;
            /* Event1 ----------------- */
            if (BIT5 & IR) /*SMA*/ cond = cond || (SIGN_BIT & AC);
            if (BIT6 & IR) /*SZA*/ cond = cond || (0 == AC);
            if (BIT7 & IR) /*SNL*/ cond = cond || (1 == L);
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
    PC++;
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
#define octal(x)     sscanf(buf,"%ho",(x))

void cli(void)
{
    static char* cmds[] = {"continue", "deposit", "examine", "help", "load", "quit", "start", "step", NULL};
    Word adr, w;
    char buf[80], cmd[80];
    int i;

    printf("PDP-5   %s\n", VERSION);

    SR = 0; reset(); cmd[0] = 0;
    for (;;) {
        printf("] ");
        buf[0] = 0;
        if (!fgets(buf, sizeof(buf)-1, stdin))
            exit(0);
        buf[strlen(buf)-1] = 0;
        if (!strlen(buf)) {
            strcpy(buf, cmd);
        }
        for (i = 0; cmds[i]; i++) {
            if (!strpfx(buf,cmds[i]))
                break;
        }
        switch (i) {
        case 0: /*continue*/ HLT = OFF; PC = SR; run(); break;
        case 1: /*deposit*/  while (octal(&w)) { M[SR++] = w; } break;
        case 2: /*examine*/
            w = SR + 8;
            if (octal(&adr)) {
                SR = adr;
                w = octal(&adr) ? adr : SR + 8;
            }
            for (; SR < w; SR++) {
                printf("%04o: %04o   ", SR, M[SR]);
                decode(SR);
                printf("\n");
            }
            break;
        case 3: /*help*/
Help:       printf("\tcontinue                continue program\n");
            printf("\tdeposit val [...val]    deposit value(s) in memory\n");
            printf("\texamine [start [end]]   examine memory\n");
            printf("\thelp                    print help\n");
            printf("\tload adr                load val into SR\n");
            printf("\tquit                    quit\n");
            printf("\tstart [adr]             start program\n");
            printf("\tstep [n]                step program\n");
            break;
        case 4: /*load*/ octal(&SR); break;
        case 5: /*quit*/ exit(0); break;
        case 6: /*start*/
            if (octal(&adr)) SR = adr;
            fin = fopen("reader", "rt");
            fout = fopen("punch", "w+");
            atexit(finish);

            reset(); run();
            break;
        case 7: /*step*/
            w = 1; PC = SR;
            if (octal(&adr)) w = adr;
            for (; w; w--) {
                status(); step();
            }
            SR = PC;
            break;
        default:
            goto Help;
        }
        strcpy(cmd, buf);
    }
}

int main(void)
{
    T = 0; Evt = 0; EvtPtr = NULL;
        
    cli();
    return 0;
}

/* vim: set ts=4 sw=4 et: */
