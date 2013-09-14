// example: class constructor
#include <iostream>
#include <fstream>
#include <vector>

#include "rvexReadConfig.h"

using namespace std;

vector<int> Stages;
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
                
                if (str.at( str.length() -1) == ';')
                    run = false;
                
                str.erase(str.length()-1,1);
                
                getal = atoi(str.c_str());
                
                Stages.push_back (getal);
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