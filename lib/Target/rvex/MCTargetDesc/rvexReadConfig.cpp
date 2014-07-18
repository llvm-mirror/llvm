// example: class constructor
#include <iostream>
#include <fstream>
#include <vector>
#include <stdlib.h>     /* atoi */

#include "rvexReadConfig.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"

vector<Stage_desc> Stages;
vector<DFAState> Itin;
int is_generic;
int is_width;

int read_config (std::string ConfigFile)
{
    //Opens for reading the file
    ifstream b_file ( ConfigFile.c_str() );

    llvm::Regex EqualRegex(llvm::StringRef("([[:alpha:]]+)[[:space:]]*=[[:space:]]*([^;]+);"));

    bool EqualMatch;

    do {
        std::string Line;
        // Read one line from the file
        getline(b_file, Line);

        llvm::SmallVector<llvm::StringRef,3> matches;
        EqualMatch = EqualRegex.match(Line, &matches);

        if (EqualMatch) {
            if (matches[1].str() == "Generic") {
                is_generic = atoi(matches[2].str().c_str());
            } else if (matches[1].str() == "Width") {
                is_width = atoi(matches[2].str().c_str());
            } else if (matches[1].str() == "Stages") {
                llvm::Regex tuple_regex(llvm::StringRef("\\{ *([[:digit:]]+) *, *([[:digit:]]+) *, *([[:digit:]]+) *\\} *,?(.*)"));
                llvm::SmallVector<llvm::StringRef,4> tuple_matches;

                llvm::StringRef to_parse = matches[2];
                do {
                    bool tuple_match = tuple_regex.match(to_parse, &tuple_matches);

                    if (tuple_match) {
                        Stage_desc state;
                        state.delay = atoi(tuple_matches[1].str().c_str());
                        state.FU = atoi(tuple_matches[2].str().c_str());
                        state.resources = atoi(tuple_matches[3].str().c_str());

                        Stages.push_back (state);

                        to_parse = tuple_matches[4];
                    } else {
                        std::cerr << "Incorrect formatting for stage description in file \""
                            << ConfigFile
                            << "\". Could not parse \""
                            << matches[0].str()
                            << "\"."
                            << std::endl;
                        return 0;
                    }
                } while (to_parse.size() > 0);
            } else if (matches[1].str() == "InstrItinerary") {
                llvm::Regex tuple_regex(llvm::StringRef("\\{ *([[:digit:]]+) *, *([[:digit:]]+) *\\} *,?(.*)"));
                llvm::SmallVector<llvm::StringRef,3> tuple_matches;

                llvm::StringRef to_parse = matches[2];
                do {
                    bool tuple_match = tuple_regex.match(to_parse, &tuple_matches);

                    if (tuple_match) {
                        DFAState Itin2;
                        Itin2.num1 = atoi(tuple_matches[1].str().c_str());
                        Itin2.num2 = atoi(tuple_matches[2].str().c_str());

                        Itin.push_back (Itin2);

                        to_parse = tuple_matches[3];
                    } else {
                        std::cerr << "Incorrect formatting for itinerary description in file \""
                            << ConfigFile
                            << "\". Could not parse \""
                            << matches[0].str()
                            << "\"."
                            << std::endl;
                        return 0;
                    }
                } while (to_parse.size() > 0);
            } else {
                std::cerr << "Unknown option \""
                    << matches[1].str()
                    << "\" in options file \""
                    << ConfigFile
                    << "\"."
                    << std::endl;
                return 0;
            }
        }
    } while (EqualMatch);

    Stage_desc state;
    state.delay = 1;
    state.FU = 3;
    state.resources = 1;
    
    Stages.push_back (state);
    Stages.push_back (state);
    
    return 1;
}
