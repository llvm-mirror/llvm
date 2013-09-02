//
//  hallo.h
//  hello
//
//  Created by Maurice Daverveldt on 8/25/13.
//  Copyright (c) 2013 Maurice Daverveldt. All rights reserved.
//

#ifndef __hello__hallo__
#define __hello__hallo__

#include <iostream>

struct DFAState
{
    int num1;
    int num2;
};

using namespace std;

extern vector<int> Stages;
extern vector<int> Itin;
extern vector<DFAState> DFAStateInputTable;
extern vector<int> DFAStateEntryTable;

void read_config (string ConfigFile);

#endif /* defined(__hello__hallo__) */
