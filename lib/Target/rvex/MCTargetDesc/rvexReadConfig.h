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

extern vector<DFAState> Stages;
extern vector<DFAState> Itin;

void read_config (string ConfigFile);

#endif /* defined(__hello__hallo__) */
