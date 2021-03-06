/* =============================================================================
 *
 * tm.h
 *
 * Utility defines for transactional memory for sequential
 *
 * =============================================================================
 *
 * Copyright (C) Stanford University, 2006.  All Rights Reserved.
 * Authors: Chi Cao Minh and Martin Trautmann
 *
 * =============================================================================
 *
 * For the license of bayes/sort.h and bayes/sort.c, please see the header
 * of the files.
 * 
 * ------------------------------------------------------------------------
 * 
 * For the license of kmeans, please see kmeans/LICENSE.kmeans
 * 
 * ------------------------------------------------------------------------
 * 
 * For the license of ssca2, please see ssca2/COPYRIGHT
 * 
 * ------------------------------------------------------------------------
 * 
 * For the license of lib/mt19937ar.c and lib/mt19937ar.h, please see the
 * header of the files.
 * 
 * ------------------------------------------------------------------------
 * 
 * For the license of lib/rbtree.h and lib/rbtree.c, please see
 * lib/LEGALNOTICE.rbtree and lib/LICENSE.rbtree
 * 
 * ------------------------------------------------------------------------
 * 
 * Unless otherwise noted, the following license applies to STAMP files:
 * 
 * Copyright (c) 2007, Stanford University
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 * 
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 * 
 *     * Neither the name of Stanford University nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY STANFORD UNIVERSITY ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL STANFORD UNIVERSITY BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * =============================================================================
 */


/* =============================================================================
 * Transactional Memory System Interface
 *
 * TM_ARG
 * TM_ARG_ALONE
 * TM_ARGDECL
 * TM_ARGDECL_ALONE
 *     Used to pass TM thread meta data to functions (see Examples below)
 *
 * TM_STARTUP(numThread)
 *     Startup the TM system (call before any other MYTM calls)
 *
 * TM_SHUTDOWN()
 *     Shutdown the TM system
 *
 * TM_THREAD_ENTER()
 *     Call when thread first enters parallel region
 *
 * TM_THREAD_EXIT()
 *     Call when thread exits last parallel region
 *
 * P_MALLOC(size)
 *     Allocate memory inside parallel region
 *
 * P_FREE(ptr)
 *     Deallocate memory inside parallel region
 *
 * TM_MALLOC(size)
 *     Allocate memory inside atomic block / transaction
 *
 * TM_FREE(ptr)
 *     Deallocate memory inside atomic block / transaction
 *
 * TM_BEGIN()
 *     Begin atomic block / transaction
 *
 * TM_BEGIN_RO()
 *     Begin atomic block / transaction that only reads shared data
 *
 * TM_END()
 *     End atomic block / transaction
 *
 * TM_RESTART()
 *     Restart atomic block / transaction
 *
 * TM_EARLY_RELEASE()
 *     Remove speculatively read line from the read set
 *
 * =============================================================================
 *
 * Example Usage:
 *
 *     MAIN(argc,argv)
 *     {
 *         TM_STARTUP(8);
 *         // create 8 threads and go parallel
 *         TM_SHUTDOWN();
 *     }
 *
 *     void parallel_region ()
 *     {
 *         TM_THREAD_ENTER();
 *         subfunction1(TM_ARG_ALONE);
 *         subfunction2(TM_ARG  1, 2, 3);
 *         TM_THREAD_EXIT();
 *     }
 *
 *     void subfunction1 (TM_ARGDECL_ALONE)
 *     {
 *         TM_BEGIN_RO()
 *         // ... do work that only reads shared data ...
 *         TM_END()
 *
 *         long* array = (long*)P_MALLOC(10 * sizeof(long));
 *         // ... do work ...
 *         P_FREE(array);
 *     }
 *
 *     void subfunction2 (TM_ARGDECL  long a, long b, long c)
 *     {
 *         TM_BEGIN();
 *         long* array = (long*)TM_MALLOC(a * b * c * sizeof(long));
 *         // ... do work that may read or write shared data ...
 *         TM_FREE(array);
 *         TM_END();
 *     }
 *
 * =============================================================================
 */


/* =============================================================================
 * HTM - Hardware Transactional Memory
 * =============================================================================
 */

#ifndef TM_H
#define TM_H 1

#include "tm_common.h"
#include "transaction.hpp"
#include "testlaunch.hpp"
// #include "tm_def.h"
#include "ringstm.hpp"
#include "qstm-old.hpp"
#include "qstm.hpp"
/* =============================================================================
 * STM - Software Transactional Memory
 * =============================================================================
 */
#if defined(RINGSTM)
#define MYTM                          RingSTM
#elif defined(OLDQUEUESTM)
#define MYTM                          OLDQSTM
#elif defined(QUEUESTM)
#define MYTM                          QSTM
#endif
#define OUT                           stdout
#define TM_ARG                        
#define TM_ARG_ALONE                  
#define TM_ARGDECL                    
#define TM_ARGDECL_ALONE              
#define TM_CALLABLE                   
extern __thread MYTM* txn;
#define TM_STARTUP(numThread)                         \
        auto* stm_factory = tc->getTMFactory();

#define TM_SHUTDOWN()             /* nothing */

#define TM_THREAD_ENTER()                             \
        int tid = (int)thread_getId();                \
        txn = tc->getTMFactory()->build(tc, tid); \
        setAffinity(tc, tid);                         \
        barrier();
        
#define TM_THREAD_EXIT()          delete(txn)

#define P_MALLOC(size)            pstm_pmalloc(size)
#define P_FREE(ptr)               pstm_pfree(ptr)
#define TM_MALLOC(size)           txn->tm_pmalloc(size)
#define TM_FREE(ptr)              txn->tm_pfree((void**)ptr)

#define TM_BEGIN()                                    \
        if(txn->nesting == 0) setjmp(txn->env);       \
        txn->tm_begin();
#define TM_BEGIN_RO()                                 \
        if(txn->nesting == 0) setjmp(txn->env);       \
        txn->tm_begin();
#define TM_END()                                      \
        txn->tm_end();
#define TM_RESTART()                  assert(0)

#  define TM_EARLY_RELEASE(var)         /* nothing */



/* =============================================================================
 * Transactional Memory System interface for shared memory accesses
 *
 * There are 3 flavors of each function:
 *
 * 1) no suffix: for accessing variables of size "long"
 * 2) _P suffix: for accessing variables of type "pointer"
 * 3) _F suffix: for accessing variables of type "float"
 * =============================================================================
 */
#define TM_SHARED_READ(var)           (long)txn->tm_read((void**)&var)
#define TM_SHARED_READ_P(var)         txn->tm_read((void**)&var)
#define TM_SHARED_READ_F(var)         (float)txn->tm_read((void**)&var)

#define TM_SHARED_WRITE(var, val)     ({txn->tm_write((void**)&var, (void*)val);})
#define TM_SHARED_WRITE_P(var, val)   ({txn->tm_write((void**)&var, (void*)val);})
#define TM_SHARED_WRITE_F(var, val)   ({txn->tm_write((void**)&var, (void*)val);})

#define TM_LOCAL_WRITE(var, val)      ({var = val; var;})
#define TM_LOCAL_WRITE_P(var, val)    ({var = val; var;})
#define TM_LOCAL_WRITE_F(var, val)    ({var = val; var;})

#endif
/* =============================================================================
 *
 * End of tm.h
 *
 * =============================================================================
 */
