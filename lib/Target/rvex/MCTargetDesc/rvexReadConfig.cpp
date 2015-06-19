#include "rvexReadConfig.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdlib.h>     /* atoi */

std::vector<Stage_desc> Stages;
std::vector<DFAState> Itin;
bool is_generic = false;
int width = 4;

RVexConfig read_config (std::string ConfigFile)
{
    //Opens for reading the file
    std::ifstream b_file ( ConfigFile );
    std::string file_content;

    if (!b_file) {
        std::cerr << "Could not open file '" << ConfigFile << "'!\n";
    }

    // Read file contents into string
    if (b_file) {
        std::ostringstream contents;
        contents << b_file.rdbuf();
        b_file.close();
        file_content = contents.str();
    }

    llvm::Regex StatementRegex(llvm::StringRef("([^;]*);[[:space:]]*(.*)"));
    llvm::Regex EqualRegex(llvm::StringRef("[[:space:]]*([[:alpha:]]+)[[:space:]]*=[[:space:]]*(.*)"));

    bool EqualMatch;

    while ( !file_content.empty() ) {
        std::string Statement;
        llvm::SmallVector<std::string, 3> matches;

        // Read one statement from the file
        llvm::SmallVector<llvm::StringRef,3> statement_matches;
        EqualMatch = StatementRegex.match(file_content, &statement_matches);

        if (!EqualMatch) {
            std::cout << "Trailing characters in configuration file '" << ConfigFile << "'.\n";
            break;
        }

        // Parse the statement
        llvm::SmallVector<llvm::StringRef,3> ref_matches;
        file_content = statement_matches[2].str();
        std::string stat = statement_matches[1].str();

        EqualMatch = EqualRegex.match(stat, &ref_matches);
        for (auto refm : ref_matches) {
            matches.push_back(refm.str());
        }

        if (EqualMatch) {
            if (matches[1] == "Generic") {
                is_generic = atoi(matches[2].c_str());
            } else if (matches[1] == "Width") {
                width = atoi(matches[2].c_str());
            } else if (matches[1] == "Stages") {
                llvm::Regex tuple_regex(llvm::StringRef("\\{[[:space:]]*([[:digit:]]+)[[:space:]]*,[[:space:]]*([[:digit:]]+)[[:space:]]*,[[:space:]]*([-[:digit:]]+)[[:space:]]*\\}[[:space:]]*,?(.*)"));
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
                            << matches[0]
                            << "\"."
                            << std::endl;
                        break;
                    }
                } while (to_parse.size() > 0);
            } else if (matches[1] == "InstrItinerary") {
                llvm::Regex tuple_regex(llvm::StringRef("\\{[[:space:]]*([[:digit:]]+)[[:space:]]*,[[:space:]]*([[:digit:]]+)[[:space:]]*\\}[[:space:]]*,?(.*)"));
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
                            << matches[0]
                            << "\"."
                            << std::endl;
                        break;
                    }
                } while (to_parse.size() > 0);
            } else {
                std::cerr << "Unknown option \""
                    << matches[1]
                    << "\" in options file \""
                    << ConfigFile
                    << "\", ignoring."
                    << std::endl;
            }
        }
    }

    return std::make_tuple(&Stages, &Itin, is_generic, width);
}
