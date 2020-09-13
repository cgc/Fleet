#include <vector>
#include <string>
#include <random>

// For speed, here we can just treat words and sets as both integers in the implementation
// since all that matters is their number of elements (although this means that unions 
// are reallly multi-unions, and intersections are a bit weird)
// TODO: We should change this to use bit fields as a set
using word = int;
using set  = long;

const word U = -999;
const double alpha = 0.9;
const double Ndata = 300; // how many data points?
double recursion_penalty = -25.0;

// probability of each set size 0,1,2,...
const std::vector<double> Np = {0.000000000, 0.683564771, 0.141145140, 0.056400989, 0.031767168, 0.028248050, 0.015693361, 0.014361803, 0.008179570, 0.009986684, 0.010652463};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Define the primitives
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"	
	
std::tuple PRIMITIVES = {
	Primitive("undef",         +[]() -> word { return U; }),
	
	Primitive("one",         +[]() -> word { return 1; }, 0.1),
	Primitive("two",         +[]() -> word { return 2; }, 0.1),
	Primitive("three",       +[]() -> word { return 3; }, 0.1),
	Primitive("four",        +[]() -> word { return 4; }, 0.1),
	Primitive("five",        +[]() -> word { return 5; }, 0.1),
	Primitive("six",         +[]() -> word { return 6; }, 0.1),
	Primitive("seven",       +[]() -> word { return 7; }, 0.1),
	Primitive("eight",       +[]() -> word { return 8; }, 0.1),
	Primitive("nine",        +[]() -> word { return 9; }, 0.1),
	Primitive("ten",         +[]() -> word { return 10; }, 0.1),
	
	Primitive("next(%s)",    +[](word w) -> word { return w == U ? U : w+1;}),
	Primitive("prev(%s)",    +[](word w) -> word { return w == U or w == 1 ? U : w-1; }),
	
	// extract from the context/utterance
	Primitive("singleton(%s)",   +[](set s) -> bool    { return s==1; }, 2.0),
	Primitive("doubleton(%s)",   +[](set s) -> bool    { return s==2; }, 2.0),
	Primitive("tripleton(%s)",   +[](set s) -> bool    { return s==3; }, 2.0),
	
	Primitive("select(%s)",      +[](set s) -> set    { 
		if(s==0) throw VMSRuntimeError();
		else return 1;
	}),
	
	// set operations on ints
	Primitive("union(%s,%s)",        +[](set x, set y) -> set { return set(x+y); }),
	Primitive("setdiff(%s,%s)",      +[](set x, set y) -> set { return set(x-y); }),
	Primitive("intersection(%s,%s)", +[](set x, set y) -> set { return (set)std::min(x,y); }),
	
	Builtin::And("and(%s,%s)", 1./3.),
	Builtin::Or("or(%s,%s)", 1./3.),
	Builtin::Not("not(%s)", 1./3.),
	
	Builtin::If<set>( "if(%s,%s,%s)", 1/2.),		
	Builtin::If<word>("if(%s,%s,%s)", 1/2.),
	Builtin::X<set>("x", 15.0),
	Builtin::Recurse<word,set>("F(%s)")	
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"

using MyGrammar = Grammar<bool,word,set>;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class MyHypothesis final : public LOTHypothesis<MyHypothesis,set,word,MyGrammar> {
public:
	using Super = LOTHypothesis<MyHypothesis,set,word,MyGrammar>;
	using Super::Super;
	
	size_t recursion_count() {
		size_t cnt = 0;
		for(auto& n : value) {
			cnt += n.rule->instr.is_a(BuiltinOp::op_RECURSE);
		}
		return cnt;
	}
		
	double compute_prior() override {
		// include recusion penalty
		return prior = Super::compute_prior() + (recursion_count() > 0 ? recursion_penalty : log1p(-exp(recursion_penalty))); 
	}
		
	// As in the original paper, we compute hte likelihood by averaging over all numbers, weighting by their probabilities
	virtual double compute_likelihood(const data_t& data, const double breakout=-infinity) {
		this->likelihood = 0.0;
		for(set x=1;x<10;x++) {
			auto v = callOne(x);
			this->likelihood += Ndata*Np[x]*log(  (1.0-alpha)/10.0 + (v==x)*alpha);
		}
		return this->likelihood;
	}	
	
	virtual void print(std::string prefix="") override {
		std::string outputstring;
		for(size_t x=1;x<=10;x++) {
			auto v = callOne(x);
			outputstring += (v == U ? "U" : str(v)) + ".";
		}
		
		prefix += QQ(outputstring)+"\t"+std::to_string(this->recursion_count())+"\t";
		Super::print(prefix);		
	}
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Main
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "ParallelTempering.h"
#include "Fleet.h"
#include "Top.h"

int main(int argc, char** argv) { 
	Fleet fleet("Simple number inference model");
	fleet.initialize(argc, argv);

	MyGrammar grammar(PRIMITIVES);

	MyHypothesis::data_t mydata; // just dummy

	// Run parallel tempering
	TopN<MyHypothesis> top;
	auto h0 = MyHypothesis::make(&grammar);
	ParallelTempering samp(h0, &mydata, top, 10, 100.0);
	samp.run(Control(), 200, 5000); 
	
	top.print();
}

