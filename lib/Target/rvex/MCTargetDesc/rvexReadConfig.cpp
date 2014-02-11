// example: class constructor
#include <iostream>
#include <fstream>
#include <vector>
#include <stdlib.h>     /* atoi */

#include "rvexReadConfig.h"

using namespace std;

vector<Stage_desc> Stages;
vector<DFAState> Itin;
int is_generic;
int is_width;

int read_config (string ConfigFile)
{
    string str, str2, str3;
    int getal, getal2, getal3;
    
    
    
    bool run = true;
    
    
    //Opens for reading the file
    ifstream b_file ( ConfigFile.c_str() );
    //Reads one string from the file
    b_file>> str;
    //Should output 'this'

    if (str == "Generic")
    {
        b_file>> str;
        if (str == "=")
        {
            run = true;
            while(run)
            {
                b_file>> str;

                
                if (str.at( str.length() -1) == ';')
                    run = false;
                
                str.erase(str.length()-1,1);

                
                
                getal = atoi(str.c_str());

                
                is_generic = getal;
            }
            
        }
    } 
    b_file>> str;
    if (str == "Width")
    {
        b_file>> str;
        if (str == "=")
        {
            run = true;
            while(run)
            {
                b_file>> str;

                
                if (str.at( str.length() -1) == ';')
                    run = false;
                
                str.erase(str.length()-1,1);

                
                
                getal = atoi(str.c_str());

                
                is_width = getal;
            }
            
        }
    } 
    b_file>> str;
    if (str == "Stages")
    {
        b_file>> str;
        if (str == "=")
        {
            run = true;
            while(run)
            {
                b_file>> str;
                b_file>> str2;
                b_file>> str3;
                
                if (str3.at( str3.length() -1) == ';')
                    run = false;
                
                str.erase(0,1);
                str.erase(str.length()-1,1);
                str2.erase(str2.length()-1,1);
                str3.erase(str3.length()-2,2);
                
                
                getal = atoi(str.c_str());
                getal2 = atoi(str2.c_str());
                getal3 = atoi(str3.c_str());
                
                Stage_desc state;
                state.delay = getal;
                state.FU = getal2;
                state.resources = getal3;
                
                Stages.push_back (state);
            }
            
        }
    }
    
    //Reads one string from the file
    b_file>> str;
    if (str == "InstrItinerary")
    {
        b_file>> str;
        if (str == "=")
        {
            run = true;
            while(run)
            {
                b_file>> str;
                b_file>> str2;
                
                if (str2.at( str2.length() -1) == ';')
                    run = false;
                
                str.erase(0,1);
                str.erase(str.length()-1,1);
                str2.erase(str2.length()-2,2);
                
                
                getal = atoi(str.c_str());
                getal2 = atoi(str2.c_str());
                
                DFAState Itin2;
                Itin2.num1 = getal;
                Itin2.num2 = getal2;
                
                Itin.push_back (Itin2);
            }
            
        }

        return 0;
    }
    
    Stage_desc state;
    state.delay = 1;
    state.FU = 3;
    state.resources = 1;
    
    Stages.push_back (state);
    Stages.push_back (state);
    
    return 1;
}