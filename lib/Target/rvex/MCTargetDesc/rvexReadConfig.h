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

struct Stage_desc
{
    int delay;
    int FU;
    int resources;
};

using namespace std;

extern vector<Stage_desc> Stages;
extern vector<DFAState> Itin;

int read_config (string ConfigFile);

#endif /* defined(__hello__hallo__) */
