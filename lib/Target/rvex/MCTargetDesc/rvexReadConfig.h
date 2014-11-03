//
//  hallo.h
//  hello
//
//  Created by Maurice Daverveldt on 8/25/13.
//  Copyright (c) 2013 Maurice Daverveldt. All rights reserved.
//

#ifndef rvexREADCONFIG_H
#define rvexREADCONFIG_H

#include <string>
#include <tuple>
#include <vector>

struct DFAState
{
    unsigned int num1;
    unsigned int num2;
};

struct Stage_desc
{
    unsigned int delay;
    unsigned int FU;
    int resources;
};

typedef std::tuple<std::vector<Stage_desc> const *, std::vector<DFAState> const *, bool, int> RVexConfig;

RVexConfig read_config (std::string ConfigFile);

#endif
