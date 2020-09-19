

///########################################################################################
// A simple example of a version of the RationalRules model. 
// This is primarily used as an example and for debugging MCMC
// My laptop gets around 200-300k samples per second on 4 threads
///########################################################################################


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We need to define some structs to hold the MyObject features
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

enum class Shape { Square, Triangle, Circle};
enum class Color { Red, Green, Blue};

#include "Object.h"

typedef Object<Color,Shape> MyObject;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is a global variable that provides a convenient way to wrap our primitives
/// where we can pair up a function with a name, and pass that as a constructor
/// to the grammar. We need a tuple here because Primitive has a bunch of template
/// types to handle thee function it has, so each is actually a different type.
/// This must be defined before we import Fleet because Fleet does some template
/// magic internally
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the grammar
/// Thid requires the types of the thing we will add to the grammar (bool,MyObject)
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

using MyGrammar = Grammar<MyObject, bool>;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define a class for handling my specific hypotheses and data. Everything is defaultly 
/// a PCFG prior and regeneration proposals, but I have to define a likelihood
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,MyObject,bool,MyGrammar> {
public:
	using Super = LOTHypothesis<MyHypothesis,MyObject,bool,MyGrammar>;
	using Super::Super; // inherit the constructors
	
	// Now, if we defaultly assume that our data is a std::vector of t_data, then we 
	// can just define the likelihood of a single data point, which is here the true
	// value with probability di.reliability, and otherwise a coin flip. 
	double compute_single_likelihood(const datum_t& di) override {
		bool out = callOne(di.input, false);
		return log((1.0-di.reliability)/2.0 + (out == di.output)*di.reliability);
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Main code
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Top.h"
#include "ParallelTempering.h"
#include "Fleet.h" 


bool myf() {
	return true;
}

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Rational rules");
	fleet.initialize(argc, argv);
	
	//------------------
	// Basic setup
	//------------------
	
	// Define the grammar (default initialize using our primitives will add all those rules)
	// in doing this, grammar deduces the types from the input and output types of each primitive
	MyGrammar grammar;
	grammar.add("red(%s)",       +[](MyObject x) -> bool { return x.is(Color::Red); });
	grammar.add("green(%s)",     +[](MyObject x) -> bool { return x.is(Color::Green); });
	grammar.add("blue(%s)",      +[](MyObject x) -> bool { return x.is(Color::Blue); });
	grammar.add("square(%s)",    +[](MyObject x) -> bool { return x.is(Shape::Square); });
	grammar.add("triangle(%s)",  +[](MyObject x) -> bool { return x.is(Shape::Triangle); });
	grammar.add("circle(%s)",    +[](MyObject x) -> bool { return x.is(Shape::Circle); });
	
	grammar.add("and(%s,%s)",    Builtins::And<MyGrammar::VirtualMachineState_t>);
	grammar.add("or(%s,%s)",     Builtins::Or<MyGrammar::VirtualMachineState_t>);
	grammar.add("not(%s)",       Builtins::Not<MyGrammar::VirtualMachineState_t>);

	grammar.add("x",             Builtins::X<MyObject,MyGrammar::VirtualMachineState_t>);
	
	grammar.add("if(%s,%s,%s)",  Builtins::If<bool,MyGrammar::VirtualMachineState_t>);
	
	grammar.add("recurse(%s)",   Builtins::Recurse<MyGrammar::VirtualMachineState_t>);
	// but we also have to add a rule for the BuiltinOp that access x, our argument
//	Builtin::X<MyObject>("x", 10.0),
//	
//	// And and,or,not -- we use Builtins here because any user defined one won't short-circuit
//	Builtin::And("and(%s,%s)"),
//	Builtin::Or("or(%s,%s)"),
//	Builtin::Not("not(%s)")
//	
	// top stores the top hypotheses we have found
	TopN<MyHypothesis> top;
	
	//------------------
	// set up the data
	//------------------
	// mydata stores the data for the inference model
	MyHypothesis::data_t mydata;
	
	mydata.push_back(MyHypothesis::datum_t{.input=MyObject{.color=Color::Red, .shape=Shape::Triangle}, .output=true,  .reliability=0.75});
	mydata.push_back(MyHypothesis::datum_t{.input=MyObject{.color=Color::Red, .shape=Shape::Square},   .output=false, .reliability=0.75});
	mydata.push_back(MyHypothesis::datum_t{.input=MyObject{.color=Color::Red, .shape=Shape::Square},   .output=false, .reliability=0.75});
	
	//------------------
	// Actually run
	//------------------
	
	auto h0 = MyHypothesis::make(&grammar);
	MCMCChain chain(h0, &mydata, top);
	tic();
	chain.run(Control());
	tic();
	
//	auto h0 = MyHypothesis::make(&grammar);
//	ParallelTempering samp(h0, &mydata, top, 16, 10.0); 
//	tic();
//	samp.run(Control(mcmc_steps,runtime,nthreads), 100, 1000); 		
//	tic();

	// Show the best we've found
	top.print();
}