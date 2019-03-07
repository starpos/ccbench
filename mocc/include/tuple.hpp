#pragma once

#include <string.h> // memcpy
#include <atomic>
#include <cstdint>

#include "lock.hpp"

#define TEMP_THRESHOLD 5
#define TEMP_MAX 20
#define TEMP_RESET_US 100

struct Tidword {
  union {
    uint64_t obj;
    struct {
      bool lock:1;
      uint64_t tid:31;
      uint64_t epoch:32;
    };
  };

  Tidword() {
    obj = 0;
  }

  bool operator==(const Tidword& right) const {
    return obj == right.obj;
  }

  bool operator!=(const Tidword& right) const {
    return !operator==(right);
  }

  bool operator<(const Tidword& right) const {
    return this->obj < right.obj;
  }


  void upLockBits() {
    Tidword expected, desired;
    expected = *this;
    desired = expected;
    desired.lock = 1;
    __atomic_store_n(&(this->obj), desired.obj, __ATOMIC_RELEASE);
  }

  void downLockBits() {
    Tidword expected, desired;
    expected = *this;
    desired = expected;
    desired.lock = 0;
    __atomic_store_n(&(this->obj), desired.obj, __ATOMIC_RELEASE);
  }
};

// 32bit temprature, 32bit epoch
struct Epotemp {
  union {
    uint64_t obj;
    struct {
      uint64_t temp:32;
      uint64_t epoch:32;
    };
  };

  Epotemp() {
    obj = 0;
  }

  bool operator==(const Epotemp& right) const {
    return obj == right.obj;
  }

  bool operator!=(const Epotemp& right) const {
    return !operator==(right);
  }

  bool eqEpoch(uint64_t epo) {
    if (epoch == epo) return true;
    else return false;
  }
};

class Tuple {
public:
  Tidword tidword;
  Epotemp epotemp;  //  temprature, min 0, max 20
#ifdef RWLOCK
  RWLock rwlock;  // 4byte
  // size to here is 20 bytes
#endif
#ifdef MQLOCK
  MQLock mqlock;
#endif

  char keypad[KEY_SIZE];
  char val[VAL_SIZE];

#ifdef RWLOCK
  int8_t pad[CACHE_LINE_SIZE - ((20 + KEY_SIZE + VAL_SIZE) % CACHE_LINE_SIZE)];
#endif // RWLOCK
};

// use for read-write set
class ReadElement {
public:
  Tidword tidword;
  unsigned int key;
  char val[VAL_SIZE];
  bool failed_verification;

  ReadElement (Tidword tidword, unsigned int key_, char *newVal) : key(key_) {
    this->tidword = tidword;
    memcpy(val, newVal, VAL_SIZE);
    this->failed_verification = false;
  }

  bool operator<(const ReadElement& right) const {
    return this->key < right.key;
  }
};
 
/*
class WriteElement {
public:
  unsigned int key;
  //char val[VAL_SIZE];

  //WriteElement(unsigned int key, char *newVal) {
  WriteElement(unsigned int key) {
    this->key = key;
    //memcpy(val, newVal, VAL_SIZE);
  }

  bool operator<(const WriteElement& right) const {
    return this->key < right.key;
  }
};
*/
