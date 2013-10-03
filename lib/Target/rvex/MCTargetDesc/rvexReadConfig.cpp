// example: class constructor
#include <iostream>
#include <fstream>
#include <vector>

#include "rvexReadConfig.h"

using namespace std;

vector<DFAState> Stages;
vector<int> Itin;

void read_config (string ConfigFile)
{
    string str, str2;
    int getal, getal2;
    
    
    
    bool run = true;
    
    
    //Opens for reading the file
    ifstream b_file ( ConfigFile.c_str() );
    //Reads one string from the file
    b_file>> str;
    //Should output 'this'
    if (str == "stages")
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
                
                DFAState state;
                state.num1 = getal;
                state.num2 = getal2;
                
                Stages.push_back (state);
            }
            
        }
    }
    
    //Reads one string from the file
    b_file>> str;
    //Should output 'this'
    if (str == "InstrItinerary")
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
                
                Itin.push_back (getal);
            }
            
        }
    }
    

    
  
}