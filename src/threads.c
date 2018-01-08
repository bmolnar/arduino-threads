#include "threads.h"

#define THR_RAMEND (RAMEND + 1)
#define THR_TBLEND (THR_RAMEND - 32)
#define MAX_THRS 4
#define STACK_SZ 384

#define _PUSH_REGISTERS() \
  asm volatile ( \
    "push r31 \n\t" \
    "push r30 \n\t" \
    "push r29 \n\t" \
    "push r28 \n\t" \
    "push r27 \n\t" \
    "push r26 \n\t" \
    "push r25 \n\t" \
    "push r24 \n\t" \
    "push r23 \n\t" \
    "push r22 \n\t" \
    "push r21 \n\t" \
    "push r20 \n\t" \
    "push r19 \n\t" \
    "push r18 \n\t" \
    "push r17 \n\t" \
    "push r16 \n\t" \
    "push r15 \n\t" \
    "push r14 \n\t" \
    "push r13 \n\t" \
    "push r12 \n\t" \
    "push r11 \n\t" \
    "push r10 \n\t" \
    "push r9 \n\t" \
    "push r8 \n\t" \
    "push r7 \n\t" \
    "push r6 \n\t" \
    "push r5 \n\t" \
    "push r4 \n\t" \
    "push r3 \n\t" \
    "push r2 \n\t" \
    "push r1 \n\t" \
    "push r0 \n\t" \
    ::) 

#define _POP_REGISTERS() \
  asm volatile ( \
    "pop r0 \n\t" \
    "pop r1 \n\t" \
    "pop r2 \n\t" \
    "pop r3 \n\t" \
    "pop r4 \n\t" \
    "pop r5 \n\t" \
    "pop r6 \n\t" \
    "pop r7 \n\t" \
    "pop r8 \n\t" \
    "pop r9 \n\t" \
    "pop r10 \n\t" \
    "pop r11 \n\t" \
    "pop r12 \n\t" \
    "pop r13 \n\t" \
    "pop r14 \n\t" \
    "pop r15 \n\t" \
    "pop r16 \n\t" \
    "pop r17 \n\t" \
    "pop r18 \n\t" \
    "pop r19 \n\t" \
    "pop r20 \n\t" \
    "pop r21 \n\t" \
    "pop r22 \n\t" \
    "pop r23 \n\t" \
    "pop r24 \n\t" \
    "pop r25 \n\t" \
    "pop r26 \n\t" \
    "pop r27 \n\t" \
    "pop r28 \n\t" \
    "pop r29 \n\t" \
    "pop r30 \n\t" \
    "pop r31 \n\t" \
    ::)


static volatile tid_t thrcnt = 1;
static volatile tid_t current = 0;
static volatile uint16_t counter = 0;

struct thr {
  memaddr_t sp;
  memaddr_t pc;
  uint16_t counter;
  uint16_t unused;
};

static inline struct thr* _thr_struct(tid_t tid)
{
  return (struct thr *)(THR_TBLEND - sizeof(struct thr) * (tid + 1));
}

static inline uint16_t* _thr_SPaddr(tid_t tid)
{
  struct thr *thr;
  thr = _thr_struct(tid);
  return (uint16_t *)(&thr->sp);
}

static inline uint16_t* _thr_PCaddr(tid_t tid)
{
  struct thr *thr;
  thr = _thr_struct(tid);
  return (uint16_t *)(&thr->pc);
}

static inline uint8_t* _thr_stackend(tid_t tid)
{
  return (uint8_t *)(THR_TBLEND - (sizeof(struct thr) * MAX_THRS) - (tid * STACK_SZ));
}

static inline uint16_t _thr_stacksz(tid_t tid)
{
  return ((uint16_t) _thr_stackend(tid)) - ((*_thr_SPaddr(tid)) + 1);
}

static void _thr_init_timers(void)
{
  /* Compare value */
  OCR1A = 0xF9;
  
  /* Set TIMER1 mode to CTC */
  TCCR1B |= (1 << WGM12);
  
  /* Enable COMPA ISR */
  TIMSK1 |= (1 << OCIE1A);
  
#if 1
  /* Set TIMER1 pre-scaler to 64 and start TIMER 1 */
  TCCR1B |= (1 << CS11) | (1 << CS10);
#else
  /* Set TIMER1 pre-scaler to 8 and start TIMER 1 */
  TCCR1B |= (1 << CS11)
#endif

  /* Enable interrupts */
  sei();
}

static void _thr_init_root(void)
{
  struct thr *thr;
  uint16_t stacklen;
  uint8_t *dstptr;
  uint8_t *srcptr;
  tid_t i;

  for (i = 0; i < MAX_THRS; i++) {
    thr = _thr_struct(i);
    thr->sp = ((uint16_t) _thr_stackend(0)) - 1;
    thr->pc = 0;
    thr->counter = 0;
  }

  stacklen = RAMEND - SP;
  srcptr = (uint8_t *)(SP + 1);
  dstptr = _thr_stackend(0) - stacklen;

  /* Save new stack pointer */
  *(_thr_SPaddr(0)) = ((uint16_t) dstptr) - 1;

  /* Copy existing stack to new location */
  while (stacklen-- > 0) {
    *(dstptr++) = *(srcptr++);
  }

  /* Update stack pointer to new location */
  SP = *(_thr_SPaddr(0));
}

static void _thr_copy_stack(tid_t dsttid, tid_t srctid)
{
  uint8_t *dstaddr;
  uint8_t *srcaddr;
  uint16_t stacksz;

  srcaddr = (uint8_t *)((*(_thr_SPaddr(srctid))) + 1);
  stacksz = _thr_stackend(srctid) - srcaddr;
  dstaddr = _thr_stackend(dsttid) - stacksz;
  *(_thr_SPaddr(dsttid)) = ((uint16_t) dstaddr) - 1;

  while (stacksz-- > 0) {
    *(dstaddr++) = *(srcaddr++);
  }
}

static void _thr_clone_impl(void)
{
  tid_t childtid;
  childtid = thrcnt++;
  _thr_copy_stack(childtid, current);
}

static void _thr_clone(void)
{
  /* Save registers to stack */
  _PUSH_REGISTERS();

  /* Save stack pointer */
  *(_thr_SPaddr(current)) = SP;

  _thr_clone_impl();

  /* Remove registers from stack */
  _POP_REGISTERS();
  //SP += 32;
}

static void _thr_schedule_impl(void)
{
  struct thr *thr;

  thr = _thr_struct(current);

  /* Save program counter */
  thr->pc = *((uint8_t *)(thr->sp + 36)) << 8 | *((uint8_t *)(thr->sp + 37));

  //*(_thr_PCaddr(current)) = *((uint8_t *)(sp + 36)) << 8 | *((uint8_t *)(sp + 37));
  current = (current + 1) % thrcnt;

  thr = _thr_struct(current);
  thr->counter = counter++;
}

static void _thr_schedule(void)
{
  /* Save registers to stack */
  _PUSH_REGISTERS();

  /* Save stack pointer */
  *(_thr_SPaddr(current)) = SP;

  /* Schedule the next thr */
  _thr_schedule_impl();

  /* Update stack pointer */
  SP = *(_thr_SPaddr(current));

  /* Pop registers from stack */
  _POP_REGISTERS();
}

/*
 * Handler for TIMER1 COMPA interrupt
 */
#ifdef  __cplusplus
extern "C" {
#endif

void __attribute__ ((signal, used, externally_visible, naked)) TIMER1_COMPA_vect(void)
//SIGNAL(TIMER1_COMPA_vect)
{
  asm volatile (
    "push r28 \n\t"
    "in r28, 0x3f \n\t"
    ::);

  _thr_schedule();

  asm volatile (
    "out 0x3f, r28 \n\t"
    "pop r28 \n\t"
    "reti \n\t"
    ::);
}

#ifdef  __cplusplus
};
#endif

/*
 * public functions
 */
void thr_init(void)
{
  _thr_init_root();
  _thr_init_timers();
}

tid_t thr_self(void)
{
  return current;
}

tid_t thr_count(void)
{
  return thrcnt;
}

void thr_clone(void)
{
  /* Disable interrupts */
  cli();

  _thr_clone();

  /* Enable interrupts */
  sei();
}

int thr_info(tid_t tid, struct thrinfo *info)
{
  struct thr *thr = _thr_struct(tid);

  info->sp = thr->sp;
  info->pc = thr->pc;
  info->stackend = _thr_stackend(tid);
  info->counter = thr->counter;
  return 0;
}
