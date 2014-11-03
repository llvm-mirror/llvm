//
//  hallo.h
//  hello
//
//  Created by Maurice Daverveldt on 8/25/13.
//  Copyright (c) 2013 Maurice Daverveldt. All rights reserved.
//

#ifndef __hello__BUILDDFA__
#define __hello__BUILDDFA__

#include <vector>
#include "rvexReadConfig.h"

// Tuple of StateInputTable and StateEntryTable
typedef std::tuple<int (*)[2], unsigned int *> DFAStateTables;

DFAStateTables rvexGetDFAStateTables();

int rvexBuildDFA (const std::vector<Stage_desc>& isnStages);

#endif /* defined(__hello__BUILDDFA__) */
