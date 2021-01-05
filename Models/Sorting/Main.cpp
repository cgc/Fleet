
// we put this at the top so that grammar will be loaded with eigen 
#include "EigenLib.h"

#include <string>
using S = std::string; // just for convenience

const bool show_data = false; // if true, when we print a hypothesis, we show all the data

S alphabet = "0123456789"; // the alphabet we use (possibly specified on command line)

// the data, comma separated
// sorting:
thread_local S datastr  = "734:347,1987:1789,113322:112233,679:679,214:124,9142385670:0123456789"; 
// max:
//thread_local S datastr  = "734:7,1987:9,113322:3,679:9,214:4,9142385670:9"; 
// last:
//thread_local S datastr  = "734:4,1987:7,113322:2,679:9,214:4,9142385670:0"; 
// one more than the max
//thread_local S datastr  = "734:8,1287:9,113322:4,678:9,214:5,14235670:8"; 
// intersperse ones
//thread_local S datastr  = "734:713141,1287:11218171,113322:111131312121,678:617181,214:211141,14235670:1141213151617101"; 
// intersperse first -- this is hard to do with only one argument. 
//thread_local S datastr  = "734:773747,1287:11218171,113322:111131312121,678:667686,214:221242,4235670:44243454647404"; 
// odd or even number of ones
//thread_local S datastr  = "33321:1,332121:0,734:0,1287:1,113322:0,678:0,214:1,111:1,1411421:0"; 
// check sorted or not:

// repetitions:
//thread_local S datastr  = "1:1,2:22,3:333,4:4444"; 

// run length coding:
//thread_local S datastr  = "13:3,25:55,32:222,44:4444,52:22222"; 

// sum -- a little interesting since we have to build it out of increment
// but it seems to learn it by cleverly reusing repeat
//thread_local S datastr  = "13:4,25:7,32:5,44:8,52:7"; 

//thread_local S datastr  = "123:122333,5678:5667778888"; 

// try some copycat
//thread_local S datastr  = "123:124,555:556"; 
//thread_local S datastr  = "123:234,555:666"; 
//thread_local S datastr  = "123:124"; 

S testinput = "12345";

const float strgamma = 0.0001;
const int   MAX_LENGTH = 64; // longest strings cons will handle
size_t      MAX_GRAMMAR_DEPTH = 256;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
/// This requires a template to specify what types they are (and what order they are stored in)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
 
#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<S,S,     S,char,bool,int>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {

		add("tail(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : s.substr(1,S::npos)); });
		add("head(%s)",      +[](S s)      -> char       { return (s.empty() ? ' ' : s[0]); }, 5);
		add("pair(%s,%s)",   +[](S a, char b) -> S        { 
				if(a.length() + 1> MAX_LENGTH) 
					throw VMSRuntimeError();
				return a+b; 
		});
		
		add("\u00D8",        +[]()         -> S          { return S(""); });
		add("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; }, 1./2.);
		add("(%s==%s)",      +[](char x, char y) -> bool { return x==y; }, 1./2.);
		
		add("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }, 1./3.);
		add("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); }, 1./3.);
		add("not(%s)",       +[](bool a)         -> bool { return (not a); },  1./3.);
		
		add("(%s<%s)",     +[](S x, S y) -> bool { return x < y; }, 1./2);
		add("(%s<%s)",     +[](char x, char y) -> bool { return x < y; }, 1./2);

		add("incr(%s)",     +[](char x) -> char { return x+1;});
		add("decr(%s)",     +[](char x) -> char { return x-1;});
			
		// Pretty easy to learn with reverse
		add("reverse(%s)",     +[](S x) -> S { return reverse(x);	}, 1.);
		
		// repetitions 
		add("repeat(%s,%s)",     +[](S x, int n) -> S { 
				S w = "";
				
				if(x.size() * n > MAX_LENGTH or n < 0 or n > MAX_LENGTH) 
					throw VMSRuntimeError(); // need n > MAX_LENGTH in case n is huge but string is empty
					
				for(int i=0;i<n;i++) {
					w = w+x;
				}
				return w;
		}, 1.);
			
			
		add("int(%s)",     +[](char c) -> int {
			return int(c-'0'); // int here starting at '0'
		});
		
		add("x",             Builtins::X<MyGrammar>, 10);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>, 1./3);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,char>, 1./3);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,int>, 1./3);
		add("recurse(%s)",   Builtins::Recurse<MyGrammar>);
			
		// interestingly if we remove the alphabet, that's even better since we can't
		// just memorize the data. 
		for(const char c : alphabet) {
			add_terminal( Q(S(1,c)), c, 5.0/alphabet.length());
		}
	}
};
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

// Declare a hypothesis class
class MyHypothesis : public LOTHypothesis<MyHypothesis,S,S,MyGrammar> {
public:
	using Super =  LOTHypothesis<MyHypothesis,S,S,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	double compute_single_likelihood(const datum_t& x) override {	
		const auto out = call(x.input, "<err>"); 
		
		const auto log_A = log(alphabet.size());
		
		// Likelihood comes from all of the ways that we can delete from the end and the append to make the observed output. 
		double lp = -infinity;
		for(auto& o : out.values()) { // add up the probability from all of the strings
			lp = logplusexp(lp, o.second + p_delete_append<strgamma,strgamma>(o.first, x.output, log_A));
		}
		return lp;
	}
		
	void print(std::string prefix="#\n") override {
		// make this hypothesis string show all the data too
		extern MyHypothesis::data_t mydata;
		
		if(show_data) {
			for(auto& di : mydata) {
				prefix = prefix+"#Train\t" + di.input + "\t" + this->call(di.input, "<err>").string() + "\n";
			}
			
			// and do the test input:
			prefix = prefix+"#Test\t" + testinput + "\t" + this->call(testinput, "<err>").string() + "\n";
		}
		
		Super::print(prefix); 
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define classes for MCTS
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "MCTS.h"
#include "MCMCChain.h"

class PriorSampleMCTS : public PartialMCTSNode<PriorSampleMCTS, MyHypothesis, TopN<MyHypothesis>> {
public:
	using Super = PartialMCTSNode<PriorSampleMCTS, MyHypothesis, TopN<MyHypothesis>>;
	using Super::Super;

	// we must declare this to use these in a vector, but we can't call it since we can't move the locks
	PriorSampleMCTS(PriorSampleMCTS&& m){ throw YouShouldNotBeHereError("*** This must be defined for but should never be called"); } 
};

class MCMCwithinMCTS : public PartialMCTSNode<MCMCwithinMCTS, MyHypothesis, TopN<MyHypothesis>> {
public:
	using Super = PartialMCTSNode<MCMCwithinMCTS, MyHypothesis, TopN<MyHypothesis>>;
	using Super::Super;

	// we must declare this to use these in a vector, but we can't call it since we can't move the locks
	MCMCwithinMCTS(MCMCwithinMCTS&& m){ throw YouShouldNotBeHereError("*** This must be defined for but should never be called"); } 
	
	virtual void playout(MyHypothesis& current) override {
		// define our own playout here to call our callback and add sample to the MCTS
		
		MyHypothesis h0 = current; // need a copy to change resampling on 
		for(auto& n : h0.get_value() ){
			n.can_resample = false;
		}

		h0.complete(); // fill in any structural gaps
		
		// check to see if we have no gaps and no resamples, then we just run one sample. 
		std::function no_resamples = +[](const Node& n) -> bool { return not n.can_resample;};
		if(h0.get_value().all(no_resamples)) {
			this->process_evaluable(h0);
		}
		else {
			// else we run vanilla MCMC
			MCMCChain chain(h0, data, callback);
			chain.run(Control(FleetArgs::inner_steps, FleetArgs::inner_runtime, 1, FleetArgs::inner_restart)); // run mcmc with restarts; we sure shouldn't run more than runtime
			this->process_evaluable(chain.current);
		}
	}
	
};



class MyFullMCTS : public FullMCTSNode<MyFullMCTS, MyHypothesis, TopN<MyHypothesis>> {
public:
	using Super = FullMCTSNode<MyFullMCTS, MyHypothesis, TopN<MyHypothesis>>;
	using Super::Super;

	// we must declare this to use these in a vector, but we can't call it since we can't move the locks
	MyFullMCTS(MyFullMCTS&& m){ throw YouShouldNotBeHereError("*** This must be defined for but should never be called"); } 
};


class MyMinimalMCTS : public MinimalMCTSNode<MyMinimalMCTS, MyHypothesis, TopN<MyHypothesis>> {
public:
	using Super = MinimalMCTSNode<MyMinimalMCTS, MyHypothesis, TopN<MyHypothesis>>;
	using Super::Super;

	// we must declare this to use these in a vector, but we can't call it since we can't move the locks
	MyMinimalMCTS(MyMinimalMCTS&& m){ throw YouShouldNotBeHereError("*** This must be defined for but should never be called"); } 
};



////////////////////////////////////////////////////////////////////////////////////////////
// This needs to be included last because it includes VirtualMachine/applyPrimitives.h
// which really requires Primitives to be defined already

// if we define DO_NOT_INCLUDE_MAIN then we can import everything *except* the below
#ifndef DO_NOT_INCLUDE_MAIN

#include "Top.h"
#include "ParallelTempering.h"
#include "EnumerationInference.h"
#include "PriorInference.h"
#include "BeamSearch.h"
#include "ChainPool.h"
#include "PartitionMCMC.h"

#include "FullLZEnumeration.h"
#include "PartialLZEnumeration.h"
#include "BasicEnumeration.h"


#include "Fleet.h" 

// we need to declare mydata up here so that it can be accessed in print
MyHypothesis::data_t mydata;

int main(int argc, char** argv){ 
	std::string arg_prefix = ""; // if we pass in a prefix as argument 
	std::string method = "parallel-tempering";
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("A simple, one-factor formal language learner");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.add_option("-d,--data",     datastr, "Comma separated list of input data strings");	
	fleet.add_option("--prefix",      arg_prefix, "Print a prefix before output");	
	fleet.add_option("--method",      method, "Specify an inference method");	
	fleet.initialize(argc, argv);
	
	//------------------
	// set up the data
	//------------------
		
	// parse into i/o pairs
	assert(alphabet.find(":") == std::string::npos); // alphabet can't have :
	for(auto di : split(datastr, ',')) {
		// add check that data is in the alphabet
		for(auto& c : di) {	
			if(c == ':') continue;
			assert(alphabet.find(c) != std::string::npos && "*** alphabet does not include all data characters");
		}
		
		auto [x,y] = divide(di, ':');
		
		// and add to data:
		mydata.push_back( MyHypothesis::datum_t({x,y}) );		
	}
	
	//------------------
	// Run 
	//------------------
	
	MyGrammar grammar;
	grammar.GRAMMAR_MAX_DEPTH = MAX_GRAMMAR_DEPTH;
	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top;
	//top.print_best = true;
	
	if(method == "parallel-tempering") {
		auto h0 = MyHypothesis::make(&grammar);
		ParallelTempering samp(h0, &mydata, top, FleetArgs::nchains, 10.0);
		samp.run(Control(), 250, 10000);		
	}
	else if(method == "prior-sampling") {
		PriorInference<MyHypothesis, decltype(top)> pri(&grammar, &mydata, top);
		pri.run(Control());
	}
	else if(method == "basic-enumeration") {
		EnumerationInference<MyHypothesis,MyGrammar,decltype(top),BasicEnumeration<MyGrammar>> e(&grammar, &mydata, top);
		e.run(Control());
	}
	else if(method == "partial-LZ-enumeration") {
		EnumerationInference<MyHypothesis,MyGrammar,decltype(top),PartialLZEnumeration<MyGrammar>> e(&grammar, &mydata, top);
		e.run(Control());
	}
	else if(method == "full-LZ-enumeration") {
		EnumerationInference<MyHypothesis,MyGrammar,decltype(top),FullLZEnumeration<MyGrammar>> e(&grammar, &mydata, top);
		e.run(Control());
	}
	else if(method == "beam") {
		MyHypothesis h0(&grammar); // don't use make -- must start with empty value
		BeamSearch bs(h0, &mydata, top, 1000.0);
		bs.run(Control());
	}
	else if(method == "chain-pool") {
		auto h0 = MyHypothesis::make(&grammar);
		ChainPool c(h0, &mydata, top, FleetArgs::nchains);
		c.run(Control());
	}
	else if(method == "prior-sample-mcts") {
		// A PartialMCTSNode is one where you stop one step after reaching an unexpanded kid in the tree
		MyHypothesis h0(&grammar);
		PriorSampleMCTS m(h0, FleetArgs::explore, &mydata, top);
		m.run(Control(), h0);
		//m.print(h0);
		//m.print(h0, "tree.txt");
		//COUT "# MCTS size: " TAB m.count() ENDL;
	}
	else if(method == "mcmc-within-mcts") {
		// A PartialMCTSNode is one where you stop one step after reaching an unexpanded kid in the tree
		MyHypothesis h0(&grammar);
		MCMCwithinMCTS m(h0, FleetArgs::explore, &mydata, top);
		m.run(Control(), h0);
		//m.print(h0);
	}
	else if(method == "full-mcts") {
		// A FullMCTSNode run is one where each time you descend the tree, you go until you make it to a terminal
		MyHypothesis h0(&grammar);
		MyFullMCTS m(h0, FleetArgs::explore, &mydata, top);
		m.run(Control(), h0);
		//m.print(h0);
		//m.print(h0, "tree.txt");
		//COUT "# MCTS size: " TAB m.count() ENDL;
	}
	else if(method == "minimal-mcts") {
		// A FullMCTSNode run is one where each time you descend the tree, you go until you make it to a terminal
		MyHypothesis h0(&grammar);
		MyMinimalMCTS m(h0, FleetArgs::explore, &mydata, top);
		m.run(Control(), h0);
		//m.print(h0);
		//m.print(h0, "tree.txt");
		//COUT "# MCTS size: " TAB m.count() ENDL;
	}
	else if(method == "partition-mcmc") {
		MyHypothesis h0(&grammar); // start empty
		PartitionMCMC c(h0, FleetArgs::partition_depth, &mydata, top);
		c.run(Control());
	}
	else {
		throw NotImplementedError();
	}
	
	top.print(arg_prefix);
}

#endif