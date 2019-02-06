#include <cctype>
#include <ctype.h>
#include <algorithm>
#include <string.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/time.h>

#define GLOBAL_VALUE_DEFINE
#include "include/atomic_tool.hpp"
#include "include/common.hpp"
#include "include/result.hpp"
#include "include/transaction.hpp"

#include "../../include/debug.hpp"
#include "../../include/random.hpp"
#include "../../include/tsc.hpp"
#include "../../include/zipf.hpp"

using namespace std;

extern bool chkClkSpan(uint64_t &start, uint64_t &stop, uint64_t threshold);
extern bool chkEpochLoaded();
extern void displayDB();
extern void displayPRO();
extern void makeDB();
extern void makeProcedure(Procedure *pro, Xoroshiro128Plus &rnd);
extern void makeProcedure(Procedure *pro, Xoroshiro128Plus &rnd, FastZipf &zipf);
extern void setThreadAffinity(int myid);
extern void waitForReadyOfAllThread();

void threadEndProcess(int *myid);

static bool
chkInt(char *arg)
{
    for (uint i=0; i<strlen(arg); ++i) {
        if (!isdigit(arg[i])) {
			cout << std::string(arg) << " is not a number." << endl;
			exit(0);
        }
    }
	return true;
}

static void 
chkArg(const int argc, char *argv[])
{
	if (argc != 10) {
    cout << "usage:./main TUPLE_NUM MAX_OPE THREAD_NUM RRATIO ZIPF_SKEW YCSB CLOCK_PER_US EPOCH_TIME EXTIME" << endl << endl;

    cout << "example:./main 1000000 10 24 50 0 ON 2400 40 3" << endl << endl;
    cout << "TUPLE_NUM(int): total numbers of sets of key-value (1, 100), (2, 100)" << endl;
    cout << "MAX_OPE(int):    total numbers of operations" << endl;
    cout << "THREAD_NUM(int): total numbers of thread." << endl;
    cout << "RRATIO : read ratio [%%]" << endl;
    cout << "ZIPF_SKEW : zipf skew. 0 ~ 0.999..." << endl;
    cout << "YCSB : ON or OFF. switch makeProcedure function." << endl;
    cout << "CLOCK_PER_US: CPU_MHZ" << endl;
    cout << "EPOCH_TIME(int)(ms): Ex. 40" << endl;
    cout << "EXTIME: execution time." << endl << endl;
	  cout << "Tuple " << sizeof(Tuple) << endl;
		cout << "uint64_t_64byte " << sizeof(uint64_t_64byte) << endl;
		exit(0);
	}
	chkInt(argv[1]);
	chkInt(argv[2]);
	chkInt(argv[3]);
	chkInt(argv[4]);
	chkInt(argv[7]);
	chkInt(argv[8]);
	chkInt(argv[9]);

	TUPLE_NUM = atoi(argv[1]);
	MAX_OPE = atoi(argv[2]);
	THREAD_NUM = atoi(argv[3]);
	RRATIO = atoi(argv[4]);
  ZIPF_SKEW = atof(argv[5]);
  string argycsb = argv[6];
	CLOCK_PER_US = atof(argv[7]);
	EPOCH_TIME = atoi(argv[8]);
	EXTIME = atoi(argv[9]);
	
	if (RRATIO > 100) {
		ERR;
	}

  if (ZIPF_SKEW >= 1) {
    cout << "ZIPF_SKEW must be 0 ~ 0.999..." << endl;
    ERR;
  }

  if (argycsb == "ON")
    YCSB = true;
  else if (argycsb == "OFF")
    YCSB = false;
  else
    ERR;

	if (THREAD_NUM < 2) {
		printf("One thread is epoch thread, and others are worker threads.\n\
So you have to set THREAD_NUM >= 2.\n\n");
	}

	try {
		if (posix_memalign((void**)&ThLocalEpoch, 64, THREAD_NUM * sizeof(uint64_t_64byte)) != 0) ERR;	//[0]は使わない
		if (posix_memalign((void**)&CTIDW, 64, THREAD_NUM * sizeof(uint64_t_64byte)) != 0) ERR;	//[0]は使わない
	} catch (bad_alloc) {
		ERR;
	}
	//init
	for (unsigned int i = 0; i < THREAD_NUM; ++i) {
		ThLocalEpoch[i].obj = 0;
		CTIDW[i].obj = 0;
	}
}

static void *
epoch_worker(void *arg)
{
//1. 40msごとに global epoch を必要に応じてインクリメントする
//2. 十分条件
//	全ての worker が最新の epoch を読み込んでいる。
//
	const int *myid = (int *)arg;
	uint64_t EpochTimerStart, EpochTimerStop;
  Result rsobject;

  setThreadAffinity(*myid);
  waitForReadyOfAllThread();

	//----------
	rsobject.Bgn = rdtsc();
	EpochTimerStart = rdtsc();
	for (;;) {
		usleep(1);
		rsobject.End = rdtsc();
		if (chkClkSpan(rsobject.Bgn, rsobject.End, EXTIME * 1000 * 1000 * CLOCK_PER_US)) {
			rsobject.Finish.store(true, std::memory_order_release);
			return nullptr;
		}

		EpochTimerStop = rdtsc();
		//chkEpochLoaded は最新のグローバルエポックを
		//全てのワーカースレッドが読み込んだか確認する．
		if (chkClkSpan(EpochTimerStart, EpochTimerStop, EPOCH_TIME * CLOCK_PER_US * 1000) && chkEpochLoaded()) {
			atomicAddGE();
			EpochTimerStart = EpochTimerStop;
		}
	}
	//----------

	return nullptr;
}

static void *
worker(void *arg)
{
	const int *myid = (int *)arg;
	Xoroshiro128Plus rnd;
	rnd.init();
	Procedure pro[MAX_OPE];
	TxnExecutor trans(*myid);
  Result rsobject;
  FastZipf zipf(&rnd, ZIPF_SKEW, TUPLE_NUM);
  //File logfile;

  setThreadAffinity(*myid);
	//printf("Thread #%d: on CPU %d\n", *myid, sched_getcpu());
	//printf("sysconf(_SC_NPROCESSORS_CONF) %d\n", sysconf(_SC_NPROCESSORS_CONF));
  waitForReadyOfAllThread();
	
	try {
		//start work(transaction)
		for (;;) {
      if (YCSB)
  			makeProcedure(pro, rnd, zipf);
      else
        makeProcedure(pro, rnd);

      asm volatile ("" ::: "memory");
RETRY:
			trans.tbegin();
			if (rsobject.Finish.load(memory_order_acquire)) {
        rsobject.sumUpCommitCounts();
        rsobject.sumUpAbortCounts();
				return nullptr;
			}

			//Read phase
			for (unsigned int i = 0; i < MAX_OPE; ++i) {
				switch(pro[i].ope) {
					case(Ope::READ):
						trans.tread(pro[i].key);
						break;
					case(Ope::WRITE):
						trans.twrite(pro[i].key, pro[i].val);
						break;
					default:
						ERR;
				}
			}
			
			//Validation phase
			if (trans.validationPhase()) {
        ++rsobject.localCommitCounts;
				trans.writePhase();
			} else {
        ++rsobject.localAbortCounts;
				trans.abort();
				goto RETRY;
			}

		}
	} catch (bad_alloc) {
		ERR;
	}

	return NULL;
}

pthread_t
threadCreate(int id)
{
	pthread_t t;
	int *myid;

	try {
		myid = new int;
	} catch (bad_alloc) {
		ERR;
	}
	*myid = id;

	if (*myid == 0) {
		if (pthread_create(&t, NULL, epoch_worker, (void *)myid)) ERR;
	} else {
		if (pthread_create(&t, NULL, worker, (void *)myid)) ERR;
	}

	return t;
}

int 
main(int argc, char *argv[]) 
{
  Result rsobject;
	chkArg(argc, argv);
	makeDB();
	
	//displayDB();
	//displayPRO();

	pthread_t thread[THREAD_NUM];

	for (unsigned int i = 0; i < THREAD_NUM; ++i) {
		thread[i] = threadCreate(i);
	}

	for (unsigned int i = 0; i < THREAD_NUM; ++i) {
		pthread_join(thread[i], NULL);
	}

	//displayDB();

  rsobject.displayTPS();
	//rsobject.displayAbortRate();

	return 0;
}