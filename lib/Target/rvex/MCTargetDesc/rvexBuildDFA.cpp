//===- DFAPacketizerEmitter.cpp - Packetization DFA for a VLIW machine-----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class parses the Schedule.td file and produces an API that can be used
// to reason about whether an instruction can be added to a packet on a VLIW
// architecture. The class internally generates a deterministic finite
// automaton (DFA) that models all possible mappings of machine instructions
// to functional units as instructions are added to a packet.
//
//===----------------------------------------------------------------------===//

//#include "CodeGenTarget.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <list>
#include <map>
#include <string>
#include <vector>

#include <iostream>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"

#include "rvexBuildDFA.h"



using namespace llvm;

namespace llvm {
    int rvexDFAStateInputTable[100][2];
    unsigned int rvexDFAStateEntryTable[100];
}

//
//
// State represents the usage of machine resources if the packet contains
// a set of instruction classes.
//
// Specifically, currentState is a set of bit-masks.
// The nth bit in a bit-mask indicates whether the nth resource is being used
// by this state. The set of bit-masks in a state represent the different
// possible outcomes of transitioning to this state.
// For example: consider a two resource architecture: resource L and resource M
// with three instruction classes: L, M, and L_or_M.
// From the initial state (currentState = 0x00), if we add instruction class
// L_or_M we will transition to a state with currentState = [0x01, 0x10]. This
// represents the possible resource states that can result from adding a L_or_M
// instruction
//
// Another way of thinking about this transition is we are mapping a NDFA with
// two states [0x01] and [0x10] into a DFA with a single state [0x01, 0x10].
//
// A State instance also contains a collection of transitions from that state:
// a map from inputs to new states.
//
namespace {
    class State {
    public:
        static int currentStateNum;
        int stateNum;
        bool isInitial;
        std::set<unsigned> stateInfo;
        typedef std::map<unsigned, State *> TransitionMap;
        TransitionMap Transitions;
        
        State();
        State(const State &S);
        
        bool operator<(const State &s) const {
            return stateNum < s.stateNum;
        }
        
        //
        // canAddInsnClass - Returns true if an instruction of type InsnClass is a
        // valid transition from this state, i.e., can an instruction of type InsnClass
        // be added to the packet represented by this state.
        //
        // PossibleStates is the set of valid resource states that ensue from valid
        // transitions.
        //
        bool canAddInsnClass(unsigned InsnClass) const;
        //
        // AddInsnClass - Return all combinations of resource reservation
        // which are possible from this state (PossibleStates).
        //
        void AddInsnClass(unsigned InsnClass, std::set<unsigned> &PossibleStates);
        //
        // addTransition - Add a transition from this state given the input InsnClass
        //
        void addTransition(unsigned InsnClass, State *To);
        //
        // hasTransition - Returns true if there is a transition from this state
        // given the input InsnClass
        //
        bool hasTransition(unsigned InsnClass);
    };
} // End anonymous namespace.

//
// class DFA: deterministic finite automaton for processor resource tracking.
//
namespace {
    class DFA {
    public:
        DFA();
        ~DFA();
        
        // Set of states. Need to keep this sorted to emit the transition table.
        typedef std::set<State *, less_ptr<State> > StateSet;
        StateSet states;
        
        State *currentState;
        
        //
        // Modify the DFA.
        //
        void initialize();
        void addState(State *);
        
        //
        // writeTable: Print out a table representing the DFA.
        //
        void writeTableAndAPI(const std::string &ClassName);
    };
} // End anonymous namespace.


//
// Constructors and destructors for State and DFA
//
State::State() :
stateNum(currentStateNum++), isInitial(false) {}


State::State(const State &S) :
stateNum(currentStateNum++), isInitial(S.isInitial),
stateInfo(S.stateInfo) {}

DFA::DFA(): currentState(NULL) {}

DFA::~DFA() {
    DeleteContainerPointers(states);
}

//
// addTransition - Add a transition from this state given the input InsnClass
//
void State::addTransition(unsigned InsnClass, State *To) {
    assert(!Transitions.count(InsnClass) &&
           "Cannot have multiple transitions for the same input");
    Transitions[InsnClass] = To;
}

//
// hasTransition - Returns true if there is a transition from this state
// given the input InsnClass
//
bool State::hasTransition(unsigned InsnClass) {
    return Transitions.count(InsnClass) > 0;
}

#define WIDTH 3
#define WIDTH2 2

unsigned stages_width, stages_num;

int NumberOfSetBits(int i)
{
    i = i - ((i >> 1) & 0x55555555);
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
    return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

bool hasResources(unsigned consume, unsigned resources)
{
    int consume_bits = NumberOfSetBits(consume);
    int resource_bits = NumberOfSetBits(resources);

    
    if ((consume_bits + resource_bits) <= stages_num)
        if ((consume & resources) == 0)
            return true;
    return false;
}

unsigned setResources(unsigned consume, unsigned resources)
{
    unsigned i, count = 0;
    count = resources;
    for (i = 0; i < 32; i ++)
    {

        if ((resources&0x01) == 0)
        {
            count |= 1<<i;
            if (!--consume) break;
        }
        resources <<=1;
    }
    
    return count;
}

unsigned consumption[] = {   0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0 };

//
// AddInsnClass - Return all combinations of resource reservation
// which are possible from this state (PossibleStates).
//
void State::AddInsnClass(unsigned InsnClass,
                         std::set<unsigned> &PossibleStates) {
    //
    // Iterate over all resource states in currentState.
    //
    
    for (std::set<unsigned>::iterator SI = stateInfo.begin();
         SI != stateInfo.end(); ++SI) {
        unsigned thisState = *SI;
        
        //
        // Iterate over all possible resources used in InsnClass.
        // For ex: for InsnClass = 0x11, all resources = {0x01, 0x10}.
        //
        DenseSet<unsigned> VisitedResourceStates;
        for (unsigned int j = 0; j <= stages_num; ++j) {
            if (consumption[InsnClass] << j <= stages_width) {
                //
                // For each possible resource used in InsnClass, generate the
                // resource state if that resource was used.
                //
                unsigned ResultingResourceState;

                
                if(hasResources((consumption[InsnClass] << j), thisState))
                    ResultingResourceState = thisState | (consumption[InsnClass] << j);
                else
                    ResultingResourceState = thisState;
                
                

                
                //
                // Check if the resulting resource state can be accommodated in this
                // packet.
                // We compute ResultingResourceState OR thisState.
                // If the result of the OR is different than thisState, it implies
                // that there is at least one resource that can be used to schedule
                // InsnClass in the current packet.
                // Insert ResultingResourceState into PossibleStates only if we haven't
                // processed ResultingResourceState before.
                //
                if ((ResultingResourceState != thisState) &&
                    (VisitedResourceStates.count(ResultingResourceState) == 0)) {
                    
                    //ResultingResourceState = setResources(consumption[InsnClass], thisState);

//                    if (InsnClass == 1)
//                    {
                        PossibleStates.insert(ResultingResourceState);
                        VisitedResourceStates.insert(ResultingResourceState);
//                    }
//                    else
//                    {
//                        VisitedResourceStates.insert(ResultingResourceState);
//                        PossibleStates.insert(ResultingResourceState);
//                    }
                }
            }
        }
    }
    
}


//
// canAddInsnClass - Quickly verifies if an instruction of type InsnClass is a
// valid transition from this state i.e., can an instruction of type InsnClass
// be added to the packet represented by this state.
//
bool State::canAddInsnClass(unsigned InsnClass) const {
    unsigned temp;
    for (std::set<unsigned>::const_iterator SI = stateInfo.begin();
         SI != stateInfo.end(); ++SI) {
        temp = *SI;
        if (hasResources(consumption[InsnClass], temp))
                return true;
    }
    return false;
}


void DFA::initialize() {
    assert(currentState && "Missing current state");
    currentState->isInitial = true;
}


void DFA::addState(State *S) {
    assert(!states.count(S) && "State already exists");
    states.insert(S);
}


int State::currentStateNum = 0;



//
// writeTableAndAPI - Print out a table representing the DFA and the
// associated API to create a DFA packetizer.
//
// Format:
// DFAStateInputTable[][2] = pairs of <Input, Transition> for all valid
//                           transitions.
// DFAStateEntryTable[i] = Index of the first entry in DFAStateInputTable for
//                         the ith state.
//
//
void DFA::writeTableAndAPI(const std::string &TargetName) {
    static const std::string SentinelEntry = "{-1, -1}";
    DFA::StateSet::iterator SI = states.begin();
    // This table provides a map to the beginning of the transitions for State s
    // in DFAStateInputTable.
    std::vector<int> StateEntry(states.size());
    
    // Tracks the total valid transitions encountered so far. It is used
    // to construct the StateEntry table.
    int ValidTransitions = 0;
    int counter = 0;
    for (unsigned i = 0; i < states.size(); ++i, ++SI) {
        assert (((*SI)->stateNum == (int) i) && "Mismatch in state numbers");
        StateEntry[i] = ValidTransitions;
        for (State::TransitionMap::iterator
             II = (*SI)->Transitions.begin(), IE = (*SI)->Transitions.end();
             II != IE; ++II) {
            
            std::cout << "{" << II->first << ", "
            << II->second->stateNum
            << "},    ";            rvexDFAStateInputTable[counter][0] = II->first;
            rvexDFAStateInputTable[counter][1] = II->second->stateNum;
            counter++;
        }
        ValidTransitions += (*SI)->Transitions.size();
        
        // If there are no valid transitions from this stage, we need a sentinel
        // transition.
        if (ValidTransitions == StateEntry[i]) {
            std::cout << SentinelEntry << ",";
            rvexDFAStateInputTable[counter][0] = -1;
            rvexDFAStateInputTable[counter][1] = -1;
            counter++;
            ++ValidTransitions;
        }
        
        std::cout << "\n";
        
    }
    
    // Print out a sentinel entry at the end of the StateInputTable. This is
    // needed to iterate over StateInputTable in DFAPacketizer::ReadTable()
    
    rvexDFAStateInputTable[counter][0] = -1;
    rvexDFAStateInputTable[counter][1] = -1;
    
    // Multiply i by 2 since each entry in DFAStateInputTable is a set of
    // two numbers.

    for (unsigned i = 0; i < states.size(); ++i)
    {
        std::cout << StateEntry[i] << ", ";
        rvexDFAStateEntryTable[i] = StateEntry[i];
    }
    
    // Print out the index to the sentinel entry in StateInputTable
    rvexDFAStateEntryTable[states.size()] = ValidTransitions;

}


//
// Run the worklist algorithm to generate the DFA.
//
int rvexBuildDFA (std::vector<Stage_desc>& isnStages) {
    //
    // Run a worklist algorithm to generate the DFA.
    //
    DFA D;
    State *Initial = new State;
    Initial->isInitial = true;
    Initial->stateInfo.insert(0x0);
    D.addState(Initial);
    SmallVector<State*, 32> WorkList;
    std::map<std::set<unsigned>, State*> Visited;
    
    WorkList.push_back(Initial);
    
    stages_width = isnStages[0].FU;
    
    
    for (stages_num = 0; stages_num < 16; stages_num++)
    {
        if (!(stages_width&0x01)) break;
        stages_width >>= 1;
    }
    
    stages_width = isnStages[0].FU;

   consumption[7] = isnStages[2].resources;
   consumption[3] = isnStages[1].resources;
   consumption[1] = isnStages[0].resources;

    // consumption[7] = 1;
    // consumption[3] = 7;
    // consumption[1] = 7;
    
    for (std::vector<Stage_desc>::iterator i = isnStages.begin(); i < isnStages.end(); i++)
        consumption[i->FU] = i->resources;

    //
    // Worklist algorithm to create a DFA for processor resource tracking.
    // C = {set of InsnClasses}
    // Begin with initial node in worklist. Initial node does not have
    // any consumed resources,
    //     ResourceState = 0x0
    // Visited = {}
    // While worklist != empty
    //    S = first element of worklist
    //    For every instruction class C
    //      if we can accommodate C in S:
    //          S' = state with resource states = {S Union C}
    //          Add a new transition: S x C -> S'
    //          If S' is not in Visited:
    //             Add S' to worklist
    //             Add S' to Visited
    //
    while (!WorkList.empty()) {
        State *current = WorkList.pop_back_val();

        for (std::vector<Stage_desc>::iterator i = isnStages.begin(); i < isnStages.end(); i++)
        {
            Stage_desc InsnClass = *i;

            std::set<unsigned> NewStateResources;

            if (!current->hasTransition(InsnClass.FU) &&
                current->canAddInsnClass(InsnClass.FU))
            {
                State *NewState = NULL;
                current->AddInsnClass(InsnClass.FU, NewStateResources);
                assert(NewStateResources.size() && "New states must be generated");

                std::map<std::set<unsigned>, State*>::iterator VI;
                if ((VI = Visited.find(NewStateResources)) != Visited.end())
                        NewState = VI->second;
                else
                {
                    NewState = new State;
                    NewState->stateInfo = NewStateResources;
                    D.addState(NewState);
                    Visited[NewStateResources] = NewState;
                    WorkList.push_back(NewState);
                }

                current->addTransition(InsnClass.FU, NewState);
            }


        }
    }
    
    // Print out the table.
    D.writeTableAndAPI("rvex");
    return 0;
}


