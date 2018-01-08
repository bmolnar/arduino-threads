#ifndef THREADS_H
#define THREADS_H

#include "Arduino.h"

#ifdef  __cplusplus
extern "C" {
#endif

typedef unsigned char tid_t;
typedef uint16_t memaddr_t;

struct thrinfo {
  memaddr_t sp;
  memaddr_t pc;
  memaddr_t stackend;
  uint16_t counter;
};

void thr_init(void);
tid_t thr_self(void);
void thr_clone();
tid_t thr_count(void);
int thr_info(tid_t tid, struct thrinfo *info);

  
#ifdef __cplusplus
};
#endif

#endif // THREADS_H
