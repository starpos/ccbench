#include <iostream>
#include <fstream>
#include <stdio.h>
#include <random>
#include "include/common.hpp"
#include "include/debug.hpp"
#include "include/procedure.hpp"

using namespace std;

void makeProcedure() {
	try {
		Pro = new Procedure*[PRO_NUM];	
		for (unsigned int i = 0; i < PRO_NUM; ++i) {
			if (posix_memalign((void**)&Pro[i], 64, MAX_OPE * sizeof(Procedure)) != 0) ERR;
			//Pro[i] = new Procedure[MAX_OPE];
		}
	} 
	catch (bad_alloc) {
		cout << "memory exhausted" << endl;
		exit(1);
	}

	random_device rnd;
	//uint64_t read(0), write(0);
	for (unsigned int i = 0; i < PRO_NUM; ++i) {
		for (unsigned int j = 0; j < MAX_OPE; ++j) {
			if ((rnd() % 100) < (READ_RATIO * 100)) {
				Pro[i][j].ope = Ope::READ;
				//read++;
			}
			else {
				Pro[i][j].ope = Ope::WRITE;
				//write++;
			}
			Pro[i][j].key = rnd() % TUPLE_NUM;
			Pro[i][j].val = rnd() % (TUPLE_NUM*10);
		}

		for (unsigned int j = 0; j < MAX_OPE; ++j) {
			if (Pro[i][j].ope == Ope::WRITE) {
				Pro[i][0].ronly = false;
				break;
			}
			if (j == (MAX_OPE - 1)) Pro[i][0].ronly = true;
		}
	}
	//cout << "read: " << read << endl;
	//cout << "write: " << write << endl;
}
